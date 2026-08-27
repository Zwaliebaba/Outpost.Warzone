#include "pch.h"
#include "CppUnitTest.h"

#include "ReplicationWriter.h"

#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Neuron;

/* One client's replication stream (Docs/ServerAuthority.md stage D).

   These read the bytes the writer actually put on the wire, rather than its
   internal record, because the record is not what a client sees. The stream is
   rendered as a short string -- "E1 U1 L1" -- so a case states the whole tick's
   traffic in one assertion instead of decoding message by message; what is
   being tested is a sequence, and asserting on a sequence one element at a time
   makes the interesting failure ("it sent an extra Update") the hardest one to
   read.

   The case that matters most is the empty one. A world that has not moved must
   cost nothing at all, and that is the property that decides whether this
   scales -- NET_CHECK_DROID broadcast position on a timer whether anything had
   changed or not, because no peer knew what any other peer already had. */
namespace NeuronServerTest
{
using End = LoopbackTransport::End;

namespace
{
/// Everything waiting on the replication channel, as one readable line.
std::string Drained(LoopbackTransport& _link)
{
  std::string out;
  LoopbackTransport::Message message;

  while (_link.Receive(End::Client, NetChannel::Replication, message))
  {
    NetReader reader{message.bytes};
    switch (static_cast<ServerMessage>(reader.U8()))
    {
    case ServerMessage::Enter:
      out += "E" + std::to_string(ServerEnter::Decode(reader).entityId);
      break;
    case ServerMessage::Update:
      out += "U" + std::to_string(ServerUpdate::Decode(reader).entityId);
      break;
    case ServerMessage::Leave:
      out += "L" + std::to_string(ServerLeave::Decode(reader).entityId);
      break;
    case ServerMessage::Destroy:
      out += "D" + std::to_string(ServerDestroy::Decode(reader).entityId);
      break;
    default:
      out += "?";
      break;
    }
    out += " ";
  }

  return out;
}

EntityState At(EntityId _id, std::uint16_t _x, float _direction = 0.0f)
{
  EntityState entity;
  entity.id = _id;
  entity.kind = EntityKind::Droid;
  entity.player = 1;
  entity.x = _x;
  entity.direction = _direction;
  return entity;
}
} // namespace

TEST_CLASS(ReplicationWriterTest)
{
public:
  TEST_METHOD(AWorldThatSitsStillCostsNothing)
  {
    LoopbackTransport link;
    ReplicationWriter writer{link};

    std::vector<EntityState> world{At(1, 10), At(2, 20)};
    writer.Write(world, {});
    Assert::AreEqual(std::string("E1 E2 "), Drained(link));
    Assert::AreEqual(static_cast<size_t>(2), writer.Known());

    writer.Write(world, {});
    Assert::AreEqual(std::string(), Drained(link));

    world[0].x = 11;
    writer.Write(world, {});
    Assert::AreEqual(std::string("U1 "), Drained(link));

    /* Turning on the spot is movement too. Direction is on the wire, so a
       change to it has to reach the client or the model faces the wrong way
       until something else moves. */
    world[1].direction = 0.25f;
    writer.Write(world, {});
    Assert::AreEqual(std::string("U2 "), Drained(link));
  }

  TEST_METHOD(LeavingAndDyingAreDifferentMessages)
  {
    LoopbackTransport link;
    ReplicationWriter writer{link};

    std::vector<EntityState> world{At(1, 1), At(2, 2), At(3, 3)};
    writer.Write(world, {});
    Drained(link);

    // 3 walks out of sight; 2 blows up
    const std::vector<EntityState> fewer{At(1, 1), At(2, 2)};
    const EntityId dead[] = {2u};
    writer.Write(fewer, dead);

    const std::string sent = Drained(link);
    Assert::IsTrue(sent.find("D2") != std::string::npos);
    Assert::IsTrue(sent.find("L3") != std::string::npos);

    /* And nothing moved on its way out: "it went there and then it exploded"
       is a frame of a corpse sliding. */
    Assert::IsTrue(sent.find("U") == std::string::npos);
    Assert::AreEqual(static_cast<size_t>(1), writer.Known());
  }

  TEST_METHOD(ADeathTheClientNeverSawIsNotAnnounced)
  {
    LoopbackTransport link;
    ReplicationWriter writer{link};

    const EntityId dead[] = {77u};
    writer.Write({}, dead);
    Assert::AreEqual(std::string(), Drained(link));
  }

  TEST_METHOD(ReEnteringTheInterestSetEntersAgain)
  {
    LoopbackTransport link;
    ReplicationWriter writer{link};
    const std::vector<EntityState> one{At(9, 5)};

    writer.Write(one, {});
    Assert::AreEqual(std::string("E9 "), Drained(link));

    writer.Write({}, {});
    Assert::AreEqual(std::string("L9 "), Drained(link));

    /* Enter rather than Update: the client threw the entity away when it left,
       so there is nothing there for an update to modify. */
    writer.Write(one, {});
    Assert::AreEqual(std::string("E9 "), Drained(link));
  }

  TEST_METHOD(WhatOnlyAnEnterCanCarryIsRestatedByAnEnter)
  {
    LoopbackTransport link;
    ReplicationWriter writer{link};
    std::vector<EntityState> one{At(4, 1)};
    writer.Write(one, {});
    Drained(link);

    /* Not something a real entity does. But the caller says what it says, and
       an Update carries neither field, so the alternative to restating is
       telling the client something false and never correcting it. */
    one[0].player = 2;
    writer.Write(one, {});
    Assert::AreEqual(std::string("E4 "), Drained(link));

    one[0].kind = EntityKind::Feature;
    writer.Write(one, {});
    Assert::AreEqual(std::string("E4 "), Drained(link));
  }

  TEST_METHOD(AnEntityBothVisibleAndDestroyedStaysDead)
  {
    LoopbackTransport link;
    ReplicationWriter writer{link};
    const std::vector<EntityState> one{At(6, 1)};
    writer.Write(one, {});
    Drained(link);

    /* A caller contradicting itself. Of the two answers, "it blew up" is the
       one that cannot be silently wrong: an entity left drawn forever is worse
       than one removed with an explosion nobody expected. */
    const EntityId dead[] = {6u};
    writer.Write(one, dead);
    Assert::AreEqual(std::string("D6 "), Drained(link));
    Assert::AreEqual(static_cast<size_t>(0), writer.Known());
  }

  TEST_METHOD(ClearReEntersTheWholeWorld)
  {
    LoopbackTransport link;
    ReplicationWriter writer{link};
    const std::vector<EntityState> world{At(1, 1), At(2, 2)};
    writer.Write(world, {});
    Drained(link);

    writer.Clear();
    Assert::AreEqual(static_cast<size_t>(0), writer.Known());

    writer.Write(world, {});
    Assert::AreEqual(std::string("E1 E2 "), Drained(link));
  }
};
} // namespace NeuronServerTest
