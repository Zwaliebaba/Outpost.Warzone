#include "pch.h"
#include "CppUnitTest.h"

#include "ServerSession.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Neuron;

/* The server's session state machine (Docs/ServerAuthority.md stage D).

   The client here is driven by hand, which is all a real one is from the
   server's side: bytes arriving on a channel. What these hold down is that a
   peer choosing those bytes cannot drive the session out of order - Ready
   before Hello, an id this build has never heard of, a hello that runs off the
   end of its buffer - that no tick is sent to a client that has not reported
   itself ready for one, that a client which loaded the wrong level is sent
   away rather than left to desync, and that what a client asks for is
   permitted by policy rather than by arriving. */
namespace NeuronServerTest
{
using End = LoopbackTransport::End;
using State = ServerSession::State;

namespace
{
constexpr std::uint32_t TheLevel = 0xC0FFEEu;

void SendId(LoopbackTransport& _link, ClientMessage _id)
{
  std::byte scratch[16]{};
  NetWriter writer{scratch};
  writer.U8(static_cast<std::uint8_t>(_id));
  _link.Send(End::Client, NetChannel::Session, writer.Written());
}

void SendHello(LoopbackTransport& _link, std::uint16_t _version, std::uint32_t _buildHash)
{
  std::byte scratch[32]{};
  NetWriter writer{scratch};
  Put(writer, ClientHello{_version, _buildHash});
  _link.Send(End::Client, NetChannel::Session, writer.Written());
}

void SendReady(LoopbackTransport& _link, std::uint32_t _loadedMapHash)
{
  std::byte scratch[32]{};
  NetWriter writer{scratch};
  Put(writer, ClientReady{_loadedMapHash});
  _link.Send(End::Client, NetChannel::Session, writer.Written());
}

void SendControl(LoopbackTransport& _link, std::uint32_t _sequence, float _speed)
{
  std::byte scratch[32]{};
  NetWriter writer{scratch};
  Put(writer, ClientSessionControl{_sequence, SessionControlKind::GameSpeed, _speed});
  _link.Send(End::Client, NetChannel::Command, writer.Written());
}

/// Takes the next message the client can see, returning its id.
bool NextForClient(LoopbackTransport& _link, LoopbackTransport::Message& _message, ServerMessage& _outId)
{
  if (!_link.Receive(End::Client, _message))
    return false;

  NetReader reader{_message.bytes};
  _outId = static_cast<ServerMessage>(reader.U8());
  return true;
}

/// Opens a session all the way to Running.
void OpenToRunning(LoopbackTransport& _link, ServerSession& _session)
{
  SendHello(_link, ProtocolVersion, 0u);
  _session.Service();
  _session.Start(TheLevel);
  SendReady(_link, TheLevel);
  _session.Service();
  Assert::IsTrue(_session.CurrentState() == State::Running);

  LoopbackTransport::Message message;
  while (_link.Receive(End::Client, message))
  {
  }
}
} // namespace

TEST_CLASS(ServerSessionTest)
{
public:
  TEST_METHOD(TheSessionOpensAndThenTicks)
  {
    LoopbackTransport link;
    ServerSession session{link, 0u, 40u};
    Assert::IsTrue(session.CurrentState() == State::Handshaking);

    SendHello(link, ProtocolVersion, 0u);
    session.Service();
    Assert::IsTrue(session.CurrentState() == State::Greeted);

    LoopbackTransport::Message message;
    ServerMessage id = ServerMessage::Count;
    Assert::IsTrue(NextForClient(link, message, id));
    Assert::IsTrue(id == ServerMessage::Hello);
    {
      NetReader reader{message.bytes};
      (void)reader.U8();
      const ServerHello answer = ServerHello::Decode(reader);
      Assert::IsTrue(answer.result == HandshakeResult::Accepted);
      Assert::IsTrue(answer.tickMs == 40u);
    }

    session.Start(TheLevel);
    Assert::IsTrue(session.CurrentState() == State::Starting);
    Assert::AreEqual(TheLevel, session.MapHash());
    Assert::IsTrue(NextForClient(link, message, id));
    Assert::IsTrue(id == ServerMessage::Start);

    /* The world must not run ahead of a client that has not said it is
       loaded, or the first thing it is told is state it has nowhere to put. */
    session.Tick();
    Assert::AreEqual(static_cast<std::uint32_t>(0), session.CurrentTick());
    Assert::AreEqual(static_cast<size_t>(0), link.Pending(End::Client));

    SendReady(link, TheLevel);
    session.Service();
    Assert::IsTrue(session.CurrentState() == State::Running);

    session.Tick();
    session.Tick();
    Assert::AreEqual(static_cast<std::uint32_t>(2), session.CurrentTick());

    Assert::IsTrue(NextForClient(link, message, id));
    Assert::IsTrue(id == ServerMessage::Tick);
    {
      NetReader reader{message.bytes};
      (void)reader.U8();
      Assert::IsTrue(ServerTick::Decode(reader).tick == 1u);
    }
    Assert::IsTrue(NextForClient(link, message, id));
    {
      NetReader reader{message.bytes};
      (void)reader.U8();
      Assert::IsTrue(ServerTick::Decode(reader).tick == 2u);
    }
  }

  /* A client that loaded a different level than the one it was told to load
     would desync from tick 1 and nothing after this could say why. So it is
     sent away here, with the reason, and the session goes deaf. */
  TEST_METHOD(AReadyForTheWrongLevelIsKicked)
  {
    LoopbackTransport link;
    ServerSession session{link, 0u, 40u};
    SendHello(link, ProtocolVersion, 0u);
    session.Service();
    session.Start(TheLevel);

    LoopbackTransport::Message message;
    while (link.Receive(End::Client, message))
    {
    }

    SendReady(link, TheLevel + 1);
    session.Service();
    Assert::IsTrue(session.CurrentState() == State::Refused);

    ServerMessage id = ServerMessage::Count;
    Assert::IsTrue(NextForClient(link, message, id));
    Assert::IsTrue(id == ServerMessage::Kick);
    {
      NetReader reader{message.bytes};
      (void)reader.U8();
      Assert::IsTrue(ServerKick::Decode(reader).reason == KickReason::MapMismatch);
    }

    session.Tick();
    Assert::AreEqual(static_cast<std::uint32_t>(0), session.CurrentTick());
    Assert::AreEqual(static_cast<size_t>(0), link.Pending(End::Client));
  }

  /* The old bodiless Ready says nothing about what was loaded, which is the
     same as saying the wrong thing. */
  TEST_METHOD(AReadyWithNoLevelIsKicked)
  {
    LoopbackTransport link;
    ServerSession session{link, 0u, 40u};
    SendHello(link, ProtocolVersion, 0u);
    session.Service();
    session.Start(TheLevel);

    SendId(link, ClientMessage::Ready);
    session.Service();
    Assert::IsTrue(session.CurrentState() == State::Refused);
  }

  TEST_METHOD(ARefusedSessionGoesDeaf)
  {
    LoopbackTransport link;
    ServerSession session{link, 0xAAAAu, 40u};

    SendHello(link, ProtocolVersion, 0xBBBBu); // a different executable
    session.Service();
    Assert::IsTrue(session.CurrentState() == State::Refused);

    LoopbackTransport::Message message;
    ServerMessage id = ServerMessage::Count;
    Assert::IsTrue(NextForClient(link, message, id));
    Assert::IsTrue(id == ServerMessage::Hello);
    {
      NetReader reader{message.bytes};
      (void)reader.U8();
      Assert::IsTrue(ServerHello::Decode(reader).result == HandshakeResult::BuildMismatch);
    }

    /* Anything further is drained rather than left queued: a refused peer that
       keeps talking must not grow a queue nobody reads. */
    SendReady(link, TheLevel);
    session.Service();
    Assert::IsTrue(session.CurrentState() == State::Refused);
    Assert::AreEqual(static_cast<size_t>(0), link.Pending(End::Server));
  }

  TEST_METHOD(APeerCannotDriveTheSessionOutOfOrder)
  {
    LoopbackTransport link;
    ServerSession session{link, 0u, 40u};

    SendReady(link, TheLevel); // before any hello
    session.Service();
    Assert::IsTrue(session.CurrentState() == State::Handshaking);

    std::byte scratch[8]{};
    NetWriter writer{scratch};
    writer.U8(0xFEu); // an id this build has never heard of
    link.Send(End::Client, NetChannel::Session, writer.Written());
    session.Service();
    Assert::IsTrue(session.CurrentState() == State::Handshaking);

    /* A control before the world exists means nothing and grants nothing. */
    SendControl(link, 1u, 2.0f);
    session.Service();
    ClientSessionControl control;
    Assert::IsFalse(session.TakeControl(control));
  }

  TEST_METHOD(ATruncatedHelloIsRefused)
  {
    LoopbackTransport link;
    ServerSession session{link, 0u, 40u};

    std::byte scratch[8]{};
    NetWriter writer{scratch};
    writer.U8(static_cast<std::uint8_t>(ClientMessage::Hello));
    writer.U8(0x01u); // half a version field and no build hash
    link.Send(End::Client, NetChannel::Session, writer.Written());

    session.Service();
    Assert::IsTrue(session.CurrentState() == State::Refused);
  }

  /* What a client asks for is permitted by policy, not by arriving. A solo
     session lets the player change the speed; a service session refuses, and
     says so, because silence would leave the client's keys doing nothing for
     no reason it could show. */
  TEST_METHOD(ASessionControlIsPermittedByPolicy)
  {
    LoopbackTransport link;
    ServerSession refusing{link, 0u, 40u, SessionPolicy{false}};
    OpenToRunning(link, refusing);

    SendControl(link, 5u, 1.5f);
    refusing.Service();

    ClientSessionControl control;
    Assert::IsFalse(refusing.TakeControl(control));

    LoopbackTransport::Message message;
    ServerMessage id = ServerMessage::Count;
    Assert::IsTrue(NextForClient(link, message, id));
    Assert::IsTrue(id == ServerMessage::CommandReject);
    {
      NetReader reader{message.bytes};
      (void)reader.U8();
      const ServerCommandReject reject = ServerCommandReject::Decode(reader);
      Assert::AreEqual(static_cast<std::uint32_t>(5), reject.sequence);
      Assert::IsTrue(reject.command == ClientMessage::SessionControl);
      Assert::IsTrue(reject.reason == RejectReason::NotPermitted);
    }

    LoopbackTransport otherLink;
    ServerSession permitting{otherLink, 0u, 40u, SessionPolicy{true}};
    OpenToRunning(otherLink, permitting);

    SendControl(otherLink, 1u, 1.5f);
    SendControl(otherLink, 2u, 0.5f);
    permitting.Service();

    Assert::IsTrue(permitting.TakeControl(control));
    Assert::AreEqual(1.5f, control.value);
    Assert::IsTrue(permitting.TakeControl(control));
    Assert::AreEqual(0.5f, control.value);
    Assert::IsFalse(permitting.TakeControl(control));
    Assert::AreEqual(static_cast<size_t>(0), otherLink.Pending(End::Client));
  }

  /* A speed the game never offered is refused rather than clamped: a client
     asking for it is a client that is wrong. */
  TEST_METHOD(ASpeedOutsideTheRangeIsRefused)
  {
    LoopbackTransport link;
    ServerSession session{link, 0u, 40u, SessionPolicy{true}};
    OpenToRunning(link, session);

    SendControl(link, 9u, 50.0f);
    session.Service();

    ClientSessionControl control;
    Assert::IsFalse(session.TakeControl(control));

    LoopbackTransport::Message message;
    ServerMessage id = ServerMessage::Count;
    Assert::IsTrue(NextForClient(link, message, id));
    Assert::IsTrue(id == ServerMessage::CommandReject);
  }

  /* A presentation call made against a client that has no world yet is
     dropped, as it would be for a client that had not connected at all; once
     Running it goes out on the replication channel, ordered against the world
     state it refers to. */
  TEST_METHOD(AUiEventGoesOutOnlyOnceRunning)
  {
    LoopbackTransport link;
    ServerSession session{link, 0u, 40u};

    ServerUiEvent event;
    event.kind = UiEventKind::CentreView;
    event.a = 12;
    event.b = 34;
    Assert::IsFalse(session.SendUiEvent(event));
    Assert::AreEqual(static_cast<size_t>(0), link.Pending(End::Client));

    OpenToRunning(link, session);
    Assert::IsTrue(session.SendUiEvent(event));

    LoopbackTransport::Message message;
    Assert::IsTrue(link.Receive(End::Client, NetChannel::Replication, message));
    NetReader reader{message.bytes};
    Assert::IsTrue(static_cast<ServerMessage>(reader.U8()) == ServerMessage::UiEvent);
    const ServerUiEvent got = ServerUiEvent::Decode(reader);
    Assert::IsTrue(reader.Ok());
    Assert::IsTrue(got.kind == UiEventKind::CentreView);
    Assert::AreEqual(static_cast<std::uint32_t>(12), got.a);
    Assert::AreEqual(static_cast<std::uint32_t>(34), got.b);
  }
};
} // namespace NeuronServerTest
