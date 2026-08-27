/*
 * ClientSession.h
 *
 * The client's half of one session: what it has been told, and what it may
 * say next.
 */

#pragma once

#include "LoopbackTransport.h"
#include "Protocol.h"

#include <cstdint>
#include <optional>

namespace Neuron
{

/// Tracks this client's end of a session, from the opening hello into the
/// ticking world.
///
/// **The mirror of ServerSession, and like it, the protocol state machine and
/// nothing else.** It does not load a level, does not own a replica store and
/// draws nothing. It decides what the client is entitled to believe and what
/// it is allowed to say; the caller pairs ReportReady() with whatever finished
/// loading, exactly as the server pairs Tick() with whatever advances the
/// world. That is what lets this be tested with no level, no device and no
/// server -- the test drives both ends by hand, because from here a server is
/// only bytes arriving on a channel.
///
/// Three asymmetries with the server's half are deliberate:
///
/// - **The client speaks first, and then waits.** Begin() sends the hello.
///   Nothing is sent before it and nothing else is sent until the verdict is
///   in, so a server never has to decide what to do with traffic from a peer
///   it has not yet accepted.
/// - **The client cannot declare itself ready on its own schedule.** Ready
///   answers Start, so ReportReady() does nothing until the server has named a
///   level. Announcing readiness for a level that was never named would be
///   claiming to have loaded nothing.
/// - **The client is told what time it is.** Nothing here advances a tick on
///   its own. m_tick moves when the server says so and never otherwise, which
///   is the whole difference between this and a client that simulates.
class ClientSession
{
public:
  enum class State : std::uint8_t
  {
    /// Nothing said yet. Begin() has not been called.
    Fresh,

    /// The hello is out; the verdict has not arrived.
    Handshaking,

    /// Accepted. Waiting for the server to name a level.
    Greeted,

    /// The level is named and the caller is loading it. Waiting for
    /// ReportReady().
    Loading,

    /// Ready is out and the world is ticking.
    Running,

    /// The server refused, or answered with something that could not be read.
    /// Nothing further is believed and nothing further is sent.
    Refused,
  };

  /// _buildHash identifies this executable to the server. Zero says this build
  /// has no identity to offer, which is the solo case where both halves are
  /// one binary and there is nothing for the server to compare against.
  ClientSession(LoopbackTransport& _link, std::uint32_t _buildHash) noexcept;

  /// Sends the hello and starts the handshake. Does nothing once the session
  /// has left Fresh: a second hello on one connection is not a thing the
  /// protocol has an answer for.
  void Begin();

  /// Reads everything the server has sent and acts on it. Called once a frame.
  ///
  /// A message that does not belong in the current state is dropped rather
  /// than acted on, and so is one this build does not know. The server is more
  /// trusted than a client is, but "more trusted" is not "believed blindly":
  /// a Tick before the level is loaded still has nowhere to go.
  void Service();

  /// Tells the server this client has the named level loaded and can be sent
  /// world state. Does nothing unless the session is Loading.
  void ReportReady();

  [[nodiscard]] State CurrentState() const noexcept { return m_state; }

  /// The server's verdict on the hello, empty until it has answered. Empty and
  /// Accepted are different answers, so this is not a bare HandshakeResult:
  /// HandshakeResult has no value meaning "not decided", and inventing one
  /// would put it on the wire where it does not belong.
  [[nodiscard]] std::optional<HandshakeResult> Verdict() const noexcept { return m_verdict; }

  /// How much game time one tick advances the world by, as the *server* states
  /// it. Zero until the hello is answered -- a client never assumes 40 ms
  /// because its own build happens to say so.
  [[nodiscard]] std::uint16_t TickMs() const noexcept { return m_tickMs; }

  /// What the server calls this connection. Zero until the hello is answered.
  [[nodiscard]] std::uint32_t ConnectionId() const noexcept { return m_connectionId; }

  /// Which level the server named. Meaningful from Loading onwards.
  [[nodiscard]] std::uint32_t MapHash() const noexcept { return m_mapHash; }

  /// Which tick the client has been told the world is on.
  [[nodiscard]] std::uint32_t CurrentTick() const noexcept { return m_tick; }

private:
  void Deliver(NetChannel _channel, std::span<const std::byte> _bytes);
  void OnHello(NetReader& _reader);
  void OnStart(NetReader& _reader);
  void OnTick(NetReader& _reader);

  /// Sends one message on the session channel. The buffer is a local: nothing
  /// the session sends is larger than a handshake record.
  template <typename Message>
  void SendSession(const Message& _message);

  /// Sends a message that is only its id. Ready has no body -- what it means
  /// is entirely in having arrived.
  void SendSessionId(ClientMessage _id);

  LoopbackTransport& m_link;
  std::uint32_t m_buildHash = 0;
  State m_state = State::Fresh;
  std::optional<HandshakeResult> m_verdict;
  std::uint16_t m_tickMs = 0;
  std::uint32_t m_connectionId = 0;
  std::uint32_t m_mapHash = 0;
  std::uint32_t m_tick = 0;
};

} // namespace Neuron
