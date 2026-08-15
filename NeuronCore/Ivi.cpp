#include "pch.h"
#include "RendMode.h"
#include "PieMode.h"
#include "RenderMatrix.h"
#include "Tex.h"

//*************************************************************************

// pass in true to reset the palette too.
void iV_Reset(int bPalReset)
{
  _TEX_INDEX = 0;
  iV_ClearFonts(); // Initialise the IVIS font module.
}

void iV_ShutDown(void)
{
  pie_ShutDown();
  pie_TexShutDown();
}
