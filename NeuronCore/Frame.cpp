#include "pch.h"

// defines the inline functions in this module
#define DEFINE_INLINE

// window focus messages 
#include "Frame.h"
#include "FrameInt.h"
#include "WDG.h"

#include "Fractions.h"

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

/* Flag if directdraw is active*/
static BOOL bActiveDDraw;

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

/************************************************************************************
 *
 * Alex's frame rate stuff
 */

/* Over how many seconds is the average required? */
#define	TIMESPAN	5

/* Initial filler value for the averages - arbitrary */
#define  IN_A_FRAME 70

/* Global variables for the frame rate stuff */
static SDWORD FrameCounts[TIMESPAN];
static SDWORD Frames; // Number of frames elapsed since start
static SDWORD LastFrames;
static SDWORD RecentFrames; // Number of frames in last second
static SDWORD PresSeconds; // Number of seconds since execution started
static SDWORD LastSeconds;
static SDWORD FrameRate; // Average frame rate since start
static SDWORD FrameIndex;
static SDWORD Total;
static SDWORD RecentAverage; // Average frame rate over last TIMSPAN seconds

/* InitFrameStuff - needs to be called once before frame loop commences */
static void InitFrameStuff(void)
{
  for (SDWORD i = 0; i < TIMESPAN; i++)
    FrameCounts[i] = IN_A_FRAME;

  Frames = 0;
  LastFrames = 0;
  RecentFrames = 0;
  RecentAverage = 0;
  PresSeconds = 0;
  LastSeconds = 0;
  LastSeconds = PresSeconds;
  FrameIndex = 0;
}

/* MaintainFrameStuff - call this during completion of each frame loop */
static void MaintainFrameStuff(void)
{
  PresSeconds = clock() / CLOCKS_PER_SEC;
  if (PresSeconds != LastSeconds)
  {
    LastSeconds = PresSeconds;
    RecentFrames = Frames - LastFrames;
    LastFrames = Frames;
    FrameCounts[FrameIndex++] = RecentFrames;
    if (FrameIndex >= TIMESPAN)
      FrameIndex = 0;

    Total = 0;
    for (SDWORD i = 0; i < TIMESPAN; i++)
      Total += FrameCounts[i];
    RecentAverage = Total / TIMESPAN;

    FrameRate = Frames / PresSeconds;
  }
  Frames++;
}

/* Return the current frame rate */
UDWORD frameGetFrameRate(void) { return RecentAverage; }

/* Return the overall frame rate */
UDWORD frameGetOverallRate(void) { return FrameRate; }

/* Return the frame rate for the last second */
UDWORD frameGetRecentRate(void) { return RecentFrames; }

UDWORD frameGetFrameNumber(void) { return Frames; }

/* Return the handle for the application window */
HWND frameGetWinHandle(void) { return hWndMain; }

// string buffer for windows error string
static STRING winErrorString[255];

// Return a string for a windows error code
STRING* winErrorToString(SDWORD error)
{
  LPVOID lpMsgBuf;

  FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                // Default language
                (LPTSTR)&lpMsgBuf, 0, nullptr);

  // Copy the string.
  strncpy(winErrorString, static_cast<const char*>(lpMsgBuf), 254);
  winErrorString[255] = '0';

  // Free the buffer.
  LocalFree(lpMsgBuf);

  return winErrorString;
}

/* If cursor on is TRUE the windows cursor will be displayed over the game window
 * (and in full screen mode).  If it is FALSE the cursor will not be displayed.
 */
void frameShowCursor(BOOL cursorOn) { displayMouse = cursorOn; }

/* Set the current cursor from a cursor handle */
void frameSetCursor(HCURSOR hNewCursor)
{
  if (hNewCursor == nullptr)
  {
    ASSERT_TEXT(FALSE,"frameSetCursor: NULL cursor handle");
    return;
  }
  hCursor = hNewCursor;
  SetCursor(hCursor);
}

/* Set the current cursor from a Resource ID */
void frameSetCursorFromRes(WORD resID)
{
  HCURSOR hNewCursor = nullptr;

  ASSERT_TEXT(resID != 0, "frameSetCursorFromRes: null resource ID");

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
      ASSERT_TEXT(nextCursor < MAX_CURSORS,"frameSetCursorFromRes: Attempting to load too many cursors\n");

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

    ASSERT_TEXT(hNewCursor != NULL, "frameSetCursorFromRes: LoadCursor failed:\n");

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
static long FAR PASCAL Wndproc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
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
            ASSERT_TEXT(FALSE,"WM_SETCURSOR off: cursor count out of sync");
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
            ASSERT_TEXT(FALSE,"WM_SETCURSOR on: cursor count out of sync");
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
  RECT sWinSize;
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

  /* Get the actual size of window we want (including the size of
     title bars etc.) */
  (void)SetRect(&sWinSize, 0, 0, width, height);
  (void)AdjustWindowRectEx(&sWinSize, WIN_STYLE, FALSE, WIN_EXSTYLE);

  /* The rectangle returned has values for the window edges relative to
     the display area origin, i.e. left and top are negative - so we have
     to adjust */
  sWinSize.right -= sWinSize.left;
  sWinSize.left = 0;
  sWinSize.bottom -= sWinSize.top;
  sWinSize.top = 0;

  /* Create the main window */
  hWndMain = CreateWindowEx(WIN_EXSTYLE, // Extended window style, defined in WinMain.h
                            "Framework", pWindowName, WIN_STYLE, // Window style, defined in WinMain.h
                            0, 0, // Initial window location
                            sWinSize.right, sWinSize.bottom, // Initial window size
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
 */
BOOL frameInitialise(HANDLE hInst, // The windows application instance
                     STRING* pWindowName, // The text to appear in the window title bar
                     UDWORD width, // The display width
                     UDWORD height, // The display height
                     UDWORD bitDepth, // The display bit depth
                     BOOL fullScreen, // Whether to start full screen or windowed
                     BOOL bVidMem) // Whether to put surfaces in video memory
{
  HWND hWndPrev;

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
  bActiveDDraw = TRUE;

  /* Initialise the trig stuff */
  if (!trigInitialise())
    return FALSE;
  /* Initialise the windows stuff and open a window */
  if (!winInitApp(hInstance, pWindowName, width, height))
    return FALSE;

  /* Initialise the Direct Draw Buffers */
  if (!screenInitialise(width, height, bitDepth, fullScreen, bVidMem, bActiveDDraw, hWndMain))
    return FALSE;
  /* Initialise the input system */
  inputInitialise();
  /* Initialise the frame rate stuff */
  InitFrameStuff();

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

  /* If things are running normally, restore the surfaces and update the framerate */
  if ((!winQuit) && (focusState == FOCUS_IN))
  {
    /* Restore any surfaces that have been lost */
    if (bActiveDDraw)
      screenRestoreSurfaces();

    /* Update the frame rate stuff */
    MaintainFrameStuff();
  }

  return retVal;
}

void frameShutDown(void)
{
  if (bActiveDDraw)
  {
    surfShutDown();

    screenShutDown();
  }
  else { RELEASE(psDD); }

  /* Free the default cursor */
  DestroyCursor(hCursor);

  /* Destroy the Application window */
  DestroyWindow(hWndMain);

  /* shutdown the trig stuff */
  trigShutDown();

  // Shutdown the resource stuff
  resShutDown();

  //unregister the windows class (incase we want to recreate it
  UnregisterClass(WINDOW_CLASS_NAME, hInstance);
}

BOOL loadFile(STRING* pFileName, UBYTE** ppFileData, UDWORD* pFileSize) { return (loadFile2(pFileName, ppFileData, pFileSize,TRUE)); }

/* Load the file with name pointed to by pFileName into a memory buffer. */
// if allocate mem is true then the memory is allocated ... else it is already in ppFileData, and the max size is in pFileSize ... this is adjusted to the actual loaded file size
//   
BOOL loadFile2(STRING* pFileName, UBYTE** ppFileData, UDWORD* pFileSize, BOOL AllocateMem)
{
  FILE* pFileHandle;
  UDWORD FileSize;

  // First we try to see if we can load the file from the freedata section of the current WDG

  BOOL res = loadFileFromWDG(pFileName, ppFileData, pFileSize,WDG_ALLOCATEMEM);
  // loaded from WDG file, and allocate memory for it
  if (res == TRUE)
    return TRUE;

  // Not in WDG so we try to load it the old fashion way !
  // This will never work on the final build of the PSX because we can *ONLY* load files
  // directly from CD, i.e. from the WDG's normal fopen/fread calls will never work!
#ifdef DEBUG
  Neuron::DebugTrace("FOPEN ! {}\n",pFileName);

#endif

#ifdef FINALBUILD
  return FALSE;
#endif

  // Not needed in a PSX FINALBUILD.
  pFileHandle = fopen(pFileName, "rb");
  if (pFileHandle == nullptr)
  {
    Neuron::Fatal("Couldn't open {}", pFileName);
    return FALSE;
  }

  /* Get the length of the file */
  if (fseek(pFileHandle, 0, SEEK_END) != 0)
  {
    Neuron::Fatal("SEEK_END failed for {}", pFileName);
    return FALSE;
  }
  FileSize = ftell(pFileHandle);
  if (fseek(pFileHandle, 0, SEEK_SET) != 0)
  {
    Neuron::Fatal("SEEK_SET failed for {}", pFileName);
    return FALSE;
  }

  if (AllocateMem == TRUE)
  {
    /* Allocate a buffer to store the data and a terminating zero */
    // we don't want this popping up in the tools (makewdg)
    *ppFileData = new (std::nothrow) UBYTE[(FileSize) + 1];
    if (*ppFileData == nullptr)
    {
      Neuron::Fatal("Out of memory");
      return FALSE;
    }
  }
  else
  {
    if (FileSize > *pFileSize)
    {
      Neuron::Fatal("no room for file");
      return FALSE;
    }
    assert(*ppFileData!=NULL);
  }
  /* Load the file data */
  if (fread(*ppFileData, 1, FileSize, pFileHandle) != FileSize)
  {
    Neuron::Fatal("Read failed for {}", pFileName);
    return FALSE;
  }

  if (fclose(pFileHandle) != 0)
  {
    Neuron::Fatal("Close failed for {}", pFileName);
    return FALSE;
  }

  // Add the terminating zero
  *((*ppFileData) + FileSize) = 0;
  *pFileSize = FileSize; // always set to correct size
  return TRUE;
}

// load a file from disk into a fixed memory buffer
BOOL loadFileToBuffer(STRING* pFileName, UBYTE* pFileBuffer, UDWORD bufferSize, UDWORD* pSize)
{
  DWORD bytesRead;

  UDWORD FileSize;

  BOOL res = loadFileFromWDG(pFileName, &pFileBuffer, &FileSize,WDG_USESUPPLIED);
  // loaded from WDG file, and allocate memory for it	if (res==TRUE)	return TRUE;
  if (res == TRUE)
  {
    *pSize = FileSize;
    return TRUE;
  }

  // try and open the file
  HANDLE hFile = CreateFile(pFileName, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (hFile == INVALID_HANDLE_VALUE)
  {
    Neuron::Fatal("Couldn't open {}\n{}", pFileName, winErrorToString(GetLastError()));
    return FALSE;
  }

  // get the size of the file
  *pSize = GetFileSize(hFile, nullptr);
  if (*pSize >= bufferSize)
  {
    Neuron::Fatal("file too big !!:{} size {}\n", pFileName, *pSize);
    return FALSE;
  }

  // load the file into the buffer
  BOOL retVal = ReadFile(hFile, pFileBuffer, *pSize, &bytesRead, nullptr);
  if (!retVal || *pSize != bytesRead)
  {
    Neuron::Fatal("Couldn't read data from {}\n{}", pFileName, winErrorToString(GetLastError()));
    return FALSE;
  }
  pFileBuffer[*pSize] = 0;

  retVal = CloseHandle(hFile);
  if (!retVal)
  {
    Neuron::Fatal("Couldn't close {}\n{}", pFileName, winErrorToString(GetLastError()));
    return FALSE;
  }

  return TRUE;
}

// as above but returns quietly if no file found
BOOL loadFileToBufferNoError(STRING* pFileName, UBYTE* pFileBuffer, UDWORD bufferSize, UDWORD* pSize)
{
  DWORD bytesRead;

  UDWORD FileSize;

  BOOL res = loadFileFromWDG(pFileName, &pFileBuffer, &FileSize,WDG_USESUPPLIED);
  // loaded from WDG file, and allocate memory for it	if (res==TRUE)	return TRUE;
  if (res == TRUE)
  {
    *pSize = FileSize;
    return TRUE;
  }

  // try and open the file
  HANDLE hFile = CreateFile(pFileName, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (hFile == INVALID_HANDLE_VALUE)
    return FALSE;

  // get the size of the file
  *pSize = GetFileSize(hFile, nullptr);
  if (*pSize >= bufferSize)
    return FALSE;

  // load the file into the buffer
  BOOL retVal = ReadFile(hFile, pFileBuffer, *pSize, &bytesRead, nullptr);
  if (!retVal || *pSize != bytesRead)
    return FALSE;
  pFileBuffer[*pSize] = 0;

  retVal = CloseHandle(hFile);
  if (!retVal)
    return FALSE;

  return TRUE;
}

/* Save the data in the buffer into the given file */
BOOL saveFile(STRING* pFileName, UBYTE* pFileData, UDWORD fileSize)
{
  /* open the file */
  FILE* pFile = fopen(pFileName, "wb");
  if (!pFile)
  {
    Neuron::Fatal("Couldn't open {}", pFileName);
    return FALSE;
  }

  if (fwrite(pFileData, fileSize, 1, pFile) != 1)
  {
    Neuron::Fatal("Write failed for {}: {}", pFileName, winErrorToString(GetLastError()) );
    return FALSE;
  }

  if (fclose(pFile) != 0)
  {
    Neuron::Fatal("Close failed for {}", pFileName);
    return FALSE;
  }

  return TRUE;
}

static UBYTE* WDGCacheStart = nullptr;
static UDWORD WDGCacheSize = 0;

#define MAXWDGDIRSIZE (7300)

static UBYTE WDGDirectory[MAXWDGDIRSIZE]; // Directory for current WDG file (MALLOCed)

static SDWORD WDGCacheStartPos = -1;
static SDWORD WDGCacheEndPos = -1;

/* next four used in HashPJW */
#define	BITS_IN_int		32
#define	THREE_QUARTERS	((UINT) ((BITS_IN_int * 3) / 4))
#define	ONE_EIGHTH		((UINT) (BITS_IN_int / 8))
#define	HIGH_BITS		( ~((UINT)(~0) >> ONE_EIGHTH ))

///////////////////////////////////////////////////////////////////

/***************************************************************************/
/*
 * HashString
 *
 * Adaptation of Peter Weinberger's (PJW) generic hashing algorithm listed
 * in Binstock+Rex, "Practical Algorithms" p 69.
 *
 * Accepts string and returns hashed integer.
 */
/***************************************************************************/
UINT HashString(char* String)
{
  UINT iHashValue, i;
  auto c = String;

  assert(String!=NULL);
  assert(*String!=0x0);

  for (iHashValue = 0; *c; ++c)
  {
    iHashValue = (iHashValue << ONE_EIGHTH) + *c;

    if ((i = iHashValue & HIGH_BITS) != 0)
      iHashValue = (iHashValue ^ (i >> THREE_QUARTERS)) & ~HIGH_BITS;
  }

  return iHashValue;
}

UINT HashStringIgnoreCase(char* String)
{
  UINT iHashValue, i;
  auto c = String;

  assert(String!=NULL);
  assert(*String!=0x0);

  for (iHashValue = 0; *c; ++c)
  {
    iHashValue = (iHashValue << ONE_EIGHTH) + ((*c) & (0xdf));

    if ((i = iHashValue & HIGH_BITS) != 0)
      iHashValue = (iHashValue ^ (i >> THREE_QUARTERS)) & ~HIGH_BITS;
  }

  return iHashValue;
}

// Examine a filename for the last dot and slash
// and so giving the extension of the file and the directory
//
// PosOfDot and/of PosOfSlash can be NULL and then nothing will be stored
//
void ScanFilename(char* Fullname, int* PosOfDot, int* PosOfSlash)
{
  int DotPos = -1;
  int SlashPos = -1;
  int Pos;

  int Namelength = strlen(Fullname);

  for (Pos = Namelength; Pos >= 0; Pos--)
  {
    if (Fullname[Pos] == '.')
    {
      DotPos = Pos;
      break;
    }
  }

  for (Pos = Namelength; Pos >= 0; Pos--)
  {
    if (Fullname[Pos] == '\\')
    {
      SlashPos = Pos;
      break;
    }
  }

  if (PosOfDot != nullptr)
    *PosOfDot = DotPos;

  if (PosOfSlash != nullptr)
    *PosOfSlash = SlashPos;
}

#ifdef DEBUG

SDWORD PercentFunc(char* File, UDWORD Line, SDWORD a, SDWORD b)
{
  if (b)
    return (a * 100) / b;
  Neuron::DebugTrace("Divide by 0 (PERCENT) in {},line {}\n",File,Line);

  return 100;
}

SDWORD PerNumFunc(char* File, UDWORD Line, SDWORD range, SDWORD a, SDWORD b)
{
  if (b)
    return (a * range) / b;

  Neuron::DebugTrace("Divide by 0 (PERNUM) in {},line {}\n",File,Line);

  return range;
}

#endif
