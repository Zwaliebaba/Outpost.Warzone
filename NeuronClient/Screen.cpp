#include "pch.h"
/*
 * Screen.cpp
 *
 * The Direct3D 9 display.
 *
 * DirectDraw had a front buffer, a back buffer and a flip; D3D9 has a swap
 * chain and Present. The back buffer is created lockable so that the paths
 * that still write pixels by hand - the backdrop, the FMV frames, the debug
 * 2D display - keep working; everything else draws through the device.
 */

#include <stdio.h>

#pragma warning (disable : 4201 4214 4115 4514)
#define WIN32_LEAN_AND_MEAN
#define WIN32_EXTRA_LEAN
#include <windows.h>
#pragma warning (default : 4201 4214 4115)

#include "Frame.h"
#include "Window.h"
#include "DXError.h"
#include "Font.h"
#include "FrameInt.h"
#include "RenderClip.h"

/* The Current screen size and bit depth */
UDWORD screenWidth = 0;
UDWORD screenHeight = 0;
UDWORD screenDepth = 0;

/* The current screen mode (full screen/windowed) */
SCREEN_MODE screenMode = SCREEN_WINDOWED;

/* Which mode (of operation) the library is running in */
DISPLAY_MODES displayMode;

/* The handle for the main application window */
/* defined in Frame.cpp */

/* The Direct3D objects */
LPDIRECT3D9 psD3D = nullptr;
LPDIRECT3DDEVICE9 psD3DDevice = nullptr;

/* The parameters the device was created with. Kept so that the device can be
 * reset - after a lost device, or a switch between windowed and full screen -
 * without having to work them out again.
 */
static D3DPRESENT_PARAMETERS sPresentParams;

/* Whether the device was created at all. frameInitialise can be asked not to,
 * and the game then runs without a display.
 */
static BOOL bDeviceCreated = FALSE;

/* The back buffer surface, only held while it is locked */
static LPDIRECT3DSURFACE9 psLockedBackBuffer = nullptr;

/* The palette entries kept for palette matching. The display has not been
 * palettised since DirectDraw went, but the game still asks for the nearest
 * entry to an RGB value when it packs its own colours.
 */
#define PAL_MAX				256
static PALETTEENTRY asPalEntries[PAL_MAX];

/* The pixel format of the display, and of the back buffer. Both are
 * X8R8G8B8 - they are kept separate because the callers still ask for each.
 */
SCREEN_PIXELFORMAT sFrontBufferPixelFormat;
SCREEN_PIXELFORMAT sBackBufferPixelFormat;

// The current flip state
FLIP_STATE screenFlipState;

// The critical section for the screen flipping
CRITICAL_SECTION sScreenFlipCritical;

// The semaphore for the screen flipping
HANDLE hScreenFlipSemaphore;

//backDrop
#define BACKDROP_WIDTH	640
#define BACKDROP_HEIGHT	480
UWORD* pBackDropData = nullptr;
BOOL bBackDrop = FALSE;
BOOL bUpload = FALSE;

//fog
DWORD fogColour = 0;

/* The current colour for line drawing */
static UDWORD lineColour = 0;

/* The current colour for text drawing */
static UDWORD textColour = 0;

/* The current fill colour */
static UDWORD fillColour = 0;

static UDWORD backDropWidth = BACKDROP_WIDTH;
static UDWORD backDropHeight = BACKDROP_HEIGHT;

/* The pixel format every surface and buffer in the framework uses */
#define SCREEN_D3DFORMAT	D3DFMT_X8R8G8B8

/*********************************************************/
/* Device creation                                       */
/*********************************************************/

/* Fill in sFrontBufferPixelFormat/sBackBufferPixelFormat for X8R8G8B8 */
static void setPixelFormats(void)
{
  sBackBufferPixelFormat.bitCount = 32;
  sBackBufferPixelFormat.rMask = 0x00ff0000;
  sBackBufferPixelFormat.gMask = 0x0000ff00;
  sBackBufferPixelFormat.bMask = 0x000000ff;
  sBackBufferPixelFormat.aMask = 0x00000000;

  sFrontBufferPixelFormat = sBackBufferPixelFormat;
}

/* Fill in the present parameters for the current screen mode and size */
static void setPresentParameters(void)
{
  memset(&sPresentParams, 0, sizeof(D3DPRESENT_PARAMETERS));

  sPresentParams.BackBufferWidth = screenWidth;
  sPresentParams.BackBufferHeight = screenHeight;
  sPresentParams.BackBufferFormat = SCREEN_D3DFORMAT;
  sPresentParams.BackBufferCount = 1;
  sPresentParams.MultiSampleType = D3DMULTISAMPLE_NONE;

  /* COPY rather than DISCARD, because the back buffer has to survive a
   * present. pie_ScreenFlip(CLEAR_OFF) means "show this and keep it" - the
   * loading screen draws its progress bar over the frame already there, and
   * the video loop only writes the movie rectangle - which is what the
   * DirectDraw front-from-back Blt gave them. DISCARD would leave both
   * reading undefined memory.
   */
  sPresentParams.SwapEffect = D3DSWAPEFFECT_COPY;
  sPresentParams.hDeviceWindow = hWndMain;
  /* Always a windowed swap chain: the borderless window covers the desktop
   * at the desktop's own mode, so there is no exclusive mode to enter and
   * no display mode change to lose the device over. */
  sPresentParams.Windowed = TRUE;
  sPresentParams.EnableAutoDepthStencil = TRUE;
  sPresentParams.AutoDepthStencilFormat = D3DFMT_D16;

  /* The backdrop, the FMV frames and the debug 2D display all write to the
   * back buffer with the CPU, so it has to be lockable. */
  sPresentParams.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;

  sPresentParams.FullScreen_RefreshRateInHz = 0;
  sPresentParams.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
}

/* Create the device with the current present parameters.
 *
 * Tries hardware vertex processing first, then software; the game feeds the
 * device pre-transformed vertices, so software processing costs it nothing
 * but is the only thing some drivers will give us.
 */
static BOOL createDevice(void)
{
  HRESULT hRes;
  DWORD behaviourFlags[2] = {D3DCREATE_HARDWARE_VERTEXPROCESSING, D3DCREATE_SOFTWARE_VERTEXPROCESSING};
  SDWORD i;

  for (i = 0; i < 2; i++)
  {
    hRes = psD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWndMain, behaviourFlags[i] | D3DCREATE_FPU_PRESERVE, &sPresentParams,
                               &psD3DDevice);
    if (hRes == D3D_OK)
      return TRUE;
  }

  /* No hardware device - fall back on the reference rasteriser so that a
   * machine without a usable driver still starts. */
  hRes = psD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_REF, hWndMain, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE,
                             &sPresentParams, &psD3DDevice);
  if (hRes != D3D_OK)
  {
    Neuron::Fatal("Create D3D device failed:\n{}", DXErrorToString(hRes));
    return FALSE;
  }

  return TRUE;
}

/* get screen window handle */
HWND screenGetHWnd(void) { return hWndMain; }

/* Initialise the display */
BOOL screenInitialise(UDWORD width, // Display width - the desktop's
                      UDWORD height, // Display height - the desktop's
                      BOOL bCreateDevice, // Whether to create the device at all
                      HANDLE hWindow) // The main windows handle
{
  /* Store the screen information */
  screenWidth = width;
  screenHeight = height;
  screenDepth = 32;

  hWndMain = static_cast<HWND>(hWindow);

  setPixelFormats();

  memset(asPalEntries, 0, sizeof(PALETTEENTRY) * PAL_MAX);
  asPalEntries[PAL_MAX - 1].peRed = 0xff;
  asPalEntries[PAL_MAX - 1].peGreen = 0xff;
  asPalEntries[PAL_MAX - 1].peBlue = 0xff;

  /* The one mode there is: a borderless window covering the desktop,
   * presented through a windowed swap chain. */
  displayMode = MODE_BOTH;
  screenMode = SCREEN_WINDOWED;

  if (!bCreateDevice)
    return TRUE;

  psD3D = Direct3DCreate9(D3D_SDK_VERSION);
  if (psD3D == nullptr)
  {
    Neuron::Fatal("Create Direct3D 9 object failed - is DirectX 9 installed?");
    return FALSE;
  }

  setPresentParameters();

  if (!createDevice())
  {
    RELEASE(psD3D);
    return FALSE;
  }

  bDeviceCreated = TRUE;

  screenFlipState = FLIP_IDLE;
  InitializeCriticalSection(&sScreenFlipCritical);
  hScreenFlipSemaphore = CreateSemaphore(nullptr, 0, 1, nullptr);
  if (hScreenFlipSemaphore == nullptr)
  {
    Neuron::Fatal("Couldn't create flip semaphore");
    return FALSE;
  }

  return TRUE;
}

/* Release the D3D objects */
void screenShutDown(void)
{
  if (bDeviceCreated)
  {
    DeleteCriticalSection(&sScreenFlipCritical);
    CloseHandle(hScreenFlipSemaphore);
    hScreenFlipSemaphore = nullptr;
    bDeviceCreated = FALSE;
  }

  RELEASE(psLockedBackBuffer);
  RELEASE(psD3DDevice);
  RELEASE(psD3D);
}

/* Return the Direct3D 9 object */
LPDIRECT3D9 screenGetD3D(void) { return psD3D; }

/* Return the Direct3D 9 device */
LPDIRECT3DDEVICE9 screenGetDevice(void) { return psD3DDevice; }

/* Return a pointer to the Front buffer pixel format */
SCREEN_PIXELFORMAT* screenGetFrontBufferPixelFormat(void)
{
  if (psD3DDevice)
    return &sFrontBufferPixelFormat;
  return nullptr;
}

/* Return a pointer to the back buffer pixel format */
SCREEN_PIXELFORMAT* screenGetBackBufferPixelFormat(void)
{
  if (psD3DDevice)
    return &sBackBufferPixelFormat;
  return nullptr;
}

/*********************************************************/
/* Device loss                                           */
/*********************************************************/

SCREEN_DEVICE_STATE screenTestDeviceState(void)
{
  HRESULT hRes;

  if (psD3DDevice == nullptr)
    return SCREEN_DEVICE_LOST;

  hRes = psD3DDevice->TestCooperativeLevel();

  if (hRes == D3D_OK)
    return SCREEN_DEVICE_OK;
  if (hRes == D3DERR_DEVICENOTRESET)
    return SCREEN_DEVICE_NEEDSRESET;

  return SCREEN_DEVICE_LOST;
}

BOOL screenResetDevice(void)
{
  HRESULT hRes;

  if (psD3DDevice == nullptr)
    return FALSE;

  /* Anything in the default pool has to be gone before the reset. The only
   * thing the framework keeps there is a lock on the back buffer, which
   * should never be held across a reset, but a lost device can leave one
   * behind. */
  RELEASE(psLockedBackBuffer);

  hRes = psD3DDevice->Reset(&sPresentParams);
  if (hRes != D3D_OK)
  {
    Neuron::DebugTrace("screenResetDevice: Reset failed:\n{}", DXErrorToString(hRes));
    return FALSE;
  }

  return TRUE;
}

/*********************************************************/
/* Back buffer access                                    */
/*********************************************************/

BOOL screenLockBackBuffer(SCREEN_LOCK* psLock)
{
  HRESULT hRes;
  D3DLOCKED_RECT sLocked;
  D3DSURFACE_DESC sDesc;

  DEBUG_ASSERT_TEXT(psLockedBackBuffer == NULL, "screenLockBackBuffer: back buffer already locked");

  if (psD3DDevice == nullptr)
    return FALSE;

  hRes = psD3DDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &psLockedBackBuffer);
  if (hRes != D3D_OK)
  {
    Neuron::DebugTrace("screenLockBackBuffer: GetBackBuffer failed:\n{}", DXErrorToString(hRes));
    return FALSE;
  }

  hRes = psLockedBackBuffer->GetDesc(&sDesc);
  if (hRes != D3D_OK)
  {
    Neuron::DebugTrace("screenLockBackBuffer: GetDesc failed:\n{}", DXErrorToString(hRes));
    RELEASE(psLockedBackBuffer);
    return FALSE;
  }

  hRes = psLockedBackBuffer->LockRect(&sLocked, nullptr, 0);
  if (hRes != D3D_OK)
  {
    Neuron::DebugTrace("screenLockBackBuffer: LockRect failed:\n{}", DXErrorToString(hRes));
    RELEASE(psLockedBackBuffer);
    return FALSE;
  }

  psLock->pPixels = static_cast<UBYTE*>(sLocked.pBits);
  psLock->pitch = sLocked.Pitch;
  psLock->width = sDesc.Width;
  psLock->height = sDesc.Height;

  return TRUE;
}

void screenUnlockBackBuffer(void)
{
  if (psLockedBackBuffer == nullptr)
    return;

  (void)psLockedBackBuffer->UnlockRect();
  RELEASE(psLockedBackBuffer);
}

/*********************************************************/
/* Backdrop                                              */
/*********************************************************/

void screen_SetBackDrop(UWORD* newBackDropBmp, UDWORD width, UDWORD height)
{
  bBackDrop = TRUE;
  pBackDropData = newBackDropBmp;
  backDropWidth = width;
  backDropHeight = height;
}

void screen_StopBackDrop(void) { bBackDrop = FALSE; }

void screen_RestartBackDrop(void) { bBackDrop = TRUE; }

UWORD* screen_GetBackDrop(void)
{
  if (bBackDrop == TRUE)
    return pBackDropData;
  return nullptr;
}

UDWORD screen_GetBackDropWidth(void)
{
  if (bBackDrop == TRUE)
    return backDropWidth;
  return 0;
}

/* Read the back buffer back into a 16 bit software buffer.
 *
 * The buffer is logical-canvas sized while the back buffer is physical, so
 * this samples every display-scale-th pixel; the backdrop draw scales the
 * result back up on the way in, which round-trips exactly.
 */
void screen_Upload(UWORD* newBackDropBmp)
{
  SCREEN_LOCK sLock;
  UDWORD x, y;
  UDWORD destWidth, destHeight, scale;
  UDWORD* pSrc;
  UWORD* pDest;

  if (newBackDropBmp == nullptr)
    return;

  if (!screenLockBackBuffer(&sLock))
    return;

  scale = Neuron::DisplayScale();
  destWidth = pie_GetVideoBufferWidth();
  destHeight = pie_GetVideoBufferHeight();

  pDest = newBackDropBmp;
  for (y = 0; (y < destHeight) && (y * scale < sLock.height); y++)
  {
    pSrc = (UDWORD*)(sLock.pPixels + sLock.pitch * (y * scale));
    for (x = 0; (x < destWidth) && (x * scale < sLock.width); x++)
      pDest[x] = screen32To565(pSrc[x * scale]);
    pDest += destWidth;
  }

  screenUnlockBackBuffer();
}

void screen_SetFogColour(UDWORD newFogColour)
{
  /* The back buffer is 32 bit ARGB, so the colour the game hands over is
   * already in the right layout - the DirectDraw code had to repack it into
   * whatever 16 bit format the card had given it. */
  fogColour = D3DCOLOR_XRGB((newFogColour >> 16) & 0xff, (newFogColour >> 8) & 0xff, newFogColour & 0xff);
}

/*********************************************************/
/* Present                                               */
/*********************************************************/

/* Draw the backdrop into the back buffer, centred and enlarged by the
 * display scale, converting 565 to 32 bit. The backdrop bitmap is in
 * logical-canvas units like everything else the game hands over, so a
 * 640x480 menu backdrop keeps its size relative to the menus on top of it.
 */
static void drawBackDrop(void)
{
  SCREEN_LOCK sLock;
  UDWORD x, y;
  UDWORD* pDest;
  UWORD* pSrc;
  UDWORD destX, destY;
  UDWORD scale, scaledWidth, scaledHeight;

  if (!screenLockBackBuffer(&sLock))
    return;

  scale = Neuron::DisplayScale();
  scaledWidth = backDropWidth * scale;
  scaledHeight = backDropHeight * scale;

  destX = (screenWidth > scaledWidth) ? (screenWidth - scaledWidth) / 2 : 0;
  destY = (screenHeight > scaledHeight) ? (screenHeight - scaledHeight) / 2 : 0;

  for (y = 0; (y < scaledHeight) && (destY + y < sLock.height); y++)
  {
    pSrc = pBackDropData + (y / scale) * backDropWidth;
    pDest = (UDWORD*)(sLock.pPixels + sLock.pitch * (destY + y)) + destX;
    for (x = 0; (x < scaledWidth) && (destX + x < sLock.width); x++)
      pDest[x] = screen565To32(pSrc[x / scale]);
  }

  screenUnlockBackBuffer();
}

/* Present the back buffer, then clear it or fill it with the backdrop */
void screenFlip(BOOL clearBackBuffer)
{
  HRESULT hRes;

  if (psD3DDevice == nullptr)
    return;

  // note that we have started a flip
  EnterCriticalSection(&sScreenFlipCritical);
  screenFlipState = FLIP_STARTED;

  hRes = psD3DDevice->Present(nullptr, nullptr, nullptr, nullptr);
  if ((hRes != D3D_OK) && (hRes != D3DERR_DEVICELOST))
    Neuron::DebugTrace("screenFlip: Present failed:\n{}", DXErrorToString(hRes));

  // note that we have finished a flip
  screenFlipState = FLIP_FINISHED;

  if (clearBackBuffer)
  {
    if (bBackDrop && (pBackDropData != nullptr))
    {
      /* Clear first so that a backdrop smaller than the display does not
       * leave the previous frame around its edges. */
      if ((screenWidth > backDropWidth * Neuron::DisplayScale()) || (screenHeight > backDropHeight * Neuron::DisplayScale()))
        (void)psD3DDevice->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);

      drawBackDrop();
    }
    else { (void)psD3DDevice->Clear(0, nullptr, D3DCLEAR_TARGET, fogColour, 1.0f, 0); }
  }

  LeaveCriticalSection(&sScreenFlipCritical);
  ReleaseSemaphore(hScreenFlipSemaphore, 1, nullptr);
}

/*********************************************************/
/* Mode query                                            */
/*                                                       */
/* There used to be a toggle between a decorated window   */
/* and Direct3D exclusive full screen here. The display   */
/* is a borderless window at the desktop mode now, so     */
/* there is nothing to toggle to; the mode query stays    */
/* because the renderer reads it.                        */
/*********************************************************/

SCREEN_MODE screenGetMode(void) { return screenMode; }

/*********************************************************/
/* Palette matching                                      */
/*********************************************************/

void screenSetPalette(UDWORD first, UDWORD count, PALETTEENTRY* psEntries)
{
  DEBUG_ASSERT_TEXT((first+count-1 < PAL_MAX), "screenSetPalette: invalid entry range");

  if (count == 0)
    return;

  /* ensure that colour 0 is black and 255 is white */
  if ((first == 0 || first == 255) && count == 1)
    return;

  if (first == 0)
  {
    first = 1;
    count -= 1;
  }
  if (first + count - 1 == PAL_MAX)
    count -= 1;

  memcpy(asPalEntries + first, psEntries + first, sizeof(PALETTEENTRY) * count);
}

/* Return the best colour match in the stored palette */
UBYTE screenGetPalEntry(UBYTE red, UBYTE green, UBYTE blue)
{
  UDWORD i, minDist, dist;
  UDWORD redDiff, greenDiff, blueDiff;
  UBYTE colour;

  minDist = 0xff * 0xff * 0xff;
  colour = 0;
  for (i = 0; i < PAL_MAX; i++)
  {
    redDiff = asPalEntries[i].peRed - red;
    greenDiff = asPalEntries[i].peGreen - green;
    blueDiff = asPalEntries[i].peBlue - blue;
    dist = redDiff * redDiff + greenDiff * greenDiff + blueDiff * blueDiff;
    if (dist < minDist)
    {
      minDist = dist;
      colour = static_cast<UBYTE>(i);
    }
    if (minDist == 0)
      break;
  }

  return colour;
}

/* Return the actual value that will be poked into screen memory
 * given an RGB value.
 */
UDWORD screenGetCacheColour(UBYTE red, UBYTE green, UBYTE blue)
{
  // RGB = 000 will be transparent to the colour keyed blits, so check for it
  // and change to RGB = 111.
  if ((red == 0) && (green == 0) && (blue == 0))
    red = green = blue = 1;

  return (static_cast<UDWORD>(red) << 16) | (static_cast<UDWORD>(green) << 8) | static_cast<UDWORD>(blue);
}

/* Set the colour for text */
void screenSetTextColour(UBYTE red, UBYTE green, UBYTE blue) { textColour = screenGetCacheColour(red, green, blue); }

/*********************************************************/
/* 2D drawing into the back buffer                       */
/*                                                       */
/* Only the debug 2D display uses these. They were        */
/* DirectDraw blits and locks; they are now plain writes  */
/* into the locked back buffer.                          */
/*********************************************************/

/* Output text to the display screen at location x,y. */
void screenTextOut(UDWORD x, UDWORD y, STRING* pFormat, ...)
{
  STRING aTxtBuff[1024];
  va_list pArgs;
  SCREEN_LOCK sLock;
  UDWORD strLen;
  UBYTE *pSrc, font;
  UDWORD* pDest;
  UDWORD px, py;

  va_start(pArgs, pFormat);
  vsprintf(aTxtBuff, pFormat, pArgs);
  va_end(pArgs);

  /* See if the string is offscreen */
  if (y >= screenHeight - FONT_HEIGHT)
    return;
  if (x >= screenWidth)
    return;

  strLen = strlen(aTxtBuff);
  if (x + strLen * FONT_WIDTH >= screenWidth)
  {
    /* Chop off the end of the string */
    aTxtBuff[strLen - 1 - ((x + strLen * FONT_WIDTH) - screenWidth) / FONT_WIDTH] = '\0';
  }

  if (!screenLockBackBuffer(&sLock))
    return;

  for (py = 0; py < FONT_HEIGHT; py++)
  {
    pDest = (UDWORD*)(sLock.pPixels + (y + py) * sLock.pitch) + x;
    for (pSrc = (UBYTE*)aTxtBuff; *pSrc != '\0'; pSrc++)
    {
      if ((*pSrc >= PRINTABLE_START) && (*pSrc < PRINTABLE_START + PRINTABLE_CHARS))
      {
        font = aFontData[*pSrc - PRINTABLE_START][py];
        for (px = 0; px < FONT_WIDTH; px++)
        {
          if (font & (1 << px))
            *pDest = textColour;
          pDest++;
        }
      }
      else
        pDest += FONT_WIDTH;
    }
  }

  screenUnlockBackBuffer();
}

/* Blit the source rectangle of the surface to the back buffer at the given
 * location, treating black as transparent. The blit is clipped to the screen.
 */
void screenBlit(SDWORD destX, SDWORD destY, // The location on screen
                LPSURFACE psSurf, // The surface to blit from
                UDWORD srcX, UDWORD srcY, UDWORD uwidth, UDWORD uheight) // The source rectangle from the surface
{
  SCREEN_LOCK sLock;
  SDWORD width, height;
  SDWORD x, y;
  UDWORD *pSrc, *pDest;
  UDWORD pixel;

  if (psSurf == nullptr)
    return;

  width = uwidth;
  height = uheight;

  /* Do clipping on the destination rect */
  if ((destX + width <= 0) || (destY + height <= 0) || (destX >= static_cast<SDWORD>(screenWidth)) || (destY >= static_cast<SDWORD>(
    screenHeight)))
  {
    /* completely off screen */
    return;
  }
  if (destX < 0)
  {
    /* clip left side */
    srcX -= destX;
    width += destX;
    destX = 0;
  }
  else if (destX + width >= static_cast<SDWORD>(screenWidth)) { width = static_cast<SDWORD>(screenWidth) - destX; }
  if (destY < 0)
  {
    /* clip top */
    srcY -= destY;
    height += destY;
    destY = 0;
  }
  else if (destY + height >= static_cast<SDWORD>(screenHeight)) { height = static_cast<SDWORD>(screenHeight) - destY; }

  /* and on the source */
  if (srcX + width > psSurf->width)
    width = psSurf->width - srcX;
  if (srcY + height > psSurf->height)
    height = psSurf->height - srcY;
  if (width <= 0 || height <= 0)
    return;

  if (!screenLockBackBuffer(&sLock))
    return;

  for (y = 0; y < height; y++)
  {
    pSrc = (UDWORD*)((UBYTE*)psSurf->pPixels + psSurf->pitch * (srcY + y)) + srcX;
    pDest = (UDWORD*)(sLock.pPixels + sLock.pitch * (destY + y)) + destX;
    for (x = 0; x < width; x++)
    {
      pixel = pSrc[x];
      if ((pixel & 0x00ffffff) != 0)
        pDest[x] = pixel;
    }
  }

  screenUnlockBackBuffer();
}

/*
 * Blit the source rectangle to the back buffer scaling it to the size of the
 * destination rectangle. This clips to the size of the back buffer.
 */
void screenScaleBlit(SDWORD destX, SDWORD destY, SDWORD destWidth, SDWORD destHeight, LPSURFACE psSurf, SDWORD srcX, SDWORD srcY,
                     SDWORD srcWidth, SDWORD srcHeight)
{
  SCREEN_LOCK sLock;
  SDWORD lowShrink;
  SDWORD highShrink;
  SDWORD x, y, sx, sy;
  UDWORD *pSrc, *pDest;
  UDWORD pixel;

  if (psSurf == nullptr || destWidth <= 0 || destHeight <= 0)
    return;

  /* Do clipping on the destination rect */
  if ((destX + destWidth <= 0) || (destY + destHeight <= 0) || (destX >= static_cast<SDWORD>(screenWidth)) || (destY >= static_cast<SDWORD>(
    screenHeight)))
  {
    /* completely off screen */
    return;
  }

  /* do the horizontal clipping */
  if ((destX < 0) || (destX + destWidth > static_cast<SDWORD>(screenWidth)))
  {
    lowShrink = destX < 0 ? -destX : 0;
    srcX += srcWidth * lowShrink / destWidth;

    highShrink = destX + destWidth > static_cast<SDWORD>(screenWidth) ? destX + destWidth - static_cast<SDWORD>(screenWidth) + 1 : 0;
    srcWidth -= srcWidth * (lowShrink + highShrink) / destWidth;

    destX = destX < 0 ? 0 : destX;
    destWidth -= lowShrink + highShrink;
  }

  /* do the vertical clipping */
  if ((destY < 0) || (destY + destHeight > static_cast<SDWORD>(screenHeight)))
  {
    lowShrink = destY < 0 ? -destY : 0;
    srcY += srcHeight * lowShrink / destHeight;

    highShrink = destY + destHeight > static_cast<SDWORD>(screenHeight) ? destY + destHeight - static_cast<SDWORD>(screenHeight) + 1 : 0;
    srcHeight -= srcHeight * (lowShrink + highShrink) / destHeight;

    destY = destY < 0 ? 0 : destY;
    destHeight -= lowShrink + highShrink;
  }

  if (destWidth <= 0 || destHeight <= 0 || srcWidth <= 0 || srcHeight <= 0)
    return;

  if (!screenLockBackBuffer(&sLock))
    return;

  for (y = 0; y < destHeight; y++)
  {
    sy = srcY + y * srcHeight / destHeight;
    if (sy < 0 || sy >= static_cast<SDWORD>(psSurf->height))
      continue;
    pSrc = (UDWORD*)((UBYTE*)psSurf->pPixels + psSurf->pitch * sy);
    pDest = (UDWORD*)(sLock.pPixels + sLock.pitch * (destY + y)) + destX;
    for (x = 0; x < destWidth; x++)
    {
      sx = srcX + x * srcWidth / destWidth;
      if (sx < 0 || sx >= static_cast<SDWORD>(psSurf->width))
        continue;
      pixel = pSrc[sx];
      if ((pixel & 0x00ffffff) != 0)
        pDest[x] = pixel;
    }
  }

  screenUnlockBackBuffer();
}

/* Blit a tile (rectangle) from the surface to the back buffer. */
void screenBlitTile(SDWORD destX, SDWORD destY, // The location on screen
                    LPSURFACE psSurf, // The surface to blit from
                    UDWORD width, UDWORD height, // The size of the tile
                    UDWORD tile) // The tile number
{
  UDWORD tilesPerLine, srcX, srcY;

  if (psSurf == nullptr || width == 0)
    return;

  /* Calculate where the tile is on the surface */
  tilesPerLine = psSurf->width / width;
  if (tilesPerLine == 0)
    return;
  srcX = (tile % tilesPerLine) * width;
  srcY = (tile / tilesPerLine) * height;

  DEBUG_ASSERT_TEXT(((srcX + width) <= psSurf->width) && ((srcY + height) <= psSurf->height), "screenBlitTile: Tile off source surface");

  /* blit the tile */
  screenBlit(destX, destY, psSurf, srcX, srcY, width, height);
}

/* Set the colour for drawing lines. */
void screenSetLineColour(UBYTE red, UBYTE green, UBYTE blue) { lineColour = screenGetCacheColour(red, green, blue); }

/* Set the value to be poked into screen memory for line drawing. */
void screenSetLineCacheColour(UDWORD colour) { lineColour = colour; }

/* Clipping outcodes are bit flags, combined with |= and tested with &, so
   the type holds combinations rather than the listed constants alone. */
enum
{
  OUT_TOP = 0x8,
  OUT_BOTTOM = 0x4,
  OUT_RIGHT = 0x2,
  OUT_LEFT = 0x1,
};

using OUTCODE = SDWORD;

__inline void compOutCode(SDWORD x, SDWORD y, OUTCODE* pCode)
{
  *pCode = 0;
  if (y >= static_cast<SDWORD>(screenHeight))
    *pCode |= OUT_TOP;
  else if (y < 0)
    *pCode |= OUT_BOTTOM;

  if (x >= static_cast<SDWORD>(screenWidth))
    *pCode |= OUT_RIGHT;
  else if (x < 0)
    *pCode |= OUT_LEFT;
}

/* Draw a line to the display in the current colour. */
void screenDrawLine(SDWORD x0, SDWORD y0, SDWORD x1, SDWORD y1)
{
  SDWORD d, x, y, ax, ay, sx, sy, dx, dy;
  SDWORD lineChange;
  UBYTE* pOffset;
  SCREEN_LOCK sLock;
  BOOL clipped;
  OUTCODE code0, code1, code;
  float cx = 0.0f, cy = 0.0f;
  // the clipping loop counter the assertion below reports. Left outside any
  // DEBUG guard so the function stays correct whether or not that assertion
  // is compiled in.
  UDWORD loops;

  clipped = FALSE;
  compOutCode(x0, y0, &code0);
  compOutCode(x1, y1, &code1);
  loops = 0;

  /* Loop until the line is accepted. return if the line is rejected */
  do
  {
    DEBUG_ASSERT_TEXT(loops++ < 20, "screenDrawLine: clipping loop reached 20 iterations\n" "                 - possible infinite loop ?");
    if ((code0 == 0) && (code1 == 0))
    {
      /* trivial acceptance */
      clipped = TRUE;
    }
    else if (code0 & code1)
    {
      /* trivial reject */
      return;
    }
    else
    {
      /* At least one of the end points is out of the screen pick it and clip */
      if (code0 != 0)
        code = code0;
      else
        code = code1;
      if (code & OUT_TOP)
      {
        cx = x0 + static_cast<float>(x1 - x0) * static_cast<float>((SDWORD)screenHeight - 1 - y0) / (y1 - y0);
        cy = static_cast<float>(screenHeight) - 1;
      }
      else if (code & OUT_BOTTOM)
      {
        cx = x0 + static_cast<float>(x1 - x0) * static_cast<float>(-y0) / static_cast<float>(y1 - y0);
        cy = static_cast<float>(0);
      }
      else if (code & OUT_RIGHT)
      {
        cy = y0 + static_cast<float>(y1 - y0) * static_cast<float>((SDWORD)screenWidth - 1 - x0) / static_cast<float>(x1 - x0);
        cx = static_cast<float>(screenWidth) - 1;
      }
      else if (code & OUT_LEFT)
      {
        cy = y0 + static_cast<float>(y1 - y0) * static_cast<float>(-x0) / static_cast<float>(x1 - x0);
        cx = static_cast<float>(0);
      }

      /* calculated intesection point get ready for next pass */
      if (code == code0)
      {
        x0 = std::lround(cx);
        y0 = std::lround(cy);
        compOutCode(x0, y0, &code0);
      }
      else
      {
        x1 = std::lround(cx);
        y1 = std::lround(cy);
        compOutCode(x1, y1, &code1);
      }
    }
  }
  while (!clipped);

  DEBUG_ASSERT_TEXT((x0 >= 0) && (x0 < static_cast<SDWORD>(screenWidth)) &&
    (x1 >= 0) && (x1 < static_cast<SDWORD>(screenWidth)) && (y0 >= 0) && (y0 < static_cast<SDWORD>(screenHeight)) && (y1 >= 0) && (y1 <
      static_cast<SDWORD>(screenHeight)), "screenDrawLine: Failed to clip to screen");

  if (!screenLockBackBuffer(&sLock))
    return;

  /* Do some initial set up for the line */
  dx = x1 - x0;
  dy = y1 - y0;
  ax = abs(dx) << 1;
  ay = abs(dy) << 1;
  sx = dx < 0 ? -1 : 1;
  sy = dy < 0 ? -1 : 1;

  x = x0;
  y = y0;
  pOffset = sLock.pPixels + sLock.pitch * y0 + (x0 << 2);
  lineChange = dy < 0 ? -sLock.pitch : sLock.pitch;
  if (ax > ay)
  {
    /* x dominant */
    d = ay - ax / 2;
    FOREVER
    {
      *((UDWORD*)pOffset) = lineColour;
      if (x == x1)
      {
        /* Finished line - end loop */
        break;
      }
      if (d >= 0)
      {
        y = y + sy;
        d = d - ax;
        pOffset += lineChange;
      }
      x = x + sx;
      d = d + ay;
      pOffset += sx * 4;
    }
  }
  else
  {
    /* y dominant */
    d = ax - ay / 2;
    FOREVER
    {
      *((UDWORD*)pOffset) = lineColour;
      if (y == y1)
      {
        /* Finished line - end loop */
        break;
      }
      if (d >= 0)
      {
        x = x + sx;
        d = d - ay;
        pOffset += sx * 4;
      }
      y = y + sy;
      d = d + ax;
      pOffset += lineChange;
    }
  }

  screenUnlockBackBuffer();
}

/* Draw a rectangle (unfilled) */
void screenDrawRect(SDWORD x0, SDWORD y0, SDWORD x1, SDWORD y1)
{
  /* Quick hack for now, could do a fast version if necessary */
  screenDrawLine(x0, y0, x1, y0);
  screenDrawLine(x1, y0, x1, y1);
  screenDrawLine(x1, y1, x0, y1);
  screenDrawLine(x0, y1, x0, y0);
}

/* Set the colour for fill operations */
void screenSetFillColour(UBYTE red, UBYTE green, UBYTE blue) { fillColour = screenGetCacheColour(red, green, blue); }

/* Set the value to be poked into screen memory for filling. */
void screenSetFillCacheColour(UDWORD colour) { fillColour = colour; }

/* Draw a filled rectangle */
void screenFillRect(SDWORD x0, SDWORD y0, SDWORD x1, SDWORD y1)
{
  D3DRECT sRect;
  SDWORD tmp;

  if (psD3DDevice == nullptr)
    return;

  /* make sure x0,y0 is top left */
  if (x1 < x0)
  {
    tmp = x1;
    x1 = x0;
    x0 = tmp;
  }
  if (y1 < y0)
  {
    tmp = y1;
    y1 = y0;
    y0 = tmp;
  }

  /* Do clipping on the destination rect */
  if (x1 < 0 || x0 >= static_cast<SDWORD>(screenWidth) || y1 < 0 || y0 >= static_cast<SDWORD>(screenHeight))
  {
    /* Completely off screen */
    return;
  }
  if (x0 < 0)
    x0 = 0;
  if (y0 < 0)
    y0 = 0;
  if (x1 >= static_cast<SDWORD>(screenWidth))
    x1 = screenWidth - 1;
  if (y1 >= static_cast<SDWORD>(screenHeight))
    y1 = screenHeight - 1;

  sRect.x1 = x0;
  sRect.y1 = y0;
  sRect.x2 = x1 + 1;
  sRect.y2 = y1 + 1;

  (void)psD3DDevice->Clear(1, &sRect, D3DCLEAR_TARGET, fillColour, 1.0f, 0);
}

/* Draw a circle/ellipse */
void screenDrawEllipse(SDWORD x0, SDWORD y0, SDWORD x1, SDWORD y1)
{
  HRESULT hRes;
  HDC hdc;
  HPEN hpen;
  LPDIRECT3DSURFACE9 psBack = nullptr;

  if (psD3DDevice == nullptr)
    return;

  hRes = psD3DDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &psBack);
  if (hRes != D3D_OK)
  {
    DEBUG_ASSERT_TEXT(FALSE, "Ellipse draw failed - couldn't get back buffer:\n{}", DXErrorToString(hRes));
    return;
  }

  /* Get the device context for the back buffer */
  hRes = psBack->GetDC(&hdc);
  if (hRes != D3D_OK)
  {
    DEBUG_ASSERT_TEXT(FALSE, "Ellipse draw failed - couldn't get device context:\n{}", DXErrorToString(hRes));
    RELEASE(psBack);
    return;
  }

  hpen = CreatePen(PS_SOLID, 0, RGB(255, 255, 255));
  SelectObject(hdc, hpen);

  (void)Arc(hdc, x0, y0, x1, y1, x0, y0, x0, y0);

  hRes = psBack->ReleaseDC(hdc);
  if (hRes != D3D_OK) { DEBUG_ASSERT_TEXT(FALSE, "Ellipse draw failed - couldn't release context:\n{}", DXErrorToString(hRes)); }

  DeleteObject(hpen);
  RELEASE(psBack);
}
