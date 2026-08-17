/*
 * Window.h
 *
 * The application window, the Win32 message pump and the cursors.
 *
 * This was the display half of Frame.h. Frame.h kept what a headless build
 * still needs - file loading, hashing, the integer percentages - and the frame
 * counters went to GTime.h, because core code stamps its logs with them.
 */
#ifndef _window_h
#define _window_h

#pragma warning (disable : 4201 4214 4115 4514)
#include <windows.h>
#pragma warning (default : 4201 4214 4115)

#include "Types.h"

/* Initialise the frame work library. The display is always a borderless
 * window covering the desktop at its resolution; the game's logical canvas
 * and the display scale between the two are derived from it here.
 */
extern BOOL frameInitialise(HANDLE hInstance, // The windows application instance
                            STRING* pWindowName); // The text to appear in the window title bar

/* Shut down the framework library.
 * This clears up all the Direct Draw stuff and ensures
 * that Windows gets restored properly after Full screen mode.
 */
extern void frameShutDown(void);

/* The current status of the framework */
using FRAME_STATUS = enum _frame_status
{
  FRAME_OK,
  // Everything normal
  FRAME_KILLFOCUS,
  // The main app window has lost focus (might well want to pause)
  FRAME_SETFOCUS,
  // The main app window has focus back
  FRAME_QUIT,
  // The main app window has been told to quit
};

/* Call this each cycle to allow the framework to deal with
 * windows messages, and do general house keeping.
 *
 * Returns FRAME_STATUS.
 */
extern FRAME_STATUS frameUpdate(void);

/* If cursor on is TRUE the windows cursor will be displayed over the game window
 * (and in full screen mode).  If it is FALSE the cursor will not be displayed.
 */
extern void frameShowCursor(BOOL cursorOn);

/* Set the current cursor from a cursor handle */
extern void frameSetCursor(HCURSOR hNewCursor);

/* Set the current cursor from a Resource ID
 * This is the same as calling:
 *       frameSetCursor(LoadCursor(MAKEINTRESOURCE(resID)));
 * but with a bit of extra error checking.
 */
extern void frameSetCursorFromRes(WORD resID);

/* The handle for the application window */
extern HWND frameGetWinHandle(void);

/* The default window procedure for the library.
 * This is initially set to the standard DefWindowProc, but can be changed
 * by this function.
 * Call this function with NULL to reset to DefWindowProc.
 */
using DEFWINPROCTYPE = LRESULT(*)(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
extern void frameSetWindowProc(DEFWINPROCTYPE winProc);

#endif
