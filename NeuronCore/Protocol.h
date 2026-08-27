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
