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
UWORD backDropBmp[BACKDROP_WIDTH * BACKDROP_HEIGHT * 2];
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
  iColour* psPalette;

  pie_SetRendMode(REND_FLAT);
  pie_SetColour(colour);
  pie_SetTexturePage(-1);

  /* Get our colour values from the ivis palette */
  psPalette = pie_GetGamePal();
  light.byte.r = psPalette[colour].r;
  light.byte.g = psPalette[colour].g;
  light.byte.b = psPalette[colour].b;
  light.byte.a = MAX_UB_LIGHT;
  pie_DrawLine(x0, y0, x1, y1, light.argb, TRUE);
}

/***************************************************************************/

void pie_Box(int x0, int y0, int x1, int y1, uint32 colour)
{
  PIELIGHT light;
  iColour* psPalette;

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

  psPalette = pie_GetGamePal();
  /* Get our colour values from the ivis palette */
  light.byte.r = psPalette[colour].r;
  light.byte.g = psPalette[colour].g;
  light.byte.b = psPalette[colour].b;
  light.byte.a = MAX_UB_LIGHT;
  pie_DrawLine(x0, y0, x1, y0, light.argb, FALSE);
  pie_DrawLine(x1, y0, x1, y1, light.argb, FALSE);
  pie_DrawLine(x1, y1, x0, y1, light.argb, FALSE);
  pie_DrawLine(x0, y1, x0, y0, light.argb, FALSE);
}

/***************************************************************************/

void pie_BoxFillIndex(int x0, int y0, int x1, int y1, UBYTE colour)
{
  PIELIGHT light;
  iColour* psPalette;

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
  psPalette = pie_GetGamePal();
  light.byte.r = psPalette[colour].r;
  light.byte.g = psPalette[colour].g;
  light.byte.b = psPalette[colour].b;
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
  screen_Upload((UWORD*)DisplayBuffer);
  screen_SetBackDrop((UWORD*)DisplayBuffer, pie_GetVideoBufferWidth(), pie_GetVideoBufferHeight());
  pie_GlobalRenderBegin();
}

BOOL pie_InitRadar(void) { return TRUE; }

BOOL pie_ShutdownRadar(void) { return TRUE; }

void pie_DownLoadRadar(unsigned char* buffer, UDWORD texPageID) { dtm_LoadRadarSurface(buffer); }

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

/*	Converts an 8 bit raw (palettised) source image to a 16 bit RGB565
	destination image.

	The bDummy argument used to say whether the destination was a 3dfx
	surface, which was always 565, as against the display's own 16 bit
	format, which was whatever DirectDraw had handed back. Both are 565 now,
	so the two branches - and the bit mask scanning that fed the second one -
	collapsed into one.
*/
void bufferTo16Bit(UBYTE* origBuffer, UWORD* newBuffer, BOOL bDummy)
{
  UBYTE paletteIndex;
  UDWORD i;
  iColour* psPalette;
  UDWORD size;

  (void)bDummy;

  psPalette = pie_GetGamePal();

  /*
    640*480, 8 bit colour source image 
    640*480, 16 bit colour destination image
  */
  size = BACKDROP_WIDTH * BACKDROP_HEIGHT;
  for (i = 0; i < size; i++)
  {
    /* Get the next colour */
    paletteIndex = *origBuffer++;

    *newBuffer++ = static_cast<UWORD>(((psPalette[paletteIndex].r >> 3) << 11) | ((psPalette[paletteIndex].g >> 2) << 5) | (psPalette[
      paletteIndex].b >> 3));
  }
}

void pie_ResetBackDrop(void) { screen_SetBackDrop(backDropBmp, BACKDROP_WIDTH, BACKDROP_HEIGHT); }

void pie_LoadBackDrop(SCREENTYPE screenType, BOOL b3DFX)
{
  iSprite backDropSprite;
  iBitmap tempBmp[BACKDROP_WIDTH * BACKDROP_HEIGHT];
  UDWORD chooser0, chooser1;
  CHAR backd[128];
  /* The backdrop is always composited in 16 bit now. It used to be left as
   * 8 bit palettised whenever the display was not 16 bit, which meant a
   * palettised display; there is no such display any more. */
  SDWORD bitDepth = 16;

  (void)b3DFX;

  //randomly load in a backdrop piccy.
  srand(static_cast<unsigned>(time(NULL)));

  chooser0 = 0;
  chooser1 = rand() % 7;

  backDropSprite.width = BACKDROP_WIDTH;
  backDropSprite.height = BACKDROP_HEIGHT;
  if (bitDepth == 8)
    backDropSprite.bmp = (UBYTE*)backDropBmp;
  else
    backDropSprite.bmp = tempBmp;

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

  if (bitDepth != 8)
    bufferTo16Bit(tempBmp, backDropBmp, b3DFX); // convert

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
