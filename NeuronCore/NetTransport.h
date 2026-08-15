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

#include "NetPlay.h"	// NETPLAYERID, StringSize, MaxGames, MaxMsgSize

/***************************************************************************/
/* A session as a browser sees it, which is to say before joining one.
 *
 * The four flags are the part that is easy to miss. DirectPlay carried them in
 * DPSESSIONDESC2's dwUser1..4, and NETgetGameFlagsUnjoined reads them out of a
 * session the player has not joined yet -- the multiplayer browser shows map,
 * version and player count from them. Whatever answers a discovery request has
 * to carry them, or the browser has nothing to display.
 */
/***************************************************************************/

#define	NETTRANS_GAME_FLAGS		4
#define	NETTRANS_ADDRESS_SIZE	64

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

/* Called once a frame. Connection maintenance, discovery replies and the
 * queues behind nettrans_Receive and nettrans_NextEvent are serviced here, so
 * that nothing above this line has to think about which thread it is on.
 */
void nettrans_Update(void);

/***************************************************************************/
/* Sessions */
/***************************************************************************/

BOOL nettrans_Host(const char szSessionName[], const char szPlayerName[], UDWORD udwMaxPlayers, const DWORD adwFlags[]);

/* Fills paSessions with what is reachable, and returns how many. Synchronous:
 * DirectPlay offered an asynchronous enumeration and NETfindGame took a flag
 * for it, but every caller passes the same value.
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
 * written and *pFrom is who sent it.
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
