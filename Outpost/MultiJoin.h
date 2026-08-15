/*
 * Multijoin.h 
 * 
 * Alex Lee, pumpkin studios, 
 * multijoin caters for all the player comings and goings of each player
 */
#include "NetPlay.h"							// NETMSG, NETPLAYERID

extern BOOL sendVersionCheck();
extern BOOL recvVersionCheck(NETMSG* pMsg);
extern BOOL intDisplayMultiJoiningStatus(UBYTE joinCount);
extern BOOL MultiPlayerLeave(NETPLAYERID dp); // A player has left the game.
extern BOOL MultiPlayerJoin(NETPLAYERID dp); // A Player has joined the game.
extern VOID setupNewPlayer(NETPLAYERID dpid, UDWORD player); // stuff to do when player joins.
//extern BOOL UpdateClient				(NETPLAYERID dest, UDWORD playerToSend);// send info about another player
extern VOID clearPlayer(UDWORD player, BOOL quietly, BOOL removeOil); // wipe a player off the face of the earth.

typedef struct
{
  DROID* psDroid;
  VOID* psNext;
} DROIDSTORE, *LPDROIDSTORE;

extern DROIDSTORE* tempDroidList;
