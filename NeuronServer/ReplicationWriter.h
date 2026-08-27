/*
 * ReplicationWriter.h
 *
 * Turning what the server knows into what a client is told.
 */

#pragma once

#include "LoopbackTransport.h"
#include "Protocol.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>

namespace Neuron
{

/// One entity as the server knows it, in the terms the wire uses.
///
/// Deliberately not the client's Replica, which it currently matches field for
/// field. One is what the server knows and the other is what a client was told,
/// and sharing a type would couple the two halves across the boundary they
/// exist to separate. They will diverge on their own: the client's copy grows
/// presentation state the server has no business holding, and the server's
/// grows fields no client is entitled to see.
struct EntityState
{
  EntityId id = 0;
  EntityKind kind = EntityKind::Droid;
  std::uint8_t player = 0;
  std::uint16_t x = 0;
  std::uint16_t y = 0;
  std::uint16_t z = 0;

  /// Radians, +ve rotation about y, as BASE_OBJECT stores it.
  float direction = 0.0f;
};

/// Writes one client's replication stream: the difference between what it was
/// last told and what is true now.
///
/// **This holds the only record of what a client believes**, which is what lets
/// it send nothing at all for an entity that has not moved -- and a world
/// mostly sits still, so that case is the one that decides whether replication
/// scales. The 1998 protocol had no such record: `NET_CHECK_DROID` broadcast
/// position on a timer whether anything had changed or not, because no peer
/// knew what any other peer already had.
///
/// It does not decide *when* to write, and it does not know whether the client
/// is ready for what it writes. The caller pairs Write() with the tick and
/// gates it on the session being Running, exactly as it pairs
/// ServerSession::Tick() with whatever advances the world. Keeping that out of
/// here is what lets one client's stream be tested without a session, and what
/// will let stage G filter _visible per player without this changing at all.
class ReplicationWriter
{
public:
  explicit ReplicationWriter(LoopbackTransport& _link) noexcept;

  /// Brings the client's world up to _visible, and announces _destroyed.
  ///
  /// _visible is everything this client is entitled to see right now; anything
  /// it knew and no longer sees has Left. _destroyed is what ceased to exist
  /// this tick, which a diff of the two sets cannot tell apart from leaving:
  /// only the server knows which happened, so only the server can say.
  ///
  /// An id in both is treated as destroyed. That is a caller that contradicted
  /// itself, and of the two answers, "it blew up" is the one that cannot be
  /// silently wrong -- an entity left drawn forever is worse than one removed
  /// with an explosion nobody expected.
  void Write(std::span<const EntityState> _visible, std::span<const EntityId> _destroyed);

  /// How many entities this client is believed to know about.
  [[nodiscard]] std::size_t Known() const noexcept { return m_known.size(); }

  /// Forgets what the client was told, for ending a session. The next Write
  /// then re-enters the whole world, which is also what a resync would want.
  void Clear() noexcept { m_known.clear(); }

private:
  /// What the client was told, and the tick it was last seen on.
  struct Told
  {
    EntityState state;
    std::uint32_t seen = 0;
  };

  template <typename Message>
  void Send(const Message& _message);

  LoopbackTransport& m_link;
  std::unordered_map<EntityId, Told> m_known;

  /// Stamped onto everything still visible, so what has left is whatever this
  /// pass did not stamp. Cheaper than building a second set per tick, and it
  /// cannot go stale: an entry that stops being visible is erased on the very
  /// next Write, so no stamp survives long enough for the counter to wrap back
  /// onto it.
  std::uint32_t m_generation = 0;
};

} // namespace Neuron
