#include "pch.h"
#include "ClientSession.h"

#include "Debug.h"

namespace Neuron
{

namespace
{
/// Big enough for any session- or command-plane record this end sends, which
/// are all a handful of fields.
constexpr std::size_t SessionScratchBytes = 64;
} // namespace

ClientSession::ClientSession(LoopbackTransport& _link, std::uint32_t _buildHash) noexcept
  : m_link(_link), m_buildHash(_buildHash)
{
}

template <typename Message>
void ClientSession::Send(NetChannel _channel, const Message& _message)
{
  std::byte scratch[SessionScratchBytes]{};
  NetWriter writer{scratch};
  Put(writer, _message);

  /* An overflow here would be a record outgrowing the buffer, which is our
     mistake rather than a peer's, so it is dropped loudly rather than sent
     half-written. */
  DEBUG_ASSERT_TEXT(!writer.Overflowed(), "ClientSession: record does not fit the scratch buffer");
  if (writer.Overflowed())
    return;

  m_link.Send(LoopbackTransport::End::Client, _channel, writer.Written());
}

void ClientSession::Begin()
{
  if (m_state != State::Fresh)
    return;

  ClientHello hello;
  hello.protocolVersion = ProtocolVersion;
  hello.buildHash = m_buildHash;
  Send(NetChannel::Session, hello);

  m_state = State::Handshaking;
}

void ClientSession::Service()
{
  /* Only the session channel. Replication bytes belong to the replica store,
     which drains its own; a session that swallowed them would leave the world
     empty and nothing to say why. */
  LoopbackTransport::Message message;
  while (m_link.Receive(LoopbackTransport::End::Client, NetChannel::Session, message))
  {
    /* Once refused, the server's bytes are drained and discarded, for the same
       reason the server drains a refused client's: nothing is acted on, and
       nothing is left to pile up behind a reader that will never come. */
    if (m_state == State::Refused)
      continue;

    Deliver(message.channel, message.bytes);
  }
}

void ClientSession::Deliver(NetChannel _channel, std::span<const std::byte> _bytes)
{
  NetReader reader{_bytes};
  const auto id = static_cast<ServerMessage>(reader.U8());

  /* A message id this build does not know is ignored: a newer server may send
     one, and the alternative is switching on whatever it collides with. */
  if (!reader.Ok() || !IsKnown(id))
    return;

  switch (id)
  {
  case ServerMessage::Hello:
    if (_channel == NetChannel::Session && m_state == State::Handshaking)
      OnHello(reader);
    break;

  case ServerMessage::Start:
    /* Start names the level, which only means something to a client the server
       has already accepted. */
    if (_channel == NetChannel::Session && m_state == State::Greeted)
      OnStart(reader);
    break;

  case ServerMessage::Tick:
    if (m_state == State::Running)
      OnTick(reader);
    break;

  case ServerMessage::Kick:
    /* A kick can come at any point after the hello was answered: the server
       has accepted this client and is now sending it away. Before the verdict
       it means nothing, since nothing has been agreed to end. */
    if (_channel == NetChannel::Session && m_state != State::Fresh && m_state != State::Handshaking)
      OnKick(reader);
    break;

  case ServerMessage::CommandReject:
    /* An answer to something this client asked for, which it can only have
       done once Running. */
    if (_channel == NetChannel::Session && m_state == State::Running)
      OnReject(reader);
    break;

  default:
    /* Every other message belongs to a plane this session does not read.
       Replication, UiEvent included, is the replica world's; it drains its
       own channel. */
    break;
  }
}

void ClientSession::OnHello(NetReader& _reader)
{
  const ServerHello hello = ServerHello::Decode(_reader);

  /* A verdict that ran off the end of its buffer is not a verdict. Treating it
     as a protocol mismatch is the honest answer: we cannot tell what the
     server said, so we cannot claim to have been accepted. */
  const HandshakeResult result = _reader.Ok() ? hello.result : HandshakeResult::ProtocolMismatch;

  m_verdict = result;

  if (result != HandshakeResult::Accepted)
  {
    m_state = State::Refused;
    return;
  }

  m_tickMs = hello.tickMs;
  m_connectionId = hello.connectionId;
  m_state = State::Greeted;
}

void ClientSession::OnStart(NetReader& _reader)
{
  const ServerStart start = ServerStart::Decode(_reader);

  /* A Start we could not read names no level, so there is nothing to load and
     nothing to answer with. Staying in Greeted leaves the session where it
     was rather than loading whatever the truncated bytes happened to say. */
  if (!_reader.Ok())
    return;

  m_mapHash = start.mapHash;
  m_tick = start.startTick;
  m_state = State::Loading;
}

void ClientSession::OnTick(NetReader& _reader)
{
  const ServerTick tick = ServerTick::Decode(_reader);
  if (!_reader.Ok())
    return;

  m_tick = tick.tick;
}

void ClientSession::OnKick(NetReader& _reader)
{
  const ServerKick kick = ServerKick::Decode(_reader);

  /* A kick that could not be read is still a kick: the server has said the
     session is over, and only the reason is missing. Decode already folded
     that to Unstated. */
  m_kicked = kick.reason;
  m_state = State::Refused;
}

void ClientSession::OnReject(NetReader& _reader)
{
  const ServerCommandReject reject = ServerCommandReject::Decode(_reader);
  if (!_reader.Ok())
    return;

  m_rejects.push_back(reject);
}

void ClientSession::ReportReady(std::uint32_t _loadedMapHash)
{
  if (m_state != State::Loading)
    return;

  ClientReady ready;
  ready.mapHash = _loadedMapHash;
  Send(NetChannel::Session, ready);

  m_state = State::Running;
}

void ClientSession::RequestGameSpeed(float _modifier)
{
  if (m_state != State::Running)
    return;

  ClientSessionControl control;
  control.sequence = m_nextSequence++;
  control.kind = SessionControlKind::GameSpeed;
  control.value = _modifier;
  Send(NetChannel::Command, control);
}

bool ClientSession::TakeReject(ServerCommandReject& _outReject)
{
  if (m_rejects.empty())
    return false;

  _outReject = m_rejects.front();
  m_rejects.erase(m_rejects.begin());
  return true;
}

} // namespace Neuron
