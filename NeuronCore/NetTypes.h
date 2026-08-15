/*
 * NetTypes.h
 *
 * How a player is identified, and nothing else.
 *
 * This exists so that a header wanting to name a player does not have to pull
 * in NetPlay.h, and through it dplay.h. MultiInt.h and MultiPlay.h both used
 * to spell the type DWORD for exactly that reason, each with a comment saying
 * it was really a DPID -- and because DPID was a typedef for DWORD, the two
 * agreed by accident until the type was given a name of its own, at which
 * point kickPlayer and player2dpid stopped linking.
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

#endif	// _NETTYPES_H_

/***************************************************************************/
