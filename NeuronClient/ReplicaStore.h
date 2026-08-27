/*
 * ReplicaStore.h
 *
 * The client's world: what the server has said exists, and nothing else.
 */

#pragma once

#include "Protocol.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>

namespace Neuron
{

/// One entity as the client knows it.
///
/// Everything here arrived on the wire. What an entity needs beyond this --
/// pitch and roll, body and turret, damage, animation state -- lands as the
/// renderer is actually fed from the store, because that is when it becomes
/// clear which of it the server has to state and which the client can derive
/// from a map it already has.
struct Replica
{
  EntityId id = 0;

  /// Only meaningful in a replica that exists, which is one an Enter created.
  /// There is deliberately no "unknown" kind: an entity nobody entered is
  /// absent from the store rather than present with a placeholder.
  EntityKind kind = EntityKind::Droid;

  std::uint8_t player = 0;
  std::uint16_t x = 0;
  std::uint16_t y = 0;
  std::uint16_t z = 0;

  /// Radians, +ve rotation about y, as BASE_OBJECT stores it.
  float direction = 0.0f;
};

/// What one replication message did.
enum class ReplicaChange : std::uint8_t
{
  /// Nothing. The message could not be read, or named an entity the store has
  /// never been told about.
  None,

  Entered,
  Updated,

  /// Left the interest set. It still exists somewhere; this client can no
  /// longer see it.
  Left,

  /// Ceased to exist.
  Destroyed,
};

/// A change and the entity it happened to.
struct ReplicaEvent
{
  ReplicaChange change = ReplicaChange::None;

  /// The entity as it now stands or, for Left and Destroyed, as it last stood
  /// before it was removed. A caller that wants to put an explosion where
  /// something died needs the position it died at, and by the time it is told,
  /// the store no longer has it.
  Replica replica;
};

/// The client's replica world, built out of replication messages.
///
/// **There is no way to put anything in here except by receiving it.** The
/// mutators are private and Apply is the only door, so a client cannot decide
/// that something ought to exist and make it so -- which is the entire
/// inversion this design exists for, stated as a class interface rather than as
/// a rule people have to remember. The 1998 client created objects because it
/// simulated them; this one cannot create one at all.
class ReplicaStore
{
public:
  using Map = std::unordered_map<EntityId, Replica>;

  /// Reads one replication message and applies it, returning what it did.
  ///
  /// A message that cannot be read changes nothing. Neither does an Update,
  /// Leave or Destroy naming an entity that was never entered: a client must
  /// not invent an entity out of a delta, because what it invented would be
  /// missing whatever that message does not carry, and it would look like a
  /// real object while being wrong.
  ReplicaEvent Apply(std::span<const std::byte> _bytes);

  /// The entity, or nullptr if the store has not been told about it.
  [[nodiscard]] const Replica* Find(EntityId _id) const noexcept;

  [[nodiscard]] std::size_t Count() const noexcept { return m_entities.size(); }

  /// For walking the world, which is what the renderer will do with it.
  [[nodiscard]] Map::const_iterator begin() const noexcept { return m_entities.begin(); }
  [[nodiscard]] Map::const_iterator end() const noexcept { return m_entities.end(); }

  /// Forgets everything, for ending a session.
  void Clear() noexcept { m_entities.clear(); }

private:
  ReplicaEvent OnEnter(NetReader& _reader);
  ReplicaEvent OnUpdate(NetReader& _reader);
  ReplicaEvent Remove(EntityId _id, ReplicaChange _change);

  Map m_entities;
};

} // namespace Neuron
