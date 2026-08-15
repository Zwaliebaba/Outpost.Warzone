#include "pch.h"
/***************************************************************************/
/*
 * NetTest.cpp
 *
 * The loopback harness for the QUIC transport. Two copies of this program talk
 * to each other over 127.0.0.1 -- one hosts, one joins -- and each checks what
 * it received against what the other said it sent.
 *
 *     NetTest.exe host <ready-file>
 *     NetTest.exe join <address>
 *
 * Two processes rather than two transports in one, because NetQuic.cpp keeps
 * its session in file statics: one host, one client, one roster. That is the
 * right shape for a game that is only ever one of those at a time, and it is
 * also closer to what is being tested.
 *
 * What this can and cannot check is worth being exact about. It exercises the
 * things nothing else can reach from a Linux container -- that Schannel accepts
 * the generated certificate, that the handshake completes, that the framing
 * reassembles, that a broadcast does not come back to its sender. It does not
 * simulate packet loss: MsQuic's public API has no way to inject it, and over
 * loopback there is none to observe. Ordering under loss is QUIC's problem
 * anyway; the framing on top of it is ours, and that is what the burst of
 * mixed-size messages is for.
 */
/***************************************************************************/

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Frame.h"
#include "NetTransport.h"

/***************************************************************************/

/* Enough messages, at enough different sizes, that the stream carries frames
 * split across reads and packed several to a read. That is the path the
 * reassembly exists for, and a handful of same-sized messages would not touch
 * it.
 */
#define	NT_RELIABLE_COUNT	120
#define	NT_UNRELIABLE_COUNT	20

/* Cycled through, in bytes of payload including the harness header. The last
 * is the ceiling the seam publishes, so the biggest message the game could
 * ever send is one of the ones tested.
 */
static const UDWORD g_aSizes[] = {9, 10, 11, 64, 300, 1500, 4000, 8000, NETTRANS_MAX_MESSAGE};
#define	NT_SIZE_COUNT		(sizeof(g_aSizes) / sizeof(g_aSizes[0]))

#define	NT_HEADER_SIZE		9

#define	NT_KIND_DATA		1
#define	NT_KIND_UNRELIABLE	2
#define	NT_KIND_BROADCAST	3
#define	NT_KIND_DONE		4

#define	NT_JOIN_ATTEMPTS	10
#define	NT_STEP_TIMEOUT_MS	30000

/***************************************************************************/

static int g_iFailures = 0;

static void nt_Fail(const char* pszWhat)
{
  printf("FAIL: %s\n", pszWhat);
  fflush(stdout);
  g_iFailures++;
}

static void nt_Say(const char* pszWhat)
{
  printf("  %s\n", pszWhat);
  fflush(stdout);
}

/***************************************************************************/

static void nt_PutU32(BYTE* pOut, UDWORD udwValue)
{
  pOut[0] = static_cast<BYTE>(udwValue >> 24);
  pOut[1] = static_cast<BYTE>(udwValue >> 16);
  pOut[2] = static_cast<BYTE>(udwValue >> 8);
  pOut[3] = static_cast<BYTE>(udwValue);
}

static UDWORD nt_GetU32(const BYTE* pIn)
{
  return (static_cast<UDWORD>(pIn[0]) << 24) | (static_cast<UDWORD>(pIn[1]) << 16) |
         (static_cast<UDWORD>(pIn[2]) << 8) | static_cast<UDWORD>(pIn[3]);
}

/* Derived from the sequence number so that a message which arrived whole but
 * out of place, or spliced from two others, is caught as well as one that
 * arrived short.
 */
static BYTE nt_PatternByte(UDWORD udwSeq, UDWORD udwOffset)
{
  return static_cast<BYTE>((udwSeq * 31 + udwOffset * 17 + (udwOffset >> 8)) & 0xFF);
}

static UDWORD nt_SizeFor(UDWORD udwSeq) { return g_aSizes[udwSeq % NT_SIZE_COUNT]; }

/***************************************************************************/

static UDWORD nt_Build(BYTE* pOut, BYTE kind, UDWORD udwSeq, UDWORD udwSize)
{
  UDWORD i;

  if (udwSize < NT_HEADER_SIZE)
    udwSize = NT_HEADER_SIZE;

  pOut[0] = kind;
  nt_PutU32(pOut + 1, udwSeq);
  nt_PutU32(pOut + 5, udwSize);

  for (i = NT_HEADER_SIZE; i < udwSize; i++)
    pOut[i] = nt_PatternByte(udwSeq, i);

  return udwSize;
}

/* Returns FALSE and says why if the message is not what its own header claims
 * it is.
 */
static BOOL nt_Verify(const BYTE* pData, UDWORD udwSize)
{
  UDWORD udwSeq, udwClaimed, i;
  char szWhat[160];

  if (udwSize < NT_HEADER_SIZE)
  {
    sprintf(szWhat, "message of %u bytes is shorter than a header", udwSize);
    nt_Fail(szWhat);
    return FALSE;
  }

  udwSeq = nt_GetU32(pData + 1);
  udwClaimed = nt_GetU32(pData + 5);

  if (udwClaimed != udwSize)
  {
    sprintf(szWhat, "message %u claims %u bytes, arrived as %u", udwSeq, udwClaimed, udwSize);
    nt_Fail(szWhat);
    return FALSE;
  }

  for (i = NT_HEADER_SIZE; i < udwSize; i++)
    if (pData[i] != nt_PatternByte(udwSeq, i))
    {
      sprintf(szWhat, "message %u corrupt at byte %u of %u", udwSeq, i, udwSize);
      nt_Fail(szWhat);
      return FALSE;
    }

  return TRUE;
}

/***************************************************************************/
/* What this side has seen from the other. */
/***************************************************************************/

using NT_STATE = struct NT_STATE
{
  NETPLAYERID local;
  NETPLAYERID peer;
  char szPeerName[StringSize];

  BOOL bPeerJoined;
  BOOL bPeerLeft;
  BOOL bHostLost;

  UDWORD udwReliable;		/* how many reliable messages have arrived     */
  UDWORD udwNextExpected;	/* and what sequence number is due next        */
  UDWORD udwUnreliable;
  UDWORD udwBroadcast;
  BOOL bDone;				/* the other side says it has sent everything  */
  BOOL bSelfDelivery;		/* a message came back from this machine       */
};

static NT_STATE g_sState;

/***************************************************************************/

static void nt_Drain(void)
{
  BYTE abData[NETTRANS_MAX_MESSAGE];
  UDWORD udwSize;
  NETPLAYERID from;
  NETTRANS_EVENT sEvent;
  char szWhat[160];

  nettrans_Update();

  while (nettrans_NextEvent(&sEvent))
  {
    switch (sEvent.type)
    {
    case NETTRANS_PLAYER_JOINED:
      g_sState.peer = sEvent.player;
      strncpy(g_sState.szPeerName, sEvent.szName, StringSize - 1);
      g_sState.szPeerName[StringSize - 1] = '\0';
      g_sState.bPeerJoined = TRUE;
      sprintf(szWhat, "joined: player %u, \"%s\"", static_cast<UDWORD>(sEvent.player),
              g_sState.szPeerName);
      nt_Say(szWhat);
      break;

    case NETTRANS_PLAYER_LEFT:
      g_sState.bPeerLeft = TRUE;
      nt_Say("the other player left");
      break;

    case NETTRANS_HOST_LOST:
      g_sState.bHostLost = TRUE;
      nt_Say("the host was lost");
      break;
    }
  }

  while (nettrans_Receive(abData, &udwSize, &from))
  {
    /* The invariant that holds everywhere: nothing this machine sent may come
     * back to it, broadcasts included. The game applies the effect of its own
     * broadcasts locally as it sends them, so a loopback copy double-applies.
     */
    if (from == g_sState.local && g_sState.local != 0)
      g_sState.bSelfDelivery = TRUE;

    if (!nt_Verify(abData, udwSize))
      continue;

    switch (abData[0])
    {
    case NT_KIND_DATA:
      if (nt_GetU32(abData + 1) != g_sState.udwNextExpected)
      {
        sprintf(szWhat, "reliable message out of order: expected %u, got %u",
                g_sState.udwNextExpected, nt_GetU32(abData + 1));
        nt_Fail(szWhat);
      }
      else
        g_sState.udwNextExpected++;
      g_sState.udwReliable++;
      break;

    case NT_KIND_UNRELIABLE: g_sState.udwUnreliable++; break;
    case NT_KIND_BROADCAST: g_sState.udwBroadcast++; break;
    case NT_KIND_DONE: g_sState.bDone = TRUE; break;
    default: nt_Fail("message of an unknown kind"); break;
    }
  }
}

/* Keeps pumping for a while with nothing to wait for. Used after the done
 * marker: that marker orders itself behind every reliable message, because
 * they share a stream, but a datagram has no such relationship to it and can
 * still be on its way. Without this the unreliable count would be a race.
 */
static void nt_DrainFor(UDWORD udwMs)
{
  ULONGLONG ullStart = GetTickCount64();

  while (GetTickCount64() - ullStart < udwMs)
  {
    nt_Drain();
    Sleep(1);
  }
}

/* Pumps the transport until the predicate holds or the clock runs out. */
static BOOL nt_WaitFor(BOOL* pbFlag, const char* pszWhat)
{
  ULONGLONG ullStart = GetTickCount64();
  char szWhat[160];

  while (!*pbFlag)
  {
    nt_Drain();

    if (GetTickCount64() - ullStart > NT_STEP_TIMEOUT_MS)
    {
      sprintf(szWhat, "timed out waiting for %s", pszWhat);
      nt_Fail(szWhat);
      return FALSE;
    }

    Sleep(1);
  }

  return TRUE;
}

/***************************************************************************/

/* The burst. Reliable messages in sequence, then unreliable ones, then -- for
 * the host -- a broadcast, and last the marker that says there is no more.
 *
 * The marker is the barrier the other side waits on: QUIC orders a stream, so
 * once it has arrived every reliable message sent before it must have arrived
 * too. Anything missing at that point is missing for good, which is what makes
 * the count assertable rather than a race.
 */
static void nt_SendBurst(BOOL bBroadcastProbe)
{
  BYTE abData[NETTRANS_MAX_MESSAGE];
  UDWORD udwSize;
  UDWORD i;

  for (i = 0; i < NT_RELIABLE_COUNT; i++)
  {
    udwSize = nt_Build(abData, NT_KIND_DATA, i, nt_SizeFor(i));
    if (!nettrans_Send(g_sState.peer, abData, udwSize, TRUE))
      nt_Fail("nettrans_Send refused a reliable message");

    /* Drained as we go: a hundred and twenty messages of up to eight kilobytes
     * is more than the other side will have collected by now, and the point is
     * to keep both directions moving rather than to fill a queue.
     */
    nt_Drain();
  }

  for (i = 0; i < NT_UNRELIABLE_COUNT; i++)
  {
    udwSize = nt_Build(abData, NT_KIND_UNRELIABLE, i, nt_SizeFor(i));
    nettrans_Send(g_sState.peer, abData, udwSize, FALSE);
    nt_Drain();
  }

  if (bBroadcastProbe)
  {
    udwSize = nt_Build(abData, NT_KIND_BROADCAST, 0, 64);
    if (!nettrans_Broadcast(abData, udwSize, TRUE))
      nt_Fail("nettrans_Broadcast refused a message");
  }

  udwSize = nt_Build(abData, NT_KIND_DONE, 0, NT_HEADER_SIZE);
  if (!nettrans_Send(g_sState.peer, abData, udwSize, TRUE))
    nt_Fail("nettrans_Send refused the done marker");
}

static void nt_CheckBurst(BOOL bExpectBroadcast)
{
  char szWhat[160];

  /* The done marker has landed, so every reliable message is in. Datagrams
   * sent before it need a moment longer.
   */
  nt_DrainFor(500);

  if (g_sState.udwReliable != NT_RELIABLE_COUNT)
  {
    sprintf(szWhat, "%u of %u reliable messages arrived", g_sState.udwReliable,
            static_cast<UDWORD>(NT_RELIABLE_COUNT));
    nt_Fail(szWhat);
  }
  else
    nt_Say("all reliable messages arrived, in order and intact");

  /* Not all of them, and deliberately not: a datagram is allowed to be
   * dropped, so requiring the full count would be a test that fails for the
   * right reason on a bad day. Zero, over loopback, means the datagram path
   * is broken rather than unlucky.
   */
  if (g_sState.udwUnreliable == 0)
    nt_Fail("no unreliable messages arrived at all");
  else
  {
    sprintf(szWhat, "%u of %u unreliable messages arrived", g_sState.udwUnreliable,
            static_cast<UDWORD>(NT_UNRELIABLE_COUNT));
    nt_Say(szWhat);
  }

  if (bExpectBroadcast && g_sState.udwBroadcast != 1)
  {
    sprintf(szWhat, "expected one broadcast, got %u", g_sState.udwBroadcast);
    nt_Fail(szWhat);
  }

  if (!bExpectBroadcast && g_sState.udwBroadcast != 0)
    nt_Fail("a broadcast came back to the machine that sent it");

  if (g_sState.bSelfDelivery)
    nt_Fail("a message sent by this machine was delivered back to it");
}

/***************************************************************************/

static void nt_CheckRoster(const char* pszExpectedPeer)
{
  char szName[StringSize];
  char szWhat[160];

  if (nettrans_PlayerCount() != 2)
  {
    sprintf(szWhat, "expected two players, transport says %u", nettrans_PlayerCount());
    nt_Fail(szWhat);
  }

  if (!nettrans_PlayerName(g_sState.peer, szName, sizeof(szName)))
    nt_Fail("the other player is not in the roster");
  else if (strcmp(szName, pszExpectedPeer) != 0)
  {
    sprintf(szWhat, "the other player is named \"%s\", expected \"%s\"", szName, pszExpectedPeer);
    nt_Fail(szWhat);
  }

  if (!nettrans_PlayerName(g_sState.local, szName, sizeof(szName)))
    nt_Fail("this player is not in its own roster");
}

/***************************************************************************/

static int nt_Host(const char* pszReadyFile)
{
  DWORD adwFlags[NETTRANS_GAME_FLAGS] = {0x11111111, 0x22222222, 0x33333333, 0x44444444};
  DWORD adwRead[NETTRANS_GAME_FLAGS];
  FILE* pFile;
  int i;

  printf("host: starting\n");
  fflush(stdout);

  if (!nettrans_Startup())
  {
    nt_Fail("nettrans_Startup");
    return 1;
  }

  if (!nettrans_Host("harness", "host", 4, adwFlags))
  {
    nt_Fail("nettrans_Host -- if this is the certificate, the reason is on stderr");
    nettrans_Shutdown();
    return 1;
  }

  nt_Say("hosting");

  g_sState.local = nettrans_LocalPlayer();
  if (g_sState.local == 0)
    nt_Fail("the host has no player id");
  if (!nettrans_IsHost())
    nt_Fail("the host does not think it is the host");

  if (!nettrans_GetFlags(adwRead))
    nt_Fail("nettrans_GetFlags");
  else
    for (i = 0; i < NETTRANS_GAME_FLAGS; i++)
      if (adwRead[i] != adwFlags[i])
        nt_Fail("a game flag came back changed");

  /* Written only once the listener is up, so the client cannot race it. */
  pFile = fopen(pszReadyFile, "wb");
  if (pFile == nullptr)
  {
    nt_Fail("cannot write the ready file");
    nettrans_Leave();
    nettrans_Shutdown();
    return 1;
  }
  fputs("ready", pFile);
  fclose(pFile);

  if (nt_WaitFor(&g_sState.bPeerJoined, "the client to join"))
  {
    nt_CheckRoster("client");
    nt_SendBurst(TRUE);
    if (nt_WaitFor(&g_sState.bDone, "the client's messages"))
      nt_CheckBurst(FALSE);

    /* The client leaves first, so this is the host watching a player go. */
    nt_WaitFor(&g_sState.bPeerLeft, "the client to leave");

    if (nettrans_PlayerCount() != 1)
      nt_Fail("the roster still has the client in it");
  }

  nettrans_Leave();
  nettrans_Shutdown();

  printf(g_iFailures == 0 ? "host: PASS\n" : "host: FAILED\n");
  return g_iFailures == 0 ? 0 : 1;
}

/***************************************************************************/

static int nt_Join(const char* pszAddress)
{
  DWORD adwFlags[NETTRANS_GAME_FLAGS];
  ULONGLONG ullStart;
  char szWhat[160];
  int iAttempt;
  int i;

  printf("client: joining %s\n", pszAddress);
  fflush(stdout);

  if (!nettrans_Startup())
  {
    nt_Fail("nettrans_Startup");
    return 1;
  }

  /* Retried, because a connection to a listener that is not up yet fails at
   * the handshake rather than at the call. The ready file should make this
   * unnecessary; it costs nothing to survive the case where it does not.
   */
  for (iAttempt = 0; iAttempt < NT_JOIN_ATTEMPTS; iAttempt++)
  {
    memset(&g_sState, 0, sizeof(g_sState));

    if (!nettrans_Join(pszAddress, "client"))
    {
      nt_Say("nettrans_Join was refused, retrying");
      Sleep(1000);
      continue;
    }

    /* The join has landed when the host has answered with an id and the
     * roster. Either that arrives, or the connection dies trying, or QUIC's
     * idle timeout ends it -- and the deadline is here in case none of those
     * happens, because a harness that hangs tells nobody anything.
     */
    ullStart = GetTickCount64();
    while (!g_sState.bPeerJoined && !g_sState.bHostLost &&
           GetTickCount64() - ullStart < NT_STEP_TIMEOUT_MS)
    {
      nt_Drain();
      Sleep(1);
    }

    if (g_sState.bPeerJoined)
      break;

    nettrans_Leave();
    nt_Say("the host went away during the handshake, retrying");
    Sleep(1000);
  }

  if (!g_sState.bPeerJoined)
  {
    nt_Fail("could not join the host");
    nettrans_Shutdown();
    return 1;
  }

  g_sState.local = nettrans_LocalPlayer();
  if (g_sState.local == 0)
    nt_Fail("the client has no player id");
  if (nettrans_IsHost())
    nt_Fail("the client thinks it is the host");

  sprintf(szWhat, "joined as player %u", static_cast<UDWORD>(g_sState.local));
  nt_Say(szWhat);

  /* The four flags travelled in the welcome, which is the only way a joiner
   * gets them.
   */
  if (!nettrans_GetFlags(adwFlags))
    nt_Fail("nettrans_GetFlags");
  else
  {
    const DWORD adwExpected[NETTRANS_GAME_FLAGS] = {0x11111111, 0x22222222, 0x33333333, 0x44444444};
    for (i = 0; i < NETTRANS_GAME_FLAGS; i++)
      if (adwFlags[i] != adwExpected[i])
      {
        sprintf(szWhat, "game flag %d arrived as 0x%08lx", i, adwFlags[i]);
        nt_Fail(szWhat);
      }
  }

  nt_CheckRoster("host");
  nt_SendBurst(FALSE);

  if (nt_WaitFor(&g_sState.bDone, "the host's messages"))
    nt_CheckBurst(TRUE);

  nettrans_Leave();
  nettrans_Shutdown();

  printf(g_iFailures == 0 ? "client: PASS\n" : "client: FAILED\n");
  return g_iFailures == 0 ? 0 : 1;
}

/***************************************************************************/

int main(int argc, char** argv)
{
  memset(&g_sState, 0, sizeof(g_sState));

  if (argc == 3 && strcmp(argv[1], "host") == 0)
    return nt_Host(argv[2]);

  if (argc == 3 && strcmp(argv[1], "join") == 0)
    return nt_Join(argv[2]);

  printf("usage: NetTest host <ready-file>\n       NetTest join <address>\n");
  return 2;
}

/***************************************************************************/
