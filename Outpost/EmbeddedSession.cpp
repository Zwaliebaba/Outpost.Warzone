#include "pch.h"
#include "EmbeddedSession.h"

#include "Base.h"
#include "Debug.h"
#include "Deliverance.h"
#include "Frame.h"
#include "FrontEnd.h"
#include "ObjMem.h"
#include "UiEvents.h"

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

/// The level the game has loaded, as the number both halves name it by. The
/// two halves are one process, so they agree by construction; what this puts
/// in place is the check itself, which a separated server will run for real.
[[nodiscard]] std::uint32_t LoadedLevelHash()
{
  return HashString(pLevelName);
}
} // namespace

EmbeddedSession& EmbeddedSession::Instance()
{
  static EmbeddedSession session;
  return session;
}

bool EmbeddedSession::EnsureOpen(void)
{
  if (m_failed)
    return false;

  if (m_client.CurrentState() == Neuron::ClientSession::State::Running)
    return true;

  const std::uint32_t level = LoadedLevelHash();

  m_client.Begin();
  m_server.Service();

  m_client.Service();
  m_server.Start(level);

  m_client.Service();
  m_client.ReportReady(level);
  m_server.Service();

  if (m_client.CurrentState() != Neuron::ClientSession::State::Running ||
      m_server.CurrentState() != Neuron::ServerSession::State::Running)
  {
    /* Cannot happen: both halves are this executable, so the version, the
       build hash and the level are its own, and the server accepts all three.
       If it ever does, this says so once and stops. */
    Neuron::DebugTrace("EmbeddedSession: the local session would not open\n");
    m_failed = true;
    return false;
  }

  return true;
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

void EmbeddedSession::ApplyControls(void)
{
  Neuron::ClientSessionControl control;
  while (m_server.TakeControl(control))
  {
    /* The session validated it against the policy and the range; applying it
       is the world's business, and the clock is the world's. */
    if (control.kind == Neuron::SessionControlKind::GameSpeed)
      gameTimeSetMod(control.value);
  }
}

void EmbeddedSession::Receive(void)
{
  /* One channel, in order: a camera move the script asked for after entering
     an object arrives after the Enter, because the server sent it after. */
  Neuron::LoopbackTransport::Message message;
  while (m_link.Receive(Neuron::LoopbackTransport::End::Client, Neuron::NetChannel::Replication, message))
  {
    Neuron::NetReader reader{message.bytes};
    const auto id = static_cast<Neuron::ServerMessage>(reader.U8());

    if (id == Neuron::ServerMessage::UiEvent)
    {
      const Neuron::ServerUiEvent event = Neuron::ServerUiEvent::Decode(reader);
      if (reader.Ok())
        ApplyUiEvent(event);
      continue;
    }

    m_store.Apply(message.bytes);
  }

  Neuron::ServerCommandReject reject;
  while (m_client.TakeReject(reject))
  {
    Neuron::DebugTrace("EmbeddedSession: the server refused request {} (reason {})\n", reject.sequence,
                       static_cast<unsigned>(reject.reason));
  }
}

void EmbeddedSession::Tick(void)
{
  if (!EnsureOpen())
    return;

  m_server.Service();
  ApplyControls();
  m_server.Tick();

  Gather();
  m_writer.Write(m_visible, m_destroyed);

  m_client.Service();
  Receive();

#ifdef DEBUG
  Verify();
#endif
}

void EmbeddedSession::Emit(const Neuron::ServerUiEvent& _event)
{
  /* A script's first tick can ask for a briefing before Tick() has ever run,
     so the session is opened here rather than the event being dropped on a
     handshake still in flight. */
  if (!EnsureOpen())
    return;

  if (!m_server.SendUiEvent(_event))
    Neuron::DebugTrace("EmbeddedSession: UiEvent {} was not sent\n", static_cast<unsigned>(_event.kind));
}

void EmbeddedSession::RequestGameSpeed(float _modifier)
{
  if (!EnsureOpen())
    return;

  m_client.RequestGameSpeed(_modifier);
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
