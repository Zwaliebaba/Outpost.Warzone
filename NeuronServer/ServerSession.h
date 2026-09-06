/*
 * ServerSession.h
 *
 * The server's half of one session: what the peer may do next, and which tick
 * the world is on.
 */

#pragma once

#include "LoopbackTransport.h"
#include "Protocol.h"

#include <cstdint>
#include <vector>

namespace Neuron
{

/// What a session's flags permit its client to ask for through
/// ClientMessage::SessionControl. A solo session permits what the player used
/// to do with a key; a service session refuses it, and the refusal is a
/// CommandReject rather than silence, so the client can say why.
struct SessionPolicy
{
  /// Whether the client may change the game speed.
  bool allowGameSpeed = false;
};

/// Tracks one connected client through the opening of a session and into the
/// ticking world.
///
/// **This is the protocol state machine and nothing else. It does not own the
/// world.** The caller pairs Tick() with whatever advances the simulation --
/// Loop.cpp's SimulateTick() while the server is embedded, OutpostServer's own
/// loop once it is not. Keeping the two apart is what lets the session be
/// tested without a world to load, and it is why this compiles into a library
/// that has never linked a level.
///
/// The order is the one Docs/ServerAuthority.md sets out: hello, verdict, then
/// the server says Start and the client answers Ready once it has the level
/// loaded. Nothing before Running advances a tick, so a client that never
/// finishes loading cannot be sent world state it has nowhere to put.
///
/// What the client asks for once Running -- a session control such as the game
/// speed -- is validated against the policy here and then *handed to the
/// owner* through TakeControl(), because applying it means touching the world,
/// and this does not own the world.
class ServerSession
{
public:
  enum class State : std::uint8_t
  {
    /// Nothing believed yet. Only a ClientHello is read.
    Handshaking,

    /// The hello was accepted; the level has not been named.
    Greeted,

    /// Start has been sent and the client is loading. Waiting for Ready.
    Starting,

    /// The client is ready and the world is ticking.
    Running,

    /// The handshake failed, or the client was kicked. Nothing further is read
    /// from this peer.
    Refused,
  };

  /// _buildHash is what a joining client must match. Zero means the server has
  /// no build of its own to compare against and does not check, which is the
  /// solo case where both halves are one binary.
  ServerSession(LoopbackTransport& _link, std::uint32_t _buildHash, std::uint16_t _tickMs,
                SessionPolicy _policy = {}) noexcept;

  /// Reads everything the client has sent and answers it. Called once a frame.
  ///
  /// A message that does not belong in the current state is dropped rather than
  /// acted on, and so is one this build does not know: a peer choosing the
  /// bytes must not be able to drive the session out of order.
  void Service();

  /// Names the level and asks the client to load it. Does nothing unless the
  /// session is Greeted.
  void Start(std::uint32_t _mapHash);

  /// Advances the session's tick and tells the client. Does nothing unless the
  /// session is Running, so the tick cannot run ahead of a client that has not
  /// reported itself ready.
  void Tick();

  /// Sends one UiEvent to the client on the replication channel, and returns
  /// whether it went. It goes only once the session is Running: before that
  /// the client has no world to put a message, a camera move or a sequence
  /// into, and a presentation call made against nothing is dropped, as it
  /// would be for a client that had not connected at all.
  bool SendUiEvent(const ServerUiEvent& _event);

  /// Takes the next session control the client asked for and the policy
  /// permitted, or returns FALSE when there is none. What was refused was
  /// already answered with a CommandReject and never reaches here.
  [[nodiscard]] bool TakeControl(ClientSessionControl& _outControl);

  [[nodiscard]] State CurrentState() const noexcept { return m_state; }

  /// Which tick the world is on. Zero until the first Tick() after Running.
  [[nodiscard]] std::uint32_t CurrentTick() const noexcept { return m_tick; }

  /// The level Start named. Zero until Start.
  [[nodiscard]] std::uint32_t MapHash() const noexcept { return m_mapHash; }

private:
  void Deliver(NetChannel _channel, std::span<const std::byte> _bytes);
  void OnHello(NetReader& _reader);
  void OnReady(NetReader& _reader);
  void OnSessionControl(NetReader& _reader);
  void Reject(std::uint32_t _sequence, ClientMessage _command, RejectReason _reason);

  /// Sends one message on the session channel. The buffer is a local: nothing
  /// the session sends is larger than a handshake record.
  template <typename Message>
  void SendSession(const Message& _message);

  LoopbackTransport& m_link;
  std::uint32_t m_buildHash = 0;
  std::uint16_t m_tickMs = 0;
  SessionPolicy m_policy;
  State m_state = State::Handshaking;
  std::uint32_t m_tick = 0;
  std::uint32_t m_mapHash = 0;

  /// Accepted controls waiting for the owner. A vector rather than a queue
  /// because there is rarely more than one, and TakeControl drains it front
  /// to back so the order the client asked in is the order they apply in.
  std::vector<ClientSessionControl> m_controls;
};

} // namespace Neuron
