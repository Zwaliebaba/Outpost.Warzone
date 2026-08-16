#include "pch.h"
/*
 * Surface.cpp
 *
 * System memory image surfaces. See Surface.h for why these are no longer
 * DirectDraw surfaces.
 */

#include "Frame.h"
#include "Surface.h"
#include "Image.h"
#include "Screen.h"
#include "FrameInt.h"

#define NUM_8BIT_PAL_ENTRIES	256

/* The list of all surfaces created by surfCreate() */
SURFACE_LIST* psSurfaces = nullptr;

/* Create a surface */
BOOL surfCreate(LPSURFACE* ppsSurface, // The created surface
                UDWORD width, UDWORD height, // The size of the surface
                BOOL bAddToList) // Add pointer to surface list
{
  LPSURFACE psSurf;
  SURFACE_LIST* psNew;

  DEBUG_ASSERT_TEXT(ppsSurface != NULL, "surfCreate: NULL surface pointer");

  if ((width == 0) || (height == 0))
  {
    Neuron::Fatal("surfCreate: zero sized surface requested");
    return FALSE;
  }

  psSurf = new (std::nothrow) SURFACE[1];
  if (psSurf == nullptr)
  {
    Neuron::Fatal("Out of memory");
    return FALSE;
  }

  psSurf->width = width;
  psSurf->height = height;
  psSurf->pitch = width * sizeof(UDWORD);
  psSurf->pPixels = new (std::nothrow) UDWORD[width * height];
  if (psSurf->pPixels == nullptr)
  {
    delete[] psSurf;
    Neuron::Fatal("Out of memory");
    return FALSE;
  }
  memset(psSurf->pPixels, 0, width * height * sizeof(UDWORD));

  if (bAddToList)
  {
    /* Create a new surface list entry */
    psNew = new (std::nothrow) SURFACE_LIST[1];
    if (psNew == nullptr)
    {
      delete[] psSurf->pPixels;
      delete[] psSurf;
      Neuron::Fatal("Out of memory");
      return FALSE;
    }

    /* Add the entry to the list */
    psNew->psSurface = psSurf;
    psNew->psNext = psSurfaces;
    psSurfaces = psNew;
  }

  *ppsSurface = psSurf;

  return TRUE;
}

/* Free a surface and its pixels */
static void surfDestroy(LPSURFACE psSurface)
{
  if (psSurface == nullptr)
    return;

  delete[] psSurface->pPixels;
  psSurface->pPixels = nullptr;
  delete[] psSurface;
}

/* Release a surface */
void surfRelease(LPSURFACE psSurface)
{
  SURFACE_LIST *psPrev = nullptr, *psCurr;

  if (psSurface == nullptr)
    return;

  if (psSurfaces == nullptr)
  {
    /* Never went on the list - just free it */
    surfDestroy(psSurface);
    return;
  }

  if (psSurfaces->psSurface == psSurface)
  {
    psCurr = psSurfaces;
    psSurfaces = psSurfaces->psNext;
    surfDestroy(psCurr->psSurface);
    delete[] psCurr;
    return;
  }

  for (psCurr = psSurfaces; (psCurr != nullptr) && (psCurr->psSurface != psSurface); psCurr = psCurr->psNext)
    psPrev = psCurr;
  DEBUG_ASSERT_TEXT(psCurr != NULL, "surfRelease: Couldn't find surface");
  if (psCurr != nullptr)
  {
    psPrev->psNext = psCurr->psNext;
    surfDestroy(psCurr->psSurface);
    delete[] psCurr;
  }
}

/* Re-create a surface after a mode change.
 *
 * Nothing to do: surfaces are in system memory, so a mode change cannot
 * lose them and their format does not depend on the display's.
 */
BOOL surfRecreate(LPSURFACE* ppsSurface)
{
  DEBUG_ASSERT_TEXT(ppsSurface != NULL && *ppsSurface != NULL, "surfRecreate: NULL surface");

  return TRUE;
}

/* Release all the allocated surfaces */
void surfShutDown(void)
{
  SURFACE_LIST *psCurr, *psNext;

  for (psCurr = psSurfaces; psCurr != nullptr; psCurr = psNext)
  {
    psNext = psCurr->psNext;
    surfDestroy(psCurr->psSurface);
    delete[] psCurr;
  }
  psSurfaces = nullptr;
}

/* Copy the data from one surface to another */
BOOL surfLoadFromSurface(LPSURFACE psDest, // The surface to load to
                         LPSURFACE psSrc) // The surface to load from
{
  UDWORD width, height, y;

  DEBUG_ASSERT_TEXT((psDest != NULL) && (psSrc != NULL), "surfLoadFromSurface: NULL surface");

  if ((psDest == nullptr) || (psSrc == nullptr))
    return FALSE;

  /* Copy the overlapping area, as the DirectDraw blit this replaces did */
  width = psSrc->width < psDest->width ? psSrc->width : psDest->width;
  height = psSrc->height < psDest->height ? psSrc->height : psDest->height;

  for (y = 0; y < height; y++)
  {
    memcpy((UBYTE*)psDest->pPixels + psDest->pitch * y, (UBYTE*)psSrc->pPixels + psSrc->pitch * y, width * sizeof(UDWORD));
  }

  return TRUE;
}

/* Load palettised image data into a surface */
BOOL surfLoadFrom8Bit(LPSURFACE psSurf, // The surface to load to
                      UDWORD width, UDWORD height, // The size of the image data
                      UBYTE* pImageData, // The image data
                      PALETTEENTRY* psPalette) // The image palette data
{
  UDWORD i, j;
  UBYTE* pImageSrc;
  UDWORD* pDest;
  UDWORD aPalette[NUM_8BIT_PAL_ENTRIES];

  DEBUG_ASSERT_TEXT(psSurf != NULL, "surfLoadFrom8Bit: NULL surface pointer");
  DEBUG_ASSERT_TEXT(psPalette != NULL, "surfLoadFrom8Bit: NULL palette");

  if ((psSurf == nullptr) || (pImageData == nullptr) || (psPalette == nullptr))
    return FALSE;

  /* Pack the palette once rather than per pixel. Entry 0 is the colour key
   * the blits treat as transparent, so it stays fully black. */
  for (i = 0; i < NUM_8BIT_PAL_ENTRIES; i++)
  {
    aPalette[i] = (static_cast<UDWORD>(psPalette[i].peRed) << 16) | (static_cast<UDWORD>(psPalette[i].peGreen) << 8) | static_cast<UDWORD>(
      psPalette[i].peBlue);
  }

  pImageSrc = pImageData;
  for (j = 0; (j < psSurf->height) && (j < height); j++)
  {
    pDest = (UDWORD*)((UBYTE*)psSurf->pPixels + psSurf->pitch * j);
    for (i = 0; (i < psSurf->width) && (i < width); i++)
      pDest[i] = aPalette[pImageSrc[i]];
    pImageSrc += width;
  }

  return TRUE;
}

/* Load a BMP file and create a surface to store it in.
 * If pWidth or pHeight is NULL the size of the image will not be returned.
 */
BOOL surfCreateFromBMP(STRING* pFileName, // The BMP file
                       LPSURFACE* ppsSurface, // The created surface
                       UDWORD* pWidth, // The width of the image
                       UDWORD* pHeight) // The height of the image
{
  UBYTE *pImageFile, *pImageData;
  UDWORD imageFileSize, width, height;
  PALETTEENTRY* psImagePalette;

  if (!loadFile(pFileName, &pImageFile, &imageFileSize))
    return FALSE;

  if (!imageParseBMP(pImageFile, imageFileSize, &width, &height, &pImageData, &psImagePalette))
  {
    delete[] pImageFile;
    return FALSE;
  }

  delete[] pImageFile;

  if (psImagePalette == nullptr)
  {
    Neuron::Fatal("Surface loading from true colour images not implemented");
    delete[] pImageData;
    return FALSE;
  }

  if (!surfCreate(ppsSurface, width, height, TRUE))
  {
    delete[] pImageData;
    delete[] psImagePalette;
    return FALSE;
  }

  if (!surfLoadFrom8Bit(*ppsSurface, width, height, pImageData, psImagePalette))
  {
    delete[] pImageData;
    delete[] psImagePalette;
    return FALSE;
  }

  delete[] pImageData;
  delete[] psImagePalette;

  /* Return the size of the image if it is needed */
  if (pWidth && pHeight)
  {
    *pWidth = width;
    *pHeight = height;
  }

  return TRUE;
}
