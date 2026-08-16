#include "pch.h"
/***************************************************************************/
/*
 * pieMatrix.c
 *
 * matrix functions for pumpkin image library.
 *
 * Phase 10: the transform stack is DirectXMath, reached through the
 * Neuron:: functions below. The remaining pie_* entry points are
 * transitional shims forwarding onto them for the call sites stage C has
 * not rewritten yet; each dies with its last caller. The stage-B parity
 * shadow is gone -- it stayed truthful only while every matrix mutation
 * went through the shims, which stage C's native call sites end. To
 * capture the parity figure, run a Debug build of the stage-B head
 * commit. See Docs/Phase10Plan.md.
 *
 */
/***************************************************************************/

#include <stdio.h>
#include <cmath>

#include "RenderTypes.h"
#include "RenderMatrix.h"
#include "RendMode.h"

using namespace DirectX;

/***************************************************************************/
/*
 *	Local Definitions
 */
/***************************************************************************/

namespace
{
  constexpr int MatrixStackDepth = 8;

  alignas(16) XMMATRIX g_matrixStack[MatrixStackDepth];
  int g_matrixIndex = 0;

  // The current transform applied to a model point, as a register value.
  XMVECTOR XM_CALLCONV TransformPoint(float _x, float _y, float _z)
  {
    return XMVector3Transform(XMVectorSet(_x, _y, _z, 1.0f), g_matrixStack[g_matrixIndex]);
  }

  // The old integer projection divided x by (z >> pshift), so the focal
  // scale in float is exactly 1 << pshift.
  float FocalScaleX(void) { return static_cast<float>(1 << psRendSurface->xpshift); }
  float FocalScaleY(void) { return static_cast<float>(1 << psRendSurface->ypshift); }
}

DirectX::XMMATRIX& Neuron::WorldMatrix(void) { return g_matrixStack[g_matrixIndex]; }

//*************************************************************************
//*** push a copy of the current matrix and make it current
//*
//******

void Neuron::MatrixPush(void)
{
  g_matrixIndex++;
  DEBUG_ASSERT_TEXT(g_matrixIndex < MatrixStackDepth, "MatrixPush past top of the stack");
  g_matrixStack[g_matrixIndex] = g_matrixStack[g_matrixIndex - 1];
}

//*************************************************************************
//*** make the previous matrix on the stack current again
//*
//******

void Neuron::MatrixPop(void)
{
  g_matrixIndex--;
  DEBUG_ASSERT_TEXT(g_matrixIndex >= 0, "MatrixPop off the bottom of the stack");
}

//*************************************************************************
//*** world -> screen through the current matrix
//*
//* returns the stretched depth; writes LONG_WAY to both coordinates when
//* the point is at or behind the near limit (MIN_STRETCHED_Z of depth,
//* 64 world units)
//*
//******

SDWORD Neuron::ProjectToScreen(SDWORD _x, SDWORD _y, SDWORD _z, SDWORD* _sx, SDWORD* _sy)
{
  const XMVECTOR world = TransformPoint(static_cast<float>(_x), static_cast<float>(_y), static_cast<float>(_z));
  const float rz = XMVectorGetZ(world);
  const float depth = rz * Neuron::StretchedDepthScale;

  if (depth < static_cast<float>(MIN_STRETCHED_Z))
  {
    *_sx = LONG_WAY; //just along way off screen
    *_sy = LONG_WAY;
  }
  else
  {
    *_sx = psRendSurface->xcentre + static_cast<SDWORD>(XMVectorGetX(world) * FocalScaleX() / rz);
    *_sy = psRendSurface->ycentre - static_cast<SDWORD>(XMVectorGetY(world) * FocalScaleY() / rz);
  }

  return static_cast<SDWORD>(depth);
}

//*************************************************************************

void Neuron::SetGeometricOffset(int _x, int _y)
{
  psRendSurface->xcentre = _x;
  psRendSurface->ycentre = _y;
}

/***************************************************************************/
/*
 *	Transitional shims -- stage C deletes each with its last caller
 */
/***************************************************************************/

void pie_MatBegin(void) { Neuron::MatrixPush(); }

void pie_MatEnd(void) { Neuron::MatrixPop(); }

void pie_MatRotY(int y)

{
  if (y != 0)
  {
    XMMATRIX& m = g_matrixStack[g_matrixIndex];
    m = XMMatrixMultiply(XMMatrixRotationY(y * Neuron::RadiansPerWorldAngle), m);
  }
}

void pie_MatRotZ(int z)

{
  if (z != 0)
  {
    XMMATRIX& m = g_matrixStack[g_matrixIndex];
    m = XMMatrixMultiply(XMMatrixRotationZ(z * Neuron::RadiansPerWorldAngle), m);
  }
}

void pie_MatRotX(int x)

{
  if (x != 0)
  {
    XMMATRIX& m = g_matrixStack[g_matrixIndex];
    m = XMMatrixMultiply(XMMatrixRotationX(x * Neuron::RadiansPerWorldAngle), m);
  }
}

//*** set the translation row of the current matrix (was the pie_MATTRANS
//*** macro: j,k,l = x,y,z << FP12_SHIFT)

void pie_MATTRANS(int _x, int _y, int _z)
{
  g_matrixStack[g_matrixIndex].r[3] =
    XMVectorSet(static_cast<float>(_x), static_cast<float>(_y), static_cast<float>(_z), 1.0f);
}

//*** translate the current matrix by a local offset (was the pie_TRANSLATE
//*** macro)

void pie_TRANSLATE(int _x, int _y, int _z)
{
  XMMATRIX& m = g_matrixStack[g_matrixIndex];
  m = XMMatrixMultiply(XMMatrixTranslation(static_cast<float>(_x), static_cast<float>(_y), static_cast<float>(_z)), m);
}

//*** scale the rotation part of the current matrix uniformly (was
//*** Display3D.cpp's scaleMatrix writing the nine 3x3 elements directly;
//*** the scale arrives in FP12, so 4096 is 100%)

void pie_MatScale(SDWORD _scaleFP12)
{
  const float scale = static_cast<float>(_scaleFP12) / static_cast<float>(FP12_MULTIPLIER);
  XMMATRIX& m = g_matrixStack[g_matrixIndex];
  m = XMMatrixMultiply(XMMatrixScaling(scale, scale, scale), m);
}

//*** 3D vector perspective projection (the iVector/iPoint spelling of
//*** Neuron::ProjectToScreen)

int32 pie_RotProj(iVector* v3d, iPoint* v2d)

{
  return Neuron::ProjectToScreen(v3d->x, v3d->y, v3d->z, &v2d->x, &v2d->y);
}

//*** project a point already in model space to screen, clipping only at the
//*** near plane (was the pie_ROTATE_PROJECT macro, which unlike pie_RotProj
//*** did not test MIN_STRETCHED_Z)

void pie_RotateProjectNear(SDWORD _x, SDWORD _y, SDWORD _z, SDWORD* _sx, SDWORD* _sy)
{
  const XMVECTOR world = TransformPoint(static_cast<float>(_x), static_cast<float>(_y), static_cast<float>(_z));
  const float rz = XMVectorGetZ(world);

  // The macro tested (z >> pshift) > 0, which in world units is one focal
  // scale of FP12: a quarter of a unit at the pshift of 10 the surface uses.
  const int pshift = psRendSurface->xpshift > psRendSurface->ypshift ? psRendSurface->xpshift : psRendSurface->ypshift;
  const float nearLimit = static_cast<float>(1 << pshift) / static_cast<float>(FP12_MULTIPLIER);

  if (rz >= nearLimit)
  {
    *_sx = psRendSurface->xcentre + static_cast<SDWORD>(XMVectorGetX(world) * FocalScaleX() / rz);
    *_sy = psRendSurface->ycentre - static_cast<SDWORD>(XMVectorGetY(world) * FocalScaleY() / rz);
  }
  else
  {
    *_sx = LONG_WAY;
    *_sy = LONG_WAY;
  }
}

//*************************************************************************
//*** normalise vector in place, scaled to FP12
//*
//* The fixed-point version approximated the magnitude octagonally; the
//* contract is unchanged -- the result is the unit vector scaled by
//* FP12_MULTIPLIER, and a zero vector is left unchanged -- but the
//* magnitude is now exact.
//*
//******

void pie_VectorNormalise(iVector* v)

{
  const XMVECTOR vec =
    XMVectorSet(static_cast<float>(v->x), static_cast<float>(v->y), static_cast<float>(v->z), 0.0f);

  if (XMVectorGetX(XMVector3LengthSq(vec)) > 0.0f)
  {
    const XMVECTOR unit = XMVector3Normalize(vec);
    v->x = static_cast<int32>(std::lrintf(XMVectorGetX(unit) * FP12_MULTIPLIER));
    v->y = static_cast<int32>(std::lrintf(XMVectorGetY(unit) * FP12_MULTIPLIER));
    v->z = static_cast<int32>(std::lrintf(XMVectorGetZ(unit) * FP12_MULTIPLIER));
  }
}

//*************************************************************************
//*** calculate surface normal
//*
//* params	p1,p2,p3	= points for forming 2 vector for cross product
//*			v			= normal vector returned, unit scaled by FP12_MULTIPLIER
//*
//* eg		if a polygon (with n points in clockwise order) normal
//*			is required, p1 = point 0, p2 = point 1, p3 = point n-1
//*
//******

void pie_SurfaceNormal(iVector* p1, iVector* p2, iVector* p3, iVector* v)

{
  const XMVECTOR a = XMVectorSet(static_cast<float>(p3->x - p1->x), static_cast<float>(p3->y - p1->y),
                                 static_cast<float>(p3->z - p1->z), 0.0f);
  const XMVECTOR b = XMVectorSet(static_cast<float>(p2->x - p1->x), static_cast<float>(p2->y - p1->y),
                                 static_cast<float>(p2->z - p1->z), 0.0f);

  const XMVECTOR normal = XMVector3Cross(a, b);
  if (XMVectorGetX(XMVector3LengthSq(normal)) > 0.0f)
  {
    const XMVECTOR unit = XMVector3Normalize(normal);
    v->x = static_cast<int32>(std::lrintf(XMVectorGetX(unit) * FP12_MULTIPLIER));
    v->y = static_cast<int32>(std::lrintf(XMVectorGetY(unit) * FP12_MULTIPLIER));
    v->z = static_cast<int32>(std::lrintf(XMVectorGetZ(unit) * FP12_MULTIPLIER));
  }
  else
    v->x = v->y = v->z = 0;
}

//*************************************************************************

BOOL pie_PieClockwise(PIEVERTEX* s) { return (((s[1].sy - s[0].sy) * (s[2].sx - s[1].sx)) <= ((s[1].sx - s[0].sx) * (s[2].sy - s[1].sy))); }

//*************************************************************************
//*** setup transformation matrices and trig tables
//*
//******

#define SC_TABLESIZE	4096

int aSinTable[SC_TABLESIZE + (SC_TABLESIZE / 4)];

void pie_MatInit(void)
{
  unsigned i, scsize;
  double conv, v;

  // sin/cos table

  scsize = SC_TABLESIZE + (SC_TABLESIZE / 4);
  conv = static_cast<float>((PI / (0.5 * SC_TABLESIZE)));

  for (i = 0; i < scsize; i++)
  {
    v = sin(i * conv) * FP12_MULTIPLIER;

    if (v >= 0.0)
      aSinTable[i] = static_cast<int32>(v + 0.5);
    else
      aSinTable[i] = static_cast<int32>(v - 0.5);
  }

  // reset the stack and make the first matrix identity

  g_matrixIndex = 0;
  g_matrixStack[0] = XMMatrixIdentity();
}
