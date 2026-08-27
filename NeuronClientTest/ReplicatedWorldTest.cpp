#include "pch.h"
#include "CppUnitTest.h"

#include "ReplicaStore.h"
#include "ReplicationWriter.h"

#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Neuron;

/* The boundary, end to end (Docs/ServerAuthority.md stage D): a server world,
   the messages it produces, and the client world rebuilt out of nothing else.

   Neither half's own tests can state this property. ReplicationWriterTest says
   the right messages go out; ReplicaStoreTest says each one is applied
   correctly; only running them against each other says the two worlds agree --
   which is the claim the whole stage rests on, and the one that would fail
   silently if an Update were dropped for an entity that had in fact moved.

   This is why NeuronClientTest references NeuronServer, which is the one place
   the project map in AGENTS.md is deviated from. The test belongs to neither
   half, the repository has no home for a test of the boundary itself, and an
   eighth project for one test class buys less than it costs. NeuronServer is
   three translation units over NeuronCore, so the direction taken is the cheap
   one; the reverse would drag D3D9, DirectInput and XAudio2 into the server's
   test. */
namespace NeuronClientTest
{
using End = LoopbackTransport::End;

namespace
{
void Feed(LoopbackTransport& _link, ReplicaStore& _store)
{
  LoopbackTransport::Message message;
  while (_link.Receive(End::Client, NetChannel::Replication, message))
    _store.Apply(message.bytes);
}

EntityState At(EntityId _id, std::uint16_t _x, std::uint16_t _y, float _direction)
{
  EntityState entity;
  entity.id = _id;
  entity.kind = EntityKind::Droid;
  entity.player = 1;
  entity.x = _x;
  entity.y = _y;
  entity.direction = _direction;
  return entity;
}

/// Whether the client's world is the server's world.
void AssertWorldsAgree(const std::vector<EntityState>& _world, const ReplicaStore& _store)
{
  Assert::AreEqual(_world.size(), _store.Count());

  for (const EntityState& entity : _world)
  {
    const Replica* replica = _store.Find(entity.id);
    Assert::IsNotNull(replica);
    if (replica == nullptr)
      continue;

    Assert::AreEqual(entity.x, replica->x);
    Assert::AreEqual(entity.y, replica->y);
    Assert::AreEqual(entity.z, replica->z);
    Assert::IsTrue(entity.direction == replica->direction);
    Assert::IsTrue(entity.kind == replica->kind);
    Assert::AreEqual(entity.player, replica->player);
  }
}
} // namespace

TEST_CLASS(ReplicatedWorldTest)
{
public:
  TEST_METHOD(TheClientsWorldIsTheServersWorld)
  {
    LoopbackTransport link;
    ReplicationWriter writer{link};
    ReplicaStore store;

    std::vector<EntityState> world{
      At(1u, 10u, 100u, 0.5f),
      At(2u, 20u, 200u, -0.5f),
      At(3u, 30u, 300u, 0.0f),
    };

    writer.Write(world, {});
    Feed(link, store);
    AssertWorldsAgree(world, store);

    /* Fifty ticks of a world that moves, loses an entity to fog, gains one, and
       loses one to an explosion -- checked every tick rather than at the end,
       because a replication bug that corrects itself later is still a frame the
       player saw wrong. */
    for (int tick = 0; tick < 50; ++tick)
    {
      for (EntityState& entity : world)
        entity.x = static_cast<std::uint16_t>(entity.x + 1);

      std::vector<EntityId> dead;

      if (tick == 10)
        world.erase(world.begin() + 1); // 2 walks into fog
      if (tick == 20)
        world.push_back(At(4u, 40u, 400u, 1.0f)); // 4 comes into view
      if (tick == 30)
      {
        dead.push_back(world.front().id); // 1 is destroyed
        world.erase(world.begin());
      }

      writer.Write(world, dead);
      Feed(link, store);
      AssertWorldsAgree(world, store);
    }

    Assert::IsNull(store.Find(1u)); // destroyed
    Assert::IsNull(store.Find(2u)); // left
    Assert::AreEqual(static_cast<size_t>(2), store.Count());
  }

  TEST_METHOD(AnEntityThatLeftAndCameBackIsWholeAgain)
  {
    LoopbackTransport link;
    ReplicationWriter writer{link};
    ReplicaStore store;

    const std::vector<EntityState> one{At(9u, 5u, 6u, 0.75f)};
    writer.Write(one, {});
    Feed(link, store);

    writer.Write({}, {});
    Feed(link, store);
    Assert::AreEqual(static_cast<size_t>(0), store.Count());

    /* Re-entering restates everything, so what comes back carries the fields an
       Update never had -- which is the reason a Leave throws the entity away
       rather than keeping a stale copy. */
    writer.Write(one, {});
    Feed(link, store);
    AssertWorldsAgree(one, store);
  }
};
} // namespace NeuronClientTest
