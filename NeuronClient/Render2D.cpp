#include "pch.h"
#include <assert.h>
/***************************************************************************/
/*
 * pieBlitFunc.c
 *
 * patch for exisitng ivis rectangle draw functions.
 *
 */
/***************************************************************************/

#include "Frame.h"
#include "Screen.h"
#include <time.h>
#include "Render2D.h"
#include "TexMan.h"
#include "RenderTypes.h"
#include "RenderModel.h"
#include "PieMode.h"
#include "PieState.h"
#include "RendFunc.h"
#include "RendMode.h"
#include "Pcx.h"
#include "RenderClip.h"
#include "PieFunc.h"
#include "RenderMatrix.h"
#include "Palette.h"
/***************************************************************************/
/*
 *	Local Definitions
 */
/***************************************************************************/
/* The backdrop bitmap, packed A8R8G8B8 like every other pixel buffer. The
 * 16 bit RGB565 halfway house it used to be went with the palette. */
iBitmap backDropBmp[BACKDROP_WIDTH * BACKDROP_HEIGHT];
SDWORD gSurfaceOffsetX;
SDWORD gSurfaceOffsetY;
UWORD* pgSrcData = nullptr;
SDWORD gSrcWidth;
SDWORD gSrcHeight;
SDWORD gSrcStride;

#define COLOURINTENSITY 0xffffffff
/***************************************************************************/
/*
 *	Local Variables
 */
/***************************************************************************/

PIESTYLE rendStyle;
POINT rectVerts[4];

/***************************************************************************/
/*
 *	Local ProtoTypes
 */
/***************************************************************************/

/***************************************************************************/
/*
 *	Source
 */
/***************************************************************************/
void pie_Line(int x0, int y0, int x1, int y1, uint32 colour)
{
  PIELIGHT light;

  pie_SetRendMode(REND_FLAT);
  pie_SetColour(colour);
  pie_SetTexturePage(-1);

  /* The colour arrives packed - it was a palette index until stage 3 of
   * the palette removal */
  light.argb = colour;
  light.byte.a = MAX_UB_LIGHT;
  pie_DrawLine(x0, y0, x1, y1, light.argb, TRUE);
}

/***************************************************************************/

void pie_Box(int x0, int y0, int x1, int y1, uint32 colour)
{
  PIELIGHT light;

  pie_SetRendMode(REND_FLAT);
  pie_SetColour(colour);
  pie_SetTexturePage(-1);

  if (x0 > psRendSurface->clip.right || x1 < psRendSurface->clip.left || y0 > psRendSurface->clip.bottom || y1 < psRendSurface->clip.top)
    return;

  if (x0 < psRendSurface->clip.left)
    x0 = psRendSurface->clip.left;
  if (x1 > psRendSurface->clip.right)
    x1 = psRendSurface->clip.right;
  if (y0 < psRendSurface->clip.top)
    y0 = psRendSurface->clip.top;
  if (y1 > psRendSurface->clip.bottom)
    y1 = psRendSurface->clip.bottom;

  /* The colour arrives packed - it was a palette index until stage 3 of
   * the palette removal */
  light.argb = colour;
  light.byte.a = MAX_UB_LIGHT;
  pie_DrawLine(x0, y0, x1, y0, light.argb, FALSE);
  pie_DrawLine(x1, y0, x1, y1, light.argb, FALSE);
  pie_DrawLine(x1, y1, x0, y1, light.argb, FALSE);
  pie_DrawLine(x0, y1, x0, y0, light.argb, FALSE);
}

/***************************************************************************/

void pie_BoxFillIndex(int x0, int y0, int x1, int y1, UDWORD colour)
{
  PIELIGHT light;

  pie_SetRendMode(REND_FLAT);
  pie_SetTexturePage(-1);

  if (x0 > psRendSurface->clip.right || x1 < psRendSurface->clip.left || y0 > psRendSurface->clip.bottom || y1 < psRendSurface->clip.top)
    return;

  if (x0 < psRendSurface->clip.left)
    x0 = psRendSurface->clip.left;
  if (x1 > psRendSurface->clip.right)
    x1 = psRendSurface->clip.right;
  if (y0 < psRendSurface->clip.top)
    y0 = psRendSurface->clip.top;
  if (y1 > psRendSurface->clip.bottom)
    y1 = psRendSurface->clip.bottom;

  /* The colour arrives packed - the "Index" in the name is what it took
   * until stage 3 of the palette removal; the name waits for the pie_
   * rename Phase 8 owns. */
  light.argb = colour;
  light.byte.a = MAX_UB_LIGHT;
  pie_DrawRect(x0, y0, x1, y1, light.argb, FALSE);
}

void pie_BoxFill(int x0, int y0, int x1, int y1, uint32 colour)
{
  PIELIGHT light;

  pie_SetRendMode(REND_FLAT);
  pie_SetTexturePage(-1);

  if (x0 > psRendSurface->clip.right || x1 < psRendSurface->clip.left || y0 > psRendSurface->clip.bottom || y1 < psRendSurface->clip.top)
    return;

  if (x0 < psRendSurface->clip.left)
    x0 = psRendSurface->clip.left;
  if (x1 > psRendSurface->clip.right)
    x1 = psRendSurface->clip.right;
  if (y0 < psRendSurface->clip.top)
    y0 = psRendSurface->clip.top;
  if (y1 > psRendSurface->clip.bottom)
    y1 = psRendSurface->clip.bottom;

  /* Get our colour values from the ivis palette */
  light.argb = colour;
  light.byte.a = MAX_UB_LIGHT;
  pie_DrawRect(x0, y0, x1, y1, light.argb, FALSE);
}

/***************************************************************************/

void pie_TransBoxFill(SDWORD x0, SDWORD y0, SDWORD x1, SDWORD y1)
{
  UDWORD rgb;
  UDWORD transparency;
  rgb = (pie_FILLRED << 16) | (pie_FILLGREEN << 8) | pie_FILLBLUE; //blue
  transparency = pie_FILLTRANS;
  pie_UniTransBoxFill(x0, y0, x1, y1, rgb, transparency);
}

/***************************************************************************/
void pie_UniTransBoxFill(SDWORD x0, SDWORD y0, SDWORD x1, SDWORD y1, UDWORD rgb, UDWORD transparency)
{
  UDWORD light;

  if (x0 > psRendSurface->clip.right || x1 < psRendSurface->clip.left || y0 > psRendSurface->clip.bottom || y1 < psRendSurface->clip.top)
    return;

  if (x0 < psRendSurface->clip.left)
    x0 = psRendSurface->clip.left;
  if (x1 > psRendSurface->clip.right)
    x1 = psRendSurface->clip.right;
  if (y0 < psRendSurface->clip.top)
    y0 = psRendSurface->clip.top;
  if (y1 > psRendSurface->clip.bottom)
    y1 = psRendSurface->clip.bottom;

  if (transparency == 0)
    transparency = 127;
  pie_SetTexturePage(-1);
  pie_SetRendMode(REND_ALPHA_FLAT);
  light = (rgb & 0x00ffffff) + (transparency << 24);
  pie_DrawRect(x0, y0, x1, y1, light, FALSE);
}

/***************************************************************************/

void pie_DrawImageFileID(IMAGEFILE* ImageFile, UWORD ID, int x, int y)
{
  IMAGEDEF* Image;
  PIEIMAGE pieImage;
  PIERECT dest;

  assert(ID < ImageFile->Header.NumImages);
  Image = &ImageFile->ImageDefs[ID];

  pieImage.texPage = ImageFile->TPageIDs[Image->TPageID];
  pieImage.tu = Image->Tu;
  pieImage.tv = Image->Tv;
  pieImage.tw = Image->Width;
  pieImage.th = Image->Height;
  dest.x = x + Image->XOffset;
  dest.y = y + Image->YOffset;
  dest.w = Image->Width;
  dest.h = Image->Height;
  pie_DrawImage(&pieImage, &dest, &rendStyle);
}

BOOL bAddSprites = FALSE;
UDWORD addSpriteLevel;

void pie_SetAdditiveSprites(BOOL val) { bAddSprites = val; }

void pie_SetAdditiveSpriteLevel(UDWORD val) { addSpriteLevel = val; }

BOOL pie_GetAdditiveSprites(void) { return (bAddSprites); }

void pie_ImageFileID(IMAGEFILE* ImageFile, UWORD ID, int x, int y)
{
  IMAGEDEF* Image;
  PIEIMAGE pieImage;
  PIERECT dest;

  assert(ID < ImageFile->Header.NumImages);
  Image = &ImageFile->ImageDefs[ID];

  if (pie_GetAdditiveSprites())
  {
    pie_SetBilinear(TRUE);
    pie_SetRendMode(REND_ALPHA_TEX);
    pie_SetColour(addSpriteLevel);
    pie_SetColourKeyedBlack(TRUE);
  }
  else
  {
    pie_SetBilinear(FALSE);
    pie_SetRendMode(REND_GOURAUD_TEX);
    pie_SetColour(COLOURINTENSITY);
    pie_SetColourKeyedBlack(TRUE);
  }
  pieImage.texPage = ImageFile->TPageIDs[Image->TPageID];
  pieImage.tu = Image->Tu;
  pieImage.tv = Image->Tv;
  pieImage.tw = Image->Width;
  pieImage.th = Image->Height;
  dest.x = x + Image->XOffset;
  dest.y = y + Image->YOffset;
  dest.w = Image->Width;
  dest.h = Image->Height;
  pie_DrawImage(&pieImage, &dest, &rendStyle);
}

/***************************************************************************/

void pie_ImageFileIDTile(IMAGEFILE* ImageFile, UWORD ID, int x, int y, int x0, int y0, int Width, int Height)
{
  IMAGEDEF* Image;
  SDWORD hRep, hRemainder, vRep, vRemainder;
  PIEIMAGE pieImage;
  PIERECT dest;
  assert(ID < ImageFile->Header.NumImages);

  assert(x0 == 0);
  assert(y0 == 0);

  Image = &ImageFile->ImageDefs[ID];

  pie_SetBilinear(FALSE);
  pie_SetRendMode(REND_GOURAUD_TEX);
  pie_SetColour(COLOURINTENSITY);
  pie_SetColourKeyedBlack(TRUE);

  pieImage.texPage = ImageFile->TPageIDs[Image->TPageID];
  pieImage.tu = Image->Tu;
  pieImage.tv = Image->Tv;
  pieImage.tw = Image->Width;
  pieImage.th = Image->Height;

  dest.x = x + Image->XOffset;
  dest.y = y + Image->YOffset;
  dest.w = Image->Width;
  dest.h = Image->Height;

  vRep = Height / Image->Height;
  vRemainder = Height - (vRep * Image->Height);

  while (vRep > 0)
  {
    hRep = Width / Image->Width;
    hRemainder = Width - (hRep * Image->Width);
    pieImage.tw = Image->Width;
    dest.x = x + Image->XOffset;
    dest.w = Image->Width;
    while (hRep > 0)
    {
      pie_DrawImage(&pieImage, &dest, &rendStyle);
      hRep--;
      dest.x += Image->Width;
    }
    //draw remainder
    if (hRemainder > 0)
    {
      pieImage.tw = hRemainder;
      dest.w = hRemainder;
      pie_DrawImage(&pieImage, &dest, &rendStyle);
    }
    vRep--;
    dest.y += Image->Height;
  }
  //draw remainder
  if (vRemainder > 0)
  {
    hRep = Width / Image->Width;
    hRemainder = Width - (hRep * Image->Width);
    pieImage.th = vRemainder;
    dest.h = vRemainder;
    //as above
    {
      pieImage.tw = Image->Width;
      dest.x = x + Image->XOffset;
      dest.w = Image->Width;
      while (hRep > 0)
      {
        pie_DrawImage(&pieImage, &dest, &rendStyle);
        hRep--;
        dest.x += Image->Width;
      }
      //draw remainder
      if (hRemainder > 0)
      {
        pieImage.tw = hRemainder;
        dest.w = hRemainder;
        pie_DrawImage(&pieImage, &dest, &rendStyle);
      }
    }
  }
}

void pie_ImageFileIDStretch(IMAGEFILE* ImageFile, UWORD ID, int x, int y, int Width, int Height)
{
  IMAGEDEF* Image;
  PIEIMAGE pieImage;
  PIERECT dest;
  assert(ID < ImageFile->Header.NumImages);

  Image = &ImageFile->ImageDefs[ID];

  pie_SetBilinear(FALSE);
  pie_SetRendMode(REND_GOURAUD_TEX);
  pie_SetColour(COLOURINTENSITY);
  pie_SetColourKeyedBlack(TRUE);

  pieImage.texPage = ImageFile->TPageIDs[Image->TPageID];
  pieImage.tu = Image->Tu;
  pieImage.tv = Image->Tv;
  pieImage.tw = Image->Width;
  pieImage.th = Image->Height;

  dest.x = x + Image->XOffset;
  dest.y = y + Image->YOffset;
  dest.w = Width;
  dest.h = Height;
  pie_DrawImage(&pieImage, &dest, &rendStyle);
}

void pie_ImageDef(IMAGEDEF* Image, iBitmap* Bmp, UDWORD Modulus, int x, int y, BOOL bBilinear)
{
  PIEIMAGE pieImage;
  PIERECT dest;

  pie_SetBilinear(bBilinear); //changed by alex 19 oct 98
  pie_SetRendMode(REND_GOURAUD_TEX);
  pie_SetColour(COLOURINTENSITY);
  pie_SetColourKeyedBlack(TRUE);

  pieImage.texPage = Image->TPageID;
  pieImage.tu = Image->Tu;
  pieImage.tv = Image->Tv;
  pieImage.tw = Image->Width;
  pieImage.th = Image->Height;
  dest.x = x + Image->XOffset;
  dest.y = y + Image->YOffset;
  dest.w = Image->Width;
  dest.h = Image->Height;
  pie_DrawImage(&pieImage, &dest, &rendStyle);
  pie_SetBilinear(FALSE); //changed by alex 19 oct 98
}

void pie_UploadDisplayBuffer(UBYTE* DisplayBuffer)
{
  //only call inside D3D render
  pie_GlobalRenderEnd(FALSE);
  screen_Upload((iBitmap*)DisplayBuffer);
  screen_SetBackDrop((iBitmap*)DisplayBuffer, pie_GetVideoBufferWidth(), pie_GetVideoBufferHeight());
  pie_GlobalRenderBegin();
}

BOOL pie_InitRadar(void) { return TRUE; }

BOOL pie_ShutdownRadar(void) { return TRUE; }

void pie_DownLoadRadar(iBitmap* buffer, UDWORD texPageID) { dtm_LoadRadarSurface(buffer); }

void pie_RenderRadar(IMAGEDEF* Image, iBitmap* Bmp, UDWORD Modulus, int x, int y)
{
  PIEIMAGE pieImage;
  PIERECT dest;
  //special case of pie_ImageDef
  pie_SetBilinear(TRUE);
  pie_SetRendMode(REND_GOURAUD_TEX);
  pie_SetColour(COLOURINTENSITY);
  pie_SetColourKeyedBlack(TRUE);
  //special case function because texture is held outside of texture list
  pieImage.texPage = RADAR_TEXPAGE_D3D;
  pieImage.tu = 0;
  pieImage.tv = 0;
  pieImage.tw = dtm_GetRadarTexImageSize();
  pieImage.th = dtm_GetRadarTexImageSize();
  dest.x = x;
  dest.y = y;
  dest.w = 128;
  dest.h = 128;
  pie_DrawImage(&pieImage, &dest, &rendStyle);
}

void pie_RenderRadarRotated(IMAGEDEF* Image, iBitmap* Bmp, UDWORD Modulus, int x, int y, int angle)
{
  PIEIMAGE pieImage;
  PIERECT dest;
  //special case of pie_ImageDef
  pie_SetBilinear(TRUE);
  pie_SetRendMode(REND_GOURAUD_TEX);
  pie_SetColour(COLOURINTENSITY);
  pie_SetColourKeyedBlack(TRUE);
  //special case function because texture is held outside of texture list
  pieImage.texPage = RADAR_TEXPAGE_D3D;
  pieImage.tu = 0;
  pieImage.tv = 0;
  pieImage.tw = dtm_GetRadarTexImageSize();
  pieImage.th = dtm_GetRadarTexImageSize();
  dest.x = x;
  dest.y = y;
  dest.w = 128;
  dest.h = 128;
  pie_DrawImage(&pieImage, &dest, &rendStyle);
}

void pie_ResetBackDrop(void) { screen_SetBackDrop(backDropBmp, BACKDROP_WIDTH, BACKDROP_HEIGHT); }

void pie_LoadBackDrop(SCREENTYPE screenType, BOOL b3DFX)
{
  iSprite backDropSprite;
  UDWORD chooser0, chooser1;
  CHAR backd[128];

  (void)b3DFX;

  //randomly load in a backdrop piccy.
  srand(static_cast<unsigned>(time(NULL)));

  chooser0 = 0;
  chooser1 = rand() % 7;

  /* The PCX loader delivers packed 32 bit pixels, so the backdrop loads
   * straight into its bitmap - the 8-to-16-bit conversion pass went with
   * the palette. */
  backDropSprite.width = BACKDROP_WIDTH;
  backDropSprite.height = BACKDROP_HEIGHT;
  backDropSprite.bmp = backDropBmp;

  switch (screenType)
  {
  case SCREEN_RANDOMBDROP:
    sprintf(backd, "texpages\\bdrops\\%d%d-bdrop.pcx", chooser0, chooser1);
    break;
  case SCREEN_MISSIONEND:
    sprintf(backd, "texpages\\bdrops\\missionend.pcx");
    break;
  case SCREEN_SLIDE1:
    sprintf(backd, "texpages\\slides\\slide1.pcx");
    break;
  case SCREEN_SLIDE2:
    sprintf(backd, "texpages\\slides\\slide2.pcx");
    break;
  case SCREEN_SLIDE3:
    sprintf(backd, "texpages\\slides\\slide3.pcx");
    break;
  case SCREEN_SLIDE4:
    sprintf(backd, "texpages\\slides\\slide4.pcx");
    break;
  case SCREEN_SLIDE5:
    sprintf(backd, "texpages\\slides\\slide5.pcx");
    break;

  case SCREEN_CREDITS:
    sprintf(backd, "texpages\\bdrops\\credits.pcx");
    break;

  default:
    sprintf(backd, "texpages\\bdrops\\credits.pcx");
    break;
  }
  if (!pie_PCXLoadToBuffer(backd, &backDropSprite, nullptr))
    return;

  screen_SetBackDrop(backDropBmp, BACKDROP_WIDTH, BACKDROP_HEIGHT);
}

void pie_D3DSetupRenderForFlip(SDWORD surfaceOffsetX, SDWORD surfaceOffsetY, UWORD* pSrcData, SDWORD srcWidth, SDWORD srcHeight,
                               SDWORD srcStride)
{
  gSurfaceOffsetX = surfaceOffsetX;
  gSurfaceOffsetY = surfaceOffsetY;
  pgSrcData = pSrcData;
  gSrcWidth = srcWidth;
  gSrcHeight = srcHeight;
  gSrcStride = srcStride;
}

void pie_D3DRenderForFlip(void)
{
  if (pgSrcData != nullptr)
  {
    pie_RenderImageToBackBuffer(gSurfaceOffsetX, gSurfaceOffsetY, pgSrcData, gSrcWidth, gSrcHeight, gSrcStride);
    pgSrcData = nullptr;
  }
}
