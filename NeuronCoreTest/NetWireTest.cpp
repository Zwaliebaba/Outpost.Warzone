#include "pch.h"
#include "CppUnitTest.h"

#include "NetWriter.h"
#include "NetReader.h"

#include <cstring>
#include <span>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Neuron;

/* The wire format both halves of the server/client seam speak
   (Docs/ServerAuthority.md stage C).

   What these pin down is the format itself, not just that a value survives a
   round trip: a round trip passes even if both ends agree on the wrong layout.
   ByteOrderIsLittleEndian asserts the actual bytes, so a change to the
   encoding has to be deliberate rather than incidental.

   The rest are the hostile-input cases. Every byte NetReader sees was chosen
   by a peer, so a lying length, a short buffer and an over-long string all
   have defined outcomes and none of them is a read outside the buffer. */
namespace NeuronCoreTest
{
TEST_CLASS(NetWireTest)
{
public:
  TEST_METHOD(ByteOrderIsLittleEndian)
  {
    std::byte buffer[8]{};
    NetWriter writer{buffer};
    writer.U32(0x11223344u);

    Assert::IsTrue(!writer.Overflowed());
    Assert::AreEqual(static_cast<size_t>(4), writer.Size());
    Assert::AreEqual(0x44, std::to_integer<int>(buffer[0]));
    Assert::AreEqual(0x33, std::to_integer<int>(buffer[1]));
    Assert::AreEqual(0x22, std::to_integer<int>(buffer[2]));
    Assert::AreEqual(0x11, std::to_integer<int>(buffer[3]));
  }

  TEST_METHOD(EveryWidthRoundTrips)
  {
    std::byte buffer[64]{};
    NetWriter writer{buffer};
    writer.U8(0xFEu);
    writer.U16(0xBEEFu);
    writer.U32(0xDEADBEEFu);
    writer.S32(-2147483647 - 1);
    writer.S32(-1);
    writer.S32(2147483647);
    writer.F32(-0.15625f); // exact in binary, so == is the right test
    writer.Bool(true);
    writer.Bool(false);
    Assert::IsTrue(!writer.Overflowed());

    NetReader reader{writer.Written()};
    Assert::IsTrue(reader.U8() == 0xFEu);
    Assert::IsTrue(reader.U16() == 0xBEEFu);
    Assert::IsTrue(reader.U32() == 0xDEADBEEFu);
    Assert::IsTrue(reader.S32() == -2147483647 - 1);
    Assert::IsTrue(reader.S32() == -1);
    Assert::IsTrue(reader.S32() == 2147483647);
    Assert::IsTrue(reader.F32() == -0.15625f);
    Assert::IsTrue(reader.Bool() == true);
    Assert::IsTrue(reader.Bool() == false);
    Assert::IsTrue(reader.Ok());
    Assert::AreEqual(static_cast<size_t>(0), reader.Remaining());
  }

  TEST_METHOD(TextRoundTrips)
  {
    std::byte buffer[64]{};
    NetWriter writer{buffer};
    writer.Text("commander");
    writer.U32(0xABCDEF01u);
    Assert::IsTrue(!writer.Overflowed());

    NetReader reader{writer.Written()};
    char name[32]{};
    reader.Text(name);
    Assert::IsTrue(std::strcmp(name, "commander") == 0);
    Assert::IsTrue(reader.U32() == 0xABCDEF01u);
    Assert::IsTrue(reader.Ok());
  }

  /* The property that matters: truncating the copy must not desynchronise the
     read, or every field after a long name is garbage. */
  TEST_METHOD(AnOverLongTextStillLeavesTheStreamAligned)
  {
    std::byte buffer[64]{};
    NetWriter writer{buffer};
    writer.Text("a-very-long-player-name");
    writer.U32(0x5A5A5A5Au);

    NetReader reader{writer.Written()};
    char small[8]{};
    reader.Text(small);
    Assert::AreEqual(static_cast<size_t>(7), std::strlen(small));
    Assert::IsTrue(std::strncmp(small, "a-very-", 7) == 0);
    Assert::IsTrue(reader.U32() == 0x5A5A5A5Au);
    Assert::IsTrue(reader.Ok());
  }

  TEST_METHOD(WriterOverflowIsStickyAndStoresNothing)
  {
    std::byte buffer[4]{};
    NetWriter writer{buffer};
    writer.U32(0x01020304u);
    Assert::IsTrue(!writer.Overflowed());

    writer.U8(0xFFu); // one byte too many
    Assert::IsTrue(writer.Overflowed());
    Assert::AreEqual(static_cast<size_t>(4), writer.Size());

    writer.U8(0x01u); // and it stays refused
    Assert::AreEqual(static_cast<size_t>(4), writer.Size());
  }

  TEST_METHOD(ReaderTruncationIsStickyAndYieldsZero)
  {
    const std::byte tiny[2]{std::byte{0x01}, std::byte{0x02}};
    NetReader reader{tiny};

    Assert::IsTrue(reader.U32() == 0); // wants four, has two
    Assert::IsTrue(reader.Truncated());
    Assert::IsTrue(reader.U8() == 0); // sticky: not the 0x01 that is there
  }

  /* A peer that describes 65535 bytes of name and sends one must not be able
     to walk the reader off the end of the buffer. */
  TEST_METHOD(ALyingTextLengthCannotLeaveTheBuffer)
  {
    std::byte buffer[4]{};
    NetWriter writer{buffer};
    writer.U16(0xFFFFu);
    writer.U8(0x41u);

    NetReader reader{writer.Written()};
    char name[16]{};
    reader.Text(name);
    Assert::IsTrue(reader.Truncated());
    Assert::IsTrue(name[0] == '\0');
  }

  TEST_METHOD(AnEmptyDestinationIsNotACrash)
  {
    std::byte buffer[16]{};
    NetWriter writer{buffer};
    writer.Text("hi");

    NetReader reader{writer.Written()};
    reader.Text(std::span<char>{});
    Assert::IsTrue(reader.Ok());
  }
};
} // namespace NeuronCoreTest
