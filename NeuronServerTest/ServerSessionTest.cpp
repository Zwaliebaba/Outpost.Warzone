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
   end of its buffer - and that no tick is sent to a client that has not
   reported itself ready for one. */
namespace NeuronServerTest
{
using End = LoopbackTransport::End;
using State = ServerSession::State;

namespace
{
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

/// Takes the next message the client can see, returning its id.
bool NextForClient(LoopbackTransport& _link, LoopbackTransport::Message& _message, ServerMessage& _outId)
{
  if (!_link.Receive(End::Client, _message))
    return false;

  NetReader reader{_message.bytes};
  _outId = static_cast<ServerMessage>(reader.U8());
  return true;
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

    session.Start(0xC0FFEEu);
    Assert::IsTrue(session.CurrentState() == State::Starting);
    Assert::IsTrue(NextForClient(link, message, id));
    Assert::IsTrue(id == ServerMessage::Start);

    /* The world must not run ahead of a client that has not said it is
       loaded, or the first thing it is told is state it has nowhere to put. */
    session.Tick();
    Assert::AreEqual(static_cast<std::uint32_t>(0), session.CurrentTick());
    Assert::AreEqual(static_cast<size_t>(0), link.Pending(End::Client));

    SendId(link, ClientMessage::Ready);
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
    SendId(link, ClientMessage::Ready);
    session.Service();
    Assert::IsTrue(session.CurrentState() == State::Refused);
    Assert::AreEqual(static_cast<size_t>(0), link.Pending(End::Server));
  }

  TEST_METHOD(APeerCannotDriveTheSessionOutOfOrder)
  {
    LoopbackTransport link;
    ServerSession session{link, 0u, 40u};

    SendId(link, ClientMessage::Ready); // before any hello
    session.Service();
    Assert::IsTrue(session.CurrentState() == State::Handshaking);

    std::byte scratch[8]{};
    NetWriter writer{scratch};
    writer.U8(0xFEu); // an id this build has never heard of
    link.Send(End::Client, NetChannel::Session, writer.Written());
    session.Service();
    Assert::IsTrue(session.CurrentState() == State::Handshaking);
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
};
} // namespace NeuronServerTest
