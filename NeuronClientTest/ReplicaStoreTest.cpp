#include "pch.h"
#include "CppUnitTest.h"

#include "ClientSession.h"
#include "ReplicaStore.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Neuron;

/* The client's replica world (Docs/ServerAuthority.md stage D).

   The property under test is the inversion itself: everything in the store got
   there by arriving, and nothing else can put anything there. So the cases that
   matter most are the ones where the store is asked to believe something it was
   not told - an update for an entity nobody entered, an entity of a kind this
   build has no name for, a message cut short - and every one of them has to
   leave the world exactly as it was. */
namespace NeuronClientTest
{
namespace
{
template <typename Message>
std::vector<std::byte> Encoded(const Message& _message)
{
  std::byte scratch[64]{};
  NetWriter writer{scratch};
  Put(writer, _message);

  const std::span<const std::byte> written = writer.Written();
  return std::vector<std::byte>(written.begin(), written.end());
}
} // namespace

TEST_CLASS(ReplicaStoreTest)
{
public:
  TEST_METHOD(AWorldArrivesMovesAndGoesAway)
  {
    ReplicaStore store;
    Assert::AreEqual(static_cast<size_t>(0), store.Count());

    ServerEnter enter;
    enter.entityId = 42;
    enter.kind = EntityKind::Structure;
    enter.player = 3;
    enter.x = 100;
    enter.y = 200;
    enter.z = 300;
    enter.direction = 1.5f;

    ReplicaEvent event = store.Apply(Encoded(enter));
    Assert::IsTrue(event.change == ReplicaChange::Entered);
    Assert::AreEqual(static_cast<std::uint32_t>(42), event.replica.id);
    Assert::AreEqual(static_cast<size_t>(1), store.Count());

    const Replica* replica = store.Find(42);
    Assert::IsNotNull(replica);
    Assert::AreEqual(static_cast<std::uint16_t>(100), replica->x);
    Assert::IsTrue(replica->direction == 1.5f); // exact in binary, so == is the right test

    event = store.Apply(Encoded(ServerUpdate{42u, 101u, 201u, 301u, -0.5f}));
    Assert::IsTrue(event.change == ReplicaChange::Updated);

    replica = store.Find(42);
    Assert::IsNotNull(replica);
    Assert::AreEqual(static_cast<std::uint16_t>(101), replica->x);
    Assert::IsTrue(replica->direction == -0.5f);

    /* Kind and player are stated once by Enter, so an Update does not carry
       them and must not lose them either. */
    Assert::IsTrue(replica->kind == EntityKind::Structure);
    Assert::AreEqual(static_cast<std::uint8_t>(3), replica->player);

    event = store.Apply(Encoded(ServerDestroy{42u}));
    Assert::IsTrue(event.change == ReplicaChange::Destroyed);

    /* Where it died, which the store no longer has -- and which whoever puts an
       explosion there needs. */
    Assert::AreEqual(static_cast<std::uint16_t>(101), event.replica.x);
    Assert::AreEqual(static_cast<size_t>(0), store.Count());
    Assert::IsNull(store.Find(42));
  }

  TEST_METHOD(LeavingIsNotDying)
  {
    ReplicaStore store;
    store.Apply(Encoded(ServerEnter{7u, EntityKind::Droid, 1u, 1u, 2u, 3u, 0.0f}));

    const ReplicaEvent event = store.Apply(Encoded(ServerLeave{7u}));
    Assert::IsTrue(event.change == ReplicaChange::Left);
    Assert::AreEqual(static_cast<std::uint32_t>(7), event.replica.id);
    Assert::AreEqual(static_cast<size_t>(0), store.Count());
  }

  TEST_METHOD(AnEntityCannotBeInventedOutOfADelta)
  {
    ReplicaStore store;

    Assert::IsTrue(store.Apply(Encoded(ServerUpdate{99u, 1u, 2u, 3u, 0.0f})).change == ReplicaChange::None);
    Assert::IsTrue(store.Apply(Encoded(ServerLeave{99u})).change == ReplicaChange::None);
    Assert::IsTrue(store.Apply(Encoded(ServerDestroy{99u})).change == ReplicaChange::None);
    Assert::AreEqual(static_cast<size_t>(0), store.Count());
  }

  TEST_METHOD(EnteringTwiceReplacesRatherThanDuplicates)
  {
    ReplicaStore store;
    store.Apply(Encoded(ServerEnter{5u, EntityKind::Droid, 1u, 10u, 10u, 10u, 0.0f}));

    const ReplicaEvent event = store.Apply(Encoded(ServerEnter{5u, EntityKind::Feature, 2u, 20u, 20u, 20u, 0.0f}));
    Assert::IsTrue(event.change == ReplicaChange::Entered);
    Assert::AreEqual(static_cast<size_t>(1), store.Count());

    const Replica* replica = store.Find(5);
    Assert::IsNotNull(replica);
    Assert::IsTrue(replica->kind == EntityKind::Feature);
    Assert::AreEqual(static_cast<std::uint8_t>(2), replica->player);
    Assert::AreEqual(static_cast<std::uint16_t>(20), replica->x);
  }

  TEST_METHOD(NonsenseChangesNothing)
  {
    ReplicaStore store;
    store.Apply(Encoded(ServerEnter{1u, EntityKind::Droid, 0u, 1u, 1u, 1u, 0.0f}));

    {
      // an id this build has never heard of
      const std::byte unknown[1]{std::byte{0xFE}};
      Assert::IsTrue(store.Apply(unknown).change == ReplicaChange::None);
    }
    {
      // an Update cut short
      std::byte scratch[8]{};
      NetWriter writer{scratch};
      writer.U8(static_cast<std::uint8_t>(ServerMessage::Update));
      writer.U32(1u);
      Assert::IsTrue(store.Apply(writer.Written()).change == ReplicaChange::None);
    }
    {
      // an entity of a kind this build has no name for
      std::byte scratch[32]{};
      NetWriter writer{scratch};
      writer.U8(static_cast<std::uint8_t>(ServerMessage::Enter));
      writer.U32(2u);
      writer.U8(0xEEu);
      writer.U8(0u);
      writer.U16(1u);
      writer.U16(1u);
      writer.U16(1u);
      writer.F32(0.0f);
      Assert::IsTrue(store.Apply(writer.Written()).change == ReplicaChange::None);
    }

    Assert::AreEqual(static_cast<size_t>(1), store.Count());
    Assert::IsNotNull(store.Find(1));
    Assert::IsNull(store.Find(2));
  }

  TEST_METHOD(TheSessionAndTheStoreShareALinkWithoutEatingEachOther)
  {
    LoopbackTransport link;
    ClientSession client{link, 0u};
    ReplicaStore store;

    client.Begin();
    {
      LoopbackTransport::Message hello;
      Assert::IsTrue(link.Receive(LoopbackTransport::End::Server, hello));
    }

    /* Replication arrives before the handshake is even answered, which is the
       case that would go wrong: a session draining the whole link would take
       these bytes and drop them, and the world would come up empty with
       nothing to say why. */
    const std::vector<std::byte> enter = Encoded(ServerEnter{11u, EntityKind::Droid, 0u, 4u, 5u, 6u, 0.0f});
    link.Send(LoopbackTransport::End::Server, NetChannel::Replication, enter);

    ServerHello answer;
    answer.result = HandshakeResult::Accepted;
    answer.tickMs = 40;
    answer.connectionId = 1;
    const std::vector<std::byte> greeting = Encoded(answer);
    link.Send(LoopbackTransport::End::Server, NetChannel::Session, greeting);

    client.Service();
    Assert::IsTrue(client.CurrentState() == ClientSession::State::Greeted);

    LoopbackTransport::Message message;
    Assert::IsTrue(link.Receive(LoopbackTransport::End::Client, NetChannel::Replication, message));
    Assert::IsTrue(store.Apply(message.bytes).change == ReplicaChange::Entered);
    Assert::AreEqual(static_cast<size_t>(1), store.Count());
    Assert::AreEqual(static_cast<size_t>(0), link.Pending(LoopbackTransport::End::Client));
  }
};
} // namespace NeuronClientTest
