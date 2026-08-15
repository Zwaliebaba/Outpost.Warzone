#include "pch.h"
/***************************************************************************/
/*
 * NetQuic.cpp
 *
 * The QUIC side of the transport seam in NetTransport.h, over MsQuic.
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
 * nettrans_FindSessions and nettrans_Join, and nothing above the seam changes.
 *
 * Threads. MsQuic runs its own workers and calls back on them, so everything
 * shared lives behind g_csNet and the game thread only ever sees the queues.
 * Two rules keep that honest, and breaking either one deadlocks:
 *
 *   - never hold g_csNet across a blocking MsQuic call (ConnectionClose,
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
#include "NetCert.h"
#include "NetTransport.h"

/***************************************************************************/

/* The port a host listens on. QUIC is UDP, and DirectPlay's own port is of no
 * relevance, so this is simply a number nothing else is using.
 */
#define	NETQ_PORT				9987

/* Application-layer protocol negotiation. QUIC requires one, and it doubles as
 * a version check: a client built against a different protocol fails the
 * handshake rather than getting halfway into a game and desynchronising.
 */
#define	NETQ_ALPN				"owz1"

/* The payload ceiling is the seam's, not this file's: nettrans_Receive writes
 * into a buffer the caller sized, so accepting a larger message from the wire
 * than the caller can hold would be a peer-controlled overrun. A frame bigger
 * than this is a peer that is confused or hostile, and its connection is
 * dropped rather than the buffer grown.
 */
#define	NETQ_MAX_FRAME			(NETTRANS_MAX_MESSAGE + 64)
#define	NETQ_ASSEMBLY_SIZE		(NETQ_MAX_FRAME + 2)

/* Thirty seconds of silence ends a connection; a keepalive every five means
 * that a lobby nobody is typing in never reaches it.
 */
#define	NETQ_IDLE_TIMEOUT_MS	30000
#define	NETQ_KEEPALIVE_MS		5000

/* A connection that has arrived but not identified itself. Ten seconds is
 * generous for a handshake and short enough that a stuck one does not hold a
 * player slot for the length of a game.
 */
#define	NETQ_HELLO_TIMEOUT_MS	10000

/* How long nettrans_Leave waits for MsQuic to finish with the connections
 * before giving up and closing the registration anyway.
 */
#define	NETQ_DRAIN_TIMEOUT_MS	5000

/* Received messages waiting for the game to collect them. The game drains this
 * every frame; a backlog this size means it has stopped doing so.
 */
#define	NETQ_MAX_QUEUED			512
#define	NETQ_MAX_EVENTS			64

/* The application error code a connection is closed with. QUIC puts it on the
 * wire, so the other end can tell a deliberate close from a crash.
 */
#define	NETQ_ERROR_SHUTDOWN		0
#define	NETQ_ERROR_PROTOCOL		1
#define	NETQ_ERROR_FULL			2

/* The host is always the first player. Zero stays free because the game
 * already reads it as "everybody, or nobody" -- it was DPID_ALLPLAYERS, and
 * NETsendFile still tests for it.
 */
#define	NETQ_ID_NONE			0
#define	NETQ_ID_HOST			1

/***************************************************************************/
/* The wire.
 *
 * On a stream, every frame is preceded by its length as a big-endian u16,
 * because a QUIC stream is a byte pipe and not a message queue. A datagram is
 * one frame with no prefix, since QUIC keeps datagram boundaries itself.
 *
 * Frames are numbered rather than named on the wire, and the payload of a DATA
 * frame is whatever the game handed to nettrans_Send -- this layer does not
 * look inside it.
 */
/***************************************************************************/

#define	QF_HELLO				1	/* client->host: u16 len, name             */
#define	QF_WELCOME				2	/* host->client: you, host, max, flags,
									   roster                                  */
#define	QF_JOINED				3	/* host->clients: u32 id, u16 len, name    */
#define	QF_LEFT					4	/* host->clients: u32 id                   */
#define	QF_DATA					5	/* either way: u32 from, u32 to, payload   */
#define	QF_REJECT				6	/* host->client: u16 reason                */
#define	QF_RENAME				7	/* both ways: u32 id, u16 len, name        */

#define	QREJECT_FULL			1
#define	QREJECT_CLOSED			2

/***************************************************************************/

static void netq_PutU16(BYTE* pOut, UWORD uwValue)
{
  pOut[0] = static_cast<BYTE>(uwValue >> 8);
  pOut[1] = static_cast<BYTE>(uwValue);
}

static void netq_PutU32(BYTE* pOut, UDWORD udwValue)
{
  pOut[0] = static_cast<BYTE>(udwValue >> 24);
  pOut[1] = static_cast<BYTE>(udwValue >> 16);
  pOut[2] = static_cast<BYTE>(udwValue >> 8);
  pOut[3] = static_cast<BYTE>(udwValue);
}

static UWORD netq_GetU16(const BYTE* pIn)
{
  return static_cast<UWORD>((static_cast<UWORD>(pIn[0]) << 8) | pIn[1]);
}

static UDWORD netq_GetU32(const BYTE* pIn)
{
  return (static_cast<UDWORD>(pIn[0]) << 24) | (static_cast<UDWORD>(pIn[1]) << 16) |
         (static_cast<UDWORD>(pIn[2]) << 8) | static_cast<UDWORD>(pIn[3]);
}

/***************************************************************************/

/* A send in flight. MsQuic does not copy what it is given: the bytes have to
 * stay put until it says it is finished with them, which is what the send
 * completion callbacks are for. The buffer descriptor and the bytes are one
 * allocation so that freeing it is one call.
 */
using QSEND = struct QSEND
{
  QUIC_BUFFER sBuffer;
  BYTE abData[1];
};

static QSEND* netq_AllocSend(UDWORD udwSize)
{
  QSEND* psSend;

  psSend = static_cast<QSEND*>(malloc(offsetof(QSEND, abData) + udwSize));
  if (psSend == nullptr)
    return nullptr;

  psSend->sBuffer.Buffer = psSend->abData;
  psSend->sBuffer.Length = udwSize;

  return psSend;
}

/***************************************************************************/

/* A message the game has not collected yet. Sized to the message rather than
 * to the maximum, because most of them are tiny and there can be hundreds
 * queued in a frame.
 */
using QMESSAGE = struct QMESSAGE
{
  struct QMESSAGE* psNext;
  NETPLAYERID from;
  UDWORD udwSize;
  BYTE abData[1];
};

/***************************************************************************/

using QPEER_STATE = enum
{
  QPEER_FREE,
  QPEER_CONNECTING,	/* has a connection, has not said who it is  */
  QPEER_READY		/* in the roster, traffic flows              */
};

/* One connection. On the host there is one of these per client; on a client
 * there is exactly one, for the host.
 */
using QPEER = struct QPEER
{
  QPEER_STATE state;
  HQUIC hConn;
  HQUIC hStream;
  NETPLAYERID id;
  ULONGLONG ullArrived;	/* for the hello timeout                     */
  UWORD uwMaxDatagram;	/* 0 until the peer says it accepts them     */

  UDWORD udwFill;
  BYTE abAssembly[NETQ_ASSEMBLY_SIZE];
};

/* A player, which is not the same thing as a connection: the host is in the
 * roster and has no connection to itself, and a client knows about players it
 * has no connection to at all.
 */
using QPLAYER = struct QPLAYER
{
  BOOL bUsed;
  NETPLAYERID id;
  char szName[StringSize];
};

/***************************************************************************/

static const QUIC_API_TABLE* g_pMsQuic = nullptr;
static HQUIC g_hRegistration = nullptr;

/* Named so it is recognisable in MsQuic's own tracing, which is otherwise a
 * sea of anonymous registrations.
 */
static const QUIC_REGISTRATION_CONFIG g_sRegConfig = {"Outpost", QUIC_EXECUTION_PROFILE_LOW_LATENCY};

static HQUIC g_hConfiguration = nullptr;
static HQUIC g_hListener = nullptr;

static CRITICAL_SECTION g_csNet;
static BOOL g_bCsReady = FALSE;

static BOOL g_bInSession = FALSE;
static BOOL g_bHost = FALSE;
static BOOL g_bLeaving = FALSE;
static BOOL g_bClosedToJoiners = FALSE;

static NETPLAYERID g_localPlayer = NETQ_ID_NONE;
static NETPLAYERID g_udwNextId = NETQ_ID_HOST;
static UDWORD g_udwMaxPlayers = 0;
static DWORD g_adwFlags[NETTRANS_GAME_FLAGS] = {0, 0, 0, 0};
static char g_szSessionName[StringSize] = "";
static char g_szLocalName[StringSize] = "";

static QPEER g_aPeers[MaxNumberOfPlayers];
static QPLAYER g_aPlayers[MaxNumberOfPlayers];

static QMESSAGE* g_psQueueHead = nullptr;
static QMESSAGE* g_psQueueTail = nullptr;
static UDWORD g_udwQueued = 0;

static NETTRANS_EVENT g_aEvents[NETQ_MAX_EVENTS];
static UDWORD g_udwEventHead = 0;
static UDWORD g_udwEventCount = 0;

/* Connections whose handles this file still owns -- adopted but not yet passed
 * to ConnectionClose. Counted rather than derived from the peer table because
 * a peer is released at the top of its shutdown and the handle is closed at the
 * bottom, and it is the bottom that nettrans_Leave has to wait for: MsQuic
 * requires every connection closed before the configuration they were started
 * with.
 */
static UDWORD g_udwLiveConns = 0;

/* Set when that count reaches zero during a departure, which is when it is safe
 * to close the rest of the session.
 */
static HANDLE g_hDrained = nullptr;

static UDWORD g_udwBytesSent = 0;
static UDWORD g_udwBytesReceived = 0;
static UDWORD g_udwPacketsSent = 0;
static UDWORD g_udwPacketsReceived = 0;

/* Traced once rather than per send, because a game that oversizes one
 * unreliable message oversizes thousands of them.
 */
static BOOL g_bWarnedDatagramSize = FALSE;

/***************************************************************************/

static QUIC_STATUS QUIC_API netq_ConnectionCallback(HQUIC hConn, void* pContext,
                                                    QUIC_CONNECTION_EVENT* psEvent);
static QUIC_STATUS QUIC_API netq_StreamCallback(HQUIC hStream, void* pContext,
                                                QUIC_STREAM_EVENT* psEvent);
static void netq_SendFrameTo(QPEER* psPeer, const BYTE* pFrame, UDWORD udwSize, BOOL bReliable);

/***************************************************************************/
/* The roster. Called with g_csNet held. */
/***************************************************************************/

static QPLAYER* netq_FindPlayer(NETPLAYERID id)
{
  int i;

  for (i = 0; i < MaxNumberOfPlayers; i++)
    if (g_aPlayers[i].bUsed && g_aPlayers[i].id == id)
      return &g_aPlayers[i];

  return nullptr;
}

static QPLAYER* netq_AddPlayer(NETPLAYERID id, const char szName[])
{
  QPLAYER* psPlayer;
  int i;

  psPlayer = netq_FindPlayer(id);
  if (psPlayer == nullptr)
  {
    for (i = 0; i < MaxNumberOfPlayers; i++)
      if (!g_aPlayers[i].bUsed)
      {
        psPlayer = &g_aPlayers[i];
        break;
      }
  }

  if (psPlayer == nullptr)
    return nullptr;

  psPlayer->bUsed = TRUE;
  psPlayer->id = id;
  strncpy(psPlayer->szName, szName, StringSize - 1);
  psPlayer->szName[StringSize - 1] = '\0';

  return psPlayer;
}

static void netq_RemovePlayer(NETPLAYERID id)
{
  QPLAYER* psPlayer;

  psPlayer = netq_FindPlayer(id);
  if (psPlayer != nullptr)
    psPlayer->bUsed = FALSE;
}

static UDWORD netq_PlayerCount(void)
{
  UDWORD udwCount = 0;
  int i;

  for (i = 0; i < MaxNumberOfPlayers; i++)
    if (g_aPlayers[i].bUsed)
      udwCount++;

  return udwCount;
}

/***************************************************************************/
/* The queues. Called with g_csNet held. */
/***************************************************************************/

static void netq_QueueMessage(NETPLAYERID from, const BYTE* pData, UDWORD udwSize)
{
  QMESSAGE* psMessage;

  if (g_udwQueued >= NETQ_MAX_QUEUED)
  {
    Neuron::DebugTrace("netquic: receive queue full, dropping {} bytes from {}\n", udwSize,
                       static_cast<UDWORD>(from));
    return;
  }

  psMessage = static_cast<QMESSAGE*>(malloc(offsetof(QMESSAGE, abData) + udwSize));
  if (psMessage == nullptr)
    return;

  psMessage->psNext = nullptr;
  psMessage->from = from;
  psMessage->udwSize = udwSize;
  memcpy(psMessage->abData, pData, udwSize);

  if (g_psQueueTail != nullptr)
    g_psQueueTail->psNext = psMessage;
  else
    g_psQueueHead = psMessage;
  g_psQueueTail = psMessage;
  g_udwQueued++;

  g_udwBytesReceived += udwSize;
  g_udwPacketsReceived++;
}

static void netq_FlushQueue(void)
{
  QMESSAGE* psMessage;

  while (g_psQueueHead != nullptr)
  {
    psMessage = g_psQueueHead;
    g_psQueueHead = psMessage->psNext;
    free(psMessage);
  }

  g_psQueueTail = nullptr;
  g_udwQueued = 0;
}

static void netq_QueueEvent(NETTRANS_EVENT_TYPE type, NETPLAYERID player, const char szName[])
{
  NETTRANS_EVENT* psEvent;

  /* Suppressed while leaving: the game asked for the session to end and does
   * not want to be told about every player it is walking away from.
   */
  if (g_bLeaving)
    return;

  if (g_udwEventCount >= NETQ_MAX_EVENTS)
  {
    Neuron::DebugTrace("netquic: event queue full, dropping event {}\n", static_cast<UDWORD>(type));
    return;
  }

  psEvent = &g_aEvents[(g_udwEventHead + g_udwEventCount) % NETQ_MAX_EVENTS];
  psEvent->type = type;
  psEvent->player = player;
  if (szName != nullptr)
  {
    strncpy(psEvent->szName, szName, StringSize - 1);
    psEvent->szName[StringSize - 1] = '\0';
  }
  else
    psEvent->szName[0] = '\0';

  g_udwEventCount++;
}

/***************************************************************************/
/* Peers. Called with g_csNet held unless said otherwise. */
/***************************************************************************/

static QPEER* netq_FindFreePeer(void)
{
  int i;

  for (i = 0; i < MaxNumberOfPlayers; i++)
    if (g_aPeers[i].state == QPEER_FREE)
      return &g_aPeers[i];

  return nullptr;
}

static QPEER* netq_FindPeerById(NETPLAYERID id)
{
  int i;

  for (i = 0; i < MaxNumberOfPlayers; i++)
    if (g_aPeers[i].state == QPEER_READY && g_aPeers[i].id == id)
      return &g_aPeers[i];

  return nullptr;
}

/* Called after ConnectionClose, and taking the lock itself, because the close
 * it follows must not happen under one.
 */
static void netq_ConnectionClosed(void)
{
  EnterCriticalSection(&g_csNet);

  if (g_udwLiveConns > 0)
    g_udwLiveConns--;

  if (g_udwLiveConns == 0 && g_bLeaving && g_hDrained != nullptr)
    SetEvent(g_hDrained);

  LeaveCriticalSection(&g_csNet);
}

/***************************************************************************/
/* Frames out. */
/***************************************************************************/

/* One frame to one peer. The stream carries a length prefix and a datagram
 * does not, which is the only difference between the two paths.
 */
static void netq_SendFrameTo(QPEER* psPeer, const BYTE* pFrame, UDWORD udwSize, BOOL bReliable)
{
  QSEND* psSend;
  QUIC_STATUS status;

  if (psPeer == nullptr || psPeer->state == QPEER_FREE)
    return;

  /* An unreliable send that will not fit in a datagram goes reliably instead.
   * Dropping it would be worse: the game asked for the message to be sent, and
   * only said it did not need it guaranteed.
   */
  if (!bReliable && (psPeer->uwMaxDatagram == 0 || udwSize > psPeer->uwMaxDatagram))
  {
    if (!g_bWarnedDatagramSize)
    {
      Neuron::DebugTrace("netquic: {} bytes will not fit a datagram (limit {}), sending reliably\n",
                         udwSize, static_cast<UDWORD>(psPeer->uwMaxDatagram));
      g_bWarnedDatagramSize = TRUE;
    }
    bReliable = TRUE;
  }

  if (bReliable && psPeer->hStream == nullptr)
    return;

  psSend = netq_AllocSend(bReliable ? udwSize + 2 : udwSize);
  if (psSend == nullptr)
    return;

  if (bReliable)
  {
    netq_PutU16(psSend->abData, static_cast<UWORD>(udwSize));
    memcpy(psSend->abData + 2, pFrame, udwSize);
    status = g_pMsQuic->StreamSend(psPeer->hStream, &psSend->sBuffer, 1, QUIC_SEND_FLAG_NONE, psSend);
  }
  else
  {
    memcpy(psSend->abData, pFrame, udwSize);
    status = g_pMsQuic->DatagramSend(psPeer->hConn, &psSend->sBuffer, 1, QUIC_SEND_FLAG_NONE, psSend);
  }

  if (QUIC_FAILED(status))
  {
    free(psSend);
    return;
  }

  g_udwBytesSent += udwSize;
  g_udwPacketsSent++;
}

/* Every ready peer except one. Passing NETQ_ID_NONE as the exception sends to
 * all of them, which is what the host's own broadcast wants.
 */
static void netq_SendFrameToAll(const BYTE* pFrame, UDWORD udwSize, BOOL bReliable,
                                NETPLAYERID except)
{
  int i;

  for (i = 0; i < MaxNumberOfPlayers; i++)
    if (g_aPeers[i].state == QPEER_READY && g_aPeers[i].id != except)
      netq_SendFrameTo(&g_aPeers[i], pFrame, udwSize, bReliable);
}

/* A DATA frame, built in place. The header is nine bytes and the payload
 * follows it, so this is the one frame worth a helper of its own.
 */
static void netq_SendData(QPEER* psPeer, NETPLAYERID from, NETPLAYERID to, const void* pData,
                          UDWORD udwSize, BOOL bReliable)
{
  BYTE abFrame[NETQ_MAX_FRAME];

  if (udwSize > NETTRANS_MAX_MESSAGE)
    return;

  abFrame[0] = QF_DATA;
  netq_PutU32(abFrame + 1, from);
  netq_PutU32(abFrame + 5, to);
  memcpy(abFrame + 9, pData, udwSize);

  netq_SendFrameTo(psPeer, abFrame, udwSize + 9, bReliable);
}

static void netq_SendDataToAll(NETPLAYERID from, NETPLAYERID to, const void* pData, UDWORD udwSize,
                               BOOL bReliable, NETPLAYERID except)
{
  BYTE abFrame[NETQ_MAX_FRAME];

  if (udwSize > NETTRANS_MAX_MESSAGE)
    return;

  abFrame[0] = QF_DATA;
  netq_PutU32(abFrame + 1, from);
  netq_PutU32(abFrame + 5, to);
  memcpy(abFrame + 9, pData, udwSize);

  netq_SendFrameToAll(abFrame, udwSize + 9, bReliable, except);
}

/* The roster, as a joiner is given it. Everything a client needs to know about
 * the session it has just been admitted to, in one frame, so that there is no
 * window in which it is connected but knows nothing.
 */
static void netq_SendWelcome(QPEER* psPeer)
{
  BYTE abFrame[NETQ_MAX_FRAME];
  UDWORD udwAt;
  UDWORD udwNameLen;
  UWORD uwCount = 0;
  UDWORD udwCountAt;
  int i;

  abFrame[0] = QF_WELCOME;
  netq_PutU32(abFrame + 1, psPeer->id);
  netq_PutU32(abFrame + 5, NETQ_ID_HOST);
  netq_PutU16(abFrame + 9, static_cast<UWORD>(g_udwMaxPlayers));
  for (i = 0; i < NETTRANS_GAME_FLAGS; i++)
    netq_PutU32(abFrame + 11 + i * 4, g_adwFlags[i]);

  udwCountAt = 11 + NETTRANS_GAME_FLAGS * 4;
  udwAt = udwCountAt + 2;

  for (i = 0; i < MaxNumberOfPlayers; i++)
  {
    if (!g_aPlayers[i].bUsed)
      continue;

    udwNameLen = strlen(g_aPlayers[i].szName);
    netq_PutU32(abFrame + udwAt, g_aPlayers[i].id);
    netq_PutU16(abFrame + udwAt + 4, static_cast<UWORD>(udwNameLen));
    memcpy(abFrame + udwAt + 6, g_aPlayers[i].szName, udwNameLen);
    udwAt += 6 + udwNameLen;
    uwCount++;
  }

  netq_PutU16(abFrame + udwCountAt, uwCount);

  netq_SendFrameTo(psPeer, abFrame, udwAt, TRUE);
}

static void netq_SendJoined(NETPLAYERID id, const char szName[], NETPLAYERID except)
{
  BYTE abFrame[7 + StringSize];
  UDWORD udwNameLen;

  udwNameLen = strlen(szName);
  abFrame[0] = QF_JOINED;
  netq_PutU32(abFrame + 1, id);
  netq_PutU16(abFrame + 5, static_cast<UWORD>(udwNameLen));
  memcpy(abFrame + 7, szName, udwNameLen);

  netq_SendFrameToAll(abFrame, 7 + udwNameLen, TRUE, except);
}

/* A rename, which travels in both directions: a client tells the host, and the
 * host tells everyone else. It is not a QF_JOINED with a new name because that
 * would raise a join event for somebody already playing.
 */
static void netq_SendRename(QPEER* psPeer, NETPLAYERID id, const char szName[], BOOL bToAll,
                            NETPLAYERID except)
{
  BYTE abFrame[7 + StringSize];
  UDWORD udwNameLen;

  udwNameLen = strlen(szName);
  if (udwNameLen >= StringSize)
    udwNameLen = StringSize - 1;

  abFrame[0] = QF_RENAME;
  netq_PutU32(abFrame + 1, id);
  netq_PutU16(abFrame + 5, static_cast<UWORD>(udwNameLen));
  memcpy(abFrame + 7, szName, udwNameLen);

  if (bToAll)
    netq_SendFrameToAll(abFrame, 7 + udwNameLen, TRUE, except);
  else
    netq_SendFrameTo(psPeer, abFrame, 7 + udwNameLen, TRUE);
}

static void netq_SendLeft(NETPLAYERID id)
{
  BYTE abFrame[5];

  abFrame[0] = QF_LEFT;
  netq_PutU32(abFrame + 1, id);

  netq_SendFrameToAll(abFrame, 5, TRUE, id);
}

static void netq_SendReject(QPEER* psPeer, UWORD uwReason)
{
  BYTE abFrame[3];

  abFrame[0] = QF_REJECT;
  netq_PutU16(abFrame + 1, uwReason);

  netq_SendFrameTo(psPeer, abFrame, 3, TRUE);
}

/***************************************************************************/
/* Frames in. Called on a MsQuic worker with g_csNet held. Returning FALSE
 * means the peer has said something it should not have, and its connection is
 * dropped by the caller.
 */
/***************************************************************************/

static BOOL netq_OnHello(QPEER* psPeer, const BYTE* pFrame, UDWORD udwSize)
{
  char szName[StringSize];
  UDWORD udwNameLen;

  if (!g_bHost || psPeer->state != QPEER_CONNECTING || udwSize < 3)
    return FALSE;

  udwNameLen = netq_GetU16(pFrame + 1);
  if (udwNameLen + 3 > udwSize || udwNameLen >= StringSize)
    return FALSE;

  memcpy(szName, pFrame + 3, udwNameLen);
  szName[udwNameLen] = '\0';

  /* A refusal is sent and then the send side is closed gracefully, rather than
   * the connection being shut down here: ConnectionShutdown discards whatever
   * is still queued on the streams, which would include the refusal itself.
   * The peer stays QPEER_CONNECTING and nettrans_Update reaps it.
   */
  if (g_bClosedToJoiners || netq_PlayerCount() >= g_udwMaxPlayers)
  {
    netq_SendReject(psPeer, g_bClosedToJoiners ? QREJECT_CLOSED : QREJECT_FULL);
    g_pMsQuic->StreamShutdown(psPeer->hStream, QUIC_STREAM_SHUTDOWN_FLAG_GRACEFUL,
                              NETQ_ERROR_FULL);
    return TRUE;
  }

  psPeer->id = ++g_udwNextId;
  psPeer->state = QPEER_READY;

  /* The welcome carries the roster as it is now, so it goes out before the
   * new player is added to it -- otherwise the joiner is told about itself.
   */
  netq_SendWelcome(psPeer);

  netq_AddPlayer(psPeer->id, szName);
  netq_SendJoined(psPeer->id, szName, psPeer->id);
  netq_QueueEvent(NETTRANS_PLAYER_JOINED, psPeer->id, szName);

  return TRUE;
}

static BOOL netq_OnWelcome(QPEER* psPeer, const BYTE* pFrame, UDWORD udwSize)
{
  UDWORD udwAt;
  UDWORD udwCount;
  UDWORD udwNameLen;
  NETPLAYERID id;
  char szName[StringSize];
  UDWORD i;

  if (g_bHost || psPeer->state != QPEER_CONNECTING)
    return FALSE;

  udwAt = 11 + NETTRANS_GAME_FLAGS * 4;
  if (udwSize < udwAt + 2)
    return FALSE;

  g_localPlayer = netq_GetU32(pFrame + 1);
  g_udwMaxPlayers = netq_GetU16(pFrame + 9);
  for (i = 0; i < NETTRANS_GAME_FLAGS; i++)
    g_adwFlags[i] = netq_GetU32(pFrame + 11 + i * 4);

  udwCount = netq_GetU16(pFrame + udwAt);
  udwAt += 2;

  psPeer->id = netq_GetU32(pFrame + 5);
  psPeer->state = QPEER_READY;

  for (i = 0; i < udwCount; i++)
  {
    if (udwAt + 6 > udwSize)
      return FALSE;

    id = netq_GetU32(pFrame + udwAt);
    udwNameLen = netq_GetU16(pFrame + udwAt + 4);
    if (udwAt + 6 + udwNameLen > udwSize || udwNameLen >= StringSize)
      return FALSE;

    memcpy(szName, pFrame + udwAt + 6, udwNameLen);
    szName[udwNameLen] = '\0';
    udwAt += 6 + udwNameLen;

    netq_AddPlayer(id, szName);
    netq_QueueEvent(NETTRANS_PLAYER_JOINED, id, szName);
  }

  /* The joiner is in the roster too, but was not in the frame: the host built
   * that before adding it. No event for it -- the game knows it just joined.
   */
  netq_AddPlayer(g_localPlayer, g_szLocalName);

  return TRUE;
}

static BOOL netq_OnJoined(const BYTE* pFrame, UDWORD udwSize)
{
  NETPLAYERID id;
  UDWORD udwNameLen;
  char szName[StringSize];

  if (g_bHost || udwSize < 7)
    return FALSE;

  id = netq_GetU32(pFrame + 1);
  udwNameLen = netq_GetU16(pFrame + 5);
  if (udwNameLen + 7 > udwSize || udwNameLen >= StringSize)
    return FALSE;

  memcpy(szName, pFrame + 7, udwNameLen);
  szName[udwNameLen] = '\0';

  netq_AddPlayer(id, szName);
  netq_QueueEvent(NETTRANS_PLAYER_JOINED, id, szName);

  return TRUE;
}

static BOOL netq_OnLeft(const BYTE* pFrame, UDWORD udwSize)
{
  NETPLAYERID id;
  QPLAYER* psPlayer;
  char szName[StringSize];

  if (g_bHost || udwSize < 5)
    return FALSE;

  id = netq_GetU32(pFrame + 1);
  psPlayer = netq_FindPlayer(id);
  if (psPlayer == nullptr)
    return TRUE;

  strncpy(szName, psPlayer->szName, StringSize - 1);
  szName[StringSize - 1] = '\0';

  netq_RemovePlayer(id);
  netq_QueueEvent(NETTRANS_PLAYER_LEFT, id, szName);

  return TRUE;
}

static BOOL netq_OnRename(QPEER* psPeer, const BYTE* pFrame, UDWORD udwSize)
{
  NETPLAYERID id;
  UDWORD udwNameLen;
  char szName[StringSize];

  if (psPeer->state != QPEER_READY || udwSize < 7)
    return FALSE;

  id = netq_GetU32(pFrame + 1);
  udwNameLen = netq_GetU16(pFrame + 5);
  if (udwNameLen + 7 > udwSize || udwNameLen >= StringSize)
    return FALSE;

  memcpy(szName, pFrame + 7, udwNameLen);
  szName[udwNameLen] = '\0';

  /* A client may rename itself and nobody else. The host relays what it
   * accepts, which is the same authority it has over QF_DATA.
   */
  if (g_bHost)
  {
    if (id != psPeer->id)
      return FALSE;

    if (netq_FindPlayer(id) == nullptr)
      return FALSE;

    netq_AddPlayer(id, szName);
    netq_SendRename(nullptr, id, szName, TRUE, id);
    return TRUE;
  }

  if (netq_FindPlayer(id) == nullptr)
    return TRUE;	/* somebody we have not been told about yet */

  netq_AddPlayer(id, szName);
  return TRUE;
}

static BOOL netq_OnData(QPEER* psPeer, const BYTE* pFrame, UDWORD udwSize, BOOL bReliable)
{
  NETPLAYERID from, to;
  const BYTE* pPayload;
  UDWORD udwPayload;
  QPEER* psTarget;

  if (psPeer->state != QPEER_READY || udwSize < 9)
    return FALSE;

  from = netq_GetU32(pFrame + 1);
  to = netq_GetU32(pFrame + 5);
  pPayload = pFrame + 9;
  udwPayload = udwSize - 9;

  /* The frame ceiling has slack in it for headers, so a maximum-sized frame
   * carries a slightly over-sized payload. nettrans_Receive writes into a
   * buffer the caller sized to NETTRANS_MAX_MESSAGE, so that slack has to be
   * refused here or a peer can choose how far past the end to write.
   */
  if (udwPayload > NETTRANS_MAX_MESSAGE)
    return FALSE;

  if (!g_bHost)
  {
    /* A client is only ever sent what is meant for it, so there is nothing to
     * decide: whatever arrives is delivered.
     */
    netq_QueueMessage(from, pPayload, udwPayload);
    return TRUE;
  }

  /* A client cannot claim to be somebody else. This is the whole of the
   * host's authority over the wire today, and it is worth having even so.
   */
  if (from != psPeer->id)
    return FALSE;

  if (to == NETQ_ID_NONE)
  {
    netq_QueueMessage(from, pPayload, udwPayload);
    netq_SendDataToAll(from, to, pPayload, udwPayload, bReliable, from);
    return TRUE;
  }

  if (to == g_localPlayer)
  {
    netq_QueueMessage(from, pPayload, udwPayload);
    return TRUE;
  }

  psTarget = netq_FindPeerById(to);
  if (psTarget != nullptr)
    netq_SendData(psTarget, from, to, pPayload, udwPayload, bReliable);

  return TRUE;
}

static BOOL netq_OnReject(const BYTE* pFrame, UDWORD udwSize)
{
  if (udwSize < 3)
    return FALSE;

  Neuron::DebugTrace("netquic: the host refused the join, reason {}\n",
                     static_cast<UDWORD>(netq_GetU16(pFrame + 1)));

  return FALSE;
}

static BOOL netq_OnFrame(QPEER* psPeer, const BYTE* pFrame, UDWORD udwSize, BOOL bReliable)
{
  BOOL bOk;

  if (udwSize < 1)
    return FALSE;

  switch (pFrame[0])
  {
  case QF_HELLO: bOk = netq_OnHello(psPeer, pFrame, udwSize); break;
  case QF_WELCOME: bOk = netq_OnWelcome(psPeer, pFrame, udwSize); break;
  case QF_JOINED: bOk = netq_OnJoined(pFrame, udwSize); break;
  case QF_LEFT: bOk = netq_OnLeft(pFrame, udwSize); break;
  case QF_DATA: bOk = netq_OnData(psPeer, pFrame, udwSize, bReliable); break;
  case QF_REJECT: bOk = netq_OnReject(pFrame, udwSize); break;
  case QF_RENAME: bOk = netq_OnRename(psPeer, pFrame, udwSize); break;
  default: bOk = FALSE; break;
  }

  /* A refusal ends the connection by design and has traced its own reason.
   * Anything else saying no is a frame this protocol cannot read.
   */
  if (!bOk && pFrame[0] != QF_REJECT)
    Neuron::DebugTrace("netquic: unreadable frame, kind {} size {}\n", static_cast<UDWORD>(pFrame[0]),
                       udwSize);

  return bOk;
}

/* A QUIC stream is a byte pipe, so what arrives has no relation to what was
 * sent: one frame can come in four pieces and four can come in one. Bytes are
 * accumulated until a length prefix and its frame are both complete.
 *
 * The buffer holds one frame at most, so a chunk larger than the space left is
 * taken in pieces rather than rejected.
 */
static BOOL netq_OnStreamBytes(QPEER* psPeer, const BYTE* pData, UDWORD udwSize)
{
  UDWORD udwTake;
  UDWORD udwFrame;

  while (udwSize > 0)
  {
    udwTake = NETQ_ASSEMBLY_SIZE - psPeer->udwFill;
    if (udwTake > udwSize)
      udwTake = udwSize;

    if (udwTake == 0)
      return FALSE;	/* a frame longer than the buffer: not one of ours */

    memcpy(psPeer->abAssembly + psPeer->udwFill, pData, udwTake);
    psPeer->udwFill += udwTake;
    pData += udwTake;
    udwSize -= udwTake;

    for (;;)
    {
      if (psPeer->udwFill < 2)
        break;

      udwFrame = netq_GetU16(psPeer->abAssembly);
      if (udwFrame == 0 || udwFrame > NETQ_MAX_FRAME)
        return FALSE;

      if (psPeer->udwFill < udwFrame + 2)
        break;

      if (!netq_OnFrame(psPeer, psPeer->abAssembly + 2, udwFrame, TRUE))
        return FALSE;

      psPeer->udwFill -= udwFrame + 2;
      memmove(psPeer->abAssembly, psPeer->abAssembly + udwFrame + 2, psPeer->udwFill);
    }
  }

  return TRUE;
}

/***************************************************************************/
/* Teardown.
 *
 * Releasing a peer and closing its connection are two steps on purpose:
 * ConnectionClose blocks until MsQuic has finished with the handle, so it can
 * never be called with g_csNet held. Every path here frees the slot under the
 * lock and closes the handle outside it.
 */
/***************************************************************************/

/* Called with g_csNet held. Hands back the connection for the caller to close
 * once it has let go of the lock. The stream is not handed back: MsQuic has
 * already raised its SHUTDOWN_COMPLETE, which closed it.
 */
static void netq_ReleasePeer(QPEER* psPeer, HQUIC* phConn)
{
  QPLAYER* psPlayer;
  char szName[StringSize];
  NETPLAYERID id;

  *phConn = psPeer->hConn;

  id = psPeer->id;
  psPeer->state = QPEER_FREE;
  psPeer->hConn = nullptr;
  psPeer->hStream = nullptr;
  psPeer->id = NETQ_ID_NONE;
  psPeer->udwFill = 0;
  psPeer->uwMaxDatagram = 0;

  if (!g_bHost)
  {
    /* The only peer a client has is the host, so losing it ends the session --
     * including when it is lost during the handshake, which is what a refused
     * or unreachable join looks like from here. There is no migration: the
     * plan says so, and the simulation could not survive one anyway.
     */
    netq_QueueEvent(NETTRANS_HOST_LOST, id, nullptr);
    return;
  }

  if (id != NETQ_ID_NONE)
  {
    psPlayer = netq_FindPlayer(id);
    if (psPlayer == nullptr)
      return;

    strncpy(szName, psPlayer->szName, StringSize - 1);
    szName[StringSize - 1] = '\0';

    netq_RemovePlayer(id);
    netq_SendLeft(id);
    netq_QueueEvent(NETTRANS_PLAYER_LEFT, id, szName);
  }
}

/***************************************************************************/
/* MsQuic callbacks. */
/***************************************************************************/

static QUIC_STATUS QUIC_API netq_StreamCallback(HQUIC hStream, void* pContext,
                                                QUIC_STREAM_EVENT* psEvent)
{
  QPEER* psPeer = static_cast<QPEER*>(pContext);
  HQUIC hConn;
  BOOL bOk = TRUE;
  UDWORD i;

  switch (psEvent->Type)
  {
  case QUIC_STREAM_EVENT_SEND_COMPLETE:
    /* No lock: MsQuic can raise this inline from StreamSend, and taking
     * g_csNet here would be taking it twice on the same thread.
     */
    free(psEvent->SEND_COMPLETE.ClientContext);
    break;

  case QUIC_STREAM_EVENT_RECEIVE:
    EnterCriticalSection(&g_csNet);
    for (i = 0; i < psEvent->RECEIVE.BufferCount && bOk; i++)
      bOk = netq_OnStreamBytes(psPeer, psEvent->RECEIVE.Buffers[i].Buffer,
                               psEvent->RECEIVE.Buffers[i].Length);
    hConn = psPeer->hConn;
    LeaveCriticalSection(&g_csNet);

    if (!bOk && hConn != nullptr)
    {
      /* Either the peer said something this protocol has no reading of, or it
       * refused our join -- both end the connection, and the reason has
       * already been traced by whichever handler decided it.
       *
       * Shutdown rather than close: this is the connection's own callback
       * thread, and the close belongs in SHUTDOWN_COMPLETE.
       */
      g_pMsQuic->ConnectionShutdown(hConn, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, NETQ_ERROR_PROTOCOL);
    }
    break;

  case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
  case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
    /* The peer is finished with the stream, which for this protocol means it
     * is finished with the session. The connection teardown does the rest.
     */
    EnterCriticalSection(&g_csNet);
    hConn = psPeer->hConn;
    LeaveCriticalSection(&g_csNet);

    if (hConn != nullptr)
      g_pMsQuic->ConnectionShutdown(hConn, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, NETQ_ERROR_SHUTDOWN);
    break;

  case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
    /* The handle is dead either way; only the peer's copy of it needs
     * clearing, and only if the peer still points at this stream.
     */
    EnterCriticalSection(&g_csNet);
    if (psPeer->hStream == hStream)
      psPeer->hStream = nullptr;
    LeaveCriticalSection(&g_csNet);
    g_pMsQuic->StreamClose(hStream);
    break;

  default: break;
  }

  return QUIC_STATUS_SUCCESS;
}

static QUIC_STATUS QUIC_API netq_ConnectionCallback(HQUIC hConn, void* pContext,
                                                    QUIC_CONNECTION_EVENT* psEvent)
{
  QPEER* psPeer = static_cast<QPEER*>(pContext);
  HQUIC hCloseConn = nullptr;
  HQUIC hStream;
  BYTE abHello[3 + StringSize];
  UDWORD udwNameLen;
  QUIC_STATUS status;

  switch (psEvent->Type)
  {
  case QUIC_CONNECTION_EVENT_CONNECTED:
    /* The client opens the one stream the protocol uses and introduces
     * itself on it. The host waits to be spoken to.
     */
    if (!g_bHost)
    {
      /* Opened outside the lock, because StreamClose blocks until the stream's
       * callbacks have drained. Nothing else can be looking at this peer's
       * stream yet -- it does not exist until the line below.
       */
      hStream = nullptr;
      status = g_pMsQuic->StreamOpen(hConn, QUIC_STREAM_OPEN_FLAG_NONE, netq_StreamCallback, psPeer,
                                     &hStream);
      if (QUIC_SUCCEEDED(status))
      {
        status = g_pMsQuic->StreamStart(hStream, QUIC_STREAM_START_FLAG_IMMEDIATE);
        if (QUIC_FAILED(status))
        {
          g_pMsQuic->StreamClose(hStream);
          hStream = nullptr;
        }
      }

      if (hStream == nullptr)
      {
        Neuron::DebugTrace("netquic: cannot open the session stream, 0x{:08x}\n",
                           static_cast<UDWORD>(status));
        g_pMsQuic->ConnectionShutdown(hConn, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, NETQ_ERROR_PROTOCOL);
        break;
      }

      EnterCriticalSection(&g_csNet);
      psPeer->hStream = hStream;
      udwNameLen = strlen(g_szLocalName);
      abHello[0] = QF_HELLO;
      netq_PutU16(abHello + 1, static_cast<UWORD>(udwNameLen));
      memcpy(abHello + 3, g_szLocalName, udwNameLen);
      netq_SendFrameTo(psPeer, abHello, 3 + udwNameLen, TRUE);
      LeaveCriticalSection(&g_csNet);
    }
    break;

  case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED:
    EnterCriticalSection(&g_csNet);
    if (psPeer->hStream != nullptr)
    {
      /* One stream per connection is the whole protocol, and the settings say
       * so, so the peer should not be able to get here. Refusing means
       * returning a failure without taking the handle: an accepted stream is
       * one whose callback handler has been set, and a second one taking this
       * peer's assembly buffer would corrupt the first.
       */
      LeaveCriticalSection(&g_csNet);
      Neuron::DebugTrace("netquic: refusing a second stream from a peer\n");
      return QUIC_STATUS_NOT_SUPPORTED;
    }

    psPeer->hStream = psEvent->PEER_STREAM_STARTED.Stream;
    g_pMsQuic->SetCallbackHandler(psPeer->hStream, reinterpret_cast<void*>(netq_StreamCallback),
                                  psPeer);
    LeaveCriticalSection(&g_csNet);
    break;

  case QUIC_CONNECTION_EVENT_DATAGRAM_STATE_CHANGED:
    EnterCriticalSection(&g_csNet);
    psPeer->uwMaxDatagram = psEvent->DATAGRAM_STATE_CHANGED.SendEnabled
                              ? psEvent->DATAGRAM_STATE_CHANGED.MaxSendLength
                              : 0;
    LeaveCriticalSection(&g_csNet);
    break;

  case QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED:
    EnterCriticalSection(&g_csNet);
    if (!netq_OnFrame(psPeer, psEvent->DATAGRAM_RECEIVED.Buffer->Buffer,
                      psEvent->DATAGRAM_RECEIVED.Buffer->Length, FALSE))
      Neuron::DebugTrace("netquic: ignoring an unreadable datagram\n");
    LeaveCriticalSection(&g_csNet);
    break;

  case QUIC_CONNECTION_EVENT_DATAGRAM_SEND_STATE_CHANGED:
    /* No lock, for the same reason as SEND_COMPLETE. The first terminal state
     * is the one that frees: MsQuic will not touch the buffer again, and
     * ACKNOWLEDGED may never arrive for a datagram nobody acknowledges.
     */
    switch (psEvent->DATAGRAM_SEND_STATE_CHANGED.State)
    {
    case QUIC_DATAGRAM_SEND_SENT:
    case QUIC_DATAGRAM_SEND_LOST_DISCARDED:
    case QUIC_DATAGRAM_SEND_ACKNOWLEDGED:
    case QUIC_DATAGRAM_SEND_ACKNOWLEDGED_SPURIOUS:
    case QUIC_DATAGRAM_SEND_CANCELED:
      if (psEvent->DATAGRAM_SEND_STATE_CHANGED.ClientContext != nullptr)
      {
        free(psEvent->DATAGRAM_SEND_STATE_CHANGED.ClientContext);
        psEvent->DATAGRAM_SEND_STATE_CHANGED.ClientContext = nullptr;
      }
      break;

    default: break;
    }
    break;

  case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
    EnterCriticalSection(&g_csNet);
    netq_ReleasePeer(psPeer, &hCloseConn);
    LeaveCriticalSection(&g_csNet);

    /* Outside the lock, because ConnectionClose waits for the connection's
     * callbacks to drain. The count comes down after the close rather than
     * before it, so that a departing session waits for the handle to be gone
     * and not merely for the peer to be forgotten.
     */
    if (hCloseConn != nullptr)
    {
      g_pMsQuic->ConnectionClose(hCloseConn);
      netq_ConnectionClosed();
    }
    break;

  default: break;
  }

  return QUIC_STATUS_SUCCESS;
}

static QUIC_STATUS QUIC_API netq_ListenerCallback(HQUIC hListener, void* pContext,
                                                  QUIC_LISTENER_EVENT* psEvent)
{
  QPEER* psPeer;
  QUIC_STATUS status;

  (void)hListener;
  (void)pContext;

  if (psEvent->Type != QUIC_LISTENER_EVENT_NEW_CONNECTION)
    return QUIC_STATUS_SUCCESS;

  EnterCriticalSection(&g_csNet);

  if (g_bLeaving || g_bClosedToJoiners || netq_PlayerCount() >= g_udwMaxPlayers)
  {
    LeaveCriticalSection(&g_csNet);
    return QUIC_STATUS_CONNECTION_REFUSED;
  }

  psPeer = netq_FindFreePeer();
  if (psPeer == nullptr)
  {
    LeaveCriticalSection(&g_csNet);
    return QUIC_STATUS_CONNECTION_REFUSED;
  }

  psPeer->state = QPEER_CONNECTING;
  psPeer->hConn = psEvent->NEW_CONNECTION.Connection;
  psPeer->hStream = nullptr;
  psPeer->id = NETQ_ID_NONE;
  psPeer->udwFill = 0;
  psPeer->uwMaxDatagram = 0;
  psPeer->ullArrived = GetTickCount64();

  g_pMsQuic->SetCallbackHandler(psPeer->hConn, reinterpret_cast<void*>(netq_ConnectionCallback),
                                psPeer);

  status = g_pMsQuic->ConnectionSetConfiguration(psPeer->hConn, g_hConfiguration);
  if (QUIC_FAILED(status))
  {
    /* Returning a failure from here hands the connection back to MsQuic to
     * dispose of, so the handle is not this file's to close and never counted.
     */
    psPeer->state = QPEER_FREE;
    psPeer->hConn = nullptr;
    LeaveCriticalSection(&g_csNet);
    Neuron::DebugTrace("netquic: ConnectionSetConfiguration failed, 0x{:08x}\n",
                       static_cast<UDWORD>(status));
    return status;
  }

  g_udwLiveConns++;
  LeaveCriticalSection(&g_csNet);

  return QUIC_STATUS_SUCCESS;
}

/***************************************************************************/
/* Configuration. */
/***************************************************************************/

static void netq_FillSettings(QUIC_SETTINGS* psSettings)
{
  memset(psSettings, 0, sizeof(*psSettings));

  psSettings->IdleTimeoutMs = NETQ_IDLE_TIMEOUT_MS;
  psSettings->IsSet.IdleTimeoutMs = TRUE;

  psSettings->KeepAliveIntervalMs = NETQ_KEEPALIVE_MS;
  psSettings->IsSet.KeepAliveIntervalMs = TRUE;

  /* One bidirectional stream, which is the one the client opens. Set on both
   * ends: a client that allowed none would make the protocol impossible to
   * extend without a version bump neither end would understand.
   */
  psSettings->PeerBidiStreamCount = 1;
  psSettings->IsSet.PeerBidiStreamCount = TRUE;

  psSettings->DatagramReceiveEnabled = TRUE;
  psSettings->IsSet.DatagramReceiveEnabled = TRUE;
}

static BOOL netq_OpenConfiguration(BOOL bHost)
{
  QUIC_SETTINGS sSettings;
  QUIC_CREDENTIAL_CONFIG sCredential;
  QUIC_CERTIFICATE_HASH sHash;
  QUIC_BUFFER sAlpn;
  QUIC_STATUS status;

  netq_FillSettings(&sSettings);

  sAlpn.Buffer = reinterpret_cast<uint8_t*>(const_cast<char*>(NETQ_ALPN));
  sAlpn.Length = static_cast<uint32_t>(strlen(NETQ_ALPN));

  status = g_pMsQuic->ConfigurationOpen(g_hRegistration, &sAlpn, 1, &sSettings, sizeof(sSettings),
                                        nullptr, &g_hConfiguration);
  if (QUIC_FAILED(status))
  {
    Neuron::DebugTrace("netquic: ConfigurationOpen failed, 0x{:08x}\n", static_cast<UDWORD>(status));
    g_hConfiguration = nullptr;
    return FALSE;
  }

  memset(&sCredential, 0, sizeof(sCredential));

  if (bHost)
  {
    if (!netcert_Acquire(sHash.ShaHash))
    {
      Neuron::DebugTrace("netquic: no host certificate, cannot host\n");
      g_pMsQuic->ConfigurationClose(g_hConfiguration);
      g_hConfiguration = nullptr;
      return FALSE;
    }

    sCredential.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_HASH;
    sCredential.Flags = QUIC_CREDENTIAL_FLAG_NONE;
    sCredential.CertificateHash = &sHash;
  }
  else
  {
    /* The certificate is self-signed and there is no name to check it
     * against, so validating it would fail every time. What this buys is
     * encryption without authentication: see NetCert.h.
     */
    sCredential.Type = QUIC_CREDENTIAL_TYPE_NONE;
    sCredential.Flags = QUIC_CREDENTIAL_FLAG_CLIENT | QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;
  }

  status = g_pMsQuic->ConfigurationLoadCredential(g_hConfiguration, &sCredential);
  if (QUIC_FAILED(status))
  {
    Neuron::DebugTrace("netquic: ConfigurationLoadCredential failed, 0x{:08x}\n",
                       static_cast<UDWORD>(status));
    g_pMsQuic->ConfigurationClose(g_hConfiguration);
    g_hConfiguration = nullptr;
    if (bHost)
      netcert_Release();
    return FALSE;
  }

  return TRUE;
}

/***************************************************************************/

/* Splits "host", "host:port" and "[v6]:port" into the two things
 * ConnectionStart wants. MsQuic resolves names and literals alike, so nothing
 * has to be looked up here.
 */
static void netq_SplitAddress(const char szAddress[], char szHost[], UDWORD udwHostSize,
                              UWORD* puwPort)
{
  const char* pColon;
  UDWORD udwLen;

  *puwPort = NETQ_PORT;

  /* A bracketed literal ends at the bracket, so a colon inside it is part of
   * the address rather than a port separator.
   */
  if (szAddress[0] == '[')
  {
    pColon = strchr(szAddress, ']');
    pColon = (pColon != nullptr) ? strchr(pColon, ':') : nullptr;
  }
  else
  {
    pColon = strrchr(szAddress, ':');

    /* An unbracketed address with more than one colon is a bare IPv6 literal,
     * and the last colon is part of it.
     */
    if (pColon != nullptr && strchr(szAddress, ':') != pColon)
      pColon = nullptr;
  }

  if (pColon != nullptr && pColon[1] != '\0')
  {
    *puwPort = static_cast<UWORD>(atoi(pColon + 1));
    if (*puwPort == 0)
      *puwPort = NETQ_PORT;
    udwLen = static_cast<UDWORD>(pColon - szAddress);
  }
  else
    udwLen = strlen(szAddress);

  /* MsQuic wants the literal without its brackets. */
  if (szAddress[0] == '[' && udwLen >= 2)
  {
    szAddress++;
    udwLen -= 2;
  }

  if (udwLen >= udwHostSize)
    udwLen = udwHostSize - 1;

  memcpy(szHost, szAddress, udwLen);
  szHost[udwLen] = '\0';
}

/***************************************************************************/

static void netq_ResetSession(void)
{
  int i;

  for (i = 0; i < MaxNumberOfPlayers; i++)
  {
    g_aPeers[i].state = QPEER_FREE;
    g_aPeers[i].hConn = nullptr;
    g_aPeers[i].hStream = nullptr;
    g_aPeers[i].id = NETQ_ID_NONE;
    g_aPeers[i].udwFill = 0;
    g_aPeers[i].uwMaxDatagram = 0;
    g_aPlayers[i].bUsed = FALSE;
  }

  netq_FlushQueue();
  g_udwEventHead = 0;
  g_udwEventCount = 0;

  g_bInSession = FALSE;
  g_bHost = FALSE;
  g_bLeaving = FALSE;
  g_bClosedToJoiners = FALSE;
  g_localPlayer = NETQ_ID_NONE;
  g_udwNextId = NETQ_ID_HOST;
  g_udwMaxPlayers = 0;
  g_szSessionName[0] = '\0';
  g_bWarnedDatagramSize = FALSE;

  for (i = 0; i < NETTRANS_GAME_FLAGS; i++)
    g_adwFlags[i] = 0;
}

/***************************************************************************/
/* Lifecycle */
/***************************************************************************/

BOOL nettrans_Startup(void)
{
  QUIC_STATUS status;

  if (g_pMsQuic != nullptr)
    return TRUE;

  if (!g_bCsReady)
  {
    InitializeCriticalSection(&g_csNet);
    g_bCsReady = TRUE;
  }

  if (g_hDrained == nullptr)
  {
    g_hDrained = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (g_hDrained == nullptr)
      return FALSE;
  }

  status = MsQuicOpen2(&g_pMsQuic);
  if (QUIC_FAILED(status))
  {
    Neuron::DebugTrace("nettrans_Startup: MsQuicOpen2 failed, 0x{:08x}\n", static_cast<UDWORD>(status));
    g_pMsQuic = nullptr;
    return FALSE;
  }

  /* LOW_LATENCY rather than the default: this carries lockstep game commands,
   * where a late packet is worse than a small one.
   */
  status = g_pMsQuic->RegistrationOpen(&g_sRegConfig, &g_hRegistration);
  if (QUIC_FAILED(status))
  {
    Neuron::DebugTrace("nettrans_Startup: RegistrationOpen failed, 0x{:08x}\n",
                       static_cast<UDWORD>(status));
    MsQuicClose(g_pMsQuic);
    g_pMsQuic = nullptr;
    g_hRegistration = nullptr;
    return FALSE;
  }

  netq_ResetSession();

  return TRUE;
}

/***************************************************************************/

void nettrans_Shutdown(void)
{
  if (g_pMsQuic == nullptr)
    return;

  nettrans_Leave();

  if (g_hRegistration != nullptr)
  {
    /* Closing the registration waits for every connection under it to finish,
     * so nothing can still be in a callback once this returns.
     */
    g_pMsQuic->RegistrationClose(g_hRegistration);
    g_hRegistration = nullptr;
  }

  MsQuicClose(g_pMsQuic);
  g_pMsQuic = nullptr;

  if (g_hDrained != nullptr)
  {
    CloseHandle(g_hDrained);
    g_hDrained = nullptr;
  }

  if (g_bCsReady)
  {
    DeleteCriticalSection(&g_csNet);
    g_bCsReady = FALSE;
  }
}

/***************************************************************************/

void nettrans_Update(void)
{
  ULONGLONG ullNow;
  UDWORD i;

  if (!g_bInSession || !g_bHost)
    return;

  ullNow = GetTickCount64();

  /* A connection that arrived and never introduced itself. Everything else
   * about a peer is driven by MsQuic's callbacks; this needs a clock, which is
   * why it is here rather than in one of them.
   *
   * The shutdown is issued under the lock, as in nettrans_Leave and for the
   * same reason: a handle read out of the peer table and used after the lock
   * is dropped can have been closed in between by the peer's own teardown.
   */
  EnterCriticalSection(&g_csNet);
  for (i = 0; i < MaxNumberOfPlayers; i++)
    if (g_aPeers[i].state == QPEER_CONNECTING && g_aPeers[i].hConn != nullptr &&
        ullNow - g_aPeers[i].ullArrived > NETQ_HELLO_TIMEOUT_MS)
    {
      Neuron::DebugTrace("netquic: dropping a connection that never said hello\n");
      g_pMsQuic->ConnectionShutdown(g_aPeers[i].hConn, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                    NETQ_ERROR_PROTOCOL);
    }
  LeaveCriticalSection(&g_csNet);
}

/***************************************************************************/
/* Sessions */
/***************************************************************************/

BOOL nettrans_Host(const char szSessionName[], const char szPlayerName[], UDWORD udwMaxPlayers,
                   const DWORD adwFlags[])
{
  QUIC_ADDR sAddress;
  QUIC_BUFFER sAlpn;
  QUIC_STATUS status;
  int i;

  if (g_pMsQuic == nullptr || g_bInSession)
    return FALSE;

  if (udwMaxPlayers == 0 || udwMaxPlayers > MaxNumberOfPlayers)
    udwMaxPlayers = MaxNumberOfPlayers;

  netq_ResetSession();

  g_bHost = TRUE;
  g_udwMaxPlayers = udwMaxPlayers;
  g_localPlayer = NETQ_ID_HOST;
  g_udwNextId = NETQ_ID_HOST;

  strncpy(g_szSessionName, szSessionName, StringSize - 1);
  g_szSessionName[StringSize - 1] = '\0';
  strncpy(g_szLocalName, szPlayerName, StringSize - 1);
  g_szLocalName[StringSize - 1] = '\0';

  if (adwFlags != nullptr)
    for (i = 0; i < NETTRANS_GAME_FLAGS; i++)
      g_adwFlags[i] = adwFlags[i];

  if (!netq_OpenConfiguration(TRUE))
  {
    g_bHost = FALSE;
    return FALSE;
  }

  status = g_pMsQuic->ListenerOpen(g_hRegistration, netq_ListenerCallback, nullptr, &g_hListener);
  if (QUIC_FAILED(status))
  {
    Neuron::DebugTrace("nettrans_Host: ListenerOpen failed, 0x{:08x}\n", static_cast<UDWORD>(status));
    g_pMsQuic->ConfigurationClose(g_hConfiguration);
    g_hConfiguration = nullptr;
    netcert_Release();
    g_bHost = FALSE;
    return FALSE;
  }

  /* UNSPEC binds both families, so a joiner may arrive over either. */
  memset(&sAddress, 0, sizeof(sAddress));
  QuicAddrSetFamily(&sAddress, QUIC_ADDRESS_FAMILY_UNSPEC);
  QuicAddrSetPort(&sAddress, NETQ_PORT);

  sAlpn.Buffer = reinterpret_cast<uint8_t*>(const_cast<char*>(NETQ_ALPN));
  sAlpn.Length = static_cast<uint32_t>(strlen(NETQ_ALPN));

  /* The host joins its own roster before the door opens. The other order has a
   * window in which a joiner's welcome would carry a roster with no host in
   * it, and the player-count check would be one short.
   */
  EnterCriticalSection(&g_csNet);
  netq_AddPlayer(g_localPlayer, g_szLocalName);
  g_bInSession = TRUE;
  LeaveCriticalSection(&g_csNet);

  status = g_pMsQuic->ListenerStart(g_hListener, &sAlpn, 1, &sAddress);
  if (QUIC_FAILED(status))
  {
    Neuron::DebugTrace("nettrans_Host: ListenerStart failed on port {}, 0x{:08x}\n",
                       static_cast<UDWORD>(NETQ_PORT), static_cast<UDWORD>(status));
    g_pMsQuic->ListenerClose(g_hListener);
    g_hListener = nullptr;
    g_pMsQuic->ConfigurationClose(g_hConfiguration);
    g_hConfiguration = nullptr;
    netcert_Release();
    netq_ResetSession();
    return FALSE;
  }

  return TRUE;
}

/***************************************************************************/

UDWORD nettrans_FindSessions(NETSESSION paSessions[], UDWORD udwMax)
{
  (void)paSessions;
  (void)udwMax;

  /* Nothing to ask. There is no discovery in this build and no server to
   * query yet, so the join screen takes a typed address. When the relay
   * server exists its game list is answered from here.
   */
  return 0;
}

/***************************************************************************/

BOOL nettrans_Join(const char szAddress[], const char szPlayerName[])
{
  char szHost[NETTRANS_ADDRESS_SIZE];
  UWORD uwPort;
  QPEER* psPeer;
  QUIC_STATUS status;

  if (g_pMsQuic == nullptr || g_bInSession)
    return FALSE;

  netq_ResetSession();

  g_bHost = FALSE;
  g_udwMaxPlayers = MaxNumberOfPlayers;
  strncpy(g_szLocalName, szPlayerName, StringSize - 1);
  g_szLocalName[StringSize - 1] = '\0';

  if (!netq_OpenConfiguration(FALSE))
    return FALSE;

  netq_SplitAddress(szAddress, szHost, sizeof(szHost), &uwPort);

  psPeer = &g_aPeers[0];
  psPeer->state = QPEER_CONNECTING;
  psPeer->hConn = nullptr;
  psPeer->hStream = nullptr;
  psPeer->id = NETQ_ID_NONE;
  psPeer->udwFill = 0;
  psPeer->uwMaxDatagram = 0;
  psPeer->ullArrived = GetTickCount64();

  /* In session before the connection is started, not after: the handshake runs
   * on a MsQuic thread and can finish before this function returns. The game
   * learns it is really in when its own id arrives with the welcome, which is
   * what nettrans_LocalPlayer reports.
   */
  g_bInSession = TRUE;

  status = g_pMsQuic->ConnectionOpen(g_hRegistration, netq_ConnectionCallback, psPeer,
                                     &psPeer->hConn);
  if (QUIC_FAILED(status))
  {
    Neuron::DebugTrace("nettrans_Join: ConnectionOpen failed, 0x{:08x}\n", static_cast<UDWORD>(status));
    g_pMsQuic->ConfigurationClose(g_hConfiguration);
    g_hConfiguration = nullptr;
    netq_ResetSession();
    return FALSE;
  }

  EnterCriticalSection(&g_csNet);
  g_udwLiveConns++;
  LeaveCriticalSection(&g_csNet);

  status = g_pMsQuic->ConnectionStart(psPeer->hConn, g_hConfiguration, QUIC_ADDRESS_FAMILY_UNSPEC,
                                      szHost, uwPort);
  if (QUIC_FAILED(status))
  {
    Neuron::DebugTrace("nettrans_Join: cannot reach {} port {}, 0x{:08x}\n", szHost,
                       static_cast<UDWORD>(uwPort), static_cast<UDWORD>(status));
    /* Closing may raise this connection's shutdown inline, which queues a
     * host-lost event; the reset below throws it away along with the rest of
     * the session that never happened.
     */
    g_pMsQuic->ConnectionClose(psPeer->hConn);
    psPeer->hConn = nullptr;
    netq_ConnectionClosed();
    g_pMsQuic->ConfigurationClose(g_hConfiguration);
    g_hConfiguration = nullptr;
    netq_ResetSession();
    return FALSE;
  }

  return TRUE;
}

/***************************************************************************/

void nettrans_Leave(void)
{
  HQUIC hListener;
  BOOL bWasHost;
  UDWORD i;

  if (g_pMsQuic == nullptr || !g_bInSession)
    return;

  /* The shutdowns are issued with the lock held, which is the one place this
   * file deliberately calls MsQuic under it. ConnectionShutdown does not
   * block, and holding the lock is what stops a peer's own teardown from
   * closing a handle in the instant between reading it and using it. If the
   * shutdown does run its callback inline, the critical section is recursive
   * and that path takes it again safely.
   */
  EnterCriticalSection(&g_csNet);
  g_bLeaving = TRUE;
  bWasHost = g_bHost;
  hListener = g_hListener;
  g_hListener = nullptr;

  if (g_udwLiveConns == 0)
    SetEvent(g_hDrained);
  else
  {
    ResetEvent(g_hDrained);
    for (i = 0; i < MaxNumberOfPlayers; i++)
      if (g_aPeers[i].state != QPEER_FREE && g_aPeers[i].hConn != nullptr)
        g_pMsQuic->ConnectionShutdown(g_aPeers[i].hConn, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE,
                                      NETQ_ERROR_SHUTDOWN);
  }
  LeaveCriticalSection(&g_csNet);

  /* Blocking, so outside the lock. No joiner can slip in behind it: the
   * listener callback refuses everything once g_bLeaving is set, and that was
   * set above under the same lock the callback takes.
   */
  if (hListener != nullptr)
    g_pMsQuic->ListenerClose(hListener);

  /* Every connection has to be closed before the configuration it was started
   * with, and the closing happens on MsQuic's threads.
   */
  if (WaitForSingleObject(g_hDrained, NETQ_DRAIN_TIMEOUT_MS) != WAIT_OBJECT_0)
    Neuron::DebugTrace("nettrans_Leave: connections did not finish in time\n");

  if (g_hConfiguration != nullptr)
  {
    g_pMsQuic->ConfigurationClose(g_hConfiguration);
    g_hConfiguration = nullptr;
  }

  if (bWasHost)
    netcert_Release();

  EnterCriticalSection(&g_csNet);
  netq_ResetSession();
  LeaveCriticalSection(&g_csNet);
}

/***************************************************************************/

BOOL nettrans_SetFlags(const DWORD adwFlags[])
{
  int i;

  if (!g_bInSession || !g_bHost || adwFlags == nullptr)
    return FALSE;

  /* Stored, not sent. These describe the session to somebody who has not
   * joined it, so they matter to whatever answers nettrans_FindSessions;
   * players already in the game learn about option changes from the game's
   * own messages.
   */
  EnterCriticalSection(&g_csNet);
  for (i = 0; i < NETTRANS_GAME_FLAGS; i++)
    g_adwFlags[i] = adwFlags[i];
  LeaveCriticalSection(&g_csNet);

  return TRUE;
}

BOOL nettrans_GetFlags(DWORD adwFlags[])
{
  int i;

  if (!g_bInSession || adwFlags == nullptr)
    return FALSE;

  EnterCriticalSection(&g_csNet);
  for (i = 0; i < NETTRANS_GAME_FLAGS; i++)
    adwFlags[i] = g_adwFlags[i];
  LeaveCriticalSection(&g_csNet);

  return TRUE;
}

/***************************************************************************/
/* Players */
/***************************************************************************/

NETPLAYERID nettrans_LocalPlayer(void) { return g_localPlayer; }

BOOL nettrans_IsHost(void) { return g_bHost; }

UDWORD nettrans_PlayerCount(void)
{
  UDWORD udwCount;

  EnterCriticalSection(&g_csNet);
  udwCount = netq_PlayerCount();
  LeaveCriticalSection(&g_csNet);

  return udwCount;
}

BOOL nettrans_PlayerName(NETPLAYERID player, char szName[], UDWORD udwSize)
{
  QPLAYER* psPlayer;
  BOOL bFound = FALSE;

  if (szName == nullptr || udwSize == 0)
    return FALSE;

  EnterCriticalSection(&g_csNet);
  psPlayer = netq_FindPlayer(player);
  if (psPlayer != nullptr)
  {
    strncpy(szName, psPlayer->szName, udwSize - 1);
    szName[udwSize - 1] = '\0';
    bFound = TRUE;
  }
  LeaveCriticalSection(&g_csNet);

  return bFound;
}

UDWORD nettrans_PlayerList(NETPLAYERID paPlayers[], UDWORD udwMax)
{
  UDWORD udwCount = 0;
  int i;

  if (paPlayers == nullptr)
    return 0;

  EnterCriticalSection(&g_csNet);

  /* In id order, which is join order, so the host comes first and the roster
   * does not shuffle under the game when somebody leaves.
   */
  for (i = 0; i < MaxNumberOfPlayers && udwCount < udwMax; i++)
    if (g_aPlayers[i].bUsed)
      paPlayers[udwCount++] = g_aPlayers[i].id;

  for (UDWORD a = 0; a + 1 < udwCount; a++)
    for (UDWORD b = 0; b + 1 < udwCount - a; b++)
      if (paPlayers[b] > paPlayers[b + 1])
      {
        NETPLAYERID swap = paPlayers[b];
        paPlayers[b] = paPlayers[b + 1];
        paPlayers[b + 1] = swap;
      }

  LeaveCriticalSection(&g_csNet);

  return udwCount;
}

/* The host is the first id assigned and ids are never reused within a session,
 * so this needs nothing kept for it.
 */
BOOL nettrans_IsHostPlayer(NETPLAYERID player) { return player == NETQ_ID_HOST ? TRUE : FALSE; }

BOOL nettrans_SetLocalName(const char szName[])
{
  if (!g_bInSession || szName == nullptr)
    return FALSE;

  EnterCriticalSection(&g_csNet);

  strncpy(g_szLocalName, szName, StringSize - 1);
  g_szLocalName[StringSize - 1] = '\0';

  if (g_localPlayer != NETQ_ID_NONE)
  {
    netq_AddPlayer(g_localPlayer, g_szLocalName);

    /* The host tells everyone; a client tells the host, which relays. */
    if (g_bHost)
      netq_SendRename(nullptr, g_localPlayer, g_szLocalName, TRUE, NETQ_ID_NONE);
    else if (g_aPeers[0].state == QPEER_READY)
      netq_SendRename(&g_aPeers[0], g_localPlayer, g_szLocalName, FALSE, NETQ_ID_NONE);
  }

  LeaveCriticalSection(&g_csNet);

  return TRUE;
}

BOOL nettrans_CloseToJoiners(void)
{
  if (!g_bInSession || !g_bHost)
    return FALSE;

  EnterCriticalSection(&g_csNet);
  g_bClosedToJoiners = TRUE;
  LeaveCriticalSection(&g_csNet);

  return TRUE;
}

/***************************************************************************/
/* Data */
/***************************************************************************/

BOOL nettrans_Send(NETPLAYERID to, const void* pData, UDWORD udwSize, BOOL bReliable)
{
  QPEER* psPeer;
  BOOL bSent = FALSE;

  if (!g_bInSession || pData == nullptr || udwSize == 0 || udwSize > NETTRANS_MAX_MESSAGE)
    return FALSE;

  EnterCriticalSection(&g_csNet);

  if (g_bHost)
  {
    /* The host is connected to everybody, so a targeted send is one hop. */
    psPeer = netq_FindPeerById(to);
    if (psPeer != nullptr)
    {
      netq_SendData(psPeer, g_localPlayer, to, pData, udwSize, bReliable);
      bSent = TRUE;
    }
  }
  else if (g_aPeers[0].state == QPEER_READY)
  {
    /* A client is connected only to the host, which forwards. Sending to the
     * host itself takes the same path and simply stops there.
     */
    netq_SendData(&g_aPeers[0], g_localPlayer, to, pData, udwSize, bReliable);
    bSent = TRUE;
  }

  LeaveCriticalSection(&g_csNet);

  return bSent;
}

/***************************************************************************/

BOOL nettrans_Broadcast(const void* pData, UDWORD udwSize, BOOL bReliable)
{
  BOOL bSent = FALSE;

  if (!g_bInSession || pData == nullptr || udwSize == 0 || udwSize > NETTRANS_MAX_MESSAGE)
    return FALSE;

  EnterCriticalSection(&g_csNet);

  if (g_bHost)
  {
    netq_SendDataToAll(g_localPlayer, NETQ_ID_NONE, pData, udwSize, bReliable, NETQ_ID_NONE);
    bSent = TRUE;
  }
  else if (g_aPeers[0].state == QPEER_READY)
  {
    /* One copy to the host, which fans it out. The sender never gets its own
     * broadcast back -- the game relies on that, and applies the effect of a
     * broadcast locally as it sends it.
     */
    netq_SendData(&g_aPeers[0], g_localPlayer, NETQ_ID_NONE, pData, udwSize, bReliable);
    bSent = TRUE;
  }

  LeaveCriticalSection(&g_csNet);

  return bSent;
}

/***************************************************************************/

BOOL nettrans_Receive(void* pData, UDWORD* pudwSize, NETPLAYERID* pFrom)
{
  QMESSAGE* psMessage;

  if (pData == nullptr || pudwSize == nullptr || pFrom == nullptr)
    return FALSE;

  EnterCriticalSection(&g_csNet);

  psMessage = g_psQueueHead;
  if (psMessage != nullptr)
  {
    g_psQueueHead = psMessage->psNext;
    if (g_psQueueHead == nullptr)
      g_psQueueTail = nullptr;
    g_udwQueued--;
  }

  LeaveCriticalSection(&g_csNet);

  if (psMessage == nullptr)
    return FALSE;

  memcpy(pData, psMessage->abData, psMessage->udwSize);
  *pudwSize = psMessage->udwSize;
  *pFrom = psMessage->from;

  free(psMessage);

  return TRUE;
}

/***************************************************************************/

BOOL nettrans_NextEvent(NETTRANS_EVENT* psEvent)
{
  BOOL bFound = FALSE;

  if (psEvent == nullptr)
    return FALSE;

  EnterCriticalSection(&g_csNet);

  if (g_udwEventCount > 0)
  {
    *psEvent = g_aEvents[g_udwEventHead];
    g_udwEventHead = (g_udwEventHead + 1) % NETQ_MAX_EVENTS;
    g_udwEventCount--;
    bFound = TRUE;
  }

  LeaveCriticalSection(&g_csNet);

  return bFound;
}

/***************************************************************************/
/* Statistics */
/***************************************************************************/

UDWORD nettrans_BytesSent(void) { return g_udwBytesSent; }
UDWORD nettrans_BytesReceived(void) { return g_udwBytesReceived; }
UDWORD nettrans_PacketsSent(void) { return g_udwPacketsSent; }
UDWORD nettrans_PacketsReceived(void) { return g_udwPacketsReceived; }

/***************************************************************************/
