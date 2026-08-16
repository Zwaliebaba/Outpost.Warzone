#include "pch.h"
#include <stdio.h>
#include "Screen.h"
#include <math.h>
#include "PieState.h"
#include "Palette.h"
#include "RendMode.h"

/*



	PC VERSION !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!


*/

iColour* psGamePal = nullptr;
BOOL bPaletteInitialised = FALSE;

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

  return 0;
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

iColour* pie_GetGamePal(void)
{
  DEBUG_ASSERT_TEXT(bPaletteInitialised, "pie_GetGamePal, palette not initialised");
  return psGamePal;
}

/*





	End of PC Version 






*/
