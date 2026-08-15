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
#include "PieDef.h"
#include "PieState.h"
#include "PieMode.h"
#include "PieMatrix.h"
#include "PieFunc.h"
#include "Tex.h"
#include "D3DRender.h"
#include "RendMode.h"
#include "PieClip.h"

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
  pie_MatInit();
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
  iV_RenderAssign(&rendSurface);

  if (!InitD3D())
  {
    iV_ShutDown();
    Neuron::Fatal("Initialise videomode failed");
    return FALSE;
  }

  pie_SetDefaultStates();
  pal_Init();

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
  UDWORD resWidth; //n.b. resolution width implies resolution height...!

  resWidth = pie_GetVideoBufferWidth();
  switch (resWidth)
  {
  case 640:
    return (100); // game runs in 640, so scale factor is 100 (normal)
    break;
  case 800:
    return (125);
    break; // as 800 is 125 percent of 640
  case 960:
    return (150);
    break;
  case 1024:
    return (160);
    break;
  case 1152:
    return (180);
    break;
  case 1280:
    return (200);
    break;
  default: DEBUG_ASSERT_TEXT(FALSE, "Unsupported resolution");
    return (100); // default to 640
    break;
  }
}

