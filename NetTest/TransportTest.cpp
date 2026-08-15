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
 * Two processes rather than two transports in one, because Transport.cpp keeps
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
#include "Transport.h"

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
static const UDWORD g_aSizes[] = {9, 10, 11, 64, 300, 1500, 4000, 8000, Transport::MaxMessageBytes};
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

static void nt_PutU32(BYTE* out, UDWORD value)
{
  out[0] = static_cast<BYTE>(value >> 24);
  out[1] = static_cast<BYTE>(value >> 16);
  out[2] = static_cast<BYTE>(value >> 8);
  out[3] = static_cast<BYTE>(value);
}

static UDWORD nt_GetU32(const BYTE* in)
{
  return (static_cast<UDWORD>(in[0]) << 24) | (static_cast<UDWORD>(in[1]) << 16) |
         (static_cast<UDWORD>(in[2]) << 8) | static_cast<UDWORD>(in[3]);
}

/* Derived from the sequence number so that a message which arrived whole but
 * out of place, or spliced from two others, is caught as well as one that
 * arrived short.
 */
static BYTE nt_PatternByte(UDWORD sequence, UDWORD offset)
{
  return static_cast<BYTE>((sequence * 31 + offset * 17 + (offset >> 8)) & 0xFF);
}

static UDWORD nt_SizeFor(UDWORD sequence) { return g_aSizes[sequence % NT_SIZE_COUNT]; }

/***************************************************************************/

static UDWORD nt_Build(BYTE* out, BYTE kind, UDWORD sequence, UDWORD size)
{
  UDWORD i;

  if (size < NT_HEADER_SIZE)
    size = NT_HEADER_SIZE;

  out[0] = kind;
  nt_PutU32(out + 1, sequence);
  nt_PutU32(out + 5, size);

  for (i = NT_HEADER_SIZE; i < size; i++)
    out[i] = nt_PatternByte(sequence, i);

  return size;
}

/* Returns FALSE and says why if the message is not what its own header claims
 * it is.
 */
static BOOL nt_Verify(const BYTE* data, UDWORD size)
{
  UDWORD sequence, udwClaimed, i;
  char text[160];

  if (size < NT_HEADER_SIZE)
  {
    sprintf(text, "message of %u bytes is shorter than a header", size);
    nt_Fail(text);
    return FALSE;
  }

  sequence = nt_GetU32(data + 1);
  udwClaimed = nt_GetU32(data + 5);

  if (udwClaimed != size)
  {
    sprintf(text, "message %u claims %u bytes, arrived as %u", sequence, udwClaimed, size);
    nt_Fail(text);
    return FALSE;
  }

  for (i = NT_HEADER_SIZE; i < size; i++)
    if (data[i] != nt_PatternByte(sequence, i))
    {
      sprintf(text, "message %u corrupt at byte %u of %u", sequence, i, size);
      nt_Fail(text);
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
  BYTE data[Transport::MaxMessageBytes];
  UDWORD size;
  NETPLAYERID from;
  Transport::Event sEvent;
  char text[160];

  Transport::Update();

  while (Transport::NextEvent(&sEvent))
  {
    switch (sEvent.type)
    {
    case TransportEventType::PlayerJoined:
      g_sState.peer = sEvent.player;
      strncpy(g_sState.szPeerName, sEvent.name, StringSize - 1);
      g_sState.szPeerName[StringSize - 1] = '\0';
      g_sState.bPeerJoined = TRUE;
      sprintf(text, "joined: player %u, \"%s\"", static_cast<UDWORD>(sEvent.player),
              g_sState.szPeerName);
      nt_Say(text);
      break;

    case TransportEventType::PlayerLeft:
      g_sState.bPeerLeft = TRUE;
      nt_Say("the other player left");
      break;

    case TransportEventType::HostLost:
      g_sState.bHostLost = TRUE;
      nt_Say("the host was lost");
      break;
    }
  }

  while (Transport::Receive(data, &size, &from))
  {
    /* The invariant that holds everywhere: nothing this machine sent may come
     * back to it, broadcasts included. The game applies the effect of its own
     * broadcasts locally as it sends them, so a loopback copy double-applies.
     */
    if (from == g_sState.local && g_sState.local != 0)
      g_sState.bSelfDelivery = TRUE;

    if (!nt_Verify(data, size))
      continue;

    switch (data[0])
    {
    case NT_KIND_DATA:
      if (nt_GetU32(data + 1) != g_sState.udwNextExpected)
      {
        sprintf(text, "reliable message out of order: expected %u, got %u",
                g_sState.udwNextExpected, nt_GetU32(data + 1));
        nt_Fail(text);
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
static void nt_DrainFor(UDWORD milliseconds)
{
  ULONGLONG startMs = GetTickCount64();

  while (GetTickCount64() - startMs < milliseconds)
  {
    nt_Drain();
    Sleep(1);
  }
}

/* Pumps the transport until the predicate holds or the clock runs out. */
static BOOL nt_WaitFor(BOOL* pbFlag, const char* pszWhat)
{
  ULONGLONG startMs = GetTickCount64();
  char text[160];

  while (!*pbFlag)
  {
    nt_Drain();

    if (GetTickCount64() - startMs > NT_STEP_TIMEOUT_MS)
    {
      sprintf(text, "timed out waiting for %s", pszWhat);
      nt_Fail(text);
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
  BYTE data[Transport::MaxMessageBytes];
  UDWORD size;
  UDWORD i;

  for (i = 0; i < NT_RELIABLE_COUNT; i++)
  {
    size = nt_Build(data, NT_KIND_DATA, i, nt_SizeFor(i));
    if (!Transport::Send(g_sState.peer, data, size, TRUE))
      nt_Fail("Transport::Send refused a reliable message");

    /* Drained as we go: a hundred and twenty messages of up to eight kilobytes
     * is more than the other side will have collected by now, and the point is
     * to keep both directions moving rather than to fill a queue.
     */
    nt_Drain();
  }

  for (i = 0; i < NT_UNRELIABLE_COUNT; i++)
  {
    size = nt_Build(data, NT_KIND_UNRELIABLE, i, nt_SizeFor(i));
    Transport::Send(g_sState.peer, data, size, FALSE);
    nt_Drain();
  }

  if (bBroadcastProbe)
  {
    size = nt_Build(data, NT_KIND_BROADCAST, 0, 64);
    if (!Transport::Broadcast(data, size, TRUE))
      nt_Fail("Transport::Broadcast refused a message");
  }

  size = nt_Build(data, NT_KIND_DONE, 0, NT_HEADER_SIZE);
  if (!Transport::Send(g_sState.peer, data, size, TRUE))
    nt_Fail("Transport::Send refused the done marker");
}

static void nt_CheckBurst(BOOL bExpectBroadcast)
{
  char text[160];

  /* The done marker has landed, so every reliable message is in. Datagrams
   * sent before it need a moment longer.
   */
  nt_DrainFor(500);

  if (g_sState.udwReliable != NT_RELIABLE_COUNT)
  {
    sprintf(text, "%u of %u reliable messages arrived", g_sState.udwReliable,
            static_cast<UDWORD>(NT_RELIABLE_COUNT));
    nt_Fail(text);
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
    sprintf(text, "%u of %u unreliable messages arrived", g_sState.udwUnreliable,
            static_cast<UDWORD>(NT_UNRELIABLE_COUNT));
    nt_Say(text);
  }

  if (bExpectBroadcast && g_sState.udwBroadcast != 1)
  {
    sprintf(text, "expected one broadcast, got %u", g_sState.udwBroadcast);
    nt_Fail(text);
  }

  if (!bExpectBroadcast && g_sState.udwBroadcast != 0)
    nt_Fail("a broadcast came back to the machine that sent it");

  if (g_sState.bSelfDelivery)
    nt_Fail("a message sent by this machine was delivered back to it");
}

/***************************************************************************/

static void nt_CheckRoster(const char* pszExpectedPeer)
{
  char name[StringSize];
  char text[160];

  if (Transport::PlayerCount() != 2)
  {
    sprintf(text, "expected two players, transport says %u", Transport::PlayerCount());
    nt_Fail(text);
  }

  if (!Transport::PlayerName(g_sState.peer, name, sizeof(name)))
    nt_Fail("the other player is not in the roster");
  else if (strcmp(name, pszExpectedPeer) != 0)
  {
    sprintf(text, "the other player is named \"%s\", expected \"%s\"", name, pszExpectedPeer);
    nt_Fail(text);
  }

  if (!Transport::PlayerName(g_sState.local, name, sizeof(name)))
    nt_Fail("this player is not in its own roster");
}

/***************************************************************************/

static int nt_Host(const char* pszReadyFile)
{
  DWORD gameFlags[Transport::GameFlagCount] = {0x11111111, 0x22222222, 0x33333333, 0x44444444};
  DWORD adwRead[Transport::GameFlagCount];
  FILE* pFile;
  int i;

  printf("host: starting\n");
  fflush(stdout);

  if (!Transport::Startup())
  {
    nt_Fail("Transport::Startup");
    return 1;
  }

  if (!Transport::Host("harness", "host", 4, gameFlags))
  {
    nt_Fail("Transport::Host -- if this is the certificate, the reason is on stderr");
    Transport::Shutdown();
    return 1;
  }

  nt_Say("hosting");

  g_sState.local = Transport::LocalPlayer();
  if (g_sState.local == 0)
    nt_Fail("the host has no player id");
  if (!Transport::IsHost())
    nt_Fail("the host does not think it is the host");

  if (!Transport::GetGameFlags(adwRead))
    nt_Fail("Transport::GetGameFlags");
  else
    for (i = 0; i < Transport::GameFlagCount; i++)
      if (adwRead[i] != gameFlags[i])
        nt_Fail("a game flag came back changed");

  /* Written only once the listener is up, so the client cannot race it. */
  pFile = fopen(pszReadyFile, "wb");
  if (pFile == nullptr)
  {
    nt_Fail("cannot write the ready file");
    Transport::Leave();
    Transport::Shutdown();
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

    if (Transport::PlayerCount() != 1)
      nt_Fail("the roster still has the client in it");
  }

  Transport::Leave();
  Transport::Shutdown();

  printf(g_iFailures == 0 ? "host: PASS\n" : "host: FAILED\n");
  return g_iFailures == 0 ? 0 : 1;
}

/***************************************************************************/

static int nt_Join(const char* pszAddress)
{
  DWORD gameFlags[Transport::GameFlagCount];
  ULONGLONG startMs;
  char text[160];
  int iAttempt;
  int i;

  printf("client: joining %s\n", pszAddress);
  fflush(stdout);

  if (!Transport::Startup())
  {
    nt_Fail("Transport::Startup");
    return 1;
  }

  /* Retried, because a connection to a listener that is not up yet fails at
   * the handshake rather than at the call. The ready file should make this
   * unnecessary; it costs nothing to survive the case where it does not.
   */
  for (iAttempt = 0; iAttempt < NT_JOIN_ATTEMPTS; iAttempt++)
  {
    memset(&g_sState, 0, sizeof(g_sState));

    if (!Transport::Join(pszAddress, "client"))
    {
      nt_Say("Transport::Join was refused, retrying");
      Sleep(1000);
      continue;
    }

    /* The join has landed when the host has answered with an id and the
     * roster. Either that arrives, or the connection dies trying, or QUIC's
     * idle timeout ends it -- and the deadline is here in case none of those
     * happens, because a harness that hangs tells nobody anything.
     */
    startMs = GetTickCount64();
    while (!g_sState.bPeerJoined && !g_sState.bHostLost &&
           GetTickCount64() - startMs < NT_STEP_TIMEOUT_MS)
    {
      nt_Drain();
      Sleep(1);
    }

    if (g_sState.bPeerJoined)
      break;

    Transport::Leave();
    nt_Say("the host went away during the handshake, retrying");
    Sleep(1000);
  }

  if (!g_sState.bPeerJoined)
  {
    nt_Fail("could not join the host");
    Transport::Shutdown();
    return 1;
  }

  g_sState.local = Transport::LocalPlayer();
  if (g_sState.local == 0)
    nt_Fail("the client has no player id");
  if (Transport::IsHost())
    nt_Fail("the client thinks it is the host");

  sprintf(text, "joined as player %u", static_cast<UDWORD>(g_sState.local));
  nt_Say(text);

  /* The four flags travelled in the welcome, which is the only way a joiner
   * gets them.
   */
  if (!Transport::GetGameFlags(gameFlags))
    nt_Fail("Transport::GetGameFlags");
  else
  {
    const DWORD adwExpected[Transport::GameFlagCount] = {0x11111111, 0x22222222, 0x33333333, 0x44444444};
    for (i = 0; i < Transport::GameFlagCount; i++)
      if (gameFlags[i] != adwExpected[i])
      {
        sprintf(text, "game flag %d arrived as 0x%08lx", i, gameFlags[i]);
        nt_Fail(text);
      }
  }

  nt_CheckRoster("host");
  nt_SendBurst(FALSE);

  if (nt_WaitFor(&g_sState.bDone, "the host's messages"))
    nt_CheckBurst(TRUE);

  Transport::Leave();
  Transport::Shutdown();

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
