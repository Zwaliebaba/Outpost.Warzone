#include "pch.h"
/***************************************************************************/
/*
 * pieMode.h
 *
 * renderer control for pumpkin library functions.
 *
 */
/***************************************************************************/

#include "Frame.h"
#include "Screen.h"
#include "PieState.h"
#include "PieMode.h"
#include "RenderMatrix.h"
#include "PieFunc.h"
#include "Tex.h"
#include "Render.h"
#include "RendMode.h"
#include "RenderClip.h"
#include "Palette.h"

/***************************************************************************/
/*
 *	Local Variables
 */
/***************************************************************************/

static SDWORD d3dActive = 0;

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

/*
 * Bring the renderer up, in the order the pieces depend on each other:
 * the maths tables, then the surface the projection reads its centre and
 * clip from, then the device (which brings the texture manager and the
 * render states with it), then the palette the texture upload needs.
 *
 * This used to be spread over pie_Initialise, _mode_D3D, InitD3D and
 * rend_InitD3D across three files, with the mode functions differing only in
 * flags nothing read.
 */
BOOL pie_Initialise(void)
{
  pie_InitMaths();
  pie_TexInit();
  Neuron::MatrixInit();
  _TEX_INDEX = 0;

  rendSurface.flags = REND_SURFACE_SCREEN;
  rendSurface.buffer = nullptr;
  rendSurface.width = pie_GetVideoBufferWidth();
  rendSurface.height = pie_GetVideoBufferHeight();
  rendSurface.xcentre = rendSurface.width >> 1;
  rendSurface.ycentre = rendSurface.height >> 1;
  rendSurface.clip.left = 0;
  rendSurface.clip.top = 0;
  rendSurface.clip.right = rendSurface.width;
  rendSurface.clip.bottom = rendSurface.height;
  rendSurface.xpshift = 10;
  rendSurface.ypshift = 10;
  Neuron::RenderAssign(&rendSurface);

  if (!InitD3D())
  {
    Neuron::ShutDown();
    Neuron::Fatal("Initialise videomode failed");
    return FALSE;
  }

  pie_SetDefaultStates();

  return TRUE;
}

void pie_ShutDown(void) { ShutDownD3D(); }

/***************************************************************************/

void pie_ScreenFlip(CLEAR_MODE clearMode)
{
  pie_D3DRenderForFlip();

  switch (clearMode)
  {
  case CLEAR_OFF:
  case CLEAR_OFF_AND_NO_BUFFER_DOWNLOAD:
    screenFlip(FALSE);
    break;
  case CLEAR_FOG:
    if (pie_GetFogEnabled())
      screen_SetFogColour(pie_GetFogColour());
    else
      screen_SetFogColour(0);
    screenFlip(TRUE);
    break;
  case CLEAR_BLACK: default:
    screen_SetFogColour(0);
    screenFlip(TRUE);
    break;
  }
}

/***************************************************************************/

void pie_GlobalRenderBegin(void)
{
  if (d3dActive == 0)
  {
    d3dActive = 1;
    BeginFrameD3D();
  }
}

void pie_GlobalRenderEnd(BOOL bForceClearToBlack)
{
  (void)bForceClearToBlack;
  if (d3dActive != 0)
  {
    d3dActive = 0;
    EndFrameD3D();
  }
}

/***************************************************************************/
UDWORD pie_GetResScalingFactor(void)
{
  /* The world is scaled so that the horizontal view span stays what the
   * 640-wide layout was designed for, whatever the logical width. The
   * vertical span follows the display's aspect ratio instead of the old
   * 4:3 - scaling off the width is the safe direction, because the fixed
   * visible-tile grid was tuned for the 4:3 span and a wider world view
   * could outrun it. */
  return (pie_GetVideoBufferWidth() * 100) / 640;
}

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
