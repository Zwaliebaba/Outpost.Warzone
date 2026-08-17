#include "pch.h"
/***************************************************************************/
/*
 * Transport.cpp
 *
 * The QUIC side of the transport seam in Transport.h, over MsQuic.
 *
 * MsQuic supplies what DirectPlay supplied and what a hand-written protocol
 * would otherwise have had to: reliable ordered streams, unreliable datagrams,
 * connection lifecycle and idle timeout, and TLS 1.3 on every byte. What is
 * written here is the session and player layer above that.
 *
 * The shape is the one DirectPlay had. The host listens; every client holds
 * one connection to it; a client sending to another client is relayed by the
 * host, because the host is the only machine everyone is connected to. There
 * is no discovery: a joiner types an address. That is deliberate -- the
 * destination is a relay server that owns the connections, where listing games
 * is a query to a known server rather than a shout at the local network, and
 * building the broadcast version first would have been building something
 * whose only future was deletion. When the server exists it goes behind
 * Transport::FindSessions and Transport::Join, and nothing above the seam changes.
 *
 * Threads. MsQuic runs its own workers and calls back on them, so everything
 * shared lives behind g_lock and the game thread only ever sees the queues.
 * Two rules keep that honest, and breaking either one deadlocks:
 *
 *   - never hold g_lock across a blocking MsQuic call (ConnectionClose,
 *     ListenerClose, RegistrationClose all wait for callbacks to drain);
 *   - the send-completion callbacks take no lock at all, because MsQuic may
 *     run them inline inside StreamSend.
 *
 * ConnectionShutdown is the deliberate exception: it does not block, and it is
 * issued with the lock held on purpose, because a connection handle read out of
 * the peer table and used after the lock is dropped can have been closed in
 * between by that peer's own teardown.
 */
/***************************************************************************/

#include <winsock2.h>
#include <windows.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* msquic_winuser.h writes `#if DEBUG` to pick the debug layout of QUIC_SQE.
 * Debug.h -- reached from pch.h long before this line -- defines DEBUG with no
 * value, which turns that into `#if` with no expression and is a preprocessor
 * error rather than a wrong answer. Undefining it for the include is also the
 * correct value: the msquic.dll the package ships is a release build, so the
 * layout it was compiled with is the one `#if DEBUG` selects when DEBUG is
 * absent.
 */
#pragma push_macro("DEBUG")
#undef DEBUG
#include <msquic.h>
#pragma pop_macro("DEBUG")

#include "Frame.h"
#include "HostCertificate.h"
#include "Transport.h"

namespace Neuron
{

/* The file-scope helpers and state below are this translation unit's alone.
 * An anonymous namespace rather than `static` on each, as AGENTS R1 asks --
 * and the mutable statics are the sm_ case that rule mentions, whose
 * thread-safety is the subject of the header comment above.
 */
namespace
{

/***************************************************************************/

/* The port a host listens on. QUIC is UDP, and DirectPlay's own port is of no
 * relevance, so this is simply a number nothing else is using.
 */
constexpr UWORD DefaultPort = 9987;

/* Application-layer protocol negotiation. QUIC requires one, and it doubles as
 * a version check: a client built against a different protocol fails the
 * handshake rather than getting halfway into a game and desynchronising.
 */
constexpr const char* Alpn = "owz1";

/* The payload ceiling is the seam's, not this file's: Transport::Receive writes
 * into a buffer the caller sized, so accepting a larger message from the wire
 * than the caller can hold would be a peer-controlled overrun. A frame bigger
 * than this is a peer that is confused or hostile, and its connection is
 * dropped rather than the buffer grown.
 */
constexpr UDWORD MaxFrameBytes = (Transport::MaxMessageBytes + 64);
constexpr UDWORD AssemblyBytes = (MaxFrameBytes + 2);

/* Thirty seconds of silence ends a connection; a keepalive every five means
 * that a lobby nobody is typing in never reaches it.
 */
constexpr UDWORD IdleTimeoutMs = 30000;
constexpr UDWORD KeepAliveMs = 5000;

/* A connection that has arrived but not identified itself. Ten seconds is
 * generous for a handshake and short enough that a stuck one does not hold a
 * player slot for the length of a game.
 */
constexpr UDWORD HelloTimeoutMs = 10000;

/* How long Transport::Leave waits for MsQuic to finish with the connections
 * before giving up and closing the registration anyway.
 */
constexpr UDWORD DrainTimeoutMs = 5000;

/* Received messages waiting for the game to collect them. The game drains this
 * every frame; a backlog this size means it has stopped doing so.
 */
constexpr UDWORD MaxQueuedMessages = 512;
constexpr UDWORD MaxQueuedEvents = 64;

/* The application error code a connection is closed with. QUIC puts it on the
 * wire, so the other end can tell a deliberate close from a crash.
 */
constexpr UDWORD ErrorShutdown = 0;
constexpr UDWORD ErrorProtocol = 1;
constexpr UDWORD ErrorSessionFull = 2;

/* The host is always the first player. Zero stays free because the game
 * already reads it as "everybody, or nobody" -- it was DPID_ALLPLAYERS, and
 * NETsendFile still tests for it.
 */
constexpr NETPLAYERID NoPlayer = 0;
constexpr NETPLAYERID HostPlayer = 1;

/***************************************************************************/
/* The wire.
 *
 * On a stream, every frame is preceded by its length as a big-endian u16,
 * because a QUIC stream is a byte pipe and not a message queue. A datagram is
 * one frame with no prefix, since QUIC keeps datagram boundaries itself.
 *
 * Frames are numbered rather than named on the wire, and the payload of a DATA
 * frame is whatever the game handed to Transport::Send -- this layer does not
 * look inside it.
 */
/***************************************************************************/

constexpr BYTE FrameHello = 1;	/* client->host: u16 len, name             */
constexpr BYTE FrameWelcome = 2;	/* host->client: you, host, max, flags,
									   roster                                  */
constexpr BYTE FrameJoined = 3;	/* host->clients: u32 id, u16 len, name    */
constexpr BYTE FrameLeft = 4;	/* host->clients: u32 id                   */
constexpr BYTE FrameData = 5;	/* either way: u32 from, u32 to, payload   */
constexpr BYTE FrameReject = 6;	/* host->client: u16 reason                */
constexpr BYTE FrameRename = 7;	/* both ways: u32 id, u16 len, name        */

constexpr UWORD RejectSessionFull = 1;
constexpr UWORD RejectClosed = 2;

/***************************************************************************/

static void PutU16(BYTE* out, UWORD value)
{
  out[0] = static_cast<BYTE>(value >> 8);
  out[1] = static_cast<BYTE>(value);
}

static void PutU32(BYTE* out, UDWORD value)
{
  out[0] = static_cast<BYTE>(value >> 24);
  out[1] = static_cast<BYTE>(value >> 16);
  out[2] = static_cast<BYTE>(value >> 8);
  out[3] = static_cast<BYTE>(value);
}

static UWORD GetU16(const BYTE* in)
{
  return static_cast<UWORD>((static_cast<UWORD>(in[0]) << 8) | in[1]);
}

static UDWORD GetU32(const BYTE* in)
{
  return (static_cast<UDWORD>(in[0]) << 24) | (static_cast<UDWORD>(in[1]) << 16) |
         (static_cast<UDWORD>(in[2]) << 8) | static_cast<UDWORD>(in[3]);
}

/***************************************************************************/

/* A send in flight. MsQuic does not copy what it is given: the bytes have to
 * stay put until it says it is finished with them, which is what the send
 * completion callbacks are for. The buffer descriptor and the bytes are one
 * allocation so that freeing it is one call.
 */
struct SendBuffer
{
  QUIC_BUFFER buffer;
  BYTE data[1];
};

static SendBuffer* AllocSendBuffer(UDWORD size)
{
  SendBuffer* send;

  send = static_cast<SendBuffer*>(malloc(offsetof(SendBuffer, data) + size));
  if (send == nullptr)
    return nullptr;

  send->buffer.Buffer = send->data;
  send->buffer.Length = size;

  return send;
}

/***************************************************************************/

/* A message the game has not collected yet. Sized to the message rather than
 * to the maximum, because most of them are tiny and there can be hundreds
 * queued in a frame.
 */
struct QueuedMessage
{
  struct QueuedMessage* next;
  NETPLAYERID from;
  UDWORD size;
  BYTE data[1];
};

/***************************************************************************/

enum class PeerState : std::uint8_t
{
  Free,
  Connecting,	/* has a connection, has not said who it is  */
  Ready			/* in the roster, traffic flows              */
};

/* One connection. On the host there is one of these per client; on a client
 * there is exactly one, for the host.
 */
struct Peer
{
  PeerState state;
  HQUIC connection;
  HQUIC stream;
  NETPLAYERID id;
  ULONGLONG arrivedAtMs;	/* for the hello timeout                     */
  UWORD maxDatagramBytes;	/* 0 until the peer says it accepts them     */

  UDWORD fill;
  BYTE assembly[AssemblyBytes];
};

/* A player, which is not the same thing as a connection: the host is in the
 * roster and has no connection to itself, and a client knows about players it
 * has no connection to at all.
 */
struct RosterEntry
{
  BOOL used;
  NETPLAYERID id;
  char name[StringSize];
};

/***************************************************************************/

static const QUIC_API_TABLE* g_msQuic = nullptr;
static HQUIC g_registration = nullptr;

/* Named so it is recognisable in MsQuic's own tracing, which is otherwise a
 * sea of anonymous registrations.
 */
static const QUIC_REGISTRATION_CONFIG g_registrationConfig = {"Outpost", QUIC_EXECUTION_PROFILE_LOW_LATENCY};

static HQUIC g_configuration = nullptr;
static HQUIC g_listener = nullptr;

static CRITICAL_SECTION g_lock;
static BOOL g_lockReady = FALSE;

static BOOL g_inSession = FALSE;
static BOOL g_hosting = FALSE;
static BOOL g_leaving = FALSE;
static BOOL g_closedToJoiners = FALSE;

static NETPLAYERID g_localPlayer = NoPlayer;
static NETPLAYERID g_nextPlayerId = HostPlayer;
static UDWORD g_maxPlayers = 0;
static DWORD g_gameFlags[Transport::GameFlagCount] = {0, 0, 0, 0};
static char g_sessionName[StringSize] = "";
static char g_localName[StringSize] = "";

static Peer g_peers[MaxNumberOfPlayers];
static RosterEntry g_roster[MaxNumberOfPlayers];

static QueuedMessage* g_queueHead = nullptr;
static QueuedMessage* g_queueTail = nullptr;
static UDWORD g_queuedCount = 0;

static Transport::Event g_events[MaxQueuedEvents];
static UDWORD g_eventHead = 0;
static UDWORD g_eventCount = 0;

/* Connections whose handles this file still owns -- adopted but not yet passed
 * to ConnectionClose. Counted rather than derived from the peer table because
 * a peer is released at the top of its shutdown and the handle is closed at the
 * bottom, and it is the bottom that Transport::Leave has to wait for: MsQuic
 * requires every connection closed before the configuration they were started
 * with.
 */
static UDWORD g_liveConnections = 0;

/* Set when that count reaches zero during a departure, which is when it is safe
 * to close the rest of the session.
 */
static HANDLE g_drained = nullptr;

static UDWORD g_bytesSent = 0;
static UDWORD g_bytesReceived = 0;
static UDWORD g_packetsSent = 0;
static UDWORD g_packetsReceived = 0;

/* Traced once rather than per send, because a game that oversizes one
 * unreliable message oversizes thousands of them.
 */
static BOOL g_warnedDatagramSize = FALSE;

/***************************************************************************/

static QUIC_STATUS QUIC_API ConnectionCallback(HQUIC connection, void* pContext,
                                                    QUIC_CONNECTION_EVENT* event);
static QUIC_STATUS QUIC_API StreamCallback(HQUIC stream, void* pContext,
                                                QUIC_STREAM_EVENT* event);
static void SendFrameTo(Peer* peer, const BYTE* frame, UDWORD size, BOOL reliable);

/***************************************************************************/
/* The roster. Called with g_lock held. */
/***************************************************************************/

static RosterEntry* FindPlayer(NETPLAYERID id)
{
  int i;

  for (i = 0; i < MaxNumberOfPlayers; i++)
    if (g_roster[i].used && g_roster[i].id == id)
      return &g_roster[i];

  return nullptr;
}

static RosterEntry* AddPlayer(NETPLAYERID id, const char name[])
{
  RosterEntry* player;
  int i;

  player = FindPlayer(id);
  if (player == nullptr)
  {
    for (i = 0; i < MaxNumberOfPlayers; i++)
      if (!g_roster[i].used)
      {
        player = &g_roster[i];
        break;
      }
  }

  if (player == nullptr)
    return nullptr;

  player->used = TRUE;
  player->id = id;
  strncpy(player->name, name, StringSize - 1);
  player->name[StringSize - 1] = '\0';

  return player;
}

static void RemovePlayer(NETPLAYERID id)
{
  RosterEntry* player;

  player = FindPlayer(id);
  if (player != nullptr)
    player->used = FALSE;
}

static UDWORD RosterCount(void)
{
  UDWORD count = 0;
  int i;

  for (i = 0; i < MaxNumberOfPlayers; i++)
    if (g_roster[i].used)
      count++;

  return count;
}

/***************************************************************************/
/* The queues. Called with g_lock held. */
/***************************************************************************/

static void QueueMessage(NETPLAYERID from, const BYTE* data, UDWORD size)
{
  QueuedMessage* message;

  if (g_queuedCount >= MaxQueuedMessages)
  {
    Neuron::DebugTrace("netquic: receive queue full, dropping {} bytes from {}\n", size,
                       static_cast<UDWORD>(from));
    return;
  }

  message = static_cast<QueuedMessage*>(malloc(offsetof(QueuedMessage, data) + size));
  if (message == nullptr)
    return;

  message->next = nullptr;
  message->from = from;
  message->size = size;
  memcpy(message->data, data, size);

  if (g_queueTail != nullptr)
    g_queueTail->next = message;
  else
    g_queueHead = message;
  g_queueTail = message;
  g_queuedCount++;

  g_bytesReceived += size;
  g_packetsReceived++;
}

static void FlushQueue(void)
{
  QueuedMessage* message;

  while (g_queueHead != nullptr)
  {
    message = g_queueHead;
    g_queueHead = message->next;
    free(message);
  }

  g_queueTail = nullptr;
  g_queuedCount = 0;
}

static void QueueEvent(TransportEventType type, NETPLAYERID player, const char name[])
{
  Transport::Event* event;

  /* Suppressed while leaving: the game asked for the session to end and does
   * not want to be told about every player it is walking away from.
   */
  if (g_leaving)
    return;

  if (g_eventCount >= MaxQueuedEvents)
  {
    Neuron::DebugTrace("netquic: event queue full, dropping event {}\n", static_cast<UDWORD>(type));
    return;
  }

  event = &g_events[(g_eventHead + g_eventCount) % MaxQueuedEvents];
  event->type = type;
  event->player = player;
  if (name != nullptr)
  {
    strncpy(event->name, name, StringSize - 1);
    event->name[StringSize - 1] = '\0';
  }
  else
    event->name[0] = '\0';

  g_eventCount++;
}

/***************************************************************************/
/* Peers. Called with g_lock held unless said otherwise. */
/***************************************************************************/

static Peer* FindFreePeer(void)
{
  int i;

  for (i = 0; i < MaxNumberOfPlayers; i++)
    if (g_peers[i].state == PeerState::Free)
      return &g_peers[i];

  return nullptr;
}

static Peer* FindPeerById(NETPLAYERID id)
{
  int i;

  for (i = 0; i < MaxNumberOfPlayers; i++)
    if (g_peers[i].state == PeerState::Ready && g_peers[i].id == id)
      return &g_peers[i];

  return nullptr;
}

/* Called after ConnectionClose, and taking the lock itself, because the close
 * it follows must not happen under one.
 */
static void OnConnectionClosed(void)
{
  EnterCriticalSection(&g_lock);

  if (g_liveConnections > 0)
    g_liveConnections--;

  if (g_liveConnections == 0 && g_leaving && g_drained != nullptr)
    SetEvent(g_drained);

  LeaveCriticalSection(&g_lock);
}

/***************************************************************************/
/* Frames out. */
/***************************************************************************/

/* One frame to one peer. The stream carries a length prefix and a datagram
 * does not, which is the only difference between the two paths.
 */
static void SendFrameTo(Peer* peer, const BYTE* frame, UDWORD size, BOOL reliable)
{
  SendBuffer* send;
  QUIC_STATUS status;

  if (peer == nullptr || peer->state == PeerState::Free)
    return;

  /* An unreliable send that will not fit in a datagram goes reliably instead.
   * Dropping it would be worse: the game asked for the message to be sent, and
   * only said it did not need it guaranteed.
   */
  if (!reliable && (peer->maxDatagramBytes == 0 || size > peer->maxDatagramBytes))
  {
    if (!g_warnedDatagramSize)
    {
      Neuron::DebugTrace("netquic: {} bytes will not fit a datagram (limit {}), sending reliably\n",
                         size, static_cast<UDWORD>(peer->maxDatagramBytes));
      g_warnedDatagramSize = TRUE;
    }
    reliable = TRUE;
  }

  if (reliable && peer->stream == nullptr)
    return;

  send = AllocSendBuffer(reliable ? size + 2 : size);
  if (send == nullptr)
    return;

  if (reliable)
  {
    PutU16(send->data, static_cast<UWORD>(size));
    memcpy(send->data + 2, frame, size);
    status = g_msQuic->StreamSend(peer->stream, &send->buffer, 1, QUIC_SEND_FLAG_NONE, send);
  }
  else
  {
    memcpy(send->data, frame, size);
    status = g_msQuic->DatagramSend(peer->connection, &send->buffer, 1, QUIC_SEND_FLAG_NONE, send);
  }

  if (QUIC_FAILED(status))
  {
    free(send);
    return;
  }

  g_bytesSent += size;
  g_packetsSent++;
}

/* Every ready peer except one. Passing NoPlayer as the exception sends to
 * all of them, which is what the host's own broadcast wants.
 */
static void SendFrameToAll(const BYTE* frame, UDWORD size, BOOL reliable,
                                NETPLAYERID except)
{
  int i;

  for (i = 0; i < MaxNumberOfPlayers; i++)
    if (g_peers[i].state == PeerState::Ready && g_peers[i].id != except)
      SendFrameTo(&g_peers[i], frame, size, reliable);
}

/* A DATA frame, built in place. The header is nine bytes and the payload
 * follows it, so this is the one frame worth a helper of its own.
 */
static void SendDataFrame(Peer* peer, NETPLAYERID from, NETPLAYERID to, const void* data,
                          UDWORD size, BOOL reliable)
{
  BYTE frame[MaxFrameBytes];

  if (size > Transport::MaxMessageBytes)
    return;

  frame[0] = FrameData;
  PutU32(frame + 1, from);
  PutU32(frame + 5, to);
  memcpy(frame + 9, data, size);

  SendFrameTo(peer, frame, size + 9, reliable);
}

static void SendDataFrameToAll(NETPLAYERID from, NETPLAYERID to, const void* data, UDWORD size,
                               BOOL reliable, NETPLAYERID except)
{
  BYTE frame[MaxFrameBytes];

  if (size > Transport::MaxMessageBytes)
    return;

  frame[0] = FrameData;
  PutU32(frame + 1, from);
  PutU32(frame + 5, to);
  memcpy(frame + 9, data, size);

  SendFrameToAll(frame, size + 9, reliable, except);
}

/* The roster, as a joiner is given it. Everything a client needs to know about
 * the session it has just been admitted to, in one frame, so that there is no
 * window in which it is connected but knows nothing.
 */
static void SendWelcome(Peer* peer)
{
  BYTE frame[MaxFrameBytes];
  UDWORD at;
  UDWORD nameLength;
  UWORD entryCount = 0;
  UDWORD countAt;
  int i;

  frame[0] = FrameWelcome;
  PutU32(frame + 1, peer->id);
  PutU32(frame + 5, HostPlayer);
  PutU16(frame + 9, static_cast<UWORD>(g_maxPlayers));
  for (i = 0; i < Transport::GameFlagCount; i++)
    PutU32(frame + 11 + i * 4, g_gameFlags[i]);

  countAt = 11 + Transport::GameFlagCount * 4;
  at = countAt + 2;

  for (i = 0; i < MaxNumberOfPlayers; i++)
  {
    if (!g_roster[i].used)
      continue;

    nameLength = static_cast<UDWORD>(strlen(g_roster[i].name));
    PutU32(frame + at, g_roster[i].id);
    PutU16(frame + at + 4, static_cast<UWORD>(nameLength));
    memcpy(frame + at + 6, g_roster[i].name, nameLength);
    at += 6 + nameLength;
    entryCount++;
  }

  PutU16(frame + countAt, entryCount);

  SendFrameTo(peer, frame, at, TRUE);
}

static void SendJoined(NETPLAYERID id, const char name[], NETPLAYERID except)
{
  BYTE frame[7 + StringSize];
  UDWORD nameLength;

  nameLength = static_cast<UDWORD>(strlen(name));
  frame[0] = FrameJoined;
  PutU32(frame + 1, id);
  PutU16(frame + 5, static_cast<UWORD>(nameLength));
  memcpy(frame + 7, name, nameLength);

  SendFrameToAll(frame, 7 + nameLength, TRUE, except);
}

/* A rename, which travels in both directions: a client tells the host, and the
 * host tells everyone else. It is not a FrameJoined with a new name because that
 * would raise a join event for somebody already playing.
 */
static void SendRename(Peer* peer, NETPLAYERID id, const char name[], BOOL toAll,
                            NETPLAYERID except)
{
  BYTE frame[7 + StringSize];
  UDWORD nameLength;

  nameLength = static_cast<UDWORD>(strlen(name));
  if (nameLength >= StringSize)
    nameLength = StringSize - 1;

  frame[0] = FrameRename;
  PutU32(frame + 1, id);
  PutU16(frame + 5, static_cast<UWORD>(nameLength));
  memcpy(frame + 7, name, nameLength);

  if (toAll)
    SendFrameToAll(frame, 7 + nameLength, TRUE, except);
  else
    SendFrameTo(peer, frame, 7 + nameLength, TRUE);
}

static void SendLeft(NETPLAYERID id)
{
  BYTE frame[5];

  frame[0] = FrameLeft;
  PutU32(frame + 1, id);

  SendFrameToAll(frame, 5, TRUE, id);
}

static void SendReject(Peer* peer, UWORD reason)
{
  BYTE frame[3];

  frame[0] = FrameReject;
  PutU16(frame + 1, reason);

  SendFrameTo(peer, frame, 3, TRUE);
}

/***************************************************************************/
/* Frames in. Called on a MsQuic worker with g_lock held. Returning FALSE
 * means the peer has said something it should not have, and its connection is
 * dropped by the caller.
 */
/***************************************************************************/

static BOOL OnHello(Peer* peer, const BYTE* frame, UDWORD size)
{
  char name[StringSize];
  UDWORD nameLength;

  if (!g_hosting || peer->state != PeerState::Connecting || size < 3)
    return FALSE;

  nameLength = GetU16(frame + 1);
  if (nameLength + 3 > size || nameLength >= StringSize)
    return FALSE;

  memcpy(name, frame + 3, nameLength);
  name[nameLength] = '\0';

  /* A refusal is sent and then the send side is closed gracefully, rather than
   * the connection being shut down here: ConnectionShutdown discards whatever
   * is still queued on the streams, which would include the refusal itself.
   * The peer stays PeerState::Connecting and Transport::Update reaps it.
   */
  if (g_closedToJoiners || RosterCount() >= g_maxPlayers)
  {
    SendReject(peer, g_closedToJoiners ? RejectClosed : RejectSessionFull);
    g_msQuic->StreamShutdown(peer->stream, QUIC_STREAM_SHUTDOWN_FLAG_GRACEFUL,
                              ErrorSessionFull);
    return TRUE;
  }

  peer->id = ++g_nextPlayerId;
  peer->state = PeerState::Ready;

  /* The welcome carries the roster as it is now, so it goes out before the
   * new player is added to it -- otherwise the joiner is told about itself.
   */
  SendWelcome(peer);

  AddPlayer(peer->id, name);
  SendJoined(peer->id, name, peer->id);
  QueueEvent(TransportEventType::PlayerJoined, peer->id, name);

  return TRUE;
}

static BOOL OnWelcome(Peer* peer, const BYTE* frame, UDWORD size)
{
  UDWORD at;
  UDWORD count;
  UDWORD nameLength;
  NETPLAYERID id;
  char name[StringSize];
  UDWORD i;

  if (g_hosting || peer->state != PeerState::Connecting)
    return FALSE;

  at = 11 + Transport::GameFlagCount * 4;
  if (size < at + 2)
    return FALSE;

  g_localPlayer = GetU32(frame + 1);
  g_maxPlayers = GetU16(frame + 9);
  for (i = 0; i < Transport::GameFlagCount; i++)
    g_gameFlags[i] = GetU32(frame + 11 + i * 4);

  count = GetU16(frame + at);
  at += 2;

  peer->id = GetU32(frame + 5);
  peer->state = PeerState::Ready;

  for (i = 0; i < count; i++)
  {
    if (at + 6 > size)
      return FALSE;

    id = GetU32(frame + at);
    nameLength = GetU16(frame + at + 4);
    if (at + 6 + nameLength > size || nameLength >= StringSize)
      return FALSE;

    memcpy(name, frame + at + 6, nameLength);
    name[nameLength] = '\0';
    at += 6 + nameLength;

    AddPlayer(id, name);
    QueueEvent(TransportEventType::PlayerJoined, id, name);
  }

  /* The joiner is in the roster too, but was not in the frame: the host built
   * that before adding it. No event for it -- the game knows it just joined.
   */
  AddPlayer(g_localPlayer, g_localName);

  return TRUE;
}

static BOOL OnJoined(const BYTE* frame, UDWORD size)
{
  NETPLAYERID id;
  UDWORD nameLength;
  char name[StringSize];

  if (g_hosting || size < 7)
    return FALSE;

  id = GetU32(frame + 1);
  nameLength = GetU16(frame + 5);
  if (nameLength + 7 > size || nameLength >= StringSize)
    return FALSE;

  memcpy(name, frame + 7, nameLength);
  name[nameLength] = '\0';

  AddPlayer(id, name);
  QueueEvent(TransportEventType::PlayerJoined, id, name);

  return TRUE;
}

static BOOL OnLeft(const BYTE* frame, UDWORD size)
{
  NETPLAYERID id;
  RosterEntry* player;
  char name[StringSize];

  if (g_hosting || size < 5)
    return FALSE;

  id = GetU32(frame + 1);
  player = FindPlayer(id);
  if (player == nullptr)
    return TRUE;

  strncpy(name, player->name, StringSize - 1);
  name[StringSize - 1] = '\0';

  RemovePlayer(id);
  QueueEvent(TransportEventType::PlayerLeft, id, name);

  return TRUE;
}

static BOOL OnRename(Peer* peer, const BYTE* frame, UDWORD size)
{
  NETPLAYERID id;
  UDWORD nameLength;
  char name[StringSize];

  if (peer->state != PeerState::Ready || size < 7)
    return FALSE;

  id = GetU32(frame + 1);
  nameLength = GetU16(frame + 5);
  if (nameLength + 7 > size || nameLength >= StringSize)
    return FALSE;

  memcpy(name, frame + 7, nameLength);
  name[nameLength] = '\0';

  /* A client may rename itself and nobody else. The host relays what it
   * accepts, which is the same authority it has over FrameData.
   */
  if (g_hosting)
  {
    if (id != peer->id)
      return FALSE;

    if (FindPlayer(id) == nullptr)
      return FALSE;

    AddPlayer(id, name);
    SendRename(nullptr, id, name, TRUE, id);
    return TRUE;
  }

  if (FindPlayer(id) == nullptr)
    return TRUE;	/* somebody we have not been told about yet */

  AddPlayer(id, name);
  return TRUE;
}

static BOOL OnData(Peer* peer, const BYTE* frame, UDWORD size, BOOL reliable)
{
  NETPLAYERID from, to;
  const BYTE* payload;
  UDWORD payloadSize;
  Peer* target;

  if (peer->state != PeerState::Ready || size < 9)
    return FALSE;

  from = GetU32(frame + 1);
  to = GetU32(frame + 5);
  payload = frame + 9;
  payloadSize = size - 9;

  /* The frame ceiling has slack in it for headers, so a maximum-sized frame
   * carries a slightly over-sized payload. Transport::Receive writes into a
   * buffer the caller sized to Transport::MaxMessageBytes, so that slack has to be
   * refused here or a peer can choose how far past the end to write.
   */
  if (payloadSize > Transport::MaxMessageBytes)
    return FALSE;

  if (!g_hosting)
  {
    /* A client is only ever sent what is meant for it, so there is nothing to
     * decide: whatever arrives is delivered.
     */
    QueueMessage(from, payload, payloadSize);
    return TRUE;
  }

  /* A client cannot claim to be somebody else. This is the whole of the
   * host's authority over the wire today, and it is worth having even so.
   */
  if (from != peer->id)
    return FALSE;

  if (to == NoPlayer)
  {
    QueueMessage(from, payload, payloadSize);
    SendDataFrameToAll(from, to, payload, payloadSize, reliable, from);
    return TRUE;
  }

  if (to == g_localPlayer)
  {
    QueueMessage(from, payload, payloadSize);
    return TRUE;
  }

  target = FindPeerById(to);
  if (target != nullptr)
    SendDataFrame(target, from, to, payload, payloadSize, reliable);

  return TRUE;
}

static BOOL OnReject(const BYTE* frame, UDWORD size)
{
  if (size < 3)
    return FALSE;

  Neuron::DebugTrace("netquic: the host refused the join, reason {}\n",
                     static_cast<UDWORD>(GetU16(frame + 1)));

  return FALSE;
}

static BOOL OnFrame(Peer* peer, const BYTE* frame, UDWORD size, BOOL reliable)
{
  BOOL ok;

  if (size < 1)
    return FALSE;

  switch (frame[0])
  {
  case FrameHello: ok = OnHello(peer, frame, size); break;
  case FrameWelcome: ok = OnWelcome(peer, frame, size); break;
  case FrameJoined: ok = OnJoined(frame, size); break;
  case FrameLeft: ok = OnLeft(frame, size); break;
  case FrameData: ok = OnData(peer, frame, size, reliable); break;
  case FrameReject: ok = OnReject(frame, size); break;
  case FrameRename: ok = OnRename(peer, frame, size); break;
  default: ok = FALSE; break;
  }

  /* A refusal ends the connection by design and has traced its own reason.
   * Anything else saying no is a frame this protocol cannot read.
   */
  if (!ok && frame[0] != FrameReject)
    Neuron::DebugTrace("netquic: unreadable frame, kind {} size {}\n", static_cast<UDWORD>(frame[0]),
                       size);

  return ok;
}

/* A QUIC stream is a byte pipe, so what arrives has no relation to what was
 * sent: one frame can come in four pieces and four can come in one. Bytes are
 * accumulated until a length prefix and its frame are both complete.
 *
 * The buffer holds one frame at most, so a chunk larger than the space left is
 * taken in pieces rather than rejected.
 */
static BOOL OnStreamBytes(Peer* peer, const BYTE* data, UDWORD size)
{
  UDWORD take;
  UDWORD frameSize;

  while (size > 0)
  {
    take = AssemblyBytes - peer->fill;
    if (take > size)
      take = size;

    if (take == 0)
      return FALSE;	/* a frame longer than the buffer: not one of ours */

    memcpy(peer->assembly + peer->fill, data, take);
    peer->fill += take;
    data += take;
    size -= take;

    for (;;)
    {
      if (peer->fill < 2)
        break;

      frameSize = GetU16(peer->assembly);
      if (frameSize == 0 || frameSize > MaxFrameBytes)
        return FALSE;

      if (peer->fill < frameSize + 2)
        break;

      if (!OnFrame(peer, peer->assembly + 2, frameSize, TRUE))
        return FALSE;

      peer->fill -= frameSize + 2;
      memmove(peer->assembly, peer->assembly + frameSize + 2, peer->fill);
    }
  }

  return TRUE;
}

/***************************************************************************/
/* Teardown.
 *
 * Releasing a peer and closing its connection are two steps on purpose:
 * ConnectionClose blocks until MsQuic has finished with the handle, so it can
 * never be called with g_lock held. Every path here frees the slot under the
 * lock and closes the handle outside it.
 */
/***************************************************************************/

/* Called with g_lock held. Hands back the connection for the caller to close
 * once it has let go of the lock. The stream is not handed back: MsQuic has
 * already raised its SHUTDOWN_COMPLETE, which closed it.
 */
static void ReleasePeer(Peer* peer, HQUIC* phConn)
{
  RosterEntry* player;
  char name[StringSize];
  NETPLAYERID id;

  *phConn = peer->connection;

  id = peer->id;
  peer->state = PeerState::Free;
  peer->connection = nullptr;
  peer->stream = nullptr;
  peer->id = NoPlayer;
  peer->fill = 0;
  peer->maxDatagramBytes = 0;

  if (!g_hosting)
  {
    /* The only peer a client has is the host, so losing it ends the session --
     * including when it is lost during the handshake, which is what a refused
     * or unreachable join looks like from here. There is no migration: the
     * plan says so, and the simulation could not survive one anyway.
     */
    QueueEvent(TransportEventType::HostLost, id, nullptr);
    return;
  }

  if (id != NoPlayer)
  {
    player = FindPlayer(id);
    if (player == nullptr)
      return;

    strncpy(name, player->name, StringSize - 1);
    name[StringSize - 1] = '\0';

    RemovePlayer(id);
    SendLeft(id);
    QueueEvent(TransportEventType::PlayerLeft, id, name);
  }
}

/***************************************************************************/
/* MsQuic callbacks. */
/***************************************************************************/

static QUIC_STATUS QUIC_API StreamCallback(HQUIC stream, void* pContext,
                                                QUIC_STREAM_EVENT* event)
{
  Peer* peer = static_cast<Peer*>(pContext);
  HQUIC connection;
  BOOL ok = TRUE;
  UDWORD i;

  switch (event->Type)
  {
  case QUIC_STREAM_EVENT_SEND_COMPLETE:
    /* No lock: MsQuic can raise this inline from StreamSend, and taking
     * g_lock here would be taking it twice on the same thread.
     */
    free(event->SEND_COMPLETE.ClientContext);
    break;

  case QUIC_STREAM_EVENT_RECEIVE:
    EnterCriticalSection(&g_lock);
    for (i = 0; i < event->RECEIVE.BufferCount && ok; i++)
      ok = OnStreamBytes(peer, event->RECEIVE.Buffers[i].Buffer,
                               event->RECEIVE.Buffers[i].Length);
    connection = peer->connection;
    LeaveCriticalSection(&g_lock);

    if (!ok && connection != nullptr)
    {
      /* Either the peer said something this protocol has no reading of, or it
       * refused our join -- both end the connection, and the reason has
       * already been traced by whichever handler decided it.
       *
       * Shutdown rather than close: this is the connection's own callback
       * thread, and the close belongs in SHUTDOWN_COMPLETE.
       */
      g_msQuic->ConnectionShutdown(connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, ErrorProtocol);
    }
    break;

  case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
  case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
    /* The peer is finished with the stream, which for this protocol means it
     * is finished with the session. The connection teardown does the rest.
     */
    EnterCriticalSection(&g_lock);
    connection = peer->connection;
    LeaveCriticalSection(&g_lock);

    if (connection != nullptr)
      g_msQuic->ConnectionShutdown(connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, ErrorShutdown);
    break;

  case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
    /* The handle is dead either way; only the peer's copy of it needs
     * clearing, and only if the peer still points at this stream.
     */
    EnterCriticalSection(&g_lock);
    if (peer->stream == stream)
      peer->stream = nullptr;
    LeaveCriticalSection(&g_lock);
    g_msQuic->StreamClose(stream);
    break;

  default: break;
  }

  return QUIC_STATUS_SUCCESS;
}

static QUIC_STATUS QUIC_API ConnectionCallback(HQUIC connection, void* pContext,
                                                    QUIC_CONNECTION_EVENT* event)
{
  Peer* peer = static_cast<Peer*>(pContext);
  HQUIC closingConnection = nullptr;
  HQUIC stream;
  BYTE hello[3 + StringSize];
  UDWORD nameLength;
  QUIC_STATUS status;

  switch (event->Type)
  {
  case QUIC_CONNECTION_EVENT_CONNECTED:
    /* The client opens the one stream the protocol uses and introduces
     * itself on it. The host waits to be spoken to.
     */
    if (!g_hosting)
    {
      /* Opened outside the lock, because StreamClose blocks until the stream's
       * callbacks have drained. Nothing else can be looking at this peer's
       * stream yet -- it does not exist until the line below.
       */
      stream = nullptr;
      status = g_msQuic->StreamOpen(connection, QUIC_STREAM_OPEN_FLAG_NONE, StreamCallback, peer,
                                     &stream);
      if (QUIC_SUCCEEDED(status))
      {
        status = g_msQuic->StreamStart(stream, QUIC_STREAM_START_FLAG_IMMEDIATE);
        if (QUIC_FAILED(status))
        {
          g_msQuic->StreamClose(stream);
          stream = nullptr;
        }
      }

      if (stream == nullptr)
      {
        Neuron::DebugTrace("netquic: cannot open the session stream, 0x{:08x}\n",
                           static_cast<UDWORD>(status));
        g_msQuic->ConnectionShutdown(connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, ErrorProtocol);
        break;
      }

      EnterCriticalSection(&g_lock);
      peer->stream = stream;
      nameLength = static_cast<UDWORD>(strlen(g_localName));
      hello[0] = FrameHello;
      PutU16(hello + 1, static_cast<UWORD>(nameLength));
      memcpy(hello + 3, g_localName, nameLength);
      SendFrameTo(peer, hello, 3 + nameLength, TRUE);
      LeaveCriticalSection(&g_lock);
    }
    break;

  case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
    EnterCriticalSection(&g_lock);
    if (peer->stream != nullptr)
    {
      /* One stream per connection is the whole protocol, and the settings say
       * so, so the peer should not be able to get here. Refusing means
       * returning a failure without taking the handle: an accepted stream is
       * one whose callback handler has been set, and a second one taking this
       * peer's assembly buffer would corrupt the first.
       */
      LeaveCriticalSection(&g_lock);
      Neuron::DebugTrace("netquic: refusing a second stream from a peer\n");
      return QUIC_STATUS_NOT_SUPPORTED;
    }

    peer->stream = event->PEER_STREAM_STARTED.Stream;
    g_msQuic->SetCallbackHandler(peer->stream, reinterpret_cast<void*>(StreamCallback),
                                  peer);
    LeaveCriticalSection(&g_lock);
    break;

  case QUIC_CONNECTION_EVENT_DATAGRAM_STATE_CHANGED:
    EnterCriticalSection(&g_lock);
    peer->maxDatagramBytes = event->DATAGRAM_STATE_CHANGED.SendEnabled
                              ? event->DATAGRAM_STATE_CHANGED.MaxSendLength
                              : 0;
    LeaveCriticalSection(&g_lock);
    break;

  case QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED:
    EnterCriticalSection(&g_lock);
    if (!OnFrame(peer, event->DATAGRAM_RECEIVED.Buffer->Buffer,
                      event->DATAGRAM_RECEIVED.Buffer->Length, FALSE))
      Neuron::DebugTrace("netquic: ignoring an unreadable datagram\n");
    LeaveCriticalSection(&g_lock);
    break;

  case QUIC_CONNECTION_EVENT_DATAGRAM_SEND_STATE_CHANGED:
    /* No lock, for the same reason as SEND_COMPLETE. The first terminal state
     * is the one that frees: MsQuic will not touch the buffer again, and
     * ACKNOWLEDGED may never arrive for a datagram nobody acknowledges.
     */
    switch (event->DATAGRAM_SEND_STATE_CHANGED.State)
    {
    case QUIC_DATAGRAM_SEND_SENT:
    case QUIC_DATAGRAM_SEND_LOST_DISCARDED:
    case QUIC_DATAGRAM_SEND_ACKNOWLEDGED:
    case QUIC_DATAGRAM_SEND_ACKNOWLEDGED_SPURIOUS:
    case QUIC_DATAGRAM_SEND_CANCELED:
      if (event->DATAGRAM_SEND_STATE_CHANGED.ClientContext != nullptr)
      {
        free(event->DATAGRAM_SEND_STATE_CHANGED.ClientContext);
        event->DATAGRAM_SEND_STATE_CHANGED.ClientContext = nullptr;
      }
      break;

    default: break;
    }
    break;

  case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
    EnterCriticalSection(&g_lock);
    ReleasePeer(peer, &closingConnection);
    LeaveCriticalSection(&g_lock);

    /* Outside the lock, because ConnectionClose waits for the connection's
     * callbacks to drain. The count comes down after the close rather than
     * before it, so that a departing session waits for the handle to be gone
     * and not merely for the peer to be forgotten.
     */
    if (closingConnection != nullptr)
    {
      g_msQuic->ConnectionClose(closingConnection);
      OnConnectionClosed();
    }
    break;

  default: break;
  }

  return QUIC_STATUS_SUCCESS;
}

static QUIC_STATUS QUIC_API ListenerCallback(HQUIC listener, void* pContext,
                                                  QUIC_LISTENER_EVENT* event)
{
  Peer* peer;
  QUIC_STATUS status;

  (void)listener;
  (void)pContext;

  if (event->Type != QUIC_LISTENER_EVENT_NEW_CONNECTION)
    return QUIC_STATUS_SUCCESS;

  EnterCriticalSection(&g_lock);

  if (g_leaving || g_closedToJoiners || RosterCount() >= g_maxPlayers)
  {
    LeaveCriticalSection(&g_lock);
    return QUIC_STATUS_CONNECTION_REFUSED;
  }

  peer = FindFreePeer();
  if (peer == nullptr)
  {
    LeaveCriticalSection(&g_lock);
    return QUIC_STATUS_CONNECTION_REFUSED;
  }

  peer->state = PeerState::Connecting;
  peer->connection = event->NEW_CONNECTION.Connection;
  peer->stream = nullptr;
  peer->id = NoPlayer;
  peer->fill = 0;
  peer->maxDatagramBytes = 0;
  peer->arrivedAtMs = GetTickCount64();

  g_msQuic->SetCallbackHandler(peer->connection, reinterpret_cast<void*>(ConnectionCallback),
                                peer);

  status = g_msQuic->ConnectionSetConfiguration(peer->connection, g_configuration);
  if (QUIC_FAILED(status))
  {
    /* Returning a failure from here hands the connection back to MsQuic to
     * dispose of, so the handle is not this file's to close and never counted.
     */
    peer->state = PeerState::Free;
    peer->connection = nullptr;
    LeaveCriticalSection(&g_lock);
    Neuron::DebugTrace("netquic: ConnectionSetConfiguration failed, 0x{:08x}\n",
                       static_cast<UDWORD>(status));
    return status;
  }

  g_liveConnections++;
  LeaveCriticalSection(&g_lock);

  return QUIC_STATUS_SUCCESS;
}

/***************************************************************************/
/* Configuration. */
/***************************************************************************/

static void FillSettings(QUIC_SETTINGS* settings)
{
  memset(settings, 0, sizeof(*settings));

  settings->IdleTimeoutMs = IdleTimeoutMs;
  settings->IsSet.IdleTimeoutMs = TRUE;

  settings->KeepAliveIntervalMs = KeepAliveMs;
  settings->IsSet.KeepAliveIntervalMs = TRUE;

  /* One bidirectional stream, which is the one the client opens. Set on both
   * ends: a client that allowed none would make the protocol impossible to
   * extend without a version bump neither end would understand.
   */
  settings->PeerBidiStreamCount = 1;
  settings->IsSet.PeerBidiStreamCount = TRUE;

  settings->DatagramReceiveEnabled = TRUE;
  settings->IsSet.DatagramReceiveEnabled = TRUE;
}

static BOOL OpenConfiguration(BOOL asHost)
{
  QUIC_SETTINGS settings;
  QUIC_CREDENTIAL_CONFIG credential;
  QUIC_CERTIFICATE_HASH certHash;
  QUIC_BUFFER alpn;
  QUIC_STATUS status;

  FillSettings(&settings);

  alpn.Buffer = reinterpret_cast<uint8_t*>(const_cast<char*>(Alpn));
  alpn.Length = static_cast<uint32_t>(strlen(Alpn));

  status = g_msQuic->ConfigurationOpen(g_registration, &alpn, 1, &settings, sizeof(settings),
                                        nullptr, &g_configuration);
  if (QUIC_FAILED(status))
  {
    Neuron::DebugTrace("netquic: ConfigurationOpen failed, 0x{:08x}\n", static_cast<UDWORD>(status));
    g_configuration = nullptr;
    return FALSE;
  }

  memset(&credential, 0, sizeof(credential));

  if (asHost)
  {
    if (!HostCertificate::Acquire(certHash.ShaHash))
    {
      Neuron::DebugTrace("netquic: no host certificate, cannot host\n");
      g_msQuic->ConfigurationClose(g_configuration);
      g_configuration = nullptr;
      return FALSE;
    }

    credential.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_HASH;
    credential.Flags = QUIC_CREDENTIAL_FLAG_NONE;
    credential.CertificateHash = &certHash;
  }
  else
  {
    /* The certificate is self-signed and there is no name to check it
     * against, so validating it would fail every time. What this buys is
     * encryption without authentication: see HostCertificate.h.
     */
    credential.Type = QUIC_CREDENTIAL_TYPE_NONE;
    credential.Flags = QUIC_CREDENTIAL_FLAG_CLIENT | QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;
  }

  status = g_msQuic->ConfigurationLoadCredential(g_configuration, &credential);
  if (QUIC_FAILED(status))
  {
    Neuron::DebugTrace("netquic: ConfigurationLoadCredential failed, 0x{:08x}\n",
                       static_cast<UDWORD>(status));
    g_msQuic->ConfigurationClose(g_configuration);
    g_configuration = nullptr;
    if (asHost)
      HostCertificate::Release();
    return FALSE;
  }

  return TRUE;
}

/***************************************************************************/

/* Splits "host", "host:port" and "[v6]:port" into the two things
 * ConnectionStart wants. MsQuic resolves names and literals alike, so nothing
 * has to be looked up here.
 */
static void SplitAddress(const char address[], char host[], UDWORD hostSize,
                              UWORD* port)
{
  const char* pColon;
  UDWORD udwLen;

  *port = DefaultPort;

  /* A bracketed literal ends at the bracket, so a colon inside it is part of
   * the address rather than a port separator.
   */
  if (address[0] == '[')
  {
    pColon = strchr(address, ']');
    pColon = (pColon != nullptr) ? strchr(pColon, ':') : nullptr;
  }
  else
  {
    pColon = strrchr(address, ':');

    /* An unbracketed address with more than one colon is a bare IPv6 literal,
     * and the last colon is part of it.
     */
    if (pColon != nullptr && strchr(address, ':') != pColon)
      pColon = nullptr;
  }

  if (pColon != nullptr && pColon[1] != '\0')
  {
    *port = static_cast<UWORD>(atoi(pColon + 1));
    if (*port == 0)
      *port = DefaultPort;
    udwLen = static_cast<UDWORD>(pColon - address);
  }
  else
    udwLen = static_cast<UDWORD>(strlen(address));

  /* MsQuic wants the literal without its brackets. */
  if (address[0] == '[' && udwLen >= 2)
  {
    address++;
    udwLen -= 2;
  }

  if (udwLen >= hostSize)
    udwLen = hostSize - 1;

  memcpy(host, address, udwLen);
  host[udwLen] = '\0';
}

/***************************************************************************/

static void ResetSession(void)
{
  int i;

  for (i = 0; i < MaxNumberOfPlayers; i++)
  {
    g_peers[i].state = PeerState::Free;
    g_peers[i].connection = nullptr;
    g_peers[i].stream = nullptr;
    g_peers[i].id = NoPlayer;
    g_peers[i].fill = 0;
    g_peers[i].maxDatagramBytes = 0;
    g_roster[i].used = FALSE;
  }

  FlushQueue();
  g_eventHead = 0;
  g_eventCount = 0;

  g_inSession = FALSE;
  g_hosting = FALSE;
  g_leaving = FALSE;
  g_closedToJoiners = FALSE;
  g_localPlayer = NoPlayer;
  g_nextPlayerId = HostPlayer;
  g_maxPlayers = 0;
  g_sessionName[0] = '\0';
  g_warnedDatagramSize = FALSE;

  for (i = 0; i < Transport::GameFlagCount; i++)
    g_gameFlags[i] = 0;
}

} // anonymous namespace

/***************************************************************************/
/* Lifecycle */
/***************************************************************************/

BOOL Transport::Startup(void)
{
  QUIC_STATUS status;

  if (g_msQuic != nullptr)
    return TRUE;

  if (!g_lockReady)
  {
    InitializeCriticalSection(&g_lock);
    g_lockReady = TRUE;
  }

  if (g_drained == nullptr)
  {
    g_drained = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (g_drained == nullptr)
      return FALSE;
  }

  status = MsQuicOpen2(&g_msQuic);
  if (QUIC_FAILED(status))
  {
    Neuron::DebugTrace("Transport::Startup: MsQuicOpen2 failed, 0x{:08x}\n", static_cast<UDWORD>(status));
    g_msQuic = nullptr;
    return FALSE;
  }

  /* LOW_LATENCY rather than the default: this carries lockstep game commands,
   * where a late packet is worse than a small one.
   */
  status = g_msQuic->RegistrationOpen(&g_registrationConfig, &g_registration);
  if (QUIC_FAILED(status))
  {
    Neuron::DebugTrace("Transport::Startup: RegistrationOpen failed, 0x{:08x}\n",
                       static_cast<UDWORD>(status));
    MsQuicClose(g_msQuic);
    g_msQuic = nullptr;
    g_registration = nullptr;
    return FALSE;
  }

  ResetSession();

  return TRUE;
}

/***************************************************************************/

void Transport::Shutdown(void)
{
  if (g_msQuic == nullptr)
    return;

  Transport::Leave();

  if (g_registration != nullptr)
  {
    /* Closing the registration waits for every connection under it to finish,
     * so nothing can still be in a callback once this returns.
     */
    g_msQuic->RegistrationClose(g_registration);
    g_registration = nullptr;
  }

  MsQuicClose(g_msQuic);
  g_msQuic = nullptr;

  if (g_drained != nullptr)
  {
    CloseHandle(g_drained);
    g_drained = nullptr;
  }

  if (g_lockReady)
  {
    DeleteCriticalSection(&g_lock);
    g_lockReady = FALSE;
  }
}

/***************************************************************************/

void Transport::Update(void)
{
  ULONGLONG nowMs;
  UDWORD i;

  if (!g_inSession || !g_hosting)
    return;

  nowMs = GetTickCount64();

  /* A connection that arrived and never introduced itself. Everything else
   * about a peer is driven by MsQuic's callbacks; this needs a clock, which is
   * why it is here rather than in one of them.
   *
   * The shutdown is issued under the lock, as in Transport::Leave and for the
   * same reason: a handle read out of the peer table and used after the lock
   * is dropped can have been closed in between by the peer's own teardown.
   */
  EnterCriticalSection(&g_lock);
  for (i = 0; i < MaxNumberOfPlayers; i++)
    if (g_peers[i].state == PeerState::Connecting && g_peers[i].connection != nullptr &&
        nowMs - g_peers[i].arrivedAtMs > HelloTimeoutMs)
    {
      Neuron::DebugTrace("netquic: dropping a connection that never said hello\n");
      g_msQuic->ConnectionShutdown(g_peers[i].connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                    ErrorProtocol);
    }
  LeaveCriticalSection(&g_lock);
}

/***************************************************************************/
/* Sessions */
/***************************************************************************/

BOOL Transport::Host(const char _sessionName[], const char _playerName[], UDWORD _maxPlayers,
                   const DWORD _gameFlags[])
{
  QUIC_ADDR address;
  QUIC_BUFFER alpn;
  QUIC_STATUS status;
  int i;

  if (g_msQuic == nullptr || g_inSession)
    return FALSE;

  if (_maxPlayers == 0 || _maxPlayers > MaxNumberOfPlayers)
    _maxPlayers = MaxNumberOfPlayers;

  ResetSession();

  g_hosting = TRUE;
  g_maxPlayers = _maxPlayers;
  g_localPlayer = HostPlayer;
  g_nextPlayerId = HostPlayer;

  strncpy(g_sessionName, _sessionName, StringSize - 1);
  g_sessionName[StringSize - 1] = '\0';
  strncpy(g_localName, _playerName, StringSize - 1);
  g_localName[StringSize - 1] = '\0';

  if (_gameFlags != nullptr)
    for (i = 0; i < Transport::GameFlagCount; i++)
      g_gameFlags[i] = _gameFlags[i];

  if (!OpenConfiguration(TRUE))
  {
    g_hosting = FALSE;
    return FALSE;
  }

  status = g_msQuic->ListenerOpen(g_registration, ListenerCallback, nullptr, &g_listener);
  if (QUIC_FAILED(status))
  {
    Neuron::DebugTrace("Transport::Host: ListenerOpen failed, 0x{:08x}\n", static_cast<UDWORD>(status));
    g_msQuic->ConfigurationClose(g_configuration);
    g_configuration = nullptr;
    HostCertificate::Release();
    g_hosting = FALSE;
    return FALSE;
  }

  /* UNSPEC binds both families, so a joiner may arrive over either. */
  memset(&address, 0, sizeof(address));
  QuicAddrSetFamily(&address, QUIC_ADDRESS_FAMILY_UNSPEC);
  QuicAddrSetPort(&address, DefaultPort);

  alpn.Buffer = reinterpret_cast<uint8_t*>(const_cast<char*>(Alpn));
  alpn.Length = static_cast<uint32_t>(strlen(Alpn));

  /* The host joins its own roster before the door opens. The other order has a
   * window in which a joiner's welcome would carry a roster with no host in
   * it, and the player-count check would be one short.
   */
  EnterCriticalSection(&g_lock);
  AddPlayer(g_localPlayer, g_localName);
  g_inSession = TRUE;
  LeaveCriticalSection(&g_lock);

  status = g_msQuic->ListenerStart(g_listener, &alpn, 1, &address);
  if (QUIC_FAILED(status))
  {
    Neuron::DebugTrace("Transport::Host: ListenerStart failed on port {}, 0x{:08x}\n",
                       static_cast<UDWORD>(DefaultPort), static_cast<UDWORD>(status));
    g_msQuic->ListenerClose(g_listener);
    g_listener = nullptr;
    g_msQuic->ConfigurationClose(g_configuration);
    g_configuration = nullptr;
    HostCertificate::Release();
    ResetSession();
    return FALSE;
  }

  return TRUE;
}

/***************************************************************************/

UDWORD Transport::FindSessions(Transport::SessionInfo _sessions[], UDWORD _max)
{
  (void)_sessions;
  (void)_max;

  /* Nothing to ask. There is no discovery in this build and no server to
   * query yet, so the join screen takes a typed address. When the relay
   * server exists its game list is answered from here.
   */
  return 0;
}

/***************************************************************************/

BOOL Transport::Join(const char _address[], const char _playerName[])
{
  char host[Transport::AddressSize];
  UWORD port;
  Peer* peer;
  QUIC_STATUS status;

  if (g_msQuic == nullptr || g_inSession)
    return FALSE;

  ResetSession();

  g_hosting = FALSE;
  g_maxPlayers = MaxNumberOfPlayers;
  strncpy(g_localName, _playerName, StringSize - 1);
  g_localName[StringSize - 1] = '\0';

  if (!OpenConfiguration(FALSE))
    return FALSE;

  SplitAddress(_address, host, sizeof(host), &port);

  peer = &g_peers[0];
  peer->state = PeerState::Connecting;
  peer->connection = nullptr;
  peer->stream = nullptr;
  peer->id = NoPlayer;
  peer->fill = 0;
  peer->maxDatagramBytes = 0;
  peer->arrivedAtMs = GetTickCount64();

  /* In session before the connection is started, not after: the handshake runs
   * on a MsQuic thread and can finish before this function returns. The game
   * learns it is really in when its own id arrives with the welcome, which is
   * what Transport::LocalPlayer reports.
   */
  g_inSession = TRUE;

  status = g_msQuic->ConnectionOpen(g_registration, ConnectionCallback, peer,
                                     &peer->connection);
  if (QUIC_FAILED(status))
  {
    Neuron::DebugTrace("Transport::Join: ConnectionOpen failed, 0x{:08x}\n", static_cast<UDWORD>(status));
    g_msQuic->ConfigurationClose(g_configuration);
    g_configuration = nullptr;
    ResetSession();
    return FALSE;
  }

  EnterCriticalSection(&g_lock);
  g_liveConnections++;
  LeaveCriticalSection(&g_lock);

  status = g_msQuic->ConnectionStart(peer->connection, g_configuration, QUIC_ADDRESS_FAMILY_UNSPEC,
                                      host, port);
  if (QUIC_FAILED(status))
  {
    Neuron::DebugTrace("Transport::Join: cannot reach {} port {}, 0x{:08x}\n", host,
                       static_cast<UDWORD>(port), static_cast<UDWORD>(status));
    /* Closing may raise this connection's shutdown inline, which queues a
     * host-lost event; the reset below throws it away along with the rest of
     * the session that never happened.
     */
    g_msQuic->ConnectionClose(peer->connection);
    peer->connection = nullptr;
    OnConnectionClosed();
    g_msQuic->ConfigurationClose(g_configuration);
    g_configuration = nullptr;
    ResetSession();
    return FALSE;
  }

  return TRUE;
}

/***************************************************************************/

void Transport::Leave(void)
{
  HQUIC listener;
  BOOL wasHosting;
  UDWORD i;

  if (g_msQuic == nullptr || !g_inSession)
    return;

  /* The shutdowns are issued with the lock held, which is the one place this
   * file deliberately calls MsQuic under it. ConnectionShutdown does not
   * block, and holding the lock is what stops a peer's own teardown from
   * closing a handle in the instant between reading it and using it. If the
   * shutdown does run its callback inline, the critical section is recursive
   * and that path takes it again safely.
   */
  EnterCriticalSection(&g_lock);
  g_leaving = TRUE;
  wasHosting = g_hosting;
  listener = g_listener;
  g_listener = nullptr;

  if (g_liveConnections == 0)
    SetEvent(g_drained);
  else
  {
    ResetEvent(g_drained);
    for (i = 0; i < MaxNumberOfPlayers; i++)
      if (g_peers[i].state != PeerState::Free && g_peers[i].connection != nullptr)
        g_msQuic->ConnectionShutdown(g_peers[i].connection, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                      ErrorShutdown);
  }
  LeaveCriticalSection(&g_lock);

  /* Blocking, so outside the lock. No joiner can slip in behind it: the
   * listener callback refuses everything once g_leaving is set, and that was
   * set above under the same lock the callback takes.
   */
  if (listener != nullptr)
    g_msQuic->ListenerClose(listener);

  /* Every connection has to be closed before the configuration it was started
   * with, and the closing happens on MsQuic's threads.
   */
  if (WaitForSingleObject(g_drained, DrainTimeoutMs) != WAIT_OBJECT_0)
    Neuron::DebugTrace("Transport::Leave: connections did not finish in time\n");

  if (g_configuration != nullptr)
  {
    g_msQuic->ConfigurationClose(g_configuration);
    g_configuration = nullptr;
  }

  if (wasHosting)
    HostCertificate::Release();

  EnterCriticalSection(&g_lock);
  ResetSession();
  LeaveCriticalSection(&g_lock);
}

/***************************************************************************/

BOOL Transport::SetGameFlags(const DWORD _gameFlags[])
{
  int i;

  if (!g_inSession || !g_hosting || _gameFlags == nullptr)
    return FALSE;

  /* Stored, not sent. These describe the session to somebody who has not
   * joined it, so they matter to whatever answers Transport::FindSessions;
   * players already in the game learn about option changes from the game's
   * own messages.
   */
  EnterCriticalSection(&g_lock);
  for (i = 0; i < Transport::GameFlagCount; i++)
    g_gameFlags[i] = _gameFlags[i];
  LeaveCriticalSection(&g_lock);

  return TRUE;
}

BOOL Transport::GetGameFlags(DWORD _gameFlags[])
{
  int i;

  if (!g_inSession || _gameFlags == nullptr)
    return FALSE;

  EnterCriticalSection(&g_lock);
  for (i = 0; i < Transport::GameFlagCount; i++)
    _gameFlags[i] = g_gameFlags[i];
  LeaveCriticalSection(&g_lock);

  return TRUE;
}

/***************************************************************************/
/* Players */
/***************************************************************************/

NETPLAYERID Transport::LocalPlayer(void) { return g_localPlayer; }

BOOL Transport::IsHost(void) { return g_hosting; }

UDWORD Transport::PlayerCount(void)
{
  UDWORD count;

  EnterCriticalSection(&g_lock);
  count = RosterCount();
  LeaveCriticalSection(&g_lock);

  return count;
}

BOOL Transport::PlayerName(NETPLAYERID _player, char _name[], UDWORD _size)
{
  RosterEntry* entry;
  BOOL found = FALSE;

  if (_name == nullptr || _size == 0)
    return FALSE;

  EnterCriticalSection(&g_lock);
  entry = FindPlayer(_player);
  if (entry != nullptr)
  {
    strncpy(_name, entry->name, _size - 1);
    _name[_size - 1] = '\0';
    found = TRUE;
  }
  LeaveCriticalSection(&g_lock);

  return found;
}

UDWORD Transport::PlayerList(NETPLAYERID _players[], UDWORD _max)
{
  UDWORD count = 0;
  int i;

  if (_players == nullptr)
    return 0;

  EnterCriticalSection(&g_lock);

  /* In id order, which is join order, so the host comes first and the roster
   * does not shuffle under the game when somebody leaves.
   */
  for (i = 0; i < MaxNumberOfPlayers && count < _max; i++)
    if (g_roster[i].used)
      _players[count++] = g_roster[i].id;

  for (UDWORD a = 0; a + 1 < count; a++)
    for (UDWORD b = 0; b + 1 < count - a; b++)
      if (_players[b] > _players[b + 1])
      {
        NETPLAYERID swap = _players[b];
        _players[b] = _players[b + 1];
        _players[b + 1] = swap;
      }

  LeaveCriticalSection(&g_lock);

  return count;
}

/* The host is the first id assigned and ids are never reused within a session,
 * so this needs nothing kept for it.
 */
BOOL Transport::IsHostPlayer(NETPLAYERID player) { return player == HostPlayer ? TRUE : FALSE; }

BOOL Transport::SetLocalName(const char _name[])
{
  if (!g_inSession || _name == nullptr)
    return FALSE;

  EnterCriticalSection(&g_lock);

  strncpy(g_localName, _name, StringSize - 1);
  g_localName[StringSize - 1] = '\0';

  if (g_localPlayer != NoPlayer)
  {
    AddPlayer(g_localPlayer, g_localName);

    /* The host tells everyone; a client tells the host, which relays. */
    if (g_hosting)
      SendRename(nullptr, g_localPlayer, g_localName, TRUE, NoPlayer);
    else if (g_peers[0].state == PeerState::Ready)
      SendRename(&g_peers[0], g_localPlayer, g_localName, FALSE, NoPlayer);
  }

  LeaveCriticalSection(&g_lock);

  return TRUE;
}

BOOL Transport::CloseToJoiners(void)
{
  if (!g_inSession || !g_hosting)
    return FALSE;

  EnterCriticalSection(&g_lock);
  g_closedToJoiners = TRUE;
  LeaveCriticalSection(&g_lock);

  return TRUE;
}

/***************************************************************************/
/* Data */
/***************************************************************************/

BOOL Transport::Send(NETPLAYERID _to, const void* _data, UDWORD _size, BOOL _reliable)
{
  Peer* peer;
  BOOL sent = FALSE;

  if (!g_inSession || _data == nullptr || _size == 0 || _size > Transport::MaxMessageBytes)
    return FALSE;

  EnterCriticalSection(&g_lock);

  if (g_hosting)
  {
    /* The host is connected _to everybody, so a targeted send is one hop. */
    peer = FindPeerById(_to);
    if (peer != nullptr)
    {
      SendDataFrame(peer, g_localPlayer, _to, _data, _size, _reliable);
      sent = TRUE;
    }
  }
  else if (g_peers[0].state == PeerState::Ready)
  {
    /* A client is connected only to the host, which forwards. Sending to the
     * host itself takes the same path and simply stops there.
     */
    SendDataFrame(&g_peers[0], g_localPlayer, _to, _data, _size, _reliable);
    sent = TRUE;
  }

  LeaveCriticalSection(&g_lock);

  return sent;
}

/***************************************************************************/

BOOL Transport::Broadcast(const void* _data, UDWORD _size, BOOL _reliable)
{
  BOOL sent = FALSE;

  if (!g_inSession || _data == nullptr || _size == 0 || _size > Transport::MaxMessageBytes)
    return FALSE;

  EnterCriticalSection(&g_lock);

  if (g_hosting)
  {
    SendDataFrameToAll(g_localPlayer, NoPlayer, _data, _size, _reliable, NoPlayer);
    sent = TRUE;
  }
  else if (g_peers[0].state == PeerState::Ready)
  {
    /* One copy to the host, which fans it out. The sender never gets its own
     * broadcast back -- the game relies on that, and applies the effect of a
     * broadcast locally as it sends it.
     */
    SendDataFrame(&g_peers[0], g_localPlayer, NoPlayer, _data, _size, _reliable);
    sent = TRUE;
  }

  LeaveCriticalSection(&g_lock);

  return sent;
}

/***************************************************************************/

BOOL Transport::Receive(void* _data, UDWORD* _size, NETPLAYERID* _from)
{
  QueuedMessage* message;

  if (_data == nullptr || _size == nullptr || _from == nullptr)
    return FALSE;

  EnterCriticalSection(&g_lock);

  message = g_queueHead;
  if (message != nullptr)
  {
    g_queueHead = message->next;
    if (g_queueHead == nullptr)
      g_queueTail = nullptr;
    g_queuedCount--;
  }

  LeaveCriticalSection(&g_lock);

  if (message == nullptr)
    return FALSE;

  memcpy(_data, message->data, message->size);
  *_size = message->size;
  *_from = message->from;

  free(message);

  return TRUE;
}

/***************************************************************************/

BOOL Transport::NextEvent(Transport::Event* _event)
{
  BOOL found = FALSE;

  if (_event == nullptr)
    return FALSE;

  EnterCriticalSection(&g_lock);

  if (g_eventCount > 0)
  {
    *_event = g_events[g_eventHead];
    g_eventHead = (g_eventHead + 1) % MaxQueuedEvents;
    g_eventCount--;
    found = TRUE;
  }

  LeaveCriticalSection(&g_lock);

  return found;
}

/***************************************************************************/
/* Statistics */
/***************************************************************************/

UDWORD Transport::BytesSent(void) { return g_bytesSent; }
UDWORD Transport::BytesReceived(void) { return g_bytesReceived; }
UDWORD Transport::PacketsSent(void) { return g_packetsSent; }
UDWORD Transport::PacketsReceived(void) { return g_packetsReceived; }

/***************************************************************************/

} // namespace Neuron

/***************************************************************************/
