/*
 * EmbeddedSession.h
 *
 * Both halves of the game in one process, talking to each other in bytes.
 */

#pragma once

#include "ClientSession.h"
#include "GTime.h"
#include "LoopbackTransport.h"
#include "ReplicaStore.h"
#include "ReplicationWriter.h"
#include "ServerSession.h"

#include <vector>

/// The solo session: the embedded server and its client, exchanging exactly the
/// bytes they would exchange over a network.
///
/// This is rung 1 of the separation ladder in Docs/ServerAuthority.md running
/// for real, against the campaign's own world rather than a test's. Every tick
/// the server gathers what exists, writes the difference to the client, and the
/// client rebuilds its world out of nothing but those bytes.
///
/// Three things cross it besides the world now:
///
/// - **UiEvents**, from the server's script VM to the client's screen. A
///   mission script that says addMessage or playVideo is running on the server,
///   which has no screen, so the instinct emits what it wants shown and the
///   client shows it (UiEvents.cpp).
/// - **Session controls**, from the client's keys to the server's clock. The
///   speed keys ask; the server's policy permits or refuses; what it permits it
///   hands back here to apply, because the session does not own the clock.
/// - **The level hash**, both ways: Start names the level and Ready names what
///   was loaded, and the server kicks a client whose answer differs.
///
/// **Nothing draws from the replica world yet, deliberately.** This step exists
/// to prove replication complete and correct while the game is still rendering
/// from the object lists, so the flip that follows is taken with evidence
/// instead of hope. In a Debug build the two worlds are compared field by field
/// every tick, which is the same assertion ReplicatedWorldTest makes -- except
/// against a real level, with real AI moving real droids, instead of four
/// fabricated entities.
///
/// It survives a level change without being told about one. The stream is a
/// difference, so a world replaced wholesale is a tick's worth of Leaves and
/// Enters and then agreement again; there is nothing to reset and so nothing
/// that can be forgotten.
class EmbeddedSession
{
public:
  /// The one session this process runs. A single instance rather than a
  /// global object because the instincts that emit UiEvents and the keys that
  /// request a speed live in files that have no business owning it.
  [[nodiscard]] static EmbeddedSession& Instance();

  /// Advances the session by one simulation tick.
  ///
  /// Call it immediately after SimulateTick(), which is the one moment the
  /// world is settled: objmemUpdate() has just run, so the object lists hold
  /// exactly what is alive and psDestroyedObj holds exactly what died on this
  /// tick and nothing older.
  void Tick(void);

  /// Sends one UiEvent from the server's side to the client's. The script VM's
  /// presentation instincts call this in place of the presentation itself.
  /// Opens the session first if it is not open yet, so a briefing a script
  /// starts on its first tick is not lost to a session still handshaking.
  void Emit(const Neuron::ServerUiEvent& _event);

  /// Asks the server, from the client's side, to run the world at _modifier
  /// times normal speed. What the speed keys do now.
  void RequestGameSpeed(float _modifier);

  /// The client's world, built from the wire.
  [[nodiscard]] const Neuron::ReplicaStore& Store(void) const noexcept { return m_store; }

private:
  /// Runs the whole handshake if the session is not Running, and returns
  /// whether it is. In one call, because both halves are here: the client's
  /// hello, the server's verdict, the level, and the client's Ready all cross
  /// a queue rather than a network. Separating the server changes how long
  /// this takes and nothing about what it says.
  [[nodiscard]] bool EnsureOpen(void);

  /// Fills m_visible and m_destroyed from the world.
  void Gather(void);

  /// Applies what the server permitted: today, the game speed.
  void ApplyControls(void);

  /// Drains the replication channel into the store, and UiEvents to the
  /// screen, in the order the server sent them.
  void Receive(void);

#ifdef DEBUG
  /// Reports the first place the client's world is not the server's world.
  ///
  /// A report rather than an assertion, and one per tick at most: a mismatch is
  /// a replication bug and wants fixing, but nothing draws from the replica
  /// world yet, so dying over one would stop the campaign being playable while
  /// this step is proving itself. It becomes an assertion when the renderer
  /// moves onto the store.
  void Verify(void) const;
#endif

  Neuron::LoopbackTransport m_link;

  /// A solo session permits what the player used to do with a key.
  Neuron::ServerSession m_server{m_link, 0u, static_cast<std::uint16_t>(Neuron::SimulationTickMs),
                                 Neuron::SessionPolicy{true}};
  Neuron::ClientSession m_client{m_link, 0u};
  Neuron::ReplicationWriter m_writer{m_link};
  Neuron::ReplicaStore m_store;

  /// Reused every tick, so a settled world stops allocating.
  std::vector<Neuron::EntityState> m_visible;
  std::vector<Neuron::EntityId> m_destroyed;

  /// Latched if the local session ever refuses to open, so the assertion that
  /// says so fires once rather than on every tick for the rest of the game.
  bool m_failed = false;
};
