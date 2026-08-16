#include "pch.h"
/***************************************************************************/
/*
 * piefunc.c
 *
 * extended render routines for 3D rendering
 *
 */
/***************************************************************************/

#include "Frame.h"
#include "Screen.h"

#include "RenderTypes.h"
#include "D3D9Vertex.h"
#include "RendMode.h"
#include "PieFunc.h"
#include "PieState.h"
#include "GTime.h"
#include "RenderMatrix.h"
#include "RenderClip.h"

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
static PIEPIXEL scrPoints[pie_MAX_POLYS];
static PIEVERTEX pieVrts[pie_MAX_POLY_VERTS];
static PIEVERTEX clippedVrts[pie_MAX_POLY_VERTS];
static D3DTLVERTEX d3dVrts[pie_MAX_POLY_VERTS];
static UBYTE aByteScale[256][256];

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

void pie_DownLoadBufferToScreen(void* pSrcData, UDWORD destX, UDWORD destY, UDWORD srcWidth, UDWORD srcHeight, UDWORD srcStride)
{
  pie_D3DSetupRenderForFlip(destX, destY, static_cast<UWORD*>(pSrcData), srcWidth, srcHeight, srcStride);
}

/* ---------------------------------------------------------------------------------- */
void pie_DrawViewingWindow(iVector* v, UDWORD x1, UDWORD y1, UDWORD x2, UDWORD y2, UDWORD colour)
{
  SDWORD clip, i;

  pie_SetTexturePage(-1);
  pie_SetRendMode(REND_ALPHA_FLAT);
  //PIE verts
  pieVrts[0].sx = v[1].x;
  pieVrts[0].sy = v[1].y;
  //cull triangles with off screen points
  pieVrts[0].sz = INTERFACE_DEPTH;

  pieVrts[0].tu = 0.0;
  pieVrts[0].tv = 0.0;
  pieVrts[0].light.argb = colour; //0x7fffffff;
  pieVrts[0].specular.argb = 0;

  memcpy(&pieVrts[1], &pieVrts[0], sizeof(PIEVERTEX));
  memcpy(&pieVrts[2], &pieVrts[0], sizeof(PIEVERTEX));
  memcpy(&pieVrts[3], &pieVrts[0], sizeof(PIEVERTEX));
  memcpy(&pieVrts[4], &pieVrts[0], sizeof(PIEVERTEX));

  pieVrts[1].sx = v[0].x;
  pieVrts[1].sy = v[0].y;

  pieVrts[2].sx = v[2].x;
  pieVrts[2].sy = v[2].y;

  pieVrts[3].sx = v[3].x;
  pieVrts[3].sy = v[3].y;

  pie_Set2DClip(x1, y1, x2 - 1, y2 - 1);
  clip = pie_ClipTextured(4, &pieVrts[0], &clippedVrts[0], FALSE);
  pie_Set2DClip(CLIP_BORDER,CLIP_BORDER, psRendSurface->width - CLIP_BORDER, psRendSurface->height - CLIP_BORDER);

#ifndef NO_RENDER

  if (clip >= 3)
  {
    if (pie_Translucent())
      D3D_PIEPolygon(clip, &clippedVrts[0]);
    else
    {
      for (i = 0; i < (clip - 1); i++)
        pie_Line(clippedVrts[i].sx, clippedVrts[i].sy, clippedVrts[i + 1].sx, clippedVrts[i + 1].sy, colour);
      pie_Line(clippedVrts[clip - 1].sx, clippedVrts[clip - 1].sy, clippedVrts[0].sx, clippedVrts[0].sy, colour);
    }
  }
#endif
}

/* ---------------------------------------------------------------------------------- */
void pie_TransColouredTriangle(PIEVERTEX* vrt, UDWORD rgb, UDWORD trans)
{
  UDWORD clip;

  // Give us a D3D version jezza!
  clip = pie_ClipTexturedTriangleFast(&vrt[0], &vrt[1], &vrt[2], &clippedVrts[0], TRUE);

  if (clip >= 3)
  {
    pie_SetTexturePage(-1);
    pie_SetRendMode(REND_ALPHA_ITERATED);
    D3D_PIEPolygon(clip, &clippedVrts[0]);
  }
}

void pie_InitMaths(void)
{
  UBYTE c;
  UDWORD a, b, bigC;

  for (a = 0; a <= UBYTE_MAX; a++)
  {
    for (b = 0; b <= UBYTE_MAX; b++)
    {
      bigC = a * b;
      bigC /= UBYTE_MAX;
      DEBUG_ASSERT_TEXT(bigC <= UBYTE_MAX, "light_InitMaths; rounding error");
      c = static_cast<UBYTE>(bigC);
      aByteScale[a][b] = c;
    }
  }
}

UBYTE pie_ByteScale(UBYTE a, UBYTE b) { return aByteScale[a][b]; }

//render raw 16 bit data in system memory to the back buffer
//use outside of a D3D scene only
void pie_RenderImageToBackBuffer(SDWORD surfaceOffsetX, SDWORD surfaceOffsetY, UWORD* pSrcData, SDWORD srcWidth, SDWORD srcHeight,
                                 SDWORD srcStride)
{
  SCREEN_LOCK sLock;
  int i, j;
  UWORD* pSrc;
  UDWORD* pDest;

  if (pSrcData == nullptr)
    return;

  if (!screenLockBackBuffer(&sLock))
  {
    Neuron::Fatal("pie_RenderImageToBackBuffer: back buffer lock failed");
    return;
  }

  pSrc = pSrcData;
  //word stride
  srcStride /= 2;

  for (i = 0; i < srcHeight; i++)
  {
    if (surfaceOffsetY + i < 0 || surfaceOffsetY + i >= static_cast<SDWORD>(sLock.height))
    {
      pSrc += srcStride;
      continue;
    }
    pDest = (UDWORD*)(sLock.pPixels + sLock.pitch * (surfaceOffsetY + i)) + surfaceOffsetX;
    for (j = 0; j < srcWidth; j++)
    {
      if (surfaceOffsetX + j >= 0 && surfaceOffsetX + j < static_cast<SDWORD>(sLock.width))
        pDest[j] = screen565To32(pSrc[j]);
    }
    pSrc += srcStride;
  }

  screenUnlockBackBuffer();
}
