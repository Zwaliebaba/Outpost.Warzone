#include "pch.h"
#include "ServerSession.h"

#include "Debug.h"

namespace Neuron
{

namespace
{
/// Big enough for any session-plane record, which are all a handful of fields.
constexpr std::size_t SessionScratchBytes = 64;
} // namespace

ServerSession::ServerSession(LoopbackTransport& _link, std::uint32_t _buildHash, std::uint16_t _tickMs) noexcept
  : m_link(_link), m_buildHash(_buildHash), m_tickMs(_tickMs)
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
  /* Only the session channel. The other channels belong to consumers this does
     not own, and eating their bytes would leave them with nothing to read. */
  LoopbackTransport::Message message;
  while (m_link.Receive(LoopbackTransport::End::Server, NetChannel::Session, message))
  {
    /* Once refused, the peer's bytes are drained and discarded. Draining rather
       than leaving them queued keeps a refused session from growing without
       bound if the peer keeps talking. */
    if (m_state == State::Refused)
      continue;

    Deliver(message.channel, message.bytes);
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
      m_state = State::Running;
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

void ServerSession::Start(std::uint32_t _mapHash)
{
  if (m_state != State::Greeted)
    return;

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

} // namespace Neuron
