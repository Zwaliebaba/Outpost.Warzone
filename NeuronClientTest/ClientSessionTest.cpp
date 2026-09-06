#include "pch.h"
#include "CppUnitTest.h"

#include "ClientSession.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Neuron;

/* The client's session state machine (Docs/ServerAuthority.md stage D).

   The server here is driven by hand, which is all a real one is from the
   client's side: bytes arriving on a channel. What these hold down is that the
   client believes only what it has actually been told -- no clock it invented,
   no level it was not given, no tick before it said it was ready -- and that a
   server sending nonsense moves it nowhere. What the client asks for goes out
   as a request the server may refuse, and a kick ends the session whatever
   state it was in.

   The two halves talking to each other with nothing hand-driven is
   ServerSessionTest's other side of the same boundary; that pairing lives in
   the native harness, because here the two libraries are separate test
   projects. */
namespace NeuronClientTest
{
using End = LoopbackTransport::End;
using State = ClientSession::State;

namespace
{
template <typename Message>
void ServerSends(LoopbackTransport& _link, const Message& _message)
{
  std::byte scratch[64]{};
  NetWriter writer{scratch};
  Put(writer, _message);
  _link.Send(End::Server, NetChannel::Session, writer.Written());
}

/// Accepts the hello the client sent and answers it, which is the shortest
/// road to a session that has been greeted.
void GreetTheClient(LoopbackTransport& _link, std::uint16_t _tickMs)
{
  LoopbackTransport::Message message;
  Assert::IsTrue(_link.Receive(End::Server, message));

  ServerHello answer;
  answer.result = HandshakeResult::Accepted;
  answer.tickMs = _tickMs;
  answer.connectionId = 1;
  ServerSends(_link, answer);
}
} // namespace

TEST_CLASS(ClientSessionTest)
{
public:
  TEST_METHOD(TheSessionOpensAndThenFollowsTheServersClock)
  {
    LoopbackTransport link;
    ClientSession client{link, 0u};
    Assert::IsTrue(client.CurrentState() == State::Fresh);
    Assert::IsFalse(client.Verdict().has_value());

    client.Begin();
    Assert::IsTrue(client.CurrentState() == State::Handshaking);

    LoopbackTransport::Message message;
    Assert::IsTrue(link.Receive(End::Server, message));
    {
      NetReader reader{message.bytes};
      Assert::IsTrue(static_cast<ClientMessage>(reader.U8()) == ClientMessage::Hello);
      Assert::IsTrue(ClientHello::Decode(reader).protocolVersion == ProtocolVersion);
    }

    ServerHello answer;
    answer.result = HandshakeResult::Accepted;
    answer.tickMs = 40;
    answer.connectionId = 7;
    ServerSends(link, answer);

    client.Service();
    Assert::IsTrue(client.CurrentState() == State::Greeted);
    Assert::IsTrue(client.Verdict().has_value() && *client.Verdict() == HandshakeResult::Accepted);

    /* The quantum came from the server rather than from this build's own idea
       of what a tick is. */
    Assert::AreEqual(static_cast<std::uint16_t>(40), client.TickMs());
    Assert::AreEqual(static_cast<std::uint32_t>(7), client.ConnectionId());

    /* Ready answers Start, so before one has arrived there is nothing to be
       ready for and nothing is sent. */
    client.ReportReady(0xC0FFEEu);
    Assert::IsTrue(client.CurrentState() == State::Greeted);
    Assert::AreEqual(static_cast<size_t>(0), link.Pending(End::Server));

    ServerSends(link, ServerStart{3u, 0u, 0xC0FFEEu});
    client.Service();
    Assert::IsTrue(client.CurrentState() == State::Loading);
    Assert::AreEqual(static_cast<std::uint32_t>(0xC0FFEE), client.MapHash());
    Assert::AreEqual(static_cast<std::uint32_t>(3), client.CurrentTick());

    /* A tick that arrives while the level is still loading has nowhere to go. */
    ServerSends(link, ServerTick{4u});
    client.Service();
    Assert::AreEqual(static_cast<std::uint32_t>(3), client.CurrentTick());

    client.ReportReady(0xC0FFEEu);
    Assert::IsTrue(client.CurrentState() == State::Running);
    Assert::IsTrue(link.Receive(End::Server, message));
    {
      NetReader reader{message.bytes};
      Assert::IsTrue(static_cast<ClientMessage>(reader.U8()) == ClientMessage::Ready);
      Assert::AreEqual(static_cast<std::uint32_t>(0xC0FFEE), ClientReady::Decode(reader).mapHash);
    }

    ServerSends(link, ServerTick{5u});
    client.Service();
    Assert::AreEqual(static_cast<std::uint32_t>(5), client.CurrentTick());
  }

  TEST_METHOD(ARefusedClientBelievesNothingFurther)
  {
    LoopbackTransport link;
    ClientSession client{link, 0xBBBBu};
    client.Begin();

    LoopbackTransport::Message message;
    Assert::IsTrue(link.Receive(End::Server, message));

    ServerHello answer;
    answer.result = HandshakeResult::BuildMismatch;
    answer.tickMs = 40;
    ServerSends(link, answer);

    client.Service();
    Assert::IsTrue(client.CurrentState() == State::Refused);
    Assert::IsTrue(client.Verdict().has_value() && *client.Verdict() == HandshakeResult::BuildMismatch);

    /* A refusal states no terms, so none are adopted. */
    Assert::AreEqual(static_cast<std::uint16_t>(0), client.TickMs());

    /* Anything further is drained rather than left queued, for the same reason
       the server drains a refused client's traffic. */
    ServerSends(link, ServerStart{0u, 0u, 0xC0FFEEu});
    client.Service();
    Assert::IsTrue(client.CurrentState() == State::Refused);
    Assert::AreEqual(static_cast<std::uint32_t>(0), client.MapHash());
    Assert::AreEqual(static_cast<size_t>(0), link.Pending(End::Client));
  }

  TEST_METHOD(AServerCannotDriveTheClientOutOfOrder)
  {
    LoopbackTransport link;
    ClientSession client{link, 0u};
    client.Begin();

    LoopbackTransport::Message message;
    Assert::IsTrue(link.Receive(End::Server, message));

    // a level named before the client has been accepted
    ServerSends(link, ServerStart{0u, 0u, 0xC0FFEEu});
    client.Service();
    Assert::IsTrue(client.CurrentState() == State::Handshaking);
    Assert::AreEqual(static_cast<std::uint32_t>(0), client.MapHash());

    // an id this build has never heard of
    std::byte scratch[8]{};
    NetWriter writer{scratch};
    writer.U8(0xFEu);
    link.Send(End::Server, NetChannel::Session, writer.Written());
    client.Service();
    Assert::IsTrue(client.CurrentState() == State::Handshaking);
  }

  TEST_METHOD(AVerdictThatRanOffTheEndIsNotAVerdict)
  {
    LoopbackTransport link;
    ClientSession client{link, 0u};
    client.Begin();

    LoopbackTransport::Message message;
    Assert::IsTrue(link.Receive(End::Server, message));

    std::byte scratch[8]{};
    NetWriter writer{scratch};
    writer.U8(static_cast<std::uint8_t>(ServerMessage::Hello));
    writer.U8(static_cast<std::uint8_t>(HandshakeResult::Accepted)); // and nothing else
    link.Send(End::Server, NetChannel::Session, writer.Written());

    client.Service();
    Assert::IsTrue(client.CurrentState() == State::Refused);
    Assert::IsTrue(client.Verdict().has_value() && *client.Verdict() == HandshakeResult::ProtocolMismatch);
  }

  TEST_METHOD(ATruncatedStartNamesNoLevel)
  {
    LoopbackTransport link;
    ClientSession client{link, 0u};
    client.Begin();
    GreetTheClient(link, 40u);
    client.Service();
    Assert::IsTrue(client.CurrentState() == State::Greeted);

    std::byte scratch[8]{};
    NetWriter writer{scratch};
    writer.U8(static_cast<std::uint8_t>(ServerMessage::Start));
    writer.U32(7u); // and no map hash
    link.Send(End::Server, NetChannel::Session, writer.Written());

    client.Service();
    Assert::IsTrue(client.CurrentState() == State::Greeted);
    Assert::AreEqual(static_cast<std::uint32_t>(0), client.MapHash());
    Assert::AreEqual(static_cast<std::uint32_t>(0), client.CurrentTick());
  }

  TEST_METHOD(ASecondBeginIsNotASecondHandshake)
  {
    LoopbackTransport link;
    ClientSession client{link, 0u};
    client.Begin();
    client.Begin();
    Assert::AreEqual(static_cast<size_t>(1), link.Pending(End::Server));
  }

  /* A kick ends the session from any state after the verdict, with the reason
     kept, and nothing further is believed. */
  TEST_METHOD(AKickEndsTheSession)
  {
    LoopbackTransport link;
    ClientSession client{link, 0u};
    client.Begin();
    GreetTheClient(link, 40u);
    client.Service();
    ServerSends(link, ServerStart{0u, 0u, 0xC0FFEEu});
    client.Service();
    client.ReportReady(0xC0FFEEu);
    Assert::IsTrue(client.CurrentState() == State::Running);

    ServerSends(link, ServerKick{KickReason::MapMismatch});
    client.Service();
    Assert::IsTrue(client.CurrentState() == State::Refused);
    Assert::IsTrue(client.Kicked().has_value() && *client.Kicked() == KickReason::MapMismatch);

    ServerSends(link, ServerTick{9u});
    client.Service();
    Assert::AreEqual(static_cast<std::uint32_t>(0), client.CurrentTick());
    Assert::AreEqual(static_cast<size_t>(0), link.Pending(End::Client));

    /* Before the verdict there is nothing agreed to end, so a kick there is
       just bytes from a server that has not answered. */
    LoopbackTransport otherLink;
    ClientSession fresh{otherLink, 0u};
    fresh.Begin();
    ServerSends(otherLink, ServerKick{KickReason::MapMismatch});
    fresh.Service();
    Assert::IsTrue(fresh.CurrentState() == State::Handshaking);
    Assert::IsFalse(fresh.Kicked().has_value());
  }

  /* The speed keys ask; they no longer set. The request goes out on the
     command channel, numbered, once there is a world to run at a speed. */
  TEST_METHOD(ARequestedSpeedGoesOutOnTheCommandChannel)
  {
    LoopbackTransport link;
    ClientSession client{link, 0u};
    client.Begin();
    GreetTheClient(link, 40u);
    client.Service();

    /* Nothing to run yet, so nothing is asked. */
    client.RequestGameSpeed(2.0f);
    Assert::AreEqual(static_cast<size_t>(0), link.Pending(End::Server));

    ServerSends(link, ServerStart{0u, 0u, 0xC0FFEEu});
    client.Service();
    client.ReportReady(0xC0FFEEu);
    LoopbackTransport::Message message;
    Assert::IsTrue(link.Receive(End::Server, NetChannel::Session, message)); // the Ready

    client.RequestGameSpeed(1.5f);
    client.RequestGameSpeed(0.5f);
    Assert::IsTrue(link.Receive(End::Server, NetChannel::Command, message));
    {
      NetReader reader{message.bytes};
      Assert::IsTrue(static_cast<ClientMessage>(reader.U8()) == ClientMessage::SessionControl);
      const ClientSessionControl control = ClientSessionControl::Decode(reader);
      Assert::AreEqual(static_cast<std::uint32_t>(1), control.sequence);
      Assert::IsTrue(control.kind == SessionControlKind::GameSpeed);
      Assert::AreEqual(1.5f, control.value);
    }
    Assert::IsTrue(link.Receive(End::Server, NetChannel::Command, message));
    {
      NetReader reader{message.bytes};
      (void)reader.U8();
      const ClientSessionControl control = ClientSessionControl::Decode(reader);
      Assert::AreEqual(static_cast<std::uint32_t>(2), control.sequence);
      Assert::AreEqual(0.5f, control.value);
    }
    Assert::AreEqual(static_cast<size_t>(0), link.Pending(End::Server));
  }

  /* A refusal is kept for the caller, which is the only way the keys can say
     why they did nothing. */
  TEST_METHOD(ARejectIsReportedToTheCaller)
  {
    LoopbackTransport link;
    ClientSession client{link, 0u};
    client.Begin();
    GreetTheClient(link, 40u);
    client.Service();
    ServerSends(link, ServerStart{0u, 0u, 0xC0FFEEu});
    client.Service();
    client.ReportReady(0xC0FFEEu);

    ServerCommandReject reject;
    Assert::IsFalse(client.TakeReject(reject));

    ServerSends(link, ServerCommandReject{3u, ClientMessage::SessionControl, RejectReason::NotPermitted});
    client.Service();
    Assert::IsTrue(client.TakeReject(reject));
    Assert::AreEqual(static_cast<std::uint32_t>(3), reject.sequence);
    Assert::IsTrue(reject.reason == RejectReason::NotPermitted);
    Assert::IsFalse(client.TakeReject(reject));
  }
};
} // namespace NeuronClientTest
