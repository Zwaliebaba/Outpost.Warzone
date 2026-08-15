#include "pch.h"
#include <stdio.h>
#include <math.h>
#include "Ivi.h"
#include "PieState.h"
#include "Palette.h"
#include "RendMode.h"

#define RED_CHROMATICITY	1
#define GREEN_CHROMATICITY	1
#define BLUE_CHROMATICITY	1

uint8 pal_GetNearestColour(uint8 r, uint8 g, uint8 b);
void pie_SetColourDefines(void);
/*
	This is how far from the end you want the drawn as the artist intended shades
	to appear
*/

#define COLOUR_BALANCE	6		// 3 from the end. (two brighter shades!)

/*



	PC VERSION !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!


*/

iColour* psGamePal = nullptr;
PALETTEENTRY* psWinPal = nullptr;
uint8 palShades[PALETTE_SIZE * PALETTE_SHADE_LEVEL];
BOOL bPaletteInitialised = FALSE;
uint8 colours[16];
/* Look up table for transparency */
/*	entry[x][y] tells you what colour to poke in when you're writing
	x over y
*/
uint8 transLookup[PALETTE_SIZE][PALETTE_SIZE];
/* The present palette packed for the two destinations that still take packed
 * pixels: the 16 bit software buffers (backdrop, FMV frames) which are fixed
 * at RGB565, and the 32 bit display.
 *
 * The Direct3D 6 version of this derived the layout from whatever 16 bit
 * pixel format DirectDraw had handed back for the front buffer, which is why
 * it opened with a page of bit mask scanning. The formats are ours to choose
 * now, so both are constants.
 */
UWORD palette16Bit[PALETTE_SIZE]; //RGB565 version of the present palette
UDWORD palette32Bit[PALETTE_SIZE]; //X8R8G8B8 version of the present palette

BOOL pal_MakePackedPalettes(void)
{
  iColour* psPal;
  UDWORD i;

  psPal = pie_GetGamePal();
  if (psPal == nullptr)
    return FALSE;

  for (i = 0; i < PALETTE_SIZE; i++)
  {
    UDWORD red = psPal[i].r;
    UDWORD green = psPal[i].g;
    UDWORD blue = psPal[i].b;

    palette16Bit[i] = static_cast<UWORD>(((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3));
    palette32Bit[i] = (red << 16) | (green << 8) | blue;
  }

  return TRUE;
}

//*************************************************************************
//*** add a new palette
//*
//* params	pal = pointer to palette to add
//*
//* returns slot number of added palette or -1 if error
//*
//******

BOOL pal_AddNewPalette(iColour* pal)
{
  // PSX version dos'nt use palettes as such, SetRGBLookup sets up a global palette instead which is generally
  // just used for colour index to RGB conversions.
  int i, rg;
  iColour* p;
  PALETTEENTRY* w;

  bPaletteInitialised = TRUE;
  if (psGamePal == nullptr)
  {
    psGamePal = new (std::nothrow) iColour[PALETTE_SIZE];
    if (psGamePal == nullptr)
    {
      Neuron::Fatal("pal_AddNewPalette - Out of memory");
      return FALSE;
    }
  }
  if (psWinPal == nullptr)
  {
    psWinPal = new (std::nothrow) PALETTEENTRY[PALETTE_SIZE];
    if (psGamePal == nullptr)
    {
      Neuron::Fatal("pal_AddNewPalette - Out of memory");
      return FALSE;
    }
  }
  p = psGamePal;
  w = psWinPal;

  for (i = 0; i < PALETTE_SIZE; i++)
  {
#ifdef RED_GREEN
    rg = pal[i].r + pal[i].g; rg /= 2;
    //set pie palette
    p[i].r = rg; p[i].g = rg; p[i].b = pal[i].b;
    //set copy of windows palette
    w[i].peRed = (uint8)rg; w[i].peGreen = (uint8)rg; w[i].peBlue = (uint8)pal[i].b; w[i].peFlags = 0;
#elif defined GREEN_BLUE
    rg = pal[i].g + pal[i].b; rg /= 2;
    //set pie palette
    p[i].r = pal[i].r; p[i].g = rg; p[i].b = rg;
    //set copy of windows palette
    w[i].peRed = (uint8)pal[i].r; w[i].peGreen = (uint8)rg; w[i].peBlue = (uint8)rg; w[i].peFlags = 0;
#else

    //set pie palette
    p[i].r = pal[i].r;
    p[i].g = pal[i].g;
    p[i].b = pal[i].b;
    //set copy of windows palette
    w[i].peRed = pal[i].r;
    w[i].peGreen = pal[i].g;
    w[i].peBlue = pal[i].b;
    w[i].peFlags = 0;
#endif
  }
  //set windows palette
  screenSetPalette(0, PALETTE_SIZE, psWinPal);

  pie_SetColourDefines();
  pal_MakePackedPalettes();
  return 0;
}

void pal_SelectPalette(int n) {}

//*************************************************************************
//***
//*
//******

void pal_SetPalette(void) {}

//*************************************************************************
//*** calculate primary colours for current palette (store in COL_ ..
//*
//* on exit	_iVCOLS[0..15] contain colour values matched
//*			COL_.. below access _iVCOLS[0..15]
//******

void pie_SetColourDefines(void)
{
  COL_BLACK = pal_GetNearestColour(1, 1, 1);
  COL_RED = pal_GetNearestColour(128, 0, 0);
  COL_GREEN = pal_GetNearestColour(0, 128, 0);
  COL_BLUE = pal_GetNearestColour(0, 0, 128);
  COL_CYAN = pal_GetNearestColour(0, 128, 128);
  COL_MAGENTA = pal_GetNearestColour(128, 0, 128);
  COL_BROWN = pal_GetNearestColour(128, 64, 0);
  COL_DARKGREY = pal_GetNearestColour(32, 32, 32);
  COL_GREY = pal_GetNearestColour(128, 128, 128);
  COL_LIGHTRED = pal_GetNearestColour(255, 0, 0);
  COL_LIGHTGREEN = pal_GetNearestColour(0, 255, 0);
  COL_LIGHTBLUE = pal_GetNearestColour(0, 0, 255);
  COL_LIGHTCYAN = pal_GetNearestColour(0, 255, 255);
  COL_LIGHTMAGENTA = pal_GetNearestColour(255, 0, 255);
  COL_YELLOW = pal_GetNearestColour(255, 255, 0);
  COL_WHITE = pal_GetNearestColour(255, 255, 255);
}

//*************************************************************************
//*** init palette (sets default palette and calc primary colours)
//*
//* on exit	psCurrentPalette = pointer to default palette (palette 0)
//******

void pal_Init(void) {}

void pal_ShutDown(void)
{
  if (bPaletteInitialised)
  {
    bPaletteInitialised = FALSE;
    delete[] psGamePal;
    psGamePal = nullptr;
    delete[] psWinPal;
    psWinPal = nullptr;
  }
}

uint8 pal_GetNearestColour(uint8 r, uint8 g, uint8 b)
{
  int c;
  int32 distance_r, distance_g, distance_b, squared_distance;
  int32 best_colour, best_squared_distance;

  DEBUG_ASSERT_TEXT(bPaletteInitialised, "pal_GetNearestColour, palette not initialised.");

  best_squared_distance = 0x10000;

  for (c = 0; c < PALETTE_SIZE; c++)
  {
#if(0)
    distance_r = r - (int32)((float)psGamePal[c].r * RED_CHROMATICITY); distance_g = g - (int32)((float)psGamePal[c].g *
      GREEN_CHROMATICITY); distance_b = b - (int32)((float)psGamePal[c].b * BLUE_CHROMATICITY);
#else
    distance_r = r - psGamePal[c].r;
    distance_g = g - psGamePal[c].g;
    distance_b = b - psGamePal[c].b;
#endif

    squared_distance = distance_r * distance_r + distance_g * distance_g + distance_b * distance_b;

    if (squared_distance < best_squared_distance)
    {
      best_squared_distance = squared_distance;
      best_colour = c;
    }
  }
  if (best_colour == 0)
    best_colour = 1;
  return static_cast<uint8>(best_colour);
}

void pie_BuildSoftwareTransparency(void)
{
  int i, j;
  int red, green, blue;

  for (i = 0; i < PALETTE_SIZE; i++)
  {
    for (j = 0; j < PALETTE_SIZE; j++)
    {
      red = (psGamePal[i].r + psGamePal[j].r) / 2;
      green = (psGamePal[i].g + psGamePal[j].g) / 2;
      blue = (psGamePal[i].b + psGamePal[j].b) / 2;
      transLookup[i][j] = pal_GetNearestColour(red, green, blue);
    }
  }
}

void pal_BuildAdjustedShadeTable(void)
{
  float redFraction, greenFraction, blueFraction;
  int seekRed, seekGreen, seekBlue;
  int numColours;
  int numShades;

  DEBUG_ASSERT_TEXT(bPaletteInitialised, "pal_BuildAdjustedShadeTable, palette not initialised.");

  for (numColours = 0; numColours < 255; numColours++)
  {
    redFraction = static_cast<float>(psGamePal[numColours].r) / static_cast<float>(16);
    greenFraction = static_cast<float>(psGamePal[numColours].g) / static_cast<float>(16);
    blueFraction = static_cast<float>(psGamePal[numColours].b) / static_cast<float>(16);

    for (numShades = COLOUR_BALANCE; numShades < 16 + COLOUR_BALANCE; numShades++)
    {
      seekRed = static_cast<int>((float)numShades * redFraction);
      seekGreen = static_cast<int>((float)numShades * greenFraction);
      seekBlue = static_cast<int>((float)numShades * blueFraction);

      if (seekRed > 255)
        seekRed = 255;
      if (seekGreen > 255)
        seekGreen = 255;
      if (seekBlue > 255)
        seekBlue = 255;

      palShades[(numColours * PALETTE_SHADE_LEVEL) + (numShades - COLOUR_BALANCE)] = pal_GetNearestColour(
        static_cast<uint8>(seekRed), static_cast<uint8>(seekGreen), static_cast<uint8>(seekBlue));
    }
  }
}

iColour* pie_GetGamePal(void)
{
  DEBUG_ASSERT_TEXT(bPaletteInitialised, "pie_GetGamePal, palette not initialised");
  return psGamePal;
}

PALETTEENTRY* pie_GetWinPal(void)
{
  DEBUG_ASSERT_TEXT(bPaletteInitialised, "pie_GetWinPal, palette not initialised");
  return psWinPal;
}

/*





	End of PC Version 






*/
