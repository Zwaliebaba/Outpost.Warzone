#include "pch.h"
#include "CppUnitTest.h"

#include "Protocol.h"

#include <cstring>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Neuron;

/* The records stage D added after the handshake (Docs/ServerAuthority.md):
   Ready with the level it loaded, Kick, the first command-plane request and
   its refusal, and UiEvent, which is what the server's script VM says in place
   of the presentation calls it used to make.

   Each round-trips, and each folds a byte a peer chose but this build cannot
   name into something safe: a session that is over stays over, a request that
   cannot be read is dropped whole, and a UiEvent nobody can show is not shown
   as something else. */
namespace NeuronCoreTest
{
TEST_CLASS(SessionPlaneTest)
{
public:
  TEST_METHOD(ReadyAndKickRoundTrip)
  {
    std::byte buffer[32]{};
    NetWriter writer{buffer};
    Put(writer, ClientReady{0xC0FFEEu});
    Put(writer, ServerKick{KickReason::MapMismatch});
    Assert::IsFalse(writer.Overflowed());

    NetReader reader{writer.Written()};
    Assert::IsTrue(static_cast<ClientMessage>(reader.U8()) == ClientMessage::Ready);
    Assert::AreEqual(static_cast<std::uint32_t>(0xC0FFEE), ClientReady::Decode(reader).mapHash);
    Assert::IsTrue(static_cast<ServerMessage>(reader.U8()) == ServerMessage::Kick);
    Assert::IsTrue(ServerKick::Decode(reader).reason == KickReason::MapMismatch);
    Assert::IsTrue(reader.Ok());
  }

  /* A kick is over whether or not its reason can be read. */
  TEST_METHOD(AnUnknownKickReasonIsStillAKick)
  {
    const std::byte raw[1]{std::byte{0xFE}};
    NetReader reader{raw};
    Assert::IsTrue(ServerKick::Decode(reader).reason == KickReason::Unstated);
    Assert::IsTrue(reader.Ok());
  }

  TEST_METHOD(SessionControlAndRejectRoundTrip)
  {
    std::byte buffer[32]{};
    NetWriter writer{buffer};
    Put(writer, ClientSessionControl{7u, SessionControlKind::GameSpeed, 1.5f});
    Put(writer, ServerCommandReject{7u, ClientMessage::SessionControl, RejectReason::NotPermitted});
    Assert::IsFalse(writer.Overflowed());

    NetReader reader{writer.Written()};
    Assert::IsTrue(static_cast<ClientMessage>(reader.U8()) == ClientMessage::SessionControl);
    const ClientSessionControl control = ClientSessionControl::Decode(reader);
    Assert::AreEqual(static_cast<std::uint32_t>(7), control.sequence);
    Assert::IsTrue(control.kind == SessionControlKind::GameSpeed);
    Assert::AreEqual(1.5f, control.value);

    Assert::IsTrue(static_cast<ServerMessage>(reader.U8()) == ServerMessage::CommandReject);
    const ServerCommandReject reject = ServerCommandReject::Decode(reader);
    Assert::AreEqual(static_cast<std::uint32_t>(7), reject.sequence);
    Assert::IsTrue(reject.command == ClientMessage::SessionControl);
    Assert::IsTrue(reject.reason == RejectReason::NotPermitted);
    Assert::IsTrue(reader.Ok());
  }

  /* A control of a kind this build cannot carry out is not carried out as
     some other kind: the record is invalid and the caller drops it. */
  TEST_METHOD(AnUnknownControlKindIsDroppedWhole)
  {
    std::byte buffer[16]{};
    NetWriter writer{buffer};
    writer.U32(1u);
    writer.U8(0xFEu);
    writer.F32(1.0f);

    NetReader reader{writer.Written()};
    (void)ClientSessionControl::Decode(reader);
    Assert::IsFalse(reader.Ok());
    Assert::IsFalse(reader.Truncated());
  }

  TEST_METHOD(UiEventRoundTripsBothStrings)
  {
    ServerUiEvent sent;
    sent.kind = UiEventKind::PlayVideo;
    sent.player = 3;
    sent.a = 11;
    sent.b = 22;
    sent.SetName("cam1\\c001.mp4");
    sent.SetText("cam1\\c001.txa");

    std::byte buffer[512]{};
    NetWriter writer{buffer};
    Put(writer, sent);
    Assert::IsFalse(writer.Overflowed());

    NetReader reader{writer.Written()};
    Assert::IsTrue(static_cast<ServerMessage>(reader.U8()) == ServerMessage::UiEvent);
    const ServerUiEvent got = ServerUiEvent::Decode(reader);
    Assert::IsTrue(reader.Ok());
    Assert::IsTrue(got.kind == UiEventKind::PlayVideo);
    Assert::AreEqual(static_cast<int>(3), static_cast<int>(got.player));
    Assert::AreEqual(static_cast<std::uint32_t>(11), got.a);
    Assert::AreEqual(static_cast<std::uint32_t>(22), got.b);
    Assert::AreEqual(0, std::strcmp(got.name, "cam1\\c001.mp4"));
    Assert::AreEqual(0, std::strcmp(got.text, "cam1\\c001.txa"));
  }

  /* The fixed fields truncate rather than overflow, and a null is an empty
     string, because a script's string table is where these come from and a
     long line there must not become a refused message. */
  TEST_METHOD(UiEventStringsTruncateIntoTheirFields)
  {
    const std::string longText(UiEventTextChars * 2, 'x');

    ServerUiEvent event;
    event.SetName(nullptr);
    event.SetText(longText.c_str());
    Assert::AreEqual(static_cast<std::size_t>(0), std::strlen(event.name));
    Assert::AreEqual(UiEventTextChars - 1, std::strlen(event.text));

    std::byte buffer[1024]{};
    NetWriter writer{buffer};
    Put(writer, event);
    NetReader reader{writer.Written()};
    (void)reader.U8();
    const ServerUiEvent got = ServerUiEvent::Decode(reader);
    Assert::IsTrue(reader.Ok());
    Assert::AreEqual(UiEventTextChars - 1, std::strlen(got.text));
  }

  TEST_METHOD(AnUnknownUiEventKindIsNotShownAsAnother)
  {
    std::byte buffer[32]{};
    NetWriter writer{buffer};
    writer.U8(0xFEu);
    writer.U8(0u);
    writer.U32(0u);
    writer.U32(0u);
    writer.Text("");
    writer.Text("");

    NetReader reader{writer.Written()};
    (void)ServerUiEvent::Decode(reader);
    Assert::IsFalse(reader.Ok());
  }
};
} // namespace NeuronCoreTest
