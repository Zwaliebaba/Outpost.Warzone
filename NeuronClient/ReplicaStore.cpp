#include "pch.h"
#include "ReplicaStore.h"

namespace Neuron
{

ReplicaEvent ReplicaStore::Apply(std::span<const std::byte> _bytes)
{
  NetReader reader{_bytes};
  const auto id = static_cast<ServerMessage>(reader.U8());

  /* A message id this build does not know is ignored: a newer server may send
     one, and the alternative is switching on whatever it collides with. */
  if (!reader.Ok() || !IsKnown(id))
    return {};

  switch (id)
  {
  case ServerMessage::Enter:
    return OnEnter(reader);

  case ServerMessage::Update:
    return OnUpdate(reader);

  /* Leave and Destroy carry the same body and do the same thing to the store.
     They stay separate messages because the client does different things with
     them -- an entity that walked into fog is not one that blew up - and each
     is decoded by its own record rather than by whichever happens to match,
     so the day one of them grows a field the other does not, this still reads
     what was actually sent. */
  case ServerMessage::Leave:
  {
    const ServerLeave leave = ServerLeave::Decode(reader);
    return reader.Ok() ? Remove(leave.entityId, ReplicaChange::Left) : ReplicaEvent{};
  }

  case ServerMessage::Destroy:
  {
    const ServerDestroy destroy = ServerDestroy::Decode(reader);
    return reader.Ok() ? Remove(destroy.entityId, ReplicaChange::Destroyed) : ReplicaEvent{};
  }

  default:
    /* Something else on the replication plane. Not this store's message, and
       not an error: the plane carries more than entity lifecycle. */
    return {};
  }
}

ReplicaEvent ReplicaStore::OnEnter(NetReader& _reader)
{
  const ServerEnter enter = ServerEnter::Decode(_reader);
  if (!_reader.Ok())
    return {};

  Replica replica;
  replica.id = enter.entityId;
  replica.kind = enter.kind;
  replica.player = enter.player;
  replica.x = enter.x;
  replica.y = enter.y;
  replica.z = enter.z;
  replica.direction = enter.direction;

  /* An Enter for an entity already here replaces it rather than being dropped.
     The server is restating the whole entity, and re-entering an interest set
     after having left it is the ordinary way that happens; two copies of one
     entity is not a state that can exist. */
  m_entities[replica.id] = replica;

  return {ReplicaChange::Entered, replica};
}

ReplicaEvent ReplicaStore::OnUpdate(NetReader& _reader)
{
  const ServerUpdate update = ServerUpdate::Decode(_reader);
  if (!_reader.Ok())
    return {};

  const Map::iterator at = m_entities.find(update.entityId);
  if (at == m_entities.end())
    return {};

  Replica& replica = at->second;
  replica.x = update.x;
  replica.y = update.y;
  replica.z = update.z;
  replica.direction = update.direction;

  return {ReplicaChange::Updated, replica};
}

ReplicaEvent ReplicaStore::Remove(EntityId _id, ReplicaChange _change)
{
  const Map::iterator at = m_entities.find(_id);
  if (at == m_entities.end())
    return {};

  const Replica removed = at->second;
  m_entities.erase(at);

  return {_change, removed};
}

const Replica* ReplicaStore::Find(EntityId _id) const noexcept
{
  const Map::const_iterator at = m_entities.find(_id);
  return at == m_entities.end() ? nullptr : &at->second;
}

} // namespace Neuron
