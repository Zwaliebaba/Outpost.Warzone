/*
 * Screen.h
 *
 * Interface to the Direct3D 9 display.
 *
 * The framework owns the D3D9 object, the device and the swap chain. The 3D
 * renderer (Render.cpp) borrows the device from here rather than creating
 * one of its own, which is how DirectDraw used to hand its back buffer to
 * Direct3D 6.
 */
#ifndef _screen_h
#define _screen_h

#pragma warning (disable : 4201 4214 4115 4514)
#define WIN32_LEAN_AND_MEAN
#define WIN32_EXTRA_LEAN
#include <windows.h>
#include <d3d9.h>
#pragma warning (default : 4201 4214 4115)

#include "Types.h"

/* Free up a COM object */
#undef RELEASE
#define RELEASE(x) if ((x) != nullptr) {(void)(x)->Release(); (x) = nullptr;}

/* The layout of a pixel. This carries what the DirectDraw DDPIXELFORMAT used
 * to carry for the code that packs colours by hand, without the rest of it.
 */
using SCREEN_PIXELFORMAT = struct _screen_pixelformat
{
  UDWORD bitCount;
  UDWORD rMask, gMask, bMask, aMask;
};

/* The display is 32 bit, but the backdrop, the FMV frames and the
 * text-over-video path are all still composited in software as RGB565 and
 * converted on the way to the display. These two are the whole of that
 * conversion.
 */
__inline UDWORD screen565To32(UWORD pixel)
{
  UDWORD r = (pixel >> 11) & 0x1f;
  UDWORD g = (pixel >> 5) & 0x3f;
  UDWORD b = pixel & 0x1f;

  /* Replicate the high bits into the low ones so that full-scale input
   * produces full-scale output. */
  r = (r << 3) | (r >> 2);
  g = (g << 2) | (g >> 4);
  b = (b << 3) | (b >> 2);

  return 0xff000000 | (r << 16) | (g << 8) | b;
}

__inline UWORD screen32To565(UDWORD pixel)
{
  UDWORD r = (pixel >> 16) & 0xff;
  UDWORD g = (pixel >> 8) & 0xff;
  UDWORD b = pixel & 0xff;

  return static_cast<UWORD>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/* The state of the device, as reported by TestCooperativeLevel */
using SCREEN_DEVICE_STATE = enum _screen_device_state
{
  SCREEN_DEVICE_OK,
  // usable
  SCREEN_DEVICE_LOST,
  // lost and not yet resettable - nothing to do but wait
  SCREEN_DEVICE_NEEDSRESET // lost and ready to be reset
};

/* Return the Direct3D 9 object */
extern LPDIRECT3D9 screenGetD3D(void);

/* Return the Direct3D 9 device */
extern LPDIRECT3DDEVICE9 screenGetDevice(void);

/* Return the pixel format of the display */
extern SCREEN_PIXELFORMAT* screenGetFrontBufferPixelFormat(void);

/* Return the pixel format of the back buffer */
extern SCREEN_PIXELFORMAT* screenGetBackBufferPixelFormat(void);

/* Ask the device whether it has been lost */
extern SCREEN_DEVICE_STATE screenTestDeviceState(void);

/* Reset the device after it has been lost. Only valid when
 * screenTestDeviceState returns SCREEN_DEVICE_NEEDSRESET.
 */
extern BOOL screenResetDevice(void);

/* A locked view of the back buffer, for the paths that still write pixels
 * directly: the backdrop, the FMV frames and the debug 2D display.
 */
using SCREEN_LOCK = struct _screen_lock
{
  UBYTE* pPixels;
  SDWORD pitch; // bytes between the start of one row and the next
  UDWORD width, height;
};

/* Lock the back buffer. Must be paired with screenUnlockBackBuffer, and must
 * not be called between a BeginScene and its EndScene.
 */
extern BOOL screenLockBackBuffer(SCREEN_LOCK* psLock);

/* Release a lock taken by screenLockBackBuffer */
extern void screenUnlockBackBuffer(void);

/* Present the back buffer */
extern void screenFlip(BOOL clearBackBuffer);

/* backDrop */
extern void screen_SetBackDrop(UWORD* newBackDropBmp, UDWORD width, UDWORD height);
extern void screen_StopBackDrop(void);
extern void screen_RestartBackDrop(void);
extern UWORD* screen_GetBackDrop(void);
extern UDWORD screen_GetBackDropWidth(void);
extern void screen_Upload(UWORD* newBackDropBmp);

/* fog */
void screen_SetFogColour(UDWORD newFogColour);

/* Full screen video playback used to be a third screen mode, because the
 * display had to drop to 16 bit to play a sequence. It is 32 bit either way
 * round now, so a sequence plays in whichever mode the game is already in.
 *
 * The display itself no longer toggles: it is always a borderless window
 * covering the desktop, so SCREEN_WINDOWED is the mode there is, and
 * SCREEN_FULLSCREEN remains only so old comparisons still type-check.
 */
using SCREEN_MODE = enum _screen_mode
{
  SCREEN_FULLSCREEN,
  SCREEN_WINDOWED
};

/* get screen window handle */
extern HWND screenGetHWnd(void);

/* Get display mode (windowed or full screen) */
extern SCREEN_MODE screenGetMode(void);

/* Set palette entries for the display buffer
 * first specifies the first palette entry. count the number of entries
 * The psPalette should have at least first + count entries in it.
 */
extern void screenSetPalette(UDWORD first, UDWORD count, PALETTEENTRY* psPalette);

/* Return the best colour match in the stored palette */
extern UBYTE screenGetPalEntry(UBYTE red, UBYTE green, UBYTE blue);

/* Set the colour for text */
extern void screenSetTextColour(UBYTE red, UBYTE green, UBYTE blue);

/* Output text to the display screen at location x,y.
 * The remaining arguments are as printf.
 */
extern void screenTextOut(UDWORD x, UDWORD y, STRING* pFormat, ...);

/* The image surfaces the 2D blits read from. Declared in Surface.h, which
 * Frame.h includes before this header.
 */
struct _surface;

/* Blit the source rectangle of the surface
 * to the back buffer at the given location.
 * The blit is clipped to the screen size.
 */
extern void screenBlit(SDWORD destX, SDWORD destY, // The location on screen
                       struct _surface* psSurf, // The surface to blit from
                       UDWORD srcX, UDWORD srcY, UDWORD width, UDWORD height); // The source rectangle from the surface

/*
 * Blit the source rectangle to the back buffer scaling it to
 * the size of the destination rectangle.
 * This clips to the size of the back buffer.
 */
extern void screenScaleBlit(SDWORD destX, SDWORD destY, SDWORD destWidth, SDWORD destHeight, struct _surface* psSurf, SDWORD srcX,
                            SDWORD srcY, SDWORD srcWidth, SDWORD srcHeight);

/* Blit a tile (rectangle) from the surface
 * to the back buffer at the given location.
 * The tile is specified by it's size and number, numbering
 * across from top left to bottom right.
 * The blit is clipped to the screen size.
 */
extern void screenBlitTile(SDWORD destX, SDWORD destY, // The location on screen
                           struct _surface* psSurf, // The surface to blit from
                           UDWORD width, UDWORD height, // The size of the tile
                           UDWORD tile); // The tile number

/* Return the actual value that will be poked into screen memory
 * given an RGB value.
 */
extern UDWORD screenGetCacheColour(UBYTE red, UBYTE green, UBYTE blue);

/* Set the colour for drawing lines. */
extern void screenSetLineColour(UBYTE red, UBYTE green, UBYTE blue);

/* Set the value to be poked into screen memory for line drawing.
 * The colour value used should be one returned by screenGetCacheColour.
 */
extern void screenSetLineCacheColour(UDWORD colour);

/* Draw a line to the display in the current colour. */
extern void screenDrawLine(SDWORD x0, SDWORD y0, SDWORD x1, SDWORD y1);

/* Draw a rectangle (unfilled) */
extern void screenDrawRect(SDWORD x0, SDWORD y0, SDWORD x1, SDWORD y1);

/* Set the colour for fill operations */
extern void screenSetFillColour(UBYTE red, UBYTE green, UBYTE blue);

/* Set the value to be poked into screen memory for filling.
 * The colour value used should be one returned by screenGetCacheColour.
 */
extern void screenSetFillCacheColour(UDWORD colour);

/* Draw a filled rectangle */
extern void screenFillRect(SDWORD x0, SDWORD y0, SDWORD x1, SDWORD y1);

/* Draw an ellipse, and therefore circles too by specifying bounding box */
/* x0,y0 - top left, x1,y1 - bottom right */
extern void screenDrawEllipse(SDWORD x0, SDWORD y0, SDWORD x1, SDWORD y1);

#endif
