/***************************************************************************/
/* 
 * loadsave.h
 */
/***************************************************************************/

#ifndef _loadsave_h
#define _loadsave_h

/***************************************************************************/
/*
 *	Global Definitions
 */
/***************************************************************************/

/* Save/load of games is gone (owner decision, 2026-08-16 -- the game is
 * heading to a server-authoritative MMO shape and has no user saves). What
 * survives of the old ten-slot requester is the force picker, which was
 * always the same widget behind two extra modes.
 */
using LOADSAVE_MODE = enum _loadsave_mode
{
  LOAD_FORCE,
  SAVE_FORCE
};

/***************************************************************************/
/*
 *	Global Variables
 */
/***************************************************************************/

extern BOOL bLoadSaveUp; // true when interface is up and should be run.
extern STRING sRequestResult[255];
extern BOOL bRequestLoad;

/***************************************************************************/
/*
 *	Global ProtoTypes
 */
/***************************************************************************/

extern void drawBlueBox(UDWORD x, UDWORD y, UDWORD w, UDWORD h);

extern BOOL addLoadSave(LOADSAVE_MODE mode, CHAR* defaultdir, CHAR* extension, CHAR* title);
extern BOOL closeLoadSave(VOID);
extern BOOL runLoadSave(VOID);
extern BOOL displayLoadSave(VOID);

extern void removeWildcards(char* pStr);
#endif/_loadsave_h
