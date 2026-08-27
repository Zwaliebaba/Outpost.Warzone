#include "pch.h"
#include "CppUnitTest.h"

#include "LoopbackTransport.h"
#include "NetWriter.h"
#include "NetReader.h"
#include "Protocol.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Neuron;

/* The rung-1 boundary (Docs/ServerAuthority.md): a client and a server in one
   process exchanging the bytes they would exchange over a network.

   What these hold down is that the link is a *byte* boundary and a per-channel
   one. If either weakened - if a message could be read without being encoded,
   or if one global queue ordered every channel together - the embedded server
   would stop being separable and code would be written against a promise QUIC
   does not keep. */
namespace NeuronCoreTest
{
TEST_CLASS(LoopbackTransportTest)
{
public:
  TEST_METHOD(AMessageCrossesAsBytesAndDecodesBack)
  {
    LoopbackTransport link;

    std::byte scratch[64]{};
    NetWriter writer{scratch};
    writer.U8(static_cast<std::uint8_t>(ClientMessage::Order));
    writer.U16(41u); // the command sequence number
    writer.U32(1207u); // a droid id
    Assert::IsTrue(!writer.Overflowed());

    link.Send(LoopbackTransport::End::Client, NetChannel::Command, writer.Written());
    Assert::AreEqual(static_cast<size_t>(1), link.Pending(LoopbackTransport::End::Server));
    Assert::AreEqual(static_cast<size_t>(0), link.Pending(LoopbackTransport::End::Client));

    LoopbackTransport::Message received;
    Assert::IsTrue(link.Receive(LoopbackTransport::End::Server, received));
    Assert::IsTrue(received.channel == NetChannel::Command);

    NetReader reader{received.bytes};
    Assert::IsTrue(static_cast<ClientMessage>(reader.U8()) == ClientMessage::Order);
    Assert::IsTrue(reader.U16() == 41u);
    Assert::IsTrue(reader.U32() == 1207u);
    Assert::IsTrue(reader.Ok());

    Assert::IsTrue(!link.Receive(LoopbackTransport::End::Server, received));
  }

  /* The sender's buffer is a NetWriter's stack scratch and gets reused for the
     next message, so the link has to have taken a copy. */
  TEST_METHOD(TheSendersBufferIsNotBorrowed)
  {
    LoopbackTransport link;

    std::byte scratch[8]{};
    {
      NetWriter writer{scratch};
      writer.U32(0xCAFEBABEu);
      link.Send(LoopbackTransport::End::Server, NetChannel::Replication, writer.Written());
    }

    for (std::byte& byte : scratch) // the caller reuses it immediately
      byte = std::byte{0xFF};

    LoopbackTransport::Message received;
    Assert::IsTrue(link.Receive(LoopbackTransport::End::Client, received));
    NetReader reader{received.bytes};
    Assert::IsTrue(reader.U32() == 0xCAFEBABEu);
  }

  TEST_METHOD(OrderIsKeptWithinAChannel)
  {
    LoopbackTransport link;

    for (std::uint8_t i = 0; i < 4; i++)
    {
      std::byte scratch[4]{};
      NetWriter writer{scratch};
      writer.U8(i);
      link.Send(LoopbackTransport::End::Client, NetChannel::Command, writer.Written());
    }

    for (std::uint8_t i = 0; i < 4; i++)
    {
      LoopbackTransport::Message received;
      Assert::IsTrue(link.Receive(LoopbackTransport::End::Server, received));
      NetReader reader{received.bytes};
      Assert::IsTrue(reader.U8() == i);
    }
  }

  /* Channels are independently ordered, so a Session message sent after a
     Bulk one is not stuck behind it. This is the property that makes a map
     download not block an order, and it has to hold here or solo play would
     exercise a stronger guarantee than the network gives. */
  TEST_METHOD(ChannelsDoNotBlockEachOther)
  {
    LoopbackTransport link;

    std::byte bulk[4]{};
    NetWriter bulkWriter{bulk};
    bulkWriter.U8(0xB0u);
    link.Send(LoopbackTransport::End::Server, NetChannel::Bulk, bulkWriter.Written());

    std::byte session[4]{};
    NetWriter sessionWriter{session};
    sessionWriter.U8(static_cast<std::uint8_t>(ServerMessage::Start));
    link.Send(LoopbackTransport::End::Server, NetChannel::Session, sessionWriter.Written());

    LoopbackTransport::Message received;
    Assert::IsTrue(link.Receive(LoopbackTransport::End::Client, received));
    Assert::IsTrue(received.channel == NetChannel::Session); // sent second, seen first
  }

  TEST_METHOD(EachDirectionIsItsOwnQueue)
  {
    LoopbackTransport link;

    std::byte scratch[4]{};
    NetWriter writer{scratch};
    writer.U8(0x01u);
    link.Send(LoopbackTransport::End::Client, NetChannel::Command, writer.Written());

    LoopbackTransport::Message received;
    Assert::IsTrue(!link.Receive(LoopbackTransport::End::Client, received)); // not an echo
    Assert::IsTrue(link.Receive(LoopbackTransport::End::Server, received));
  }

  TEST_METHOD(ClearEmptiesBothDirections)
  {
    LoopbackTransport link;

    std::byte scratch[4]{};
    NetWriter writer{scratch};
    writer.U8(0x01u);
    link.Send(LoopbackTransport::End::Client, NetChannel::Command, writer.Written());
    link.Send(LoopbackTransport::End::Server, NetChannel::Replication, writer.Written());

    link.Clear();
    Assert::AreEqual(static_cast<size_t>(0), link.Pending(LoopbackTransport::End::Client));
    Assert::AreEqual(static_cast<size_t>(0), link.Pending(LoopbackTransport::End::Server));
  }
};
} // namespace NeuronCoreTest
