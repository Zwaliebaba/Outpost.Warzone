#include "pch.h"

/*
 * Window.cpp
 *
 * The application window, the Win32 message pump and the cursors.
 *
 * This was the top half of Frame.cpp. It is here because it is the half that
 * cannot exist without a display: it creates the window, pumps messages,
 * forwards them to the input layer and owns the cursor. Frame.cpp kept the
 * half that a headless build still needs - file loading and hashing - and the
 * frame counters moved to GTime.cpp, which core code reads.
 */

// defines the inline functions in this module
#define DEFINE_INLINE

#include "Frame.h"
#include "Window.h"
#include "GTime.h"
#include "Input.h"
#include "DXInput.h"
#include "FrameResource.h"
#include "FrameInt.h"
#include "RenderClip.h"

#include <assert.h>

#define MAX_CURSORS 26

using CURSOR_RESOURCE = struct _cursor_resource
{
  WORD resID;
  HCURSOR hCursor;
};

/* Program hInstance */
HINSTANCE hInstance;

/* Handle for the main window */
HWND hWndMain;

// window class name
#define WINDOW_CLASS_NAME	"Framework"

/* Handle for the cursor */
static HCURSOR hCursor;
/* Handle for the internal cursor */
static HCURSOR hInternalCursor;

static WORD currentCursorResID = UWORD_MAX;
static SDWORD nextCursor;
CURSOR_RESOURCE aCursors[MAX_CURSORS];

/* The default window function */
static DEFWINPROCTYPE frameWinProc;

/* Stores whether a windows quit message has been received */
static BOOL winQuit = FALSE;

using FOCUS_STATE = enum _focus_state
{
  FOCUS_OUT,
  // Window does not have the focus
  FOCUS_SET,
  // Just received WM_SETFOCUS
  FOCUS_IN,
  // Window has got the focus
  FOCUS_KILL,
  // Just received WM_KILLFOCUS
};

FOCUS_STATE focusState, focusLast;

/* Whether the mouse is currently being displayed or not */
static BOOL mouseOn = TRUE;

/* Whether the mouse should be displayed in the app workspace */
static BOOL displayMouse = TRUE;

/* Graphics data for a default bitmap */
#define DEF_CURSOR_WIDTH	32
#define DEF_CURSOR_HEIGHT	32
#define DEF_CURSOR_X		0
#define DEF_CURSOR_Y		0
static UDWORD aCursorData[DEF_CURSOR_HEIGHT] = {
  0x00000000, 0x00000000, 0x00000040, 0x00000060, 0x00000070, 0x00000078, 0x0000007C, 0x0000007E, 0x0000007F, 0x0000807F, 0x0000007C,
  0x0000006C, 0x00000046, 0x00000006, 0x00000003, 0x00000003, 0x00008001, 0x00008001, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static UDWORD aCursorMask[DEF_CURSOR_HEIGHT] = {
  0xffffff7f, 0xffffff3f, 0xffffff1f, 0xffffff0f, 0xffffff07, 0xffffff03, 0xffffff01, 0xffffff00, 0xffff7f00, 0xffff3f00, 0xffff1f00,
  0xffffff01, 0xffffff10, 0xffffff30, 0xffff7f78, 0xffff7ff8, 0xffff3ffc, 0xffff3ffc, 0xffff7ffe, 0xffffffff, 0xffffffff, 0xffffffff,
  0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
};
/* Return the handle for the application window */
HWND frameGetWinHandle(void) { return hWndMain; }

/* If cursor on is TRUE the windows cursor will be displayed over the game window
 * (and in full screen mode).  If it is FALSE the cursor will not be displayed.
 */
void frameShowCursor(BOOL cursorOn) { displayMouse = cursorOn; }

/* Set the current cursor from a cursor handle */
void frameSetCursor(HCURSOR hNewCursor)
{
  if (hNewCursor == nullptr)
  {
    DEBUG_ASSERT_TEXT(FALSE, "frameSetCursor: NULL cursor handle");
    return;
  }
  hCursor = hNewCursor;
  SetCursor(hCursor);
}

/* Set the current cursor from a Resource ID */
void frameSetCursorFromRes(WORD resID)
{
  HCURSOR hNewCursor = nullptr;

  DEBUG_ASSERT_TEXT(resID != 0, "frameSetCursorFromRes: null resource ID");

  //If we are already using this cursor then  return
  if (resID != currentCursorResID)
  {
    //if its loaded then get cursor handle
    for (SDWORD i = 0; (i < nextCursor && hNewCursor == nullptr); i++)
    {
      if (aCursors[i].resID == resID)
        hNewCursor = aCursors[i].hCursor;
    }

    //if cursor wasnt loaded, load the cursor and add it to array
    if (hNewCursor == nullptr)
    {
      DEBUG_ASSERT_TEXT(nextCursor < MAX_CURSORS, "frameSetCursorFromRes: Attempting to load too many cursors\n");

      if (nextCursor >= MAX_CURSORS)
        nextCursor = MAX_CURSORS - 1;
      hNewCursor = LoadCursor(hInstance, MAKEINTRESOURCE(resID));
      if (hNewCursor != nullptr)
      {
        //store it
        aCursors[nextCursor].resID = resID;
        aCursors[nextCursor].hCursor = hNewCursor;
        nextCursor++;
      }
    }

    DEBUG_ASSERT_TEXT(hNewCursor != NULL, "frameSetCursorFromRes: LoadCursor failed:\n");

    //if we got a new cursor set it
    if (hNewCursor != nullptr)
    {
      frameSetCursor(hNewCursor);
      currentCursorResID = resID;
    }
  }
}

/*
 * Wndproc
 *
 * The windows message processing function.
 */
/* LRESULT, not long: LRESULT is LONG_PTR, so on x64 it is 64 bits wide and
 * a long return truncates every value forwarded from DefWindowProc and
 * frameWinProc.  On Win32 the two types are identical, so this is a no-op
 * there.  CALLBACK is the portable spelling of the PASCAL/__stdcall this
 * carried, which x64 ignores. */
static LRESULT CALLBACK Wndproc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  SDWORD res;

  if ((message >= WM_MOUSEFIRST) && (message <= WM_MOUSELAST) || (message >= WM_KEYFIRST) && (message <= WM_KEYLAST))
  {
    inputProcessMessages(message, wParam, lParam);
    return 0;
  }
  switch (message)
  {
  case WM_SETFOCUS: 
    if (focusState != FOCUS_IN)
    {
      focusState = FOCUS_SET;
      DInpMouseAcc(DINP_MOUSEACQUIRE);
    }
    return 0;
  case WM_KILLFOCUS: 
    if (focusState != FOCUS_OUT)
    {
      focusState = FOCUS_KILL;
      DInpMouseAcc(DINP_MOUSERELEASE);
    }
    /* Have to tell the input system that we've lost focus */
    inputProcessMessages(message, wParam, lParam);
    return 0;

  case WM_SETCURSOR:
    {
      if (LOWORD(lParam) == HTCLIENT)
      {
        /* Turn off the cursor if necessary */
        if (!displayMouse && mouseOn)
        {
          res = ShowCursor(FALSE);
          if (res >= 0)
          {
            DEBUG_ASSERT_TEXT(FALSE, "WM_SETCURSOR off: cursor count out of sync");
            while (ShowCursor(FALSE) >= 0) { ; }
          }
          mouseOn = FALSE;
        }
        SetCursor(hCursor);
        return 0;
      }
      if (LOWORD(lParam) == HTCAPTION)
      {
        if (!mouseOn)
        {
          res = ShowCursor(TRUE);
          if (res < 0)
          {
            DEBUG_ASSERT_TEXT(FALSE, "WM_SETCURSOR on: cursor count out of sync");
            while (ShowCursor(FALSE) < 0) { ; }
          }
          mouseOn = TRUE;
        }
        SetCursor(hInternalCursor);
        return 0;
      }
    }
    break;
  case WM_CLOSE:
    /* Request for the application to end */
    res = MessageBox(hWndMain, "Do you want to quit?", "Confirmation", MB_ICONQUESTION | MB_YESNO);
    if (res == IDYES)
      winQuit = TRUE;
    return 0;
  case WM_DESTROY:
    /* Shut down the game and quit */
    winQuit = TRUE;
    return 0;
  case WM_SIZE:
    /* Just ignore this */
    return 0;
  case WM_ERASEBKGND:
    // Tell windows we have erased the background - cos we will when we draw next.
    return 1;
  default:
    break;
  }

  /* Default behaviour for any messages not dealt with above */
  if (frameWinProc)
    return frameWinProc(hWnd, message, wParam, lParam);

  // No extra window procedure set, use the default one
  return DefWindowProc(hWnd, message, wParam, lParam);
}

/* The default window procedure for the library.
 * This is initially set to the standard DefWindowProc, but can be changed
 * by this function
 */
extern void frameSetWindowProc(DEFWINPROCTYPE winProc) { frameWinProc = winProc; }

/*
 * SetDpiAwareness
 *
 * Without this a scaled desktop reports virtualised metrics - a 1920x1080
 * display at 125% would claim to be 1536x864 - and the compositor stretches
 * the window back up, blurrily. Both calls are resolved at run time: the
 * context call needs Windows 10 1703, SetProcessDPIAware is the fallback.
 */
static void SetDpiAwareness(void)
{
#ifdef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
  using SetContextFn = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);

  HMODULE hUser32 = GetModuleHandle("user32.dll");
  if (hUser32 != nullptr)
  {
    SetContextFn pSetContext = reinterpret_cast<SetContextFn>(GetProcAddress(hUser32, "SetProcessDpiAwarenessContext"));
    if (pSetContext != nullptr && pSetContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != nullptr)
      return;
  }
#endif
  (void)SetProcessDPIAware();
}

/*
 * ChooseDisplayScale
 *
 * Pick the integer factor every drawn coordinate is multiplied by. The UI
 * lays itself out on a 640x480-anchored logical canvas; the factor is the
 * largest whole number that leaves that canvas at least 960x540, so the
 * interface keeps roughly the same physical size whatever the pixel density.
 * A display too small for that draws unscaled.
 */
static UDWORD ChooseDisplayScale(UDWORD _widthPx, UDWORD _heightPx)
{
  UDWORD scale = 1;

  while ((_widthPx / (scale + 1) >= 960) && (_heightPx / (scale + 1) >= 540))
    scale += 1;

  return scale;
}

/*
 * winInitApp
 *
 * Do that Windows initialization thang...
 */

static BOOL winInitApp(HANDLE hInstance, // Instance handle for the program
                       STRING* pWindowName, // The text to put on the window title bar
                       UDWORD width, // The window width
                       UDWORD height) // The window height
{
  WNDCLASS wc;
  STRING* pMsgBuf;

  /* Create the default cursor for the app - a simple arrow */
  hInternalCursor = CreateCursor(static_cast<HINSTANCE>(hInstance), DEF_CURSOR_X, DEF_CURSOR_Y, DEF_CURSOR_WIDTH,DEF_CURSOR_HEIGHT,
                                 aCursorMask, aCursorData);
  if (hInternalCursor == nullptr)
  {
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, nullptr, GetLastError(),
                  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
                  (LPTSTR)&pMsgBuf, 0, nullptr);
    Neuron::Fatal("Create Cursor failed:\n{}", pMsgBuf);
    // Free the message buffer.
    LocalFree(pMsgBuf);

    return FALSE;
  }
  hCursor = hInternalCursor;

  /* Create a windows class for the application */
  wc.style = CS_DBLCLKS | // Want to get double click messages
    CS_PARENTDC; // Single DC for the window - should speed things
  // up a bit
  wc.lpfnWndProc = Wndproc;
  wc.cbClsExtra = 0;
  wc.cbWndExtra = 0;
  wc.hInstance = static_cast<HINSTANCE>(hInstance);
  wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
  wc.hCursor = nullptr; //hCursor;
  wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH)); //(COLOR_WINDOW+1); //GetStockObject( WHITE_BRUSH );
  wc.lpszMenuName = nullptr;
  wc.lpszClassName = WINDOW_CLASS_NAME;

  BOOL rc = RegisterClass(&wc);
  if (!rc)
  {
    Neuron::Fatal("Failed to register windows class");
    return FALSE;
  }

  /* Create the main window: borderless, covering the desktop. A popup
   * window has no non-client area, so the client area is the window and
   * nothing needs adjusting for title bars. WS_EX_APPWINDOW keeps it on
   * the task bar. */
  hWndMain = CreateWindowEx(WS_EX_APPWINDOW, "Framework", pWindowName, WS_POPUP | WS_VISIBLE,
                            0, 0, // Initial window location
                            width, height, // Initial window size
                            nullptr, nullptr, static_cast<HINSTANCE>(hInstance), nullptr);

  if (!hWndMain)
  {
    Neuron::Fatal("Couldn't create main window.");
    return FALSE;
  }

  /* Store the default window procedure */
  frameWinProc = nullptr;

  return TRUE;
}

/*
 * frameInitialise
 *
 * Initialise the framework library. - PC version
 *
 * The display is the desktop: a borderless window covering it, at its
 * resolution, with a windowed swap chain - so no display mode ever changes.
 * The game lays out on the logical canvas this derives (the desktop size
 * divided by the display scale) and the renderer multiplies back up.
 */
BOOL frameInitialise(HANDLE hInst, // The windows application instance
                     STRING* pWindowName) // The text to appear in the window title bar
{
  HWND hWndPrev;
  UDWORD width, height, scale;

  /* exit if existing window with pWindowName name (i.e. only run one version) */
  if ((hWndPrev = FindWindow(WINDOW_CLASS_NAME, pWindowName)) != nullptr)
  {
    SetForegroundWindow(hWndPrev);
    return FALSE;
  }

  winQuit = FALSE;
  focusState = FOCUS_IN;
  focusLast = FOCUS_IN;
  mouseOn = TRUE;
  displayMouse = TRUE;
  hInstance = static_cast<HINSTANCE>(hInst);

  /* Must come before any metrics are read or any window exists. */
  SetDpiAwareness();

  /* The physical display is the desktop */
  width = static_cast<UDWORD>(GetSystemMetrics(SM_CXSCREEN));
  height = static_cast<UDWORD>(GetSystemMetrics(SM_CYSCREEN));
  if (width < 640)
    width = 640;
  if (height < 480)
    height = 480;

  /* The logical canvas the game sees */
  scale = ChooseDisplayScale(width, height);
  Neuron::SetDisplayScale(scale);
  (void)pie_SetVideoBufferWidth(width / scale);
  (void)pie_SetVideoBufferHeight(height / scale);

  /* Initialise the windows stuff and open a window */
  if (!winInitApp(hInstance, pWindowName, width, height))
    return FALSE;

  /* Create the Direct3D device and its swap chain */
  if (!screenInitialise(width, height, TRUE, hWndMain))
    return FALSE;
  /* Initialise the input system */
  inputInitialise();
  /* Initialise the frame rate stuff */
  gtimeFrameCountInit();

  // Initialise the resource stuff
  if (!resInitialise())
    return FALSE;

  return TRUE;
}

/*
 * frameUpdate
 *
 * Call this each cycle to allow the framework to deal with
 * windows messages, and do general house keeping.
 *
 * Returns FRAME_STATUS.
 */
FRAME_STATUS frameUpdate(void)
{
  MSG sMsg;

  /* Tell the input system about the start of another frame */
  inputNewFrame();

  /* Deal with any windows messages */
  while (PeekMessage(&sMsg, nullptr, 0, 0, PM_REMOVE))
  {
    if (sMsg.message == WM_QUIT)
      break;
    TranslateMessage(&sMsg);
    (void)DispatchMessage(&sMsg);
  }

  /* Now figure out what to return */
  FRAME_STATUS retVal = FRAME_OK;
  if (winQuit)
    retVal = FRAME_QUIT;
  else if ((focusState == FOCUS_SET) && (focusLast == FOCUS_OUT))
  {
    focusState = FOCUS_IN;
    retVal = FRAME_SETFOCUS;
  }
  else if ((focusState == FOCUS_KILL) && (focusLast == FOCUS_IN))
  {
    focusState = FOCUS_OUT;
    retVal = FRAME_KILLFOCUS;
  }

  if ((focusState == FOCUS_SET) || (focusState == FOCUS_KILL))
  {
    /* Got a SET or KILL when we were already in or out of
       focus respectively */
    focusState = focusLast;
  }
  else if (focusLast != focusState)
  {
    focusLast = focusState;
  }

  /* If things are running normally, update the framerate */
  if ((!winQuit) && (focusState == FOCUS_IN))
  {
    /* Update the frame rate stuff */
    gtimeFrameCountUpdate();
  }

  return retVal;
}

void frameShutDown(void)
{
  surfShutDown();
  screenShutDown();

  /* Free the default cursor */
  DestroyCursor(hCursor);

  /* Destroy the Application window */
  DestroyWindow(hWndMain);

  // Shutdown the resource stuff
  resShutDown();

  //unregister the windows class (incase we want to recreate it
  UnregisterClass(WINDOW_CLASS_NAME, hInstance);
}

