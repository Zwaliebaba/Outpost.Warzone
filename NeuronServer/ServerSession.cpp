#include "pch.h"
#include "ServerSession.h"

#include "Debug.h"

namespace Neuron
{

namespace
{
/// Big enough for any session-plane record, which are all a handful of fields.
constexpr std::size_t SessionScratchBytes = 64;

/// A UiEvent carries two length-prefixed strings, so it gets its own buffer.
constexpr std::size_t UiEventScratchBytes = 512;

/// The game speeds the keys have always offered, as the bounds of what a
/// client may ask for. Anything outside is refused rather than clamped: a
/// client asking for a speed the game never had is a client that is wrong.
constexpr float SlowestGameSpeed = 1.0f / 3.0f;
constexpr float FastestGameSpeed = 2.0f;
} // namespace

ServerSession::ServerSession(LoopbackTransport& _link, std::uint32_t _buildHash, std::uint16_t _tickMs,
                             SessionPolicy _policy) noexcept
  : m_link(_link), m_buildHash(_buildHash), m_tickMs(_tickMs), m_policy(_policy)
{
}

template <typename Message>
void ServerSession::SendSession(const Message& _message)
{
  std::byte scratch[SessionScratchBytes]{};
  NetWriter writer{scratch};
  Put(writer, _message);

  /* An overflow here would be a record outgrowing the buffer, which is our
     mistake rather than a peer's, so it is dropped loudly rather than sent
     half-written. */
  DEBUG_ASSERT_TEXT(!writer.Overflowed(), "ServerSession: session record does not fit the scratch buffer");
  if (writer.Overflowed())
    return;

  m_link.Send(LoopbackTransport::End::Server, NetChannel::Session, writer.Written());
}

void ServerSession::Service()
{
  /* The session channel and the command channel, and only those. Replication
     belongs to the writer, which is the other way round -- it is this end that
     sends on it -- and eating a channel that is not ours would leave its
     consumer with nothing to read. */
  LoopbackTransport::Message message;
  for (const NetChannel channel : {NetChannel::Session, NetChannel::Command})
  {
    while (m_link.Receive(LoopbackTransport::End::Server, channel, message))
    {
      /* Once refused, the peer's bytes are drained and discarded. Draining
         rather than leaving them queued keeps a refused session from growing
         without bound if the peer keeps talking. */
      if (m_state == State::Refused)
        continue;

      Deliver(message.channel, message.bytes);
    }
  }
}

void ServerSession::Deliver(NetChannel _channel, std::span<const std::byte> _bytes)
{
  NetReader reader{_bytes};
  const auto id = static_cast<ClientMessage>(reader.U8());

  /* A message id this build does not know is ignored: a newer peer may send
     one, and the alternative is switching on whatever it collides with. */
  if (!reader.Ok() || !IsKnown(id))
    return;

  switch (id)
  {
  case ClientMessage::Hello:
    if (_channel == NetChannel::Session && m_state == State::Handshaking)
      OnHello(reader);
    break;

  case ClientMessage::Ready:
    /* Ready answers Start. Arriving in any other state means the peer is
       driving the session out of order, so it is dropped. */
    if (_channel == NetChannel::Session && m_state == State::Starting)
      OnReady(reader);
    break;

  case ClientMessage::SessionControl:
    /* A control names something to do to a running world. Before Running
       there is no world it could mean. */
    if (_channel == NetChannel::Command && m_state == State::Running)
      OnSessionControl(reader);
    break;

  default:
    /* Every other message belongs to a plane this session does not serve yet.
       Stage D adds them one at a time as their handlers arrive. */
    break;
  }
}

void ServerSession::OnHello(NetReader& _reader)
{
  const ClientHello hello = ClientHello::Decode(_reader);

  /* A hello that ran off the end of its buffer is not a hello. Refusing on
     protocol grounds is the honest answer: we cannot tell what it meant. */
  const HandshakeResult result = _reader.Ok() ? Consider(hello, m_buildHash) : HandshakeResult::ProtocolMismatch;

  ServerHello answer;
  answer.result = result;
  answer.tickMs = m_tickMs;
  answer.connectionId = 1;
  SendSession(answer);

  m_state = result == HandshakeResult::Accepted ? State::Greeted : State::Refused;
}

void ServerSession::OnReady(NetReader& _reader)
{
  const ClientReady ready = ClientReady::Decode(_reader);

  /* The client says which level it loaded, and it has to be the one Start
     named: a client that loaded a different one would desync from tick 1 and
     nothing after this could tell why. A Ready that could not be read says
     nothing about what was loaded, which is the same as saying the wrong
     thing. Either way the session ends here rather than a tick later. */
  if (!_reader.Ok() || ready.mapHash != m_mapHash)
  {
    ServerKick kick;
    kick.reason = KickReason::MapMismatch;
    SendSession(kick);
    m_state = State::Refused;
    return;
  }

  m_state = State::Running;
}

void ServerSession::OnSessionControl(NetReader& _reader)
{
  const ClientSessionControl control = ClientSessionControl::Decode(_reader);
  if (!_reader.Ok())
    return;

  switch (control.kind)
  {
  case SessionControlKind::GameSpeed:
    if (!m_policy.allowGameSpeed)
    {
      Reject(control.sequence, ClientMessage::SessionControl, RejectReason::NotPermitted);
      return;
    }
    if (!(control.value >= SlowestGameSpeed && control.value <= FastestGameSpeed))
    {
      Reject(control.sequence, ClientMessage::SessionControl, RejectReason::NotPermitted);
      return;
    }
    break;

  default:
    /* Decode refused any kind this build has no name for, so this is
       unreachable; it is here so a kind added later fails loudly rather than
       being permitted by falling through. */
    Reject(control.sequence, ClientMessage::SessionControl, RejectReason::NotPermitted);
    return;
  }

  m_controls.push_back(control);
}

void ServerSession::Reject(std::uint32_t _sequence, ClientMessage _command, RejectReason _reason)
{
  ServerCommandReject reject;
  reject.sequence = _sequence;
  reject.command = _command;
  reject.reason = _reason;
  SendSession(reject);
}

bool ServerSession::TakeControl(ClientSessionControl& _outControl)
{
  if (m_controls.empty())
    return false;

  _outControl = m_controls.front();
  m_controls.erase(m_controls.begin());
  return true;
}

void ServerSession::Start(std::uint32_t _mapHash)
{
  if (m_state != State::Greeted)
    return;

  m_mapHash = _mapHash;

  ServerStart start;
  start.startTick = m_tick;
  start.countdownMs = 0;
  start.mapHash = _mapHash;
  SendSession(start);

  m_state = State::Starting;
}

void ServerSession::Tick()
{
  if (m_state != State::Running)
    return;

  m_tick += 1;

  ServerTick tick;
  tick.tick = m_tick;
  SendSession(tick);
}

bool ServerSession::SendUiEvent(const ServerUiEvent& _event)
{
  if (m_state != State::Running)
    return false;

  std::byte scratch[UiEventScratchBytes]{};
  NetWriter writer{scratch};
  Put(writer, _event);

  DEBUG_ASSERT_TEXT(!writer.Overflowed(), "ServerSession: UiEvent does not fit the scratch buffer");
  if (writer.Overflowed())
    return false;

  /* The replication channel: reliable and ordered against the world state it
     refers to, so a camera move arrives after the entity it centres on. */
  m_link.Send(LoopbackTransport::End::Server, NetChannel::Replication, writer.Written());
  return true;
}

} // namespace Neuron
