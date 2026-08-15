/*
 * NetTransport.h
 *
 * The seam between the game's networking and whatever moves the bytes.
 *
 * DirectPlay is on one side of it today; MsQuic goes on the other in Phase 5
 * step 7. Nothing above this header names either, which is the point: when the
 * implementation is replaced, NetPlay.cpp, NetJoin.cpp and NetUsers.cpp are the
 * only files that have to notice.
 *
 * This header deliberately includes no networking header of its own. If it ever
 * needs one, the seam has leaked.
 */

#ifndef _NETTRANSPORT_H_
#define _NETTRANSPORT_H_

/***************************************************************************/

/* NetTypes.h rather than NetPlay.h, which is the other way round from how this
 * started: NetPlay.h now includes this header, so taking anything from it
 * would be a cycle.
 *
 * BOOL, DWORD and UDWORD are assumed to be visible already, as every header in
 * this tree assumes -- Types.h refuses outright to be included except through
 * Frame.h, and Windows.h arrives through pch.h. So a unit that wants this
 * header includes Frame.h first, like every other unit does.
 */
#include "NetTypes.h"	// NETPLAYERID, StringSize, MaxGames, MaxMsgSize

/***************************************************************************/
/* A session as a browser sees it, which is to say before joining one.
 *
 * The four flags are the part that is easy to miss. DirectPlay carried them in
 * DPSESSIONDESC2's dwUser1..4, and NETgetGameFlagsUnjoined reads them out of a
 * session the player has not joined yet -- the multiplayer browser shows map,
 * version and player count from them. Whatever lists sessions has to carry
 * them, or the browser has nothing to display.
 */
/***************************************************************************/

#define	NETTRANS_GAME_FLAGS		4
#define	NETTRANS_ADDRESS_SIZE	64

/* The largest message this seam will carry, and therefore the smallest buffer
 * nettrans_Receive may be given. It is the size of NETMSG -- size, paddedBytes
 * and type, then the body -- because NETMSG is the only thing the game sends,
 * and a transport that would accept more than the receiver can hold is a
 * buffer overrun waiting for a peer to ask for one.
 */
#define	NETTRANS_MAX_MESSAGE	(MaxMsgSize + 4)

using NETSESSION = struct NETSESSION
{
  char szName[StringSize];
  char szAddress[NETTRANS_ADDRESS_SIZE]; /* how to reach the host           */
  UDWORD udwCurrentPlayers;
  UDWORD udwMaxPlayers;
  DWORD adwFlags[NETTRANS_GAME_FLAGS];
};

/***************************************************************************/
/* What the transport reports upwards, replacing DirectPlay's system messages.
 *
 * NETrecv used to notice DPID_SYSMSG and hand the message to
 * DirectPlaySystemMessageHandler, which switched on DPSYS_CREATEPLAYERORGROUP,
 * DPSYS_DESTROYPLAYERORGROUP and DPSYS_HOST. Those three become the three
 * events below, except that the last one is not a promotion: the session ends
 * when the host goes, so NETTRANS_HOST_LOST is terminal.
 */
/***************************************************************************/

using NETTRANS_EVENT_TYPE = enum
{
  NETTRANS_PLAYER_JOINED,
  NETTRANS_PLAYER_LEFT,
  NETTRANS_HOST_LOST
};

using NETTRANS_EVENT = struct NETTRANS_EVENT
{
  NETTRANS_EVENT_TYPE type;
  NETPLAYERID player;
  char szName[StringSize];
};

/***************************************************************************/
/* Lifecycle */
/***************************************************************************/

BOOL nettrans_Startup(void);
void nettrans_Shutdown(void);

/* Called once a frame. Connection maintenance and the queues behind
 * nettrans_Receive and nettrans_NextEvent are serviced here, so that nothing
 * above this line has to think about which thread it is on.
 */
void nettrans_Update(void);

/***************************************************************************/
/* Sessions */
/***************************************************************************/

BOOL nettrans_Host(const char szSessionName[], const char szPlayerName[], UDWORD udwMaxPlayers, const DWORD adwFlags[]);

/* Fills paSessions with what is reachable, and returns how many. Synchronous:
 * DirectPlay offered an asynchronous enumeration and NETfindGame took a flag
 * for it, but every caller passes the same value.
 *
 * Answers nothing today, and the join screen takes a typed address instead.
 * LAN broadcast discovery was deliberately not built: the destination is a
 * relay server that owns the connections, where listing sessions is a query to
 * a known server. That query goes here, behind this signature, and the game
 * above the seam does not learn about it.
 */
UDWORD nettrans_FindSessions(NETSESSION paSessions[], UDWORD udwMax);

BOOL nettrans_Join(const char szAddress[], const char szPlayerName[]);
void nettrans_Leave(void);

/* The four flags again, on a session this machine is hosting. Reading them
 * back for a session not joined is what nettrans_FindSessions is for.
 */
BOOL nettrans_SetFlags(const DWORD adwFlags[]);
BOOL nettrans_GetFlags(DWORD adwFlags[]);

/***************************************************************************/
/* Players */
/***************************************************************************/

NETPLAYERID nettrans_LocalPlayer(void);
BOOL nettrans_IsHost(void);
UDWORD nettrans_PlayerCount(void);
BOOL nettrans_PlayerName(NETPLAYERID player, char szName[], UDWORD udwSize);

/* Fills paPlayers with everybody in the session and returns how many, so the
 * game can rebuild its own roster. DirectPlay offered this as an enumeration
 * with a callback; a list is the same thing without the inversion.
 */
UDWORD nettrans_PlayerList(NETPLAYERID paPlayers[], UDWORD udwMax);

/* Whether a given player is the one hosting, which is not the same question as
 * nettrans_IsHost -- that one asks about this machine.
 */
BOOL nettrans_IsHostPlayer(NETPLAYERID player);

/* Renames the local player everywhere. Only the local player: DirectPlay would
 * let a machine rename anyone and nothing ever did.
 */
BOOL nettrans_SetLocalName(const char szName[]);

/* Stops anyone else joining a session this machine hosts. */
BOOL nettrans_CloseToJoiners(void);

/***************************************************************************/
/* Data
 *
 * Reliable sends are ordered with respect to each other, which is what the
 * lockstep command traffic depends on and what DPSEND_GUARANTEED gave it.
 * Unreliable sends have no ordering and may be dropped.
 *
 * A client sending to another client is relayed by the host, because the host
 * is the only machine everyone is connected to.
 */
/***************************************************************************/

BOOL nettrans_Send(NETPLAYERID to, const void* pData, UDWORD udwSize, BOOL bReliable);
BOOL nettrans_Broadcast(const void* pData, UDWORD udwSize, BOOL bReliable);

/* Returns FALSE when nothing is waiting. On TRUE, *pudwSize is how much was
 * written and *pFrom is who sent it. pData must have room for
 * NETTRANS_MAX_MESSAGE bytes.
 */
BOOL nettrans_Receive(void* pData, UDWORD* pudwSize, NETPLAYERID* pFrom);

/* Drained in the same loop as nettrans_Receive. Returns FALSE when empty. */
BOOL nettrans_NextEvent(NETTRANS_EVENT* psEvent);

/***************************************************************************/
/* Statistics, which the game shows on its multiplayer screens. */
/***************************************************************************/

UDWORD nettrans_BytesSent(void);
UDWORD nettrans_BytesReceived(void);
UDWORD nettrans_PacketsSent(void);
UDWORD nettrans_PacketsReceived(void);

/***************************************************************************/

#endif	// _NETTRANSPORT_H_

/***************************************************************************/
