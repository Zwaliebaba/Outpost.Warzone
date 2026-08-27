#include "pch.h"
#include "EmbeddedSession.h"

#include "Base.h"
#include "Debug.h"
#include "Deliverance.h"
#include "Frame.h"
#include "ObjMem.h"

namespace
{
using Neuron::EntityKind;
using Neuron::EntityState;

/// Which of the wire's kinds an object is, or FALSE if it is not a thing the
/// world contains. OBJ_TARGET is a camera-tracking placeholder rather than an
/// entity, so it has no kind and is never replicated.
[[nodiscard]] bool KindOf(OBJECT_TYPE _type, EntityKind& _outKind)
{
  switch (_type)
  {
  case OBJ_DROID: _outKind = EntityKind::Droid; return true;
  case OBJ_STRUCTURE: _outKind = EntityKind::Structure; return true;
  case OBJ_FEATURE: _outKind = EntityKind::Feature; return true;
  case OBJ_BULLET: _outKind = EntityKind::Projectile; return true;
  default: return false;
  }
}

/// One object in the terms the wire uses.
///
/// Templated on the concrete type rather than taking a BASE_OBJECT*, so the
/// fields are read through the type the object actually is. Every one of them
/// comes from BASE_ELEMENTS and so exists on all three lists, but reaching them
/// through a cast would be asking the compiler to take the layout on trust for
/// no gain.
template <typename Object>
[[nodiscard]] EntityState StateOf(const Object* _object, EntityKind _kind)
{
  EntityState entity;
  entity.id = _object->id;
  entity.kind = _kind;
  entity.player = static_cast<std::uint8_t>(_object->player);
  entity.x = _object->x;
  entity.y = _object->y;
  entity.z = _object->z;
  entity.direction = _object->direction;
  return entity;
}

/// Walks one player's list into _outVisible.
///
/// Anything with a died stamp is skipped. On these lists that should be
/// nothing -- an object is taken off its list before it joins the destroyed
/// one -- but an object that was both alive and dead in the same tick would be
/// named as visible and destroyed at once, and the writer would then be right
/// to bury it while the world still drew it.
template <typename Object>
void GatherList(const Object* _list, EntityKind _kind, std::vector<EntityState>& _outVisible)
{
  for (const Object* object = _list; object != nullptr; object = object->psNext)
  {
    if (object->died != 0)
      continue;

    _outVisible.push_back(StateOf(object, _kind));
  }
}
} // namespace

void EmbeddedSession::Open(void)
{
  m_client.Begin();
  m_server.Service();

  m_client.Service();

  /* No map hash yet. Naming the level is what ServerStart is for, but a client
     that loaded the wrong one still cannot say so -- ClientMessage::Ready
     carries no body -- so a hash here would be checked against nothing.
     Docs/ServerAuthority.md carries that gap; this is the other end of it. */
  m_server.Start(0u);

  m_client.Service();
  m_client.ReportReady();
  m_server.Service();
}

void EmbeddedSession::Gather(void)
{
  m_visible.clear();
  m_destroyed.clear();

  for (UDWORD player = 0; player < MAX_PLAYERS; player += 1)
  {
    GatherList(apsDroidLists[player], EntityKind::Droid, m_visible);
    GatherList(apsStructLists[player], EntityKind::Structure, m_visible);
    GatherList(apsFeatureLists[player], EntityKind::Feature, m_visible);
  }

  /* objmemUpdate has just run and freed everything that died before this tick,
     so what is left on the list died on it. An object the client never saw is
     named here too; the writer drops those, since a client is not told about
     the death of something it was never told existed. */
  for (const BASE_OBJECT* dead = psDestroyedObj; dead != nullptr; dead = dead->psNext)
  {
    EntityKind kind = EntityKind::Droid;
    if (KindOf(dead->type, kind))
      m_destroyed.push_back(dead->id);
  }
}

void EmbeddedSession::Tick(void)
{
  if (m_failed)
    return;

  if (m_client.CurrentState() != Neuron::ClientSession::State::Running)
  {
    Open();

    if (m_client.CurrentState() != Neuron::ClientSession::State::Running)
    {
      /* Cannot happen: both halves are this executable, so the version and the
         build hash are its own and Consider accepts. If it ever does, this says
         so once and stops. */
      Neuron::DebugTrace("EmbeddedSession: the local session would not open\n");
      m_failed = true;
      return;
    }
  }

  m_server.Service();
  m_server.Tick();

  Gather();
  m_writer.Write(m_visible, m_destroyed);

  m_client.Service();

  Neuron::LoopbackTransport::Message message;
  while (m_link.Receive(Neuron::LoopbackTransport::End::Client, Neuron::NetChannel::Replication, message))
    m_store.Apply(message.bytes);

#ifdef DEBUG
  Verify();
#endif
}

#ifdef DEBUG
void EmbeddedSession::Verify(void) const
{
  /* Reported rather than asserted, and only the first disagreement in a tick.
     A mismatch here is a replication bug and wants fixing -- that is what this
     whole step is for -- but nothing draws from the replica world yet, so
     killing the game over one would stop stage D's own gate, booting CAM_1A and
     playing it to a mission win, from being runnable at all. It becomes an
     assertion in the step that puts the renderer on the store, where a
     disagreement stops being survivable. */

  if (m_store.Count() != m_visible.size())
  {
    Neuron::DebugTrace("EmbeddedSession: the client has {} entities and the world has {}\n", m_store.Count(),
                       m_visible.size());
    return;
  }

  for (const EntityState& entity : m_visible)
  {
    const Neuron::Replica* replica = m_store.Find(entity.id);

    if (replica == nullptr)
    {
      Neuron::DebugTrace("EmbeddedSession: entity {} was never replicated\n", entity.id);
      return;
    }

    if (replica->x != entity.x || replica->y != entity.y || replica->z != entity.z)
    {
      Neuron::DebugTrace("EmbeddedSession: entity {} is at ({},{},{}) and the client has it at ({},{},{})\n", entity.id,
                         entity.x, entity.y, entity.z, replica->x, replica->y, replica->z);
      return;
    }

    if (replica->direction != entity.direction)
    {
      Neuron::DebugTrace("EmbeddedSession: entity {} faces {} and the client has {}\n", entity.id, entity.direction,
                         replica->direction);
      return;
    }

    if (replica->kind != entity.kind || replica->player != entity.player)
    {
      Neuron::DebugTrace("EmbeddedSession: entity {} is kind {} player {} and the client has kind {} player {}\n",
                         entity.id, static_cast<unsigned>(entity.kind), static_cast<unsigned>(entity.player),
                         static_cast<unsigned>(replica->kind), static_cast<unsigned>(replica->player));
      return;
    }
  }
}
#endif
