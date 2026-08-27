#include "pch.h"
#include "CppUnitTest.h"

#include "LoopbackTransport.h"
#include "Protocol.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Neuron;

/* The session handshake, and the first real conversation across the boundary
   (Docs/ServerAuthority.md stage D).

   The exchange is what NET_VERSION and the executable hash smuggled inside
   NET_OPTIONS used to do, except that it happens before anything else is
   believed rather than after the lobby has been joined. TheWholeHandshake runs
   it end to end over a LoopbackTransport, which is the shape a solo session
   takes at rung 1. */
namespace NeuronCoreTest
{
TEST_CLASS(HandshakeTest)
{
  using End = LoopbackTransport::End;

public:
  TEST_METHOD(PutWritesTheIdAndTheBody)
  {
    std::byte buffer[64]{};
    NetWriter writer{buffer};
    Put(writer, ClientHello{ProtocolVersion, 0xABCD1234u});
    Assert::IsTrue(!writer.Overflowed());

    NetReader reader{writer.Written()};
    Assert::IsTrue(static_cast<ClientMessage>(reader.U8()) == ClientMessage::Hello);

    const ClientHello hello = ClientHello::Decode(reader);
    Assert::IsTrue(hello.protocolVersion == ProtocolVersion);
    Assert::IsTrue(hello.buildHash == 0xABCD1234u);
    Assert::IsTrue(reader.Ok());
  }

  TEST_METHOD(TheServerRecordsRoundTrip)
  {
    std::byte buffer[64]{};
    NetWriter writer{buffer};
    Put(writer, ServerHello{HandshakeResult::Accepted, 40u, 7u});
    Put(writer, ServerStart{1000u, 3000u, 0xFEEDu});

    NetReader reader{writer.Written()};
    Assert::IsTrue(static_cast<ServerMessage>(reader.U8()) == ServerMessage::Hello);
    const ServerHello hello = ServerHello::Decode(reader);
    Assert::IsTrue(hello.result == HandshakeResult::Accepted);
    Assert::IsTrue(hello.tickMs == 40u);
    Assert::IsTrue(hello.connectionId == 7u);

    Assert::IsTrue(static_cast<ServerMessage>(reader.U8()) == ServerMessage::Start);
    const ServerStart start = ServerStart::Decode(reader);
    Assert::IsTrue(start.startTick == 1000u);
    Assert::IsTrue(start.countdownMs == 3000u);
    Assert::IsTrue(start.mapHash == 0xFEEDu);
    Assert::IsTrue(reader.Ok());
  }

  /* Whether to accept a connection is policy, so it is a function with a test
     rather than an if buried in a handler. A server with no build hash of its
     own cannot check anyone's - which is the solo case, where both halves are
     the same binary. */
  TEST_METHOD(ConsiderIsThePolicy)
  {
    Assert::IsTrue(Consider(ClientHello{ProtocolVersion, 5u}, 5u) == HandshakeResult::Accepted);
    Assert::IsTrue(Consider(ClientHello{ProtocolVersion, 5u}, 0u) == HandshakeResult::Accepted);
    Assert::IsTrue(Consider(ClientHello{ProtocolVersion, 6u}, 5u) == HandshakeResult::BuildMismatch);

    const auto newer = static_cast<std::uint16_t>(ProtocolVersion + 1);
    Assert::IsTrue(Consider(ClientHello{newer, 5u}, 5u) == HandshakeResult::ProtocolMismatch);
  }

  /* A result byte off the wire is a byte a peer chose. An unknown one folds to
     a refusal rather than becoming a wild enum value to switch on: refusing for
     a reason we cannot name is still refusing. */
  TEST_METHOD(AnUnknownResultFoldsToARefusal)
  {
    std::byte buffer[8]{};
    NetWriter writer{buffer};
    writer.U8(0xFEu);
    writer.U16(40u);
    writer.U32(1u);

    NetReader reader{writer.Written()};
    Assert::IsTrue(ServerHello::Decode(reader).result == HandshakeResult::ProtocolMismatch);
    Assert::IsTrue(reader.Ok());
  }

  TEST_METHOD(ATruncatedHelloIsNotHalfBelieved)
  {
    const std::byte stub[2]{std::byte{0x01}, std::byte{0x00}};
    NetReader reader{stub};

    const ClientHello hello = ClientHello::Decode(reader);
    Assert::IsTrue(reader.Truncated());
    Assert::IsTrue(hello.buildHash == 0);
  }

  /* Client hello, server verdict, client ready, server start - the whole
     opening of a session, across the boundary, in the shape solo play takes. */
  TEST_METHOD(TheWholeHandshakeCrossesTheBoundary)
  {
    LoopbackTransport link;
    std::byte scratch[64]{};
    LoopbackTransport::Message message;

    {
      NetWriter writer{scratch};
      Put(writer, ClientHello{ProtocolVersion, 0u});
      link.Send(End::Client, NetChannel::Session, writer.Written());
    }

    Assert::IsTrue(link.Receive(End::Server, message));
    NetReader helloReader{message.bytes};
    Assert::IsTrue(static_cast<ClientMessage>(helloReader.U8()) == ClientMessage::Hello);
    const HandshakeResult verdict = Consider(ClientHello::Decode(helloReader), 0u);
    Assert::IsTrue(verdict == HandshakeResult::Accepted);

    {
      NetWriter writer{scratch};
      Put(writer, ServerHello{verdict, 40u, 1u});
      link.Send(End::Server, NetChannel::Session, writer.Written());
    }

    Assert::IsTrue(link.Receive(End::Client, message));
    NetReader verdictReader{message.bytes};
    Assert::IsTrue(static_cast<ServerMessage>(verdictReader.U8()) == ServerMessage::Hello);
    const ServerHello answer = ServerHello::Decode(verdictReader);
    Assert::IsTrue(answer.result == HandshakeResult::Accepted);
    Assert::IsTrue(answer.tickMs == 40u); // the client's clock comes from here

    {
      NetWriter writer{scratch};
      writer.U8(static_cast<std::uint8_t>(ClientMessage::Ready)); // no body
      link.Send(End::Client, NetChannel::Session, writer.Written());
    }

    Assert::IsTrue(link.Receive(End::Server, message));
    NetReader readyReader{message.bytes};
    Assert::IsTrue(static_cast<ClientMessage>(readyReader.U8()) == ClientMessage::Ready);

    {
      NetWriter writer{scratch};
      Put(writer, ServerStart{0u, 0u, 0xC0FFEEu});
      link.Send(End::Server, NetChannel::Session, writer.Written());
    }

    Assert::IsTrue(link.Receive(End::Client, message));
    NetReader startReader{message.bytes};
    Assert::IsTrue(static_cast<ServerMessage>(startReader.U8()) == ServerMessage::Start);
    Assert::IsTrue(ServerStart::Decode(startReader).mapHash == 0xC0FFEEu);

    Assert::AreEqual(static_cast<size_t>(0), link.Pending(End::Client));
    Assert::AreEqual(static_cast<size_t>(0), link.Pending(End::Server));
  }
};
} // namespace NeuronCoreTest
