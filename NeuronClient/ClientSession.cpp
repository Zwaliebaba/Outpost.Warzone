#include "pch.h"
#include "ClientSession.h"

#include "Debug.h"

namespace Neuron
{

namespace
{
/// Big enough for any session-plane record, which are all a handful of fields.
constexpr std::size_t SessionScratchBytes = 64;
} // namespace

ClientSession::ClientSession(LoopbackTransport& _link, std::uint32_t _buildHash) noexcept
  : m_link(_link), m_buildHash(_buildHash)
{
}

template <typename Message>
void ClientSession::SendSession(const Message& _message)
{
  std::byte scratch[SessionScratchBytes]{};
  NetWriter writer{scratch};
  Put(writer, _message);

  /* An overflow here would be a record outgrowing the buffer, which is our
     mistake rather than a peer's, so it is dropped loudly rather than sent
     half-written. */
  DEBUG_ASSERT_TEXT(!writer.Overflowed(), "ClientSession: session record does not fit the scratch buffer");
  if (writer.Overflowed())
    return;

  m_link.Send(LoopbackTransport::End::Client, NetChannel::Session, writer.Written());
}

void ClientSession::SendSessionId(ClientMessage _id)
{
  std::byte scratch[1]{};
  NetWriter writer{scratch};
  writer.U8(static_cast<std::uint8_t>(_id));
  m_link.Send(LoopbackTransport::End::Client, NetChannel::Session, writer.Written());
}

void ClientSession::Begin()
{
  if (m_state != State::Fresh)
    return;

  ClientHello hello;
  hello.protocolVersion = ProtocolVersion;
  hello.buildHash = m_buildHash;
  SendSession(hello);

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

  default:
    /* Every other message belongs to a plane this session does not read yet.
       Stage D adds them one at a time as their handlers arrive -- the replica
       store is the next of them. */
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

void ClientSession::ReportReady()
{
  if (m_state != State::Loading)
    return;

  SendSessionId(ClientMessage::Ready);
  m_state = State::Running;
}

} // namespace Neuron
