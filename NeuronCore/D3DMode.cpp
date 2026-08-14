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
static D3DTLVERTEX d3dVrts[pie_MAX_POLY_VERTS];

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

/***************************************************************************/
/* SetD3DFlags translated the PIE_ polygon flags into the D3D_TRI_ flags the
 * Direct3D 6 triangle setup took. Nothing called it, and DrawPrimitiveUP has
 * no per-triangle flags to take, so it has gone with them.
 */
/***************************************************************************/

void _palette_D3D(int i, int r, int g, int b)
{
  i;
  r;
  g;
  b;
}

/***************************************************************************/

void _vsync_D3D(void) {}

void _dummyFunc1_D3D(int i, int j, unsigned int k)
{
  i;
  j;
  k;
}

/***************************************************************************/

void _dummyFunc2_D3D(int i, int j, int k, int l, unsigned int m)
{
  i;
  j;
  k;
  l;
  m;
}

/***************************************************************************/

void _dummyFunc3_D3D(int i, int j, int k, unsigned int l)
{
  i;
  j;
  k;
  l;
}

/***************************************************************************/

void _dummyFunc4_D3D(unsigned char* i, int j, int k, int l, int m)
{
  i;
  j;
  k;
  l;
  m;
}

/***************************************************************************/

void _dummyFunc5_D3D(unsigned char* i, int j, int k, int l, int m, int n)
{
  i;
  j;
  k;
  l;
  m;
  n;
}

/***************************************************************************/

void _dummyFunc6_D3D(unsigned char* i, int j, int k, int l, int m, int n, int p)
{
  i;
  j;
  k;
  l;
  m;
  n;
  p;
}

/***************************************************************************/

void _dummyFunc7_D3D(int i, int j, int k, unsigned int l)
{
  i;
  j;
  k;
  l;
}

/***************************************************************************/

void _dummyFunc8_D3D(iVertex* i, iTexture* j, uint32 k)
{
  i;
  j;
  k;
}

/***************************************************************************/

void _dummyFunc9_D3D(int i, int j, int k, unsigned int l)
{
  i;
  j;
  k;
  l;
}

/***************************************************************************/

void SetTransFilter_D3D(UDWORD filter, UDWORD tablenumber) {}
void TransBoxFill_D3D(UDWORD a, UDWORD b, UDWORD c, UDWORD d) {}
