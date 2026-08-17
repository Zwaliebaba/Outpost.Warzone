#include "pch.h"

/* Fixed-point shift for the clipper's edge interpolation. Lived in Ivi.h,
 * which had nothing else left in it; this file is its only user. */
#define DIVSHIFT 15
/***************************************************************************/
/*
 * pieClip.c
 *
 * clipping routines for all render modes
 *
 */
/***************************************************************************/

#include "RenderClip.h"
#include "Frame.h"
#include "RenderTypes.h"
#include "PieState.h"
#include "RendMode.h"
#include "Render.h"

/***************************************************************************/
/*
 *	Local Definitions
 */
/***************************************************************************/

/***************************************************************************/
/*
 *	Local Variables
 */
/***************************************************************************/

static BOOL bClipSpecular = TRUE;
static UDWORD videoBufferWidth = 640, videoBufferHeight = 480;

/* The integer factor between the logical canvas above and the physical back
 * buffer. Every coordinate the game computes is logical; D3DDrawPoly and the
 * software compositing paths multiply by this on the way to the device, and
 * the input layer divides the physical mouse position by it. */
static UDWORD g_displayScale = 1;

/***************************************************************************/
/*
 *	Local ProtoTypes
 */
/***************************************************************************/

static int pie_ClipXT(PIEVERTEX* s1, PIEVERTEX* s2, PIEVERTEX* clip);
static int pie_ClipYT(PIEVERTEX* s1, PIEVERTEX* s2, PIEVERTEX* clip);

/***************************************************************************/
/*
 *	Source
 */
/***************************************************************************/
BOOL pie_SetVideoBufferWidth(UDWORD width)
{
  /* Any width goes, as long as the 640-wide UI layout still fits. */
  DEBUG_ASSERT_TEXT(width >= 640, "Warning: width below the 640 the UI lays out on");
  if (width < 640)
    width = 640;
  videoBufferWidth = width;
  return (TRUE);
}

/***************************************************************************/
BOOL pie_SetVideoBufferHeight(UDWORD height)
{
  /* Any height goes, as long as the 480-high UI layout still fits. */
  DEBUG_ASSERT_TEXT(height >= 480, "Warning: height below the 480 the UI lays out on");
  if (height < 480)
    height = 480;
  videoBufferHeight = height;
  return (TRUE);
}

/***************************************************************************/
UDWORD pie_GetVideoBufferWidth(void) { return (videoBufferWidth); }
/***************************************************************************/
UDWORD pie_GetVideoBufferHeight(void) { return (videoBufferHeight); }
/***************************************************************************/

namespace Neuron
{

void SetDisplayScale(UDWORD _scale)
{
  DEBUG_ASSERT_TEXT(_scale >= 1, "SetDisplayScale: scale must be at least 1");
  if (_scale < 1)
    _scale = 1;
  g_displayScale = _scale;
}

UDWORD DisplayScale(void) { return g_displayScale; }

} // namespace Neuron
/***************************************************************************/

void pie_Set2DClip(int x0, int y0, int x1, int y1)
{
  /*	if (pie_GetRenderEngine() == ENGINE_GLIDE)
    {
      grClipWindow((FxU32)x0,(FxU32)y0,(FxU32)x1,(FxU32)y1);
    }
    else if (pie_GetRenderEngine() == ENGINE_D3D)
    {
      D3DSetClipWindow(x0,y0,x1,y1);
    }
    else
  */
  {
    psRendSurface->clip.left = x0;
    psRendSurface->clip.top = y0;
    psRendSurface->clip.right = x1;
    psRendSurface->clip.bottom = y1;
  }
}

//*************************************************************************
// New clipper that clips rgb lighting values
int pie_ClipTextured(int npoints, PIEVERTEX* points, PIEVERTEX* clip, BOOL bSpecular)

{
  static PIEVERTEX xclip[pie_MAX_POLY_SIZE + 4];
  PIEVERTEX *p0, *p1;
  int n1, n, i;

  bClipSpecular = bSpecular;

  p0 = &points[0];
  p1 = &points[1];

  for (i = 0, n1 = 0; i < npoints; i++, p0++, p1++)
  {
    if (i == (npoints - 1))
      p1 = &points[0];

    if ((p0->sx == 1 << 15) || (p0->sy == -1 << 15)) //check for invalid points jps19aug97
      return 0;

    n1 += pie_ClipXT(p0, p1, &xclip[n1]);
  }

  p0 = &xclip[0];
  p1 = &xclip[1];

  for (i = 0, n = 0; i < n1; p0++, p1++, i++)
  {
    if (i == (n1 - 1))
      p1 = &xclip[0];
    n += pie_ClipYT(p0, p1, &clip[n]);
  }

  return n;
}

//*************************************************************************
/* Alex - much faster tri clipper - won't clip owt else tho' */
int pie_ClipTexturedTriangleFast(PIEVERTEX* v1, PIEVERTEX* v2, PIEVERTEX* v3, PIEVERTEX* clipped, BOOL bSpecular)
{
  static PIEVERTEX xClip[pie_MAX_POLY_SIZE + 4]; // plus 4 hopefully is limit?
  static PIEVERTEX *p0, *p1;
  UDWORD numPreY, numAll;
  UDWORD i;

  bClipSpecular = bSpecular;

  numPreY = 0;
  if ((v1->sx > LONG_TEST) OR (v1->sy > LONG_TEST))
  {
    /* bomb out for out of range points */
    return (0);
  }
  numPreY += pie_ClipXT(v1, v2, &xClip[numPreY]);

  if ((v2->sx > LONG_TEST) OR (v2->sy > LONG_TEST))
  {
    /* bomb out for out of range points */
    return (0);
  }
  numPreY += pie_ClipXT(v2, v3, &xClip[numPreY]);

  if ((v3->sx > LONG_TEST) OR (v3->sy > LONG_TEST))
  {
    /* bomb out for out of range points */
    return (0);
  }
  numPreY += pie_ClipXT(v3, v1, &xClip[numPreY]);

  /* We've now clipped against x axis - now for Y */

  p0 = &xClip[0];
  p1 = &xClip[1];

  for (i = 0, numAll = 0; i < numPreY; p0++, p1++, i++)
  {
    if (i == (numPreY - 1))
      p1 = &xClip[0];
    numAll += pie_ClipYT(p0, p1, &clipped[numAll]);
  }

  return numAll;
}

/***************************************************************************/
// New version that clips rgb lighting values
static int pie_ClipXT(PIEVERTEX* s1, PIEVERTEX* s2, PIEVERTEX* clip)

{
  int32 n, dx;
  int32 t;

  n = 1;

  if (s2->sx >= s1->sx)
  {
    if (s1->sx < psRendSurface->clip.left)
    {
      if (s2->sx <= psRendSurface->clip.left)
        return 0;

      dx = s2->sx - s1->sx;

      if (dx != 0)
        clip->sy = s1->sy + (s2->sy - s1->sy) * (psRendSurface->clip.left - s1->sx) / dx;
      else
        clip->sy = s1->sy;

      clip->sx = psRendSurface->clip.left;

      // clip uv
      t = ((clip->sx - s1->sx) << DIVSHIFT) / dx;

      clip->tu = s1->tu + ((t * (s2->tu - s1->tu)) >> DIVSHIFT);
      clip->tv = s1->tv + ((t * (s2->tv - s1->tv)) >> DIVSHIFT);
      clip->sz = s1->sz + ((t * (s2->sz - s1->sz)) >> DIVSHIFT);
      clip->light.byte.r = s1->light.byte.r + ((t * (s2->light.byte.r - s1->light.byte.r)) >> DIVSHIFT);
      clip->light.byte.g = s1->light.byte.g + ((t * (s2->light.byte.g - s1->light.byte.g)) >> DIVSHIFT);
      clip->light.byte.b = s1->light.byte.b + ((t * (s2->light.byte.b - s1->light.byte.b)) >> DIVSHIFT);
      clip->light.byte.a = s1->light.byte.a + ((t * (s2->light.byte.a - s1->light.byte.a)) >> DIVSHIFT);
      if (bClipSpecular)
      {
        clip->specular.byte.r = s1->specular.byte.r + ((t * (s2->specular.byte.r - s1->specular.byte.r)) >> DIVSHIFT);
        clip->specular.byte.g = s1->specular.byte.g + ((t * (s2->specular.byte.g - s1->specular.byte.g)) >> DIVSHIFT);
        clip->specular.byte.b = s1->specular.byte.b + ((t * (s2->specular.byte.b - s1->specular.byte.b)) >> DIVSHIFT);
        clip->specular.byte.a = s1->specular.byte.a + ((t * (s2->specular.byte.a - s1->specular.byte.a)) >> DIVSHIFT);
      }
    }
    else
      *clip = *s1;

    if (s2->sx > psRendSurface->clip.right)
    {
      if (s1->sx > psRendSurface->clip.right)
        return 0;

      clip++;

      dx = s2->sx - s1->sx;

      if (dx != 0)
        clip->sy = s2->sy - (s2->sy - s1->sy) * (s2->sx - psRendSurface->clip.right) / dx;
      else
        clip->sy = s2->sy;

      clip->sx = psRendSurface->clip.right;

      // clip uv
      t = ((clip->sx - s1->sx) << DIVSHIFT) / dx;
      clip->tu = s1->tu + ((t * (s2->tu - s1->tu)) >> DIVSHIFT);
      clip->tv = s1->tv + ((t * (s2->tv - s1->tv)) >> DIVSHIFT);
      clip->sz = s1->sz + ((t * (s2->sz - s1->sz)) >> DIVSHIFT);
      clip->light.byte.r = s1->light.byte.r + ((t * (s2->light.byte.r - s1->light.byte.r)) >> DIVSHIFT);
      clip->light.byte.g = s1->light.byte.g + ((t * (s2->light.byte.g - s1->light.byte.g)) >> DIVSHIFT);
      clip->light.byte.b = s1->light.byte.b + ((t * (s2->light.byte.b - s1->light.byte.b)) >> DIVSHIFT);
      clip->light.byte.a = s1->light.byte.a + ((t * (s2->light.byte.a - s1->light.byte.a)) >> DIVSHIFT);
      if (bClipSpecular)
      {
        clip->specular.byte.r = s1->specular.byte.r + ((t * (s2->specular.byte.r - s1->specular.byte.r)) >> DIVSHIFT);
        clip->specular.byte.g = s1->specular.byte.g + ((t * (s2->specular.byte.g - s1->specular.byte.g)) >> DIVSHIFT);
        clip->specular.byte.b = s1->specular.byte.b + ((t * (s2->specular.byte.b - s1->specular.byte.b)) >> DIVSHIFT);
        clip->specular.byte.a = s1->specular.byte.a + ((t * (s2->specular.byte.a - s1->specular.byte.a)) >> DIVSHIFT);
      }

      n = 2;
    }

    return n;
  }
  if (s1->sx > psRendSurface->clip.right)
  {
    if (s2->sx >= psRendSurface->clip.right)
      return 0;

    dx = s1->sx - s2->sx;

    if (dx != 0)
      clip->sy = s1->sy - (s1->sy - s2->sy) * (s1->sx - psRendSurface->clip.right) / dx;
    else
      clip->sy = s1->sy;

    clip->sx = psRendSurface->clip.right;

    // clip uv
    t = ((clip->sx - s1->sx) << DIVSHIFT) / dx;
    clip->tu = s1->tu + ((t * (s1->tu - s2->tu)) >> DIVSHIFT);
    clip->tv = s1->tv + ((t * (s1->tv - s2->tv)) >> DIVSHIFT);
    clip->sz = s1->sz + ((t * (s1->sz - s2->sz)) >> DIVSHIFT);
    clip->light.byte.r = s1->light.byte.r + ((t * (s1->light.byte.r - s2->light.byte.r)) >> DIVSHIFT);
    clip->light.byte.g = s1->light.byte.g + ((t * (s1->light.byte.g - s2->light.byte.g)) >> DIVSHIFT);
    clip->light.byte.b = s1->light.byte.b + ((t * (s1->light.byte.b - s2->light.byte.b)) >> DIVSHIFT);
    clip->light.byte.a = s1->light.byte.a + ((t * (s1->light.byte.a - s2->light.byte.a)) >> DIVSHIFT);
    if (bClipSpecular)
    {
      clip->specular.byte.r = s1->specular.byte.r + ((t * (s1->specular.byte.r - s2->specular.byte.r)) >> DIVSHIFT);
      clip->specular.byte.g = s1->specular.byte.g + ((t * (s1->specular.byte.g - s2->specular.byte.g)) >> DIVSHIFT);
      clip->specular.byte.b = s1->specular.byte.b + ((t * (s1->specular.byte.b - s2->specular.byte.b)) >> DIVSHIFT);
      clip->specular.byte.a = s1->specular.byte.a + ((t * (s1->specular.byte.a - s2->specular.byte.a)) >> DIVSHIFT);
    }
  }
  else
    *clip = *s1;

  if (s2->sx < psRendSurface->clip.left)
  {
    if (s1->sx < psRendSurface->clip.left)
      return 0;

    clip++;

    dx = s1->sx - s2->sx;

    if (dx != 0)
      clip->sy = s2->sy + (s1->sy - s2->sy) * (psRendSurface->clip.left - s2->sx) / dx;
    else
      clip->sy = s2->sy;

    clip->sx = psRendSurface->clip.left;

    // clip uv
    t = ((clip->sx - s1->sx) << DIVSHIFT) / dx;
    clip->tu = s1->tu + ((t * (s1->tu - s2->tu)) >> DIVSHIFT);
    clip->tv = s1->tv + ((t * (s1->tv - s2->tv)) >> DIVSHIFT);
    clip->sz = s1->sz + ((t * (s1->sz - s2->sz)) >> DIVSHIFT);
    clip->light.byte.r = s1->light.byte.r + ((t * (s1->light.byte.r - s2->light.byte.r)) >> DIVSHIFT);
    clip->light.byte.g = s1->light.byte.g + ((t * (s1->light.byte.g - s2->light.byte.g)) >> DIVSHIFT);
    clip->light.byte.b = s1->light.byte.b + ((t * (s1->light.byte.b - s2->light.byte.b)) >> DIVSHIFT);
    clip->light.byte.a = s1->light.byte.a + ((t * (s1->light.byte.a - s2->light.byte.a)) >> DIVSHIFT);
    if (bClipSpecular)
    {
      clip->specular.byte.r = s1->specular.byte.r + ((t * (s1->specular.byte.r - s2->specular.byte.r)) >> DIVSHIFT);
      clip->specular.byte.g = s1->specular.byte.g + ((t * (s1->specular.byte.g - s2->specular.byte.g)) >> DIVSHIFT);
      clip->specular.byte.b = s1->specular.byte.b + ((t * (s1->specular.byte.b - s2->specular.byte.b)) >> DIVSHIFT);
      clip->specular.byte.a = s1->specular.byte.a + ((t * (s1->specular.byte.a - s2->specular.byte.a)) >> DIVSHIFT);
    }

    n = 2;
  }
  return n;
}

//*************************************************************************
// New version - clips rgb lighting values
static int pie_ClipYT(PIEVERTEX* s1, PIEVERTEX* s2, PIEVERTEX* clip)

{
  int n, dy;
  int32 t;

  n = 1;

  if (s2->sy >= s1->sy)
  {
    if (s1->sy < psRendSurface->clip.top)
    {
      if (s2->sy <= psRendSurface->clip.top)
        return 0;

      dy = s2->sy - s1->sy;

      if (dy != 0)
        clip->sx = s1->sx + (s2->sx - s1->sx) * (psRendSurface->clip.top - s1->sy) / dy;
      else
        clip->sx = s1->sx;

      clip->sy = psRendSurface->clip.top;

      // clip uv
      t = ((clip->sy - s1->sy) << DIVSHIFT) / dy;
      clip->tu = s1->tu + ((t * (s2->tu - s1->tu)) >> DIVSHIFT);
      clip->tv = s1->tv + ((t * (s2->tv - s1->tv)) >> DIVSHIFT);
      clip->sz = s1->sz + ((t * (s2->sz - s1->sz)) >> DIVSHIFT);
      clip->light.byte.r = s1->light.byte.r + ((t * (s2->light.byte.r - s1->light.byte.r)) >> DIVSHIFT);
      clip->light.byte.g = s1->light.byte.g + ((t * (s2->light.byte.g - s1->light.byte.g)) >> DIVSHIFT);
      clip->light.byte.b = s1->light.byte.b + ((t * (s2->light.byte.b - s1->light.byte.b)) >> DIVSHIFT);
      clip->light.byte.a = s1->light.byte.a + ((t * (s2->light.byte.a - s1->light.byte.a)) >> DIVSHIFT);
      if (bClipSpecular)
      {
        clip->specular.byte.r = s1->specular.byte.r + ((t * (s2->specular.byte.r - s1->specular.byte.r)) >> DIVSHIFT);
        clip->specular.byte.g = s1->specular.byte.g + ((t * (s2->specular.byte.g - s1->specular.byte.g)) >> DIVSHIFT);
        clip->specular.byte.b = s1->specular.byte.b + ((t * (s2->specular.byte.b - s1->specular.byte.b)) >> DIVSHIFT);
        clip->specular.byte.a = s1->specular.byte.a + ((t * (s2->specular.byte.a - s1->specular.byte.a)) >> DIVSHIFT);
      }
    }
    else
      *clip = *s1;

    if (s2->sy > psRendSurface->clip.bottom)
    {
      if (s1->sy > psRendSurface->clip.bottom)
        return 0;

      clip++;

      dy = s2->sy - s1->sy;

      if (dy != 0)
        clip->sx = s2->sx - (s2->sx - s1->sx) * (s2->sy - psRendSurface->clip.bottom) / dy;
      else
        clip->sx = s2->sx;

      clip->sy = psRendSurface->clip.bottom;

      // clip uv
      t = ((clip->sy - s1->sy) << DIVSHIFT) / dy;
      clip->tu = s1->tu + ((t * (s2->tu - s1->tu)) >> DIVSHIFT);
      clip->tv = s1->tv + ((t * (s2->tv - s1->tv)) >> DIVSHIFT);
      clip->sz = s1->sz + ((t * (s2->sz - s1->sz)) >> DIVSHIFT);
      clip->light.byte.r = s1->light.byte.r + ((t * (s2->light.byte.r - s1->light.byte.r)) >> DIVSHIFT);
      clip->light.byte.g = s1->light.byte.g + ((t * (s2->light.byte.g - s1->light.byte.g)) >> DIVSHIFT);
      clip->light.byte.b = s1->light.byte.b + ((t * (s2->light.byte.b - s1->light.byte.b)) >> DIVSHIFT);
      clip->light.byte.a = s1->light.byte.a + ((t * (s2->light.byte.a - s1->light.byte.a)) >> DIVSHIFT);
      if (bClipSpecular)
      {
        clip->specular.byte.r = s1->specular.byte.r + ((t * (s2->specular.byte.r - s1->specular.byte.r)) >> DIVSHIFT);
        clip->specular.byte.g = s1->specular.byte.g + ((t * (s2->specular.byte.g - s1->specular.byte.g)) >> DIVSHIFT);
        clip->specular.byte.b = s1->specular.byte.b + ((t * (s2->specular.byte.b - s1->specular.byte.b)) >> DIVSHIFT);
        clip->specular.byte.a = s1->specular.byte.a + ((t * (s2->specular.byte.a - s1->specular.byte.a)) >> DIVSHIFT);
      }

      n = 2;
    }

    return n;
  }
  if (s1->sy > psRendSurface->clip.bottom)
  {
    if (s2->sy >= psRendSurface->clip.bottom)
      return 0;

    dy = s1->sy - s2->sy;

    if (dy != 0)
      clip->sx = s1->sx - (s1->sx - s2->sx) * (s1->sy - psRendSurface->clip.bottom) / dy;
    else
      clip->sx = s1->sx;

    clip->sy = psRendSurface->clip.bottom;

    // clip uv
    t = ((clip->sy - s1->sy) << DIVSHIFT) / dy;
    clip->tu = s1->tu + ((t * (s1->tu - s2->tu)) >> DIVSHIFT);
    clip->tv = s1->tv + ((t * (s1->tv - s2->tv)) >> DIVSHIFT);
    clip->sz = s1->sz + ((t * (s1->sz - s2->sz)) >> DIVSHIFT);
    clip->light.byte.r = s1->light.byte.r + ((t * (s1->light.byte.r - s2->light.byte.r)) >> DIVSHIFT);
    clip->light.byte.g = s1->light.byte.g + ((t * (s1->light.byte.g - s2->light.byte.g)) >> DIVSHIFT);
    clip->light.byte.b = s1->light.byte.b + ((t * (s1->light.byte.b - s2->light.byte.b)) >> DIVSHIFT);
    clip->light.byte.a = s1->light.byte.a + ((t * (s1->light.byte.a - s2->light.byte.a)) >> DIVSHIFT);
    if (bClipSpecular)
    {
      clip->specular.byte.r = s1->specular.byte.r + ((t * (s1->specular.byte.r - s2->specular.byte.r)) >> DIVSHIFT);
      clip->specular.byte.g = s1->specular.byte.g + ((t * (s1->specular.byte.g - s2->specular.byte.g)) >> DIVSHIFT);
      clip->specular.byte.b = s1->specular.byte.b + ((t * (s1->specular.byte.b - s2->specular.byte.b)) >> DIVSHIFT);
      clip->specular.byte.a = s1->specular.byte.a + ((t * (s1->specular.byte.a - s2->specular.byte.a)) >> DIVSHIFT);
    }
  }
  else
    *clip = *s1;

  if (s2->sy < psRendSurface->clip.top)
  {
    if (s1->sy < psRendSurface->clip.top)
      return 0;

    clip++;

    dy = s1->sy - s2->sy;

    if (dy != 0)
      clip->sx = s2->sx + (s1->sx - s2->sx) * (psRendSurface->clip.top - s2->sy) / dy;
    else
      clip->sx = s2->sx;

    clip->sy = psRendSurface->clip.top;

    // clip uv
    t = ((clip->sy - s1->sy) << DIVSHIFT) / dy;
    clip->tu = s1->tu + ((t * (s1->tu - s2->tu)) >> DIVSHIFT);
    clip->tv = s1->tv + ((t * (s1->tv - s2->tv)) >> DIVSHIFT);
    clip->sz = s1->sz + ((t * (s1->sz - s2->sz)) >> DIVSHIFT);
    clip->light.byte.r = s1->light.byte.r + ((t * (s1->light.byte.r - s2->light.byte.r)) >> DIVSHIFT);
    clip->light.byte.g = s1->light.byte.g + ((t * (s1->light.byte.g - s2->light.byte.g)) >> DIVSHIFT);
    clip->light.byte.b = s1->light.byte.b + ((t * (s1->light.byte.b - s2->light.byte.b)) >> DIVSHIFT);
    clip->light.byte.a = s1->light.byte.a + ((t * (s1->light.byte.a - s2->light.byte.a)) >> DIVSHIFT);
    if (bClipSpecular)
    {
      clip->specular.byte.r = s1->specular.byte.r + ((t * (s1->specular.byte.r - s2->specular.byte.r)) >> DIVSHIFT);
      clip->specular.byte.g = s1->specular.byte.g + ((t * (s1->specular.byte.g - s2->specular.byte.g)) >> DIVSHIFT);
      clip->specular.byte.b = s1->specular.byte.b + ((t * (s1->specular.byte.b - s2->specular.byte.b)) >> DIVSHIFT);
      clip->specular.byte.a = s1->specular.byte.a + ((t * (s1->specular.byte.a - s2->specular.byte.a)) >> DIVSHIFT);
    }

    n = 2;
  }

  return n;
}

int pie_ClipFlat2dLine(SDWORD x0, SDWORD y0, SDWORD x1, SDWORD y1)
{
  if ((x0 > LONG_TEST) OR (y0 > LONG_TEST))
  {
    /* bomb out for out of range points */
    return (0);
  }

  if ((x1 > LONG_TEST) OR (y1 > LONG_TEST))
  {
    /* bomb out for out of range points */
    return (0);
  }

  //check x0
  if (x0 < psRendSurface->clip.left)
  {
    if (x1 < psRendSurface->clip.left)
      return (0);
    //relign left edge

    y0 = y0 + ((y1 - y0) * (psRendSurface->clip.left - x0)) / (x1 - x0);
    x0 = psRendSurface->clip.left;
  }
  else if (x0 > psRendSurface->clip.right)
  {
    if (x1 > psRendSurface->clip.right)
      return (0);
    //relign right edge
    y0 = y0 - ((y1 - y0) * (psRendSurface->clip.right - x0)) / (x1 - x0);
    x0 = psRendSurface->clip.right;
  }
  //check y0
  if (y0 < psRendSurface->clip.top)
  {
    if (y1 < psRendSurface->clip.top)
      return (0);
    //relign top

    x0 = x0 + ((x1 - x0) * (psRendSurface->clip.top - y0)) / (y1 - y0);
    y0 = psRendSurface->clip.top;
  }
  else if (y0 > psRendSurface->clip.bottom)
  {
    if (y1 > psRendSurface->clip.bottom)
      return (0);
    //relign bottom
    x0 = x0 - ((x1 - x0) * (psRendSurface->clip.bottom - y0)) / (y1 - y0);
    y0 = psRendSurface->clip.bottom;
  }
  if (x1 < psRendSurface->clip.left)
  {
    //relign left edge
    y1 = y1 + ((y0 - y1) * (psRendSurface->clip.left - x1)) / (x0 - x1);
    x1 = psRendSurface->clip.left;
  }
  else if (x1 > psRendSurface->clip.right)
  {
    //relign right edge
    y1 = y1 - ((y0 - y1) * (psRendSurface->clip.right - x1)) / (x0 - x1);
    x1 = psRendSurface->clip.right;
  }
  //check y1
  if (y1 < psRendSurface->clip.top)
  {
    //relign top
    x1 = x1 + ((x0 - x1) * (psRendSurface->clip.top - y1)) / (y0 - y1);
    y1 = psRendSurface->clip.top;
  }
  else if (y1 > psRendSurface->clip.bottom)
  {
    //relign bottom
    x1 = x1 - ((x0 - x1) * (psRendSurface->clip.bottom - y1)) / (y0 - y1);
    y1 = psRendSurface->clip.bottom;
  }
  return 2;
}
