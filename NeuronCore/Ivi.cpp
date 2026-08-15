#include "pch.h"
#include "RendMode.h"
#include "PieMode.h"
#include "RenderMatrix.h"
#include "Tex.h"

//*************************************************************************

// pass in true to reset the palette too.
void Neuron::Reset(int bPalReset)
{
  _TEX_INDEX = 0;
  Neuron::ClearFonts(); // Initialise the IVIS font module.
}

void Neuron::ShutDown(void)
{
  pie_ShutDown();
  pie_TexShutDown();
}
