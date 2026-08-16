#include "pch.h"
#include <stdio.h>
#include "Screen.h"
#include <math.h>
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
uint8 palShades[PALETTE_SIZE * PALETTE_SHADE_LEVEL];
BOOL bPaletteInitialised = FALSE;
uint8 colours[16];
/* The present palette packed X8R8G8B8, for the FMV subtitle glyphs - the one
 * path that still expands 8 bit pixels straight into the back buffer.
 *
 * The RGB565 twin this had went with its last reader: the backdrop and FMV
 * buffers do their own conversion from the game palette.
 */
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
  int i;
  iColour* p;

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
  p = psGamePal;

  for (i = 0; i < PALETTE_SIZE; i++)
  {
    p[i].r = pal[i].r;
    p[i].g = pal[i].g;
    p[i].b = pal[i].b;
  }

  pie_SetColourDefines();
  pal_MakePackedPalettes();
  return 0;
}

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

void pal_ShutDown(void)
{
  if (bPaletteInitialised)
  {
    bPaletteInitialised = FALSE;
    delete[] psGamePal;
    psGamePal = nullptr;
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

/*





	End of PC Version 






*/
