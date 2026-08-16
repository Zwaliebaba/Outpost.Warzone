/***************************************************************************/
/*
 * FileRequester.h
 *
 * The ten-slot file requester popup. It does no file I/O itself: it lists
 * what matches a path and extension, and returns the name the player picked
 * or typed.
 *
 * It was the save/load screen, which is why the game once had a module named
 * for an operation the widget never performed. Save/load went by owner
 * decision (2026-08-16); the multiplayer force picker was always the same
 * widget behind two more modes, and is what is left.
 */
/***************************************************************************/

#ifndef _filerequester_h
#define _filerequester_h

/***************************************************************************/
/*
 *	Global Definitions
 */
/***************************************************************************/

using REQUESTER_MODE = enum _requester_mode
{
  LOAD_FORCE,
  SAVE_FORCE
};

/***************************************************************************/
/*
 *	Global Variables
 */
/***************************************************************************/

extern BOOL bRequesterUp; // true when interface is up and should be run.
extern STRING sRequestResult[255];
extern BOOL bRequestLoad;

/***************************************************************************/
/*
 *	Global ProtoTypes
 */
/***************************************************************************/

extern BOOL requesterOpen(REQUESTER_MODE mode, CHAR* defaultdir, CHAR* extension, CHAR* title);
extern BOOL requesterClose(VOID);
extern BOOL requesterRun(VOID);
extern BOOL requesterDisplay(VOID);

extern void removeWildcards(char* pStr);

#endif // _filerequester_h
