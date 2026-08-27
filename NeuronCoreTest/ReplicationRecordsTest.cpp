#include "pch.h"
#include "CppUnitTest.h"

#include "NetReader.h"
#include "NetWriter.h"
#include "Protocol.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Neuron;

/* The replication records (Docs/ServerAuthority.md stage D): the messages that
   give a client a world it did not simulate.

   Two things are worth a test rather than a comment. The layouts are pinned by
   their bytes, not by a round trip, because a round trip passes even when both
   ends agree on the wrong layout - and these are the records a future
   compression pass will be tempted to rearrange. And a field that arrives whole
   and still means nothing has to be distinguishable from one that was cut
   short, because a client that drew an entity of a kind it does not know would
   be drawing a guess. */
namespace NeuronCoreTest
{
TEST_CLASS(ReplicationRecordsTest)
{
public:
  TEST_METHOD(TheRecordsRoundTrip)
  {
    std::byte scratch[64]{};
    NetWriter writer{scratch};

    ServerEnter enter;
    enter.entityId = 0xDEADBEEF;
    enter.kind = EntityKind::Structure;
    enter.player = 5;
    enter.x = 1000;
    enter.y = 2000;
    enter.z = 3000;
    enter.direction = -1.25f;
    Put(writer, enter);

    NetReader reader{writer.Written()};
    Assert::IsTrue(static_cast<ServerMessage>(reader.U8()) == ServerMessage::Enter);

    const ServerEnter back = ServerEnter::Decode(reader);
    Assert::IsTrue(reader.Ok());
    Assert::AreEqual(static_cast<std::uint32_t>(0xDEADBEEF), back.entityId);
    Assert::IsTrue(back.kind == EntityKind::Structure);
    Assert::AreEqual(static_cast<std::uint8_t>(5), back.player);
    Assert::AreEqual(static_cast<std::uint16_t>(1000), back.x);
    Assert::AreEqual(static_cast<std::uint16_t>(2000), back.y);
    Assert::AreEqual(static_cast<std::uint16_t>(3000), back.z);
    Assert::IsTrue(back.direction == -1.25f); // exact in binary, so == is the right test
  }

  TEST_METHOD(TheMovingRecordsRoundTrip)
  {
    std::byte scratch[64]{};
    NetWriter writer{scratch};
    Put(writer, ServerUpdate{7u, 11u, 22u, 33u, 0.5f});
    Put(writer, ServerLeave{8u});
    Put(writer, ServerDestroy{9u});

    NetReader reader{writer.Written()};

    Assert::IsTrue(static_cast<ServerMessage>(reader.U8()) == ServerMessage::Update);
    const ServerUpdate update = ServerUpdate::Decode(reader);
    Assert::AreEqual(static_cast<std::uint32_t>(7), update.entityId);
    Assert::AreEqual(static_cast<std::uint16_t>(11), update.x);
    Assert::AreEqual(static_cast<std::uint16_t>(22), update.y);
    Assert::AreEqual(static_cast<std::uint16_t>(33), update.z);
    Assert::IsTrue(update.direction == 0.5f);

    Assert::IsTrue(static_cast<ServerMessage>(reader.U8()) == ServerMessage::Leave);
    Assert::AreEqual(static_cast<std::uint32_t>(8), ServerLeave::Decode(reader).entityId);

    Assert::IsTrue(static_cast<ServerMessage>(reader.U8()) == ServerMessage::Destroy);
    Assert::AreEqual(static_cast<std::uint32_t>(9), ServerDestroy::Decode(reader).entityId);

    Assert::IsTrue(reader.Ok());
    Assert::AreEqual(static_cast<size_t>(0), reader.Remaining());
  }

  TEST_METHOD(TheEnterLayoutIsPinned)
  {
    std::byte scratch[32]{};
    NetWriter writer{scratch};

    ServerEnter enter;
    enter.entityId = 0x04030201;
    enter.kind = EntityKind::Feature;
    enter.player = 0x77;
    enter.x = 0x0605;
    enter.y = 0x0807;
    enter.z = 0x0A09;
    enter.direction = 0.0f;
    enter.Encode(writer);

    const std::span<const std::byte> bytes = writer.Written();
    Assert::AreEqual(static_cast<size_t>(16), bytes.size());

    const std::uint8_t expected[] = {
      0x01, 0x02, 0x03, 0x04,                     // entityId, little-endian
      static_cast<std::uint8_t>(EntityKind::Feature),
      0x77,                                       // player
      0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,         // x, y, z
      0x00, 0x00, 0x00, 0x00,                     // direction
    };
    for (size_t i = 0; i < bytes.size(); ++i)
      Assert::AreEqual(expected[i], static_cast<std::uint8_t>(bytes[i]));
  }

  TEST_METHOD(AnEntityKindThisBuildDoesNotKnowIsNotDecoded)
  {
    std::byte scratch[32]{};
    NetWriter writer{scratch};
    writer.U32(1u);
    writer.U8(0xEEu);   // a kind from a newer server
    writer.U8(0u);
    writer.U16(1u);
    writer.U16(2u);
    writer.U16(3u);
    writer.F32(0.0f);

    NetReader reader{writer.Written()};
    (void)ServerEnter::Decode(reader);

    /* Every byte was there, so this is not truncation -- and the two facts stay
       apart, because "cut short" and "arrived whole and meant nothing" are
       different things to have to diagnose. */
    Assert::IsFalse(reader.Ok());
    Assert::IsFalse(reader.Truncated());
  }

  TEST_METHOD(InvalidityIsStickyLikeTruncation)
  {
    std::byte scratch[8]{};
    NetWriter writer{scratch};
    writer.U32(0x12345678u);

    NetReader reader{writer.Written()};
    Assert::IsTrue(reader.Ok());

    reader.Invalidate();
    Assert::IsFalse(reader.Ok());
    Assert::IsFalse(reader.Truncated());

    /* Nothing clears it, so a caller that asks once at the end still gets the
       answer however early the field that failed was read. */
    Assert::IsFalse(reader.Ok());
  }
};
} // namespace NeuronCoreTest
