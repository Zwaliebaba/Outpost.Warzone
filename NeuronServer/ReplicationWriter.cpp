#include "pch.h"
#include "ReplicationWriter.h"

#include "Debug.h"

namespace Neuron
{

namespace
{
/// Big enough for any replication record; the largest is Enter at 17 bytes.
constexpr std::size_t ReplicationScratchBytes = 64;

/// Whether anything an Update carries has moved.
[[nodiscard]] bool Moved(const EntityState& _was, const EntityState& _is) noexcept
{
  return _was.x != _is.x || _was.y != _is.y || _was.z != _is.z || _was.direction != _is.direction;
}

/// Whether anything only an Enter can state has changed. Not something a real
/// entity does -- but the caller says what it says, and an Update cannot carry
/// either field, so the alternative to restating is telling the client
/// something false and never correcting it.
[[nodiscard]] bool Restated(const EntityState& _was, const EntityState& _is) noexcept
{
  return _was.kind != _is.kind || _was.player != _is.player;
}

/// Whether _ids names _id. A raw scan because the list is normally empty and
/// almost never more than a handful: nothing dies on most ticks.
[[nodiscard]] bool Names(std::span<const EntityId> _ids, EntityId _id) noexcept
{
  for (const EntityId id : _ids)
  {
    if (id == _id)
      return true;
  }

  return false;
}

[[nodiscard]] ServerEnter EnterFor(const EntityState& _entity) noexcept
{
  ServerEnter enter;
  enter.entityId = _entity.id;
  enter.kind = _entity.kind;
  enter.player = _entity.player;
  enter.x = _entity.x;
  enter.y = _entity.y;
  enter.z = _entity.z;
  enter.direction = _entity.direction;
  return enter;
}

[[nodiscard]] ServerUpdate UpdateFor(const EntityState& _entity) noexcept
{
  ServerUpdate update;
  update.entityId = _entity.id;
  update.x = _entity.x;
  update.y = _entity.y;
  update.z = _entity.z;
  update.direction = _entity.direction;
  return update;
}
} // namespace

ReplicationWriter::ReplicationWriter(LoopbackTransport& _link) noexcept : m_link(_link)
{
}

template <typename Message>
void ReplicationWriter::Send(const Message& _message)
{
  std::byte scratch[ReplicationScratchBytes]{};
  NetWriter writer{scratch};
  Put(writer, _message);

  /* An overflow here would be a record outgrowing the buffer, which is our
     mistake rather than a peer's, so it is dropped loudly rather than sent
     half-written. */
  DEBUG_ASSERT_TEXT(!writer.Overflowed(), "ReplicationWriter: record does not fit the scratch buffer");
  if (writer.Overflowed())
    return;

  m_link.Send(LoopbackTransport::End::Server, NetChannel::Replication, writer.Written());
}

void ReplicationWriter::Write(std::span<const EntityState> _visible, std::span<const EntityId> _destroyed)
{
  m_generation += 1;

  /* Deaths first. An entity that died this tick must not be told to move on its
     way out: "it went there and then it exploded" is a worse story than "it
     exploded", and it is one frame of a corpse sliding. */
  for (const EntityId id : _destroyed)
  {
    if (m_known.erase(id) != 0)
      Send(ServerDestroy{id});
  }

  for (const EntityState& entity : _visible)
  {
    /* Named as both visible and destroyed. The death has already been sent and
       the entity forgotten, so entering it again here would resurrect it. */
    if (Names(_destroyed, entity.id))
      continue;

    const std::unordered_map<EntityId, Told>::iterator at = m_known.find(entity.id);

    if (at == m_known.end())
    {
      Send(EnterFor(entity));
      m_known.emplace(entity.id, Told{entity, m_generation});
      continue;
    }

    Told& told = at->second;

    if (Restated(told.state, entity))
      Send(EnterFor(entity));
    else if (Moved(told.state, entity))
      Send(UpdateFor(entity));

    /* Everything else costs nothing at all, which is the point. */
    told.state = entity;
    told.seen = m_generation;
  }

  /* Whatever this pass did not stamp is no longer visible and has not died, so
     the client is told it left rather than that it is gone. */
  for (std::unordered_map<EntityId, Told>::iterator at = m_known.begin(); at != m_known.end();)
  {
    if (at->second.seen == m_generation)
    {
      ++at;
      continue;
    }

    Send(ServerLeave{at->first});
    at = m_known.erase(at);
  }
}

} // namespace Neuron
