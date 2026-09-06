/*
 * Protocol.h
 *
 * What the client and the server say to each other, and on what.
 */

#pragma once

#include "NetReader.h"
#include "NetWriter.h"

#include <cstdint>

namespace Neuron
{

/// Bumped when a change stops an older peer reading a newer one. The Hello
/// exchange compares it and refuses a mismatch with a reason, which is the job
/// NET_VERSION and the NEThashVal smuggled inside NET_OPTIONS used to do.
inline constexpr std::uint16_t ProtocolVersion = 1;

/// What a message travels on.
///
/// The 1998 protocol had one implicit channel and a guarantee flag, so a map
/// download and a move order queued behind each other. QUIC gives independently
/// ordered streams and unreliable datagrams for free (Phase 5 already links
/// MsQuic), so the split is a naming exercise rather than an implementation:
/// nothing above the Transport seam learns that QUIC is underneath.
enum class NetChannel : std::uint8_t
{
  /// Reliable, ordered. Hello, auth, lobby, chat, start and stop.
  Session,

  /// Reliable, ordered, client to server. Player intents, sequence numbered.
  Command,

  /// Reliable, server to client. Entity lifecycle and player-scoped state --
  /// the things a client must not miss.
  Replication,

  /// Unreliable datagrams, server to client. Per-tick deltas, where the next
  /// one supersedes the last, so a lost packet is cheaper to drop than to
  /// retransmit.
  Snapshot,

  /// Reliable, one stream per file, so a map download cannot head-of-line
  /// block an order.
  Bulk,

  Count,
};

/* The two message catalogues.
 *
 * These are wire values: the number is what goes in the first byte of a
 * message, so **append, never insert**. Renumbering an existing message
 * silently changes what an older peer thinks it received, and the version
 * handshake only catches that between builds that bothered to bump it.
 *
 * Both enums are contiguous and end in Count, so an id off the wire can be
 * range-checked before it is switched on -- see IsKnown below. Every byte here
 * was chosen by a peer, and a mis-decode must be distinguishable from a
 * message this build simply does not have yet.
 *
 * Docs/ServerAuthority.md carries what each one means and which 1998 NET_*
 * message it replaces; that table is the specification and this is its
 * spelling.
 */

/// Client to server. Everything here is a *request*: the 1998 protocol let a
/// client assert that a droid existed or that research was finished, and none
/// of those assertions survives as a message a client may send.
enum class ClientMessage : std::uint8_t
{
  // --- session plane ---
  Hello,
  Auth,
  GameList,
  CreateGame,
  Join,
  LobbySet,
  Ready,
  Chat,
  Ping,
  MapRequest,

  // --- command plane, all sequence numbered ---
  Order,
  Build,
  TemplateSet,
  TemplateDelete,
  Produce,
  Research,
  StructureMode,
  Embark,
  LasSat,
  Gift,
  Alliance,

  /// Game speed and the cheat console: what a solo session's flags permit and
  /// a service session refuses. Pause is deliberately not here -- it was
  /// removed as a feature rather than carried across the seam.
  SessionControl,

  /// Asks for a fresh keyframe of the interest set after datagram famine.
  Resync,

  Count,
};

/// Server to client. Everything here is a *statement*: the server is the only
/// thing that knows what happened.
enum class ServerMessage : std::uint8_t
{
  // --- session plane ---
  Hello,
  Auth,
  GameList,
  LobbyState,
  PlayerJoined,
  PlayerLeft,
  Kick,
  Chat,
  Pong,
  File,
  Start,
  GameOver,

  /// Answers a command the server would not run. Acceptance is not announced:
  /// it is visible as world change.
  CommandReject,

  // --- replication plane ---
  Tick,
  Enter,
  Update,
  Leave,
  Destroy,
  PlayerState,
  ResearchDone,
  Alliance,
  Effect,

  /// What the server's script VM emits in place of the presentation calls it
  /// makes today -- intelligence messages, console text, camera moves, sounds,
  /// sequence triggers. The campaign rides this one.
  UiEvent,

  Scores,
  GameState,

  Count,
};

/// Why a command was refused. The UI needs the distinction: "no power" and
/// "not yours" want different feedback, and RateLimited is the one the player
/// should never see.
enum class RejectReason : std::uint8_t
{
  NotYours,
  NoSuchTarget,

  /// The target exists but the sender cannot see it. This is the check the
  /// 1998 protocol never made, and ordering through fog is what it allowed.
  NotVisible,

  NotResearched,
  NoPower,
  IllegalPlacement,
  NotPermitted,
  RateLimited,

  Count,
};

/// Whether an id off the wire names a message this build knows.
///
/// An unknown id is not by itself an error -- a newer peer may send one -- but
/// it has to be told apart from a known one, so the receiver can ignore the
/// message instead of switching on a value it will mis-handle.
[[nodiscard]] constexpr bool IsKnown(ClientMessage _id) noexcept
{
  return static_cast<std::uint8_t>(_id) < static_cast<std::uint8_t>(ClientMessage::Count);
}

[[nodiscard]] constexpr bool IsKnown(ServerMessage _id) noexcept
{
  return static_cast<std::uint8_t>(_id) < static_cast<std::uint8_t>(ServerMessage::Count);
}

/* ---- The session handshake ------------------------------------------------
 *
 * The first exchange on any connection, local or remote. It is what
 * NET_VERSION and the executable hash smuggled inside NET_OPTIONS used to do,
 * except that it happens before anything else is believed rather than after
 * the lobby has already been joined.
 *
 * Each record encodes its *body*. The message id goes on separately, because a
 * receiver has to read the id to know which Decode to call -- so Put below
 * writes both and takes the id from the record's own Id, which is the only way
 * to get them out of step and it removes it.
 */

/// Why a server would not take a connection. A code rather than a string: the
/// client has to act on it, not just print it.
enum class HandshakeResult : std::uint8_t
{
  Accepted,

  /// The two builds do not speak the same protocol version at all.
  ProtocolMismatch,

  /// Same protocol, different executable. What NEThashVal caught at join.
  BuildMismatch,

  ServerFull,

  Count,
};

/// ClientMessage::Hello -- the first bytes a client sends.
struct ClientHello
{
  static constexpr ClientMessage Id = ClientMessage::Hello;

  std::uint16_t protocolVersion = ProtocolVersion;
  std::uint32_t buildHash = 0;

  void Encode(NetWriter& _writer) const;

  /// Reads the body. The caller has already taken the id off the reader.
  [[nodiscard]] static ClientHello Decode(NetReader& _reader);
};

/// ServerMessage::Hello -- the answer, and the session's terms.
struct ServerHello
{
  static constexpr ServerMessage Id = ServerMessage::Hello;

  HandshakeResult result = HandshakeResult::Accepted;

  /// How much game time one simulation tick advances the world by. The client
  /// derives its clock from this rather than assuming, so a server may be
  /// retuned without every client being rebuilt.
  std::uint16_t tickMs = 0;

  std::uint32_t connectionId = 0;

  void Encode(NetWriter& _writer) const;
  [[nodiscard]] static ServerHello Decode(NetReader& _reader);
};

/// ServerMessage::Start -- the level is agreed, here is when the world begins.
struct ServerStart
{
  static constexpr ServerMessage Id = ServerMessage::Start;

  std::uint32_t startTick = 0;
  std::uint16_t countdownMs = 0;
  std::uint32_t mapHash = 0;

  void Encode(NetWriter& _writer) const;
  [[nodiscard]] static ServerStart Decode(NetReader& _reader);
};

/// ServerMessage::Tick -- heads each tick's traffic, and is the client's only
/// statement of what time it is in the world.
struct ServerTick
{
  static constexpr ServerMessage Id = ServerMessage::Tick;

  std::uint32_t tick = 0;

  void Encode(NetWriter& _writer) const;
  [[nodiscard]] static ServerTick Decode(NetReader& _reader);
};

/// ClientMessage::Ready -- the client has a level loaded and can be sent world
/// state.
///
/// It names the level it *actually* loaded, so a client that loaded something
/// other than what Start named is found out here rather than left to desync
/// from tick 1 with nothing to say why.
struct ClientReady
{
  static constexpr ClientMessage Id = ClientMessage::Ready;

  std::uint32_t mapHash = 0;

  void Encode(NetWriter& _writer) const;
  [[nodiscard]] static ClientReady Decode(NetReader& _reader);
};

/// Why a server ended a session it had accepted. A code rather than a string,
/// for the same reason HandshakeResult is one.
enum class KickReason : std::uint8_t
{
  /// The server gave a reason this build has no name for. The session is
  /// still over; only the reason is missing.
  Unstated,

  /// Ready named a different level than Start did.
  MapMismatch,

  Count,
};

/// ServerMessage::Kick -- this session is over.
struct ServerKick
{
  static constexpr ServerMessage Id = ServerMessage::Kick;

  KickReason reason = KickReason::Unstated;

  void Encode(NetWriter& _writer) const;
  [[nodiscard]] static ServerKick Decode(NetReader& _reader);
};

/* ---- The command plane -----------------------------------------------------
 *
 * What a client asks for. Every record here is sequence numbered so that a
 * refusal can say which request it refused; acceptance is never announced,
 * because it is visible as world change.
 */

/// What a SessionControl asks the session to do.
enum class SessionControlKind : std::uint8_t
{
  /// Run the world at value times normal speed. What the speed keys used to do
  /// to the clock directly.
  GameSpeed,

  Count,
};

/// ClientMessage::SessionControl -- game speed, and in time the cheat console:
/// what a solo session's flags permit and a service session refuses.
struct ClientSessionControl
{
  static constexpr ClientMessage Id = ClientMessage::SessionControl;

  std::uint32_t sequence = 0;
  SessionControlKind kind = SessionControlKind::GameSpeed;
  float value = 1.0f;

  void Encode(NetWriter& _writer) const;
  [[nodiscard]] static ClientSessionControl Decode(NetReader& _reader);
};

/// ServerMessage::CommandReject -- the server would not run a command.
struct ServerCommandReject
{
  static constexpr ServerMessage Id = ServerMessage::CommandReject;

  /// The sequence number of the request refused.
  std::uint32_t sequence = 0;

  /// Which kind of request it was.
  ClientMessage command = ClientMessage::SessionControl;

  RejectReason reason = RejectReason::NotPermitted;

  void Encode(NetWriter& _writer) const;
  [[nodiscard]] static ServerCommandReject Decode(NetReader& _reader);
};

/* ---- The replication plane ------------------------------------------------
 *
 * How the client comes to have a world. Everything the client draws arrives
 * through these: nothing is created because the client decided something ought
 * to exist. That is the whole inversion, and it is why the ids below are
 * server-minted rather than the 1998 (objID << 3) | player, which every peer
 * could compute because every peer created objects.
 */

/// Server-minted, unique for the life of a session. Zero is not an entity.
using EntityId = std::uint32_t;

/// What kind of thing an entity is, as far as the wire is concerned.
///
/// This is the world's half of OBJECT_TYPE and not all of it: OBJ_TARGET is a
/// camera-tracking placeholder rather than something that exists in the world,
/// so it is not replicable and has no value here.
enum class EntityKind : std::uint8_t
{
  Droid,
  Structure,
  Feature,
  Projectile,

  Count,
};

/// ServerMessage::Enter -- this entity is now in your interest set, and here is
/// everything needed to start drawing it.
///
/// Enter is the only message that brings an entity into existence on a client.
/// An Update naming an entity nobody has entered is dropped rather than used to
/// invent one, so a lost Enter costs a missing object rather than a half-known
/// one that is missing whatever Update does not carry.
struct ServerEnter
{
  static constexpr ServerMessage Id = ServerMessage::Enter;

  EntityId entityId = 0;
  EntityKind kind = EntityKind::Droid;
  std::uint8_t player = 0;
  std::uint16_t x = 0;
  std::uint16_t y = 0;
  std::uint16_t z = 0;

  /// Radians, +ve rotation about y, as BASE_OBJECT stores it.
  float direction = 0.0f;

  void Encode(NetWriter& _writer) const;
  [[nodiscard]] static ServerEnter Decode(NetReader& _reader);
};

/// ServerMessage::Update -- where an entity is now.
///
/// Carries only what moves. Kind and player are stated once by Enter because
/// they do not change, which is the difference between the two messages and the
/// reason an Update is small enough to send every tick.
struct ServerUpdate
{
  static constexpr ServerMessage Id = ServerMessage::Update;

  EntityId entityId = 0;
  std::uint16_t x = 0;
  std::uint16_t y = 0;
  std::uint16_t z = 0;
  float direction = 0.0f;

  void Encode(NetWriter& _writer) const;
  [[nodiscard]] static ServerUpdate Decode(NetReader& _reader);
};

/// ServerMessage::Leave -- this entity has left your interest set.
///
/// It still exists; you can no longer see it. Distinct from Destroy because the
/// client does different things: an entity that walked into fog is not an
/// entity that blew up, and the 1998 model could not tell the two apart because
/// every peer knew everything all the time.
struct ServerLeave
{
  static constexpr ServerMessage Id = ServerMessage::Leave;

  EntityId entityId = 0;

  void Encode(NetWriter& _writer) const;
  [[nodiscard]] static ServerLeave Decode(NetReader& _reader);
};

/// ServerMessage::Destroy -- this entity no longer exists.
struct ServerDestroy
{
  static constexpr ServerMessage Id = ServerMessage::Destroy;

  EntityId entityId = 0;

  void Encode(NetWriter& _writer) const;
  [[nodiscard]] static ServerDestroy Decode(NetReader& _reader);
};

/* ---- UiEvent -----------------------------------------------------------------
 *
 * What the server's script VM emits in place of the presentation calls it used
 * to make directly. A mission script that says addMessage, centreView or
 * playVideo is running on the server, and the server has no screen; it says
 * what it wants shown and the client shows it.
 */

/// The player a UiEvent is addressed to when it is addressed to everyone.
inline constexpr std::uint8_t UiEventEveryPlayer = 0xFF;

/// The fixed record fields a UiEvent's strings land in. A name longer than
/// this is truncated into it, as NetReader::Text does everywhere.
inline constexpr std::size_t UiEventNameChars = 64;
inline constexpr std::size_t UiEventTextChars = 256;

/// What a UiEvent asks the client to show. The fields each kind uses are named
/// beside it; the rest are zero.
enum class UiEventKind : std::uint8_t
{
  /// An intelligence message. name is the view data, a is the message type,
  /// b is non-zero to display it immediately.
  AddMessage,

  /// Remove an intelligence message. name is the view data, a is the type.
  RemoveMessage,

  /// Centre the view. a and b are the tile coordinates.
  CentreView,

  /// Play a sound. a is the sound id.
  PlaySound,

  /// Show console text. text is the line; a is non-zero if it is permanent.
  ConsoleText,

  ClearConsole,

  /// The tutorial is over; the console goes back to normal.
  TutorialEnd,

  /// Play a sequence. name is the video, text the subtitle file.
  PlayVideo,

  Count,
};

/// ServerMessage::UiEvent.
struct ServerUiEvent
{
  static constexpr ServerMessage Id = ServerMessage::UiEvent;

  UiEventKind kind = UiEventKind::ClearConsole;

  /// Whose presentation this is for, or UiEventEveryPlayer. A client showing
  /// another player's world ignores what is not addressed to it.
  std::uint8_t player = UiEventEveryPlayer;

  std::uint32_t a = 0;
  std::uint32_t b = 0;
  char name[UiEventNameChars] = {};
  char text[UiEventTextChars] = {};

  /// Copies into the fixed fields, truncating to fit. A null pointer is an
  /// empty string.
  void SetName(const char* _name) noexcept;
  void SetText(const char* _text) noexcept;

  void Encode(NetWriter& _writer) const;
  [[nodiscard]] static ServerUiEvent Decode(NetReader& _reader);
};

/// Writes a message id and its body together, taking the id from the record so
/// the two cannot disagree.
template <typename Message>
void Put(NetWriter& _writer, const Message& _message)
{
  _writer.U8(static_cast<std::uint8_t>(Message::Id));
  _message.Encode(_writer);
}

/// Whether a server would accept this hello. The version check is a function
/// rather than an inline test because it is policy, and policy is worth a test.
[[nodiscard]] HandshakeResult Consider(const ClientHello& _hello, std::uint32_t _serverBuildHash) noexcept;

} // namespace Neuron
