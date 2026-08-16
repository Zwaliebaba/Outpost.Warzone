#include "pch.h"
#include <directxmath.h>
#include "Frame.h"
#include "Model.h"
#include "IMD.h"
#include "RendMode.h"
#include "PieFunc.h"
#include "RenderMatrix.h"
#include "Tex.h"
#include "RenderTypes.h"
#include "RenderModel.h"
#include "D3D9Vertex.h"
#include "PieState.h"
#include "RenderClip.h"
#include "Render.h"

using namespace DirectX;

#define MIST

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

static PIEPIXEL scrPoints[pie_MAX_POINTS];
static PIEVERTEX pieVrts[pie_MAX_POLY_VERTS];
static PIEVERTEX clippedVrts[pie_MAX_POLY_VERTS];
static D3DTLVERTEX d3dVrts[pie_MAX_POLY_VERTS];
static SDWORD pieCount = 0;
static SDWORD tileCount = 0;
static SDWORD polyCount = 0;

/***************************************************************************/
/*
 *	Local ProtoTypes
 */
/***************************************************************************/

//pievertex draw poly (low level) //all modes from PIEVERTEX data
/* Only these two static functions ever see it, so it stays out of the headers. */
using PIEPOLY = struct
{
  UDWORD flags;
  SDWORD nVrts;
  PIEVERTEX* pVrts;
  iTexAnim* pTexAnim;
};

static void pie_PiePoly(PIEPOLY* poly, BOOL bClip);
static void pie_PiePolyFrame(PIEPOLY* poly, SDWORD frame, BOOL bClip);

/***************************************************************************/
/*
 *	Source
 */
/***************************************************************************/

/***************************************************************************
 * pie_Draw3dShape
 *
 * Project and render a pumpkin image to render surface
 * Will support zbuffering, texturing, coloured lighting and alpha effects
 * Avoids recalculating vertex projections for every poly 
 ***************************************************************************/

void pie_Draw3DShape(iIMDShape* shape, int frame, int team, UDWORD col, UDWORD spec, int pieFlag, int pieFlagData)
{
  // needed for AMD
  int amd_scale = 0x3a800000; // 2^-10
  int amd_pie_RAISE_SCALE = 0x3b800000; // 2^-8
  int amd_sign = 0x80000000;
  int amd_RAISE = 0;
  int amd_HEIGHT_SCALED = 0x3f800000;

  // needed for intel
  int32 tempY;

  int i, n;
  iVector* pVertices;
  PIEPIXEL* pPixels;
  iIMDPoly* pPolys;
  PIEPOLY piePoly;
  VERTEXID* index;
  PIELIGHT colour, specular;
  UBYTE alpha;

  pieCount++;

  // Fix for transparent buildings and features!! */
  if ((pieFlag & pie_TRANSLUCENT) AND (pieFlagData > 220))
    pieFlag = pieFlagData = 0; // force to bilinear and non-transparent
  // Fix for transparent buildings and features!! */

  // WARZONE light as byte passed in colour so expand
  if (col <= MAX_UB_LIGHT)
  {
    colour.byte.a = 255; //no fog
    colour.byte.r = static_cast<UBYTE>(col);
    colour.byte.g = static_cast<UBYTE>(col);
    colour.byte.b = static_cast<UBYTE>(col);
  }
  else
    colour.argb = col;
  specular.argb = spec;

  if (frame == 0)
    frame = team;

  if (!pie_Translucent())
  {
    if ((pieFlag & pie_ADDITIVE) || (pieFlag & pie_TRANSLUCENT))
    {
      pieFlag &= (pie_FLAG_MASK - pie_TRANSLUCENT - pie_ADDITIVE);
      pieFlag |= pie_NO_BILINEAR;
    }
  }
  else if (!pie_Additive())
  {
    if (pieFlag & pie_ADDITIVE) //Assume also translucent
    {
      pieFlag -= pie_ADDITIVE;
      pieFlag |= pie_TRANSLUCENT;
      pieFlag |= pie_NO_BILINEAR;
      pieFlagData /= 2;
    }
  }
  /* Set tranlucency */
  if (pieFlag & pie_ADDITIVE) //Assume also translucent
  {
    pie_SetFogStatus(FALSE);
    pie_SetRendMode(REND_ADDITIVE_TEX);
    alpha = 255 - specular.byte.a;
    alpha = pie_ByteScale(alpha, static_cast<UBYTE>(pieFlagData)); //scale transparency by fog value
    colour.byte.a = alpha;
    colour.byte.r = pie_ByteScale(alpha, colour.byte.r);
    colour.byte.g = pie_ByteScale(alpha, colour.byte.g);
    colour.byte.b = pie_ByteScale(alpha, colour.byte.b);
    specular.argb = 0;
    pie_SetBilinear(TRUE);
  }
  else if (pieFlag & pie_TRANSLUCENT)
  {
    pie_SetFogStatus(FALSE);
    pie_SetRendMode(REND_ALPHA_TEX);
    alpha = 255 - specular.byte.a;
    alpha = pie_ByteScale(alpha, static_cast<UBYTE>(pieFlagData)); //scale transparency by fog value
    colour.byte.a = alpha;
    specular.argb = 0;
    pie_SetBilinear(FALSE); //never bilinear with constant alpha, gives black edges 
  }
  else
  {
    if (pieFlag & pie_BUTTON)
    {
      pie_SetFogStatus(FALSE);
      pie_SetDepthBufferStatus(DEPTH_CMP_LEQ_WRT_ON);
    }
    else
      pie_SetFogStatus(TRUE);
    pie_SetRendMode(REND_GOURAUD_TEX);
    //if hardware fog then alpha is set else unused in decal mode
    if (pieFlag & pie_NO_BILINEAR)
      pie_SetBilinear(FALSE);
    else
      pie_SetBilinear(TRUE);
  }

  if (pieFlag & pie_RAISE)
    pieFlagData = (shape->ymax * (pie_RAISE_SCALE - pieFlagData)) / pie_RAISE_SCALE;

  pie_SetTexturePage(shape->texpage);

  //now draw the shape
  //rotate and project points from shape->points to scrPoints
  pVertices = shape->points;
  pPixels = &scrPoints[0];

  const XMMATRIX worldMatrix = Neuron::WorldMatrix();
  const float focalX = static_cast<float>(1 << psRendSurface->xpshift);
  const float focalY = static_cast<float>(1 << psRendSurface->ypshift);

  for (i = 0; i < shape->npoints; i++, pVertices++, pPixels++)
  {
    tempY = pVertices->y;
    if (pieFlag & pie_RAISE)
    {
      tempY = pVertices->y - pieFlagData;
      if (tempY < 0)
        tempY = 0;
    }
    else if (pieFlag & pie_HEIGHT_SCALED)
    {
      if (pVertices->y > 0)
        tempY = (pVertices->y * pieFlagData) / pie_RAISE_SCALE;
    }
    const XMINT3 modelPoint(pVertices->x, tempY, pVertices->z);
    const XMVECTOR world = XMVector3Transform(XMLoadSInt3(&modelPoint), worldMatrix);
    const float rz = XMVectorGetZ(world);

    pPixels->d3dz = rz * Neuron::StretchedDepthScale;

    if (pPixels->d3dz < D3DVAL(MIN_STRETCHED_Z))
    {
      pPixels->d3dx = static_cast<float>(LONG_WAY); //just along way off screen
      pPixels->d3dy = static_cast<float>(LONG_WAY);
    }
    else
    {
      pPixels->d3dx = static_cast<float>(psRendSurface->xcentre) + XMVectorGetX(world) * focalX / rz;
      pPixels->d3dy = static_cast<float>(psRendSurface->ycentre) - XMVectorGetY(world) * focalY / rz;
    }

#ifdef DEBUG
    pie_MatParityCheck(pVertices->x, tempY, pVertices->z, pPixels->d3dx, pPixels->d3dy);
#endif
  }

  //--

  //build and render polygons
  pPolys = shape->polys;
  for (i = 0; i < shape->npolys; i++, pPolys++)
  {
    index = pPolys->pindex;
    piePoly.flags = pPolys->flags;
    if (pieFlag & pie_TRANSLUCENT)
      piePoly.flags |= PIE_ALPHA;
    else if (pieFlag & pie_ADDITIVE)
      piePoly.flags &= (0xffffffff - PIE_COLOURKEYED); //dont treat additive images as colour keyed
    for (n = 0; n < pPolys->npnts; n++, index++)
    {
      pieVrts[n].sx = std::lrintf(scrPoints[*index].d3dx);
      pieVrts[n].sy = std::lrintf(scrPoints[*index].d3dy);
      //cull triangles with off screen points
      if (scrPoints[*index].d3dy > static_cast<float>(LONG_TEST))
        piePoly.flags = 0;
      pieVrts[n].sz = std::lrintf(scrPoints[*index].d3dz);
      pieVrts[n].tu = pPolys->vrt[n].u;
      pieVrts[n].tv = pPolys->vrt[n].v;
      pieVrts[n].light.argb = colour.argb;
      pieVrts[n].specular.argb = specular.argb;
    }
    piePoly.nVrts = pPolys->npnts;
    piePoly.pVrts = &pieVrts[0];
    piePoly.pTexAnim = pPolys->pTexAnim;
    if (piePoly.flags > 0)
      pie_PiePolyFrame(&piePoly, frame,TRUE); // draw the polygon ... this is an inline function
  }
  if (pieFlag & pie_BUTTON)
    pie_SetDepthBufferStatus(DEPTH_CMP_ALWAYS_WRT_ON);
}
// THE VERSION MIKE CHANGED.
// 3D NOW Specific (and FASTER) shape renderer.
/***************************************************************************
 * pie_Drawimage
 *
 * General purpose blit function
 * Will support zbuffering, non_textured, coloured lighting and alpha effects
 *
 * replaces all ivis blit functions 
 *
 ***************************************************************************/
//d3d loses edge pixels in triangle draw
//this is a temporary correction that may become an option 

# define EDGE_CORRECTION 0

void pie_DrawImage(PIEIMAGE* image, PIERECT* dest, PIESTYLE* style)
{
  /* Set transparent color to be 0 red, 0 green, 0 blue, 0 alpha */
  polyCount++;

  pie_SetTexturePage(image->texPage);

  style->colour.argb = pie_GetColour();
  style->specular.argb = 0x00000000;

  //set up 4 pie verts
  pieVrts[0].sx = dest->x;
  pieVrts[0].sy = dest->y;
  pieVrts[0].sz = static_cast<SDWORD>(INTERFACE_DEPTH);
  pieVrts[0].tu = image->tu;
  pieVrts[0].tv = image->tv;
  pieVrts[0].light.argb = style->colour.argb;
  pieVrts[0].specular.argb = style->specular.argb;

  pieVrts[1].sx = dest->x + dest->w + EDGE_CORRECTION;
  pieVrts[1].sy = dest->y;
  pieVrts[1].sz = static_cast<SDWORD>(INTERFACE_DEPTH);
  pieVrts[1].tu = image->tu + image->tw + EDGE_CORRECTION;
  pieVrts[1].tv = image->tv;
  pieVrts[1].light.argb = style->colour.argb;
  pieVrts[1].specular.argb = style->specular.argb;

  pieVrts[2].sx = dest->x + dest->w + EDGE_CORRECTION;
  pieVrts[2].sy = dest->y + dest->h + EDGE_CORRECTION;
  pieVrts[2].sz = static_cast<SDWORD>(INTERFACE_DEPTH);
  pieVrts[2].tu = image->tu + image->tw + EDGE_CORRECTION;
  pieVrts[2].tv = image->tv + image->th + EDGE_CORRECTION;
  pieVrts[2].light.argb = style->colour.argb;
  pieVrts[2].specular.argb = style->specular.argb;

  pieVrts[3].sx = dest->x;
  pieVrts[3].sy = dest->y + dest->h + EDGE_CORRECTION;
  pieVrts[3].sz = static_cast<SDWORD>(INTERFACE_DEPTH);
  pieVrts[3].tu = image->tu;
  pieVrts[3].tv = image->tv + image->th + EDGE_CORRECTION;
  pieVrts[3].light.argb = style->colour.argb;
  pieVrts[3].specular.argb = style->specular.argb;

  D3D_PIEPolygon(4, pieVrts);
}

/***************************************************************************
 * pie_Drawimage270
 *
 * General purpose blit function
 * Will support zbuffering, non_textured, coloured lighting and alpha effects
 *
 * replaces all ivis blit functions 
 *
 ***************************************************************************/

void pie_DrawImage270(PIEIMAGE* image, PIERECT* dest, PIESTYLE* style)
{
  /* Set transparent color to be 0 red, 0 green, 0 blue, 0 alpha */
  polyCount++;

  pie_SetTexturePage(image->texPage);

  style->colour.argb = pie_GetColour();
  style->specular.argb = 0x00000000;

  //set up 4 pie verts
  //set up 4 pie verts
  pieVrts[0].sx = dest->x;
  pieVrts[0].sy = dest->y;
  pieVrts[0].sz = static_cast<SDWORD>(INTERFACE_DEPTH);
  pieVrts[3].tu = image->tu;
  pieVrts[3].tv = image->tv;
  pieVrts[0].light.argb = style->colour.argb;
  pieVrts[0].specular.argb = style->specular.argb;

  pieVrts[1].sx = dest->x + dest->h + EDGE_CORRECTION;
  pieVrts[1].sy = dest->y;
  pieVrts[1].sz = static_cast<SDWORD>(INTERFACE_DEPTH);
  pieVrts[0].tu = image->tu + image->tw + EDGE_CORRECTION;
  pieVrts[0].tv = image->tv;
  pieVrts[1].light.argb = style->colour.argb;
  pieVrts[1].specular.argb = style->specular.argb;

  pieVrts[2].sx = dest->x + dest->h + EDGE_CORRECTION;
  pieVrts[2].sy = dest->y + dest->w + EDGE_CORRECTION;
  pieVrts[2].sz = static_cast<SDWORD>(INTERFACE_DEPTH);
  pieVrts[1].tu = image->tu + image->tw + EDGE_CORRECTION;
  pieVrts[1].tv = image->tv + image->th + EDGE_CORRECTION;
  pieVrts[2].light.argb = style->colour.argb;
  pieVrts[2].specular.argb = style->specular.argb;

  pieVrts[3].sx = dest->x;
  pieVrts[3].sy = dest->y + dest->w + EDGE_CORRECTION;
  pieVrts[3].sz = static_cast<SDWORD>(INTERFACE_DEPTH);
  pieVrts[2].tu = image->tu;
  pieVrts[2].tv = image->tv + image->th + EDGE_CORRECTION;
  pieVrts[3].light.argb = style->colour.argb;
  pieVrts[3].specular.argb = style->specular.argb;

  D3D_PIEPolygon(4, pieVrts);
}

/***************************************************************************
 * pie_DrawLine
 *
 * universal line function for hardware
 *
 * Assumes render mode set up externally
 *
 ***************************************************************************/

void pie_DrawLine(SDWORD x0, SDWORD y0, SDWORD x1, SDWORD y1, UDWORD colour, BOOL bClip)
{
  SDWORD n;
  polyCount++;

  pie_SetTexturePage(-1);

  if (bClip)
  {
    n = pie_ClipFlat2dLine(x0, y0, x1, y1);
    if (n != 2)
      return;
  }
  d3dVrts[0].sx = static_cast<float>(x0);
  d3dVrts[0].sy = static_cast<float>(y0);

  d3dVrts[0].sz = (INTERFACE_DEPTH) * INV_MAX_Z;
  d3dVrts[0].rhw = static_cast<float>(1.0) / d3dVrts[0].sz;

  d3dVrts[0].tu = static_cast<float>(0.0);
  d3dVrts[0].tv = static_cast<float>(0.0);
  d3dVrts[0].color = colour;
  d3dVrts[0].specular = 0;

  memcpy(&d3dVrts[1], &d3dVrts[0], sizeof(D3DTLVERTEX));
  d3dVrts[1].sx = static_cast<float>(x1);
  d3dVrts[1].sy = static_cast<float>(y1);

#ifndef NO_RENDER
  D3DDrawPoly(2, &d3dVrts[0]);
#endif
}

/***************************************************************************
 * pie_DrawRect
 *
 * universal rectangle function for hardware
 *
 * Assumes render mode set up externally, draws filled rectangle
 *
 ***************************************************************************/
# define D3D_RECT_CORRECTION 0

void pie_DrawRect(SDWORD x0, SDWORD y0, SDWORD x1, SDWORD y1, UDWORD colour, BOOL bClip)
{
  SDWORD swap;
  polyCount++;

  if (bClip)
  {
    if (x0 > psRendSurface->clip.right || x1 < psRendSurface->clip.left || y0 > psRendSurface->clip.bottom || y1 < psRendSurface->clip.top)
      return;

    if (x0 < psRendSurface->clip.left)
      x0 = psRendSurface->clip.left;
    if (x1 > psRendSurface->clip.right)
      x1 = psRendSurface->clip.right;
    if (y0 < psRendSurface->clip.top)
      y0 = psRendSurface->clip.top;
    if (y1 > psRendSurface->clip.bottom)
      y1 = psRendSurface->clip.bottom;
  }
  if (x1 < x0)
  {
    swap = x0;
    x0 = x1;
    x1 = swap;
  }
  if (y1 < y0)
  {
    swap = y0;
    y0 = y1;
    y1 = swap;
  }
  d3dVrts[0].sx = static_cast<float>(x0);
  d3dVrts[0].sy = static_cast<float>(y0);
  //cull triangles with off screen points
  d3dVrts[0].sz = (INTERFACE_DEPTH) * INV_MAX_Z;
  d3dVrts[0].rhw = static_cast<float>(1.0) / d3dVrts[0].sz;

  d3dVrts[0].tu = static_cast<float>(0.0);
  d3dVrts[0].tv = static_cast<float>(0.0);
  d3dVrts[0].color = colour;
  d3dVrts[0].specular = 0;

  memcpy(&d3dVrts[1], &d3dVrts[0], sizeof(D3DTLVERTEX));
  memcpy(&d3dVrts[2], &d3dVrts[0], sizeof(D3DTLVERTEX));
  memcpy(&d3dVrts[3], &d3dVrts[0], sizeof(D3DTLVERTEX));
  memcpy(&d3dVrts[4], &d3dVrts[0], sizeof(D3DTLVERTEX));

  d3dVrts[1].sx = static_cast<float>(x1) + D3D_RECT_CORRECTION;
  d3dVrts[1].sy = static_cast<float>(y0);

  d3dVrts[2].sx = static_cast<float>(x1) + D3D_RECT_CORRECTION;
  d3dVrts[2].sy = static_cast<float>(y1) + D3D_RECT_CORRECTION;

  d3dVrts[3].sx = static_cast<float>(x0);
  d3dVrts[3].sy = static_cast<float>(y1) + D3D_RECT_CORRECTION;

#ifndef NO_RENDER
  D3DDrawPoly(3, &d3dVrts[2]);
  D3DDrawPoly(3, &d3dVrts[0]);
#endif
}

/***************************************************************************
 * pie_PiePoly
 *
 * universal poly draw function for hardware
 *
 * Assumes render mode set up externally
 *
 ***************************************************************************/

static void pie_PiePoly(PIEPOLY* poly, BOOL bClip)
{
  SDWORD n;
  static BOOL bBilinear;

  polyCount++;
  // handle texture animated polygons
  if (!(poly->flags & PIE_NO_CULL) && (poly->nVrts >= 3))
  {
    //cull if backfaced
    if (!pie_PieClockwise(poly->pVrts))
      return; //culled
  }

  if (bClip)
  {
    n = pie_ClipTextured(poly->nVrts, poly->pVrts, &clippedVrts[0],TRUE);
    poly->nVrts = n;
    poly->pVrts = &clippedVrts[0];
  }
  if (poly->nVrts >= 3)
  {
    if (poly->flags & PIE_COLOURKEYED)
    {
      bBilinear = pie_GetBilinear();
      pie_SetBilinear(FALSE);
      D3D_PIEPolygon(poly->nVrts, poly->pVrts);
      pie_SetBilinear(bBilinear);
    }
    else
      D3D_PIEPolygon(poly->nVrts, poly->pVrts);
  }
}

static void pie_PiePolyFrame(PIEPOLY* poly, int frame, BOOL bClip)
{
  int uFrame, vFrame, j, framesPerLine;

  // handle texture animated polygons
  if (!(poly->flags & PIE_NO_CULL) && (poly->nVrts >= 3))
  {
    //cull if backfaced
    if (!pie_PieClockwise(poly->pVrts))
      return; //culled
    poly->flags |= PIE_NO_CULL; //dont check culling again for this poly
  }

  if ((poly->flags & IMD_TEXANIM) && (frame != 0))
  {
    if (poly->pTexAnim != nullptr)
    {
      if (poly->pTexAnim->nFrames >= 0)
        frame %= poly->pTexAnim->nFrames;
      else //frame is colour key
        frame %= (-poly->pTexAnim->nFrames);
      if (frame > 0)
      {
        // HACK - fix this!!!!
        framesPerLine = 256 / poly->pTexAnim->textureWidth;
        //should be		framesPerLine = TEXTEX(texPage)->width / poly->pTexAnim->textureWidth;
        vFrame = 0;
        while (frame >= framesPerLine)
        {
          frame -= framesPerLine;
          vFrame += poly->pTexAnim->textureHeight;
        }
        uFrame = frame * poly->pTexAnim->textureWidth;

        for (j = 0; j < poly->nVrts; j++)
        {
          poly->pVrts[j].tu += uFrame;
          poly->pVrts[j].tv += vFrame;
        }
      }
    }
  }
#ifndef NO_RENDER
  //draw with new texture data
  pie_PiePoly(poly, bClip);
#endif
}

void pie_DrawPoly(SDWORD numVrts, PIEVERTEX* aVrts, SDWORD texPage, void* psEffects)
{
  SDWORD i, nVrts;
  BOOL bClockwise;
  UBYTE alpha, *psAlpha;

  /*	Since this is only used from within source for the terrain draw - we can backface cull the
    polygons.


  */
  if (((aVrts[1].sy - aVrts[0].sy) * (aVrts[2].sx - aVrts[1].sx)) <= ((aVrts[1].sx - aVrts[0].sx) * (aVrts[2].sy - aVrts[1].sy)))
    bClockwise = TRUE;
  else
    return;

  tileCount++;
  pie_SetTexturePage(texPage);
  pie_SetFogStatus(TRUE);
  if (psEffects == nullptr) //jps 15apr99 translucent water code
    pie_SetRendMode(REND_GOURAUD_TEX); //jps 15apr99 old solid water code
  else //jps 15apr99 translucent water code
    pie_SetRendMode(REND_ALPHA_TEX); //jps 15apr99 old solid water code
  pie_SetBilinear(TRUE);

  nVrts = pie_ClipTextured(numVrts, &aVrts[0], &clippedVrts[0], TRUE);
  /* The off-screen test the other draw paths make is deliberately absent
   * here. It was written, into a flags field nothing read, so a terrain poly
   * with an off-screen vertex has always been drawn rather than culled.
   * Preserved as it is: making it cull would change what appears on screen.
   */
  for (i = 0; i < nVrts; i++)
  {
    d3dVrts[i].sx = static_cast<float>(clippedVrts[i].sx);
    d3dVrts[i].sy = static_cast<float>(clippedVrts[i].sy);
    d3dVrts[i].sz = static_cast<float>(clippedVrts[i].sz) * INV_MAX_Z;
    d3dVrts[i].rhw = static_cast<float>(1.0) / static_cast<float>(clippedVrts[i].sz);
    d3dVrts[i].tu = static_cast<float>(clippedVrts[i].tu) * INV_TEX_SIZE;
    d3dVrts[i].tv = static_cast<float>(clippedVrts[i].tv) * INV_TEX_SIZE;
    if (psEffects == nullptr) //jps 15apr99 translucent water code
    {
      d3dVrts[i].color = clippedVrts[i].light.argb; //jps 15apr99 old solid water code
      d3dVrts[i].specular = clippedVrts[i].specular.argb; //jps 15apr99 old solid water code
    }
    else //jps 15apr99 translucent water code
    {
      psAlpha = static_cast<UBYTE*>(psEffects);
      //			alpha = pie_ByteScale(alpha, *psAlpha);//scale transparency by fog value
      alpha = *psAlpha; //dont scale transparency by fog value
      clippedVrts[i].light.byte.a = alpha;
      d3dVrts[i].color = clippedVrts[i].light.argb;
      d3dVrts[i].specular = clippedVrts[i].specular.argb;
    }
  }
  if (nVrts >= 3)
  {
    polyCount++;
    D3DDrawPoly(nVrts, &d3dVrts[0]);
  }
}

void pie_GetResetCounts(SDWORD* pPieCount, SDWORD* pTileCount, SDWORD* pPolyCount, SDWORD* pStateCount)
{
  *pPieCount = pieCount;
  *pTileCount = tileCount;
  *pPolyCount = polyCount;
  *pStateCount = pieStateCount;

  pieCount = 0;
  tileCount = 0;
  polyCount = 0;
  pieStateCount = 0;
}
