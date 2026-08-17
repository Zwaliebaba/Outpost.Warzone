/*
 * NetTypes.h
 *
 * The vocabulary the networking headers share, and nothing else.
 *
 * This exists so that a header wanting to name a player, or a message size,
 * does not have to pull in NetPlay.h. It began as somewhere to put the player
 * id: MultiInt.h and MultiPlay.h both used to spell the type DWORD for exactly
 * that reason, each with a comment saying it was really a DPID -- and because
 * DPID was a typedef for DWORD, the two agreed by accident until the type was
 * given a name of its own, at which point kickPlayer and player2dpid stopped
 * linking.
 *
 * It now also holds the size constants, so that Transport.h can name them
 * without including NetPlay.h and NetPlay.h can include Transport.h. They
 * were in NetPlay.h and the two headers were a cycle away from each other.
 *
 * Deliberately includes nothing.
 */

#ifndef _NETTYPES_H_
#define _NETTYPES_H_

/***************************************************************************/

/* Was DirectPlay's DPID, which is a DWORD, so the width -- and therefore the
 * bytes NetAdd puts on the wire -- is unchanged.
 */
using NETPLAYERID = unsigned int;

/***************************************************************************/

#define MaxNumberOfPlayers	8					// max number of players in a game.
#define MaxMsgSize			8000				// max size of a message in bytes.
#define	StringSize			64					// size of strings used.
#define MaxGames			12					// max number of concurrently playable games to allow.
// longest filename a transferred file may carry. The length travels as one
// byte, so this cannot exceed 255 whatever else it is set to.
#define MaxFileNameChars	127

/***************************************************************************/

#endif	// _NETTYPES_H_

/***************************************************************************/
