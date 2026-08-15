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
#include "D3DMode.h"
#include "RendMode.h"
#include "PieClip.h"

/***************************************************************************/
/*
 *	Local Definitions
 */
/***************************************************************************/
#define DIVIDE_TABLE_SIZE		1024
/***************************************************************************/
/*
 *	Local Variables
 */
/***************************************************************************/
int32 _iVPRIM_DIVTABLE[DIVIDE_TABLE_SIZE];

static SDWORD d3dActive = 0;
static BOOL bDither = FALSE;

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

BOOL pie_GetDitherStatus(void) { return (bDither); }

void pie_SetDitherStatus(BOOL val) { bDither = val; }

BOOL pie_Initialise(void)
{
  BOOL r; //result
  int i;

  pie_InitMaths();
  pie_TexInit();

  rendSurface.flags = REND_SURFACE_UNDEFINED;
  rendSurface.buffer = nullptr;
  rendSurface.size = 0;

  // divtable: first entry == unity to (ie n/0 == 1 !)
  _iVPRIM_DIVTABLE[0] = iV_DIVMULTP;

  for (i = 1; i < DIVIDE_TABLE_SIZE; i++)
    _iVPRIM_DIVTABLE[i - 0] = std::lrintf((1.0f / static_cast<float>(i)) * iV_DIVMULTP);

  pie_MatInit();
  _TEX_INDEX = 0;

  iV_RenderAssign(&rendSurface);
  r = _mode_D3D();

  if (r)
    pie_SetDefaultStates();

  if (r)
    pal_Init();
  else
  {
    iV_ShutDown();
    Neuron::Fatal("Initialise videomode failed");
    return FALSE;
  }
  return TRUE;
}

void pie_ShutDown(void) { _close_D3D(); }

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

void pie_Clear(UDWORD colour) { (void)colour; }
/***************************************************************************/

void pie_GlobalRenderBegin(void)
{
  if (d3dActive == 0)
  {
    d3dActive = 1;
    _renderBegin_D3D();
  }
}

void pie_GlobalRenderEnd(BOOL bForceClearToBlack)
{
  (void)bForceClearToBlack;
  if (d3dActive != 0)
  {
    d3dActive = 0;
    _renderEnd_D3D();
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

/***************************************************************************/
void pie_LocalRenderBegin(void) {}

void pie_LocalRenderEnd(void) {}

void pie_RenderSetup(void) {}
