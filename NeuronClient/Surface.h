/*
 * Surface.h
 *
 * Image surfaces for the 2D blit paths.
 *
 * These used to be DirectDraw offscreen surfaces. Direct3D 9 has no
 * equivalent - it has textures and render targets, neither of which suits a
 * colour-keyed CPU blit into the back buffer - so they are now plain system
 * memory images in the format of the display, and the blits in Screen.cpp
 * copy them by hand.
 */
#ifndef _surface_h
#define _surface_h

#include "Types.h"

/* A system memory image in the pixel format of the display (X8R8G8B8) */
using SURFACE = struct _surface
{
  UDWORD width;
  UDWORD height;
  UDWORD pitch; // bytes between the start of one row and the next
  UDWORD* pPixels;
};

using LPSURFACE = SURFACE*;

/* Create a surface. The contents are zeroed, which for these images means
 * transparent black - the colour the blits key on.
 */
extern BOOL surfCreate(LPSURFACE* ppsSurface, // The created surface
                       UDWORD width, UDWORD height, // The size of the surface
                       BOOL bAddToList); // Add pointer to surface list

/* Release a surface.
 * This should be used for all surfaces allocated with surfCreate.
 */
extern void surfRelease(LPSURFACE psSurface);

/* Re-create a surface after a mode change.
 *
 * Surfaces live in system memory and so cannot be lost. Kept because the
 * callers still have a mode change to respond to, and doing nothing is the
 * correct response now rather than something that happens to work.
 */
extern BOOL surfRecreate(LPSURFACE* ppsSurface);

/* Load palettised image data into a surface */
extern BOOL surfLoadFrom8Bit(LPSURFACE psSurf, // The surface to load to
                             UDWORD width, UDWORD height, // The size of the image data
                             UBYTE* pImageData, // The image data
                             PALETTEENTRY* psPalette); // The image palette data

/* Copy the data from one surface to another */
extern BOOL surfLoadFromSurface(LPSURFACE psDest, // The surface to load to
                                LPSURFACE psSrc); // The surface to load from

/* Load a BMP file and create a surface to store it in */
extern BOOL surfCreateFromBMP(STRING* pFileName, // The BMP file
                              LPSURFACE* ppsSurface, // The created surface
                              UDWORD* pWidth, // The width of the image
                              UDWORD* pHeight); // The height of the image

#endif
