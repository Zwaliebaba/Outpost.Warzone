#include "pch.h"

#include "RendMode.h"
#include "PieClip.h"
#include "D3DRender.h"
#include "TexMan.h"

/***************************************************************************/

#define WIDTH_D3D		pie_GetVideoBufferWidth()
#define HEIGHT_D3D		pie_GetVideoBufferHeight()
#define DEPTH_D3D		16
#define SIZE_D3D		(WIDTH_D3D * HEIGHT_D3D)

// whole file is not needed when using PSX data

/***************************************************************************/

static SCREEN_MODE g_ScreenMode;
static D3DINFO g_sD3Dinfo;

/***************************************************************************/

iBool _mode_D3D(void)
{
  int i;

  g_sD3Dinfo.bZBufferOn = TRUE;
  g_sD3Dinfo.bAlphaKey = FALSE;

  // set surface attributes

  rendSurface.buffer = nullptr;
  rendSurface.flags = REND_SURFACE_SCREEN;
  rendSurface.size = SIZE_D3D;
  rendSurface.width = WIDTH_D3D;
  rendSurface.height = HEIGHT_D3D;
  rendSurface.xcentre = WIDTH_D3D >> 1;
  rendSurface.ycentre = HEIGHT_D3D >> 1;
  rendSurface.clip.left = 0;
  rendSurface.clip.top = 0;
  rendSurface.clip.right = WIDTH_D3D;
  rendSurface.clip.bottom = HEIGHT_D3D;
  rendSurface.xpshift = 10;
  rendSurface.ypshift = 10;

  for (i = 0; i < iV_SCANTABLE_MAX; i++)
    rendSurface.scantable[i] = i * WIDTH_D3D;

  g_ScreenMode = screenGetMode();

  return InitD3D(&g_sD3Dinfo);
}

/***************************************************************************/

iBool _mode_D3D_RGB(void)
{
  g_sD3Dinfo.bHardware = FALSE;
  g_sD3Dinfo.bReference = FALSE;
  return _mode_D3D();
}

/***************************************************************************/

iBool _mode_D3D_HAL(void)
{
  g_sD3Dinfo.bHardware = TRUE;
  g_sD3Dinfo.bReference = FALSE;
  return _mode_D3D();
}

/***************************************************************************/

iBool _mode_D3D_REF(void)
{
  g_sD3Dinfo.bHardware = TRUE;
  g_sD3Dinfo.bReference = TRUE;

  return _mode_D3D();
}

/***************************************************************************/

void _close_D3D(void) { ShutDownD3D(); }

/***************************************************************************/

void _renderBegin_D3D(void)
{
  /* check whether need to restart */
  if (g_ScreenMode != screenGetMode())
  {
    D3DReInit();
    g_ScreenMode = screenGetMode();
  }

  BeginSceneD3D();
}

/***************************************************************************/

void _renderEnd_D3D(void) { EndSceneD3D(); }
