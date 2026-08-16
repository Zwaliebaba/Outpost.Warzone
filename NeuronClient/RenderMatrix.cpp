#include "pch.h"
/***************************************************************************/
/*
 * pieMatrix.c
 *
 * matrix functions for pumpkin image library.
 *
 * Phase 10 stage B: the stack is DirectXMath. The pie_* entry points keep
 * their fixed-point signatures as transitional shims for the game's call
 * sites (stage C rewrites those and deletes the shims); the arithmetic
 * behind them is XMMATRIX/XMVECTOR. In DEBUG builds the old fixed-point
 * pipeline is maintained in parallel as a parity shadow so a run can
 * measure the divergence the swap introduced -- stage D deletes it.
 * See Docs/Phase10Plan.md.
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
  XMVECTOR XM_CALLCONV TransformPoint(SDWORD _x, SDWORD _y, SDWORD _z)
  {
    return XMVector3Transform(XMVectorSet(static_cast<float>(_x), static_cast<float>(_y), static_cast<float>(_z), 1.0f),
                              g_matrixStack[g_matrixIndex]);
  }

  // The old integer projection divided x by (z >> pshift), so the focal
  // scale in float is exactly 1 << pshift.
  float FocalScaleX(void) { return static_cast<float>(1 << psRendSurface->xpshift); }
  float FocalScaleY(void) { return static_cast<float>(1 << psRendSurface->ypshift); }
}

DirectX::XMMATRIX& Neuron::WorldMatrix(void) { return g_matrixStack[g_matrixIndex]; }

#ifdef DEBUG

namespace
{
  /* The stage-B parity shadow: the fixed-point pipeline stage B replaced,
   * maintained alongside the XMMATRIX stack so a CAM_1A run can measure the
   * divergence between the two. Every shim mirrors its operation here in
   * 4.12 fixed point, exactly as pieMatrix.c computed it. Stage D deletes
   * this block, pie_MatParityCheck and pie_MatParityReport.
   */
  struct ParityMatrix
  {
    SDWORD a, b, c, d, e, f, g, h, i, j, k, l;
  };

  ParityMatrix g_parityStack[MatrixStackDepth];

  float g_parityWorstX = 0.0f;
  float g_parityWorstY = 0.0f;
  int g_parityPoints = 0;
  int g_parityVisMismatches = 0;

  ParityMatrix& ParityTop(void) { return g_parityStack[g_matrixIndex]; }
}

void pie_MatParityCheck(SDWORD _x, SDWORD _y, SDWORD _z, float _sx, float _sy)
{
  const ParityMatrix& m = ParityTop();

  const int32 fx = _x * m.a + _y * m.d + _z * m.g + m.j;
  const int32 fy = _x * m.b + _y * m.e + _z * m.h + m.k;
  const int32 fz = _x * m.c + _y * m.f + _z * m.i + m.l;

  const int32 zz = fz >> STRETCHED_Z_SHIFT;
  const int32 zfx = fz >> psRendSurface->xpshift;
  const int32 zfy = fz >> psRendSurface->ypshift;

  SDWORD fixedX, fixedY;
  if ((zfx <= 0) || (zfy <= 0) || (zz < MIN_STRETCHED_Z))
  {
    fixedX = LONG_WAY;
    fixedY = LONG_WAY;
  }
  else
  {
    fixedX = psRendSurface->xcentre + (fx / zfx);
    fixedY = psRendSurface->ycentre - (fy / zfy);
  }

  g_parityPoints++;

  const bool fixedVisible = (fixedX != LONG_WAY);
  const bool floatVisible = (_sx != static_cast<float>(LONG_WAY));
  if (fixedVisible != floatVisible)
  {
    g_parityVisMismatches++; // a point straddling the near limit; counted, not compared
    return;
  }

  if (fixedVisible)
  {
    const float divergenceX = std::fabs(_sx - static_cast<float>(fixedX));
    const float divergenceY = std::fabs(_sy - static_cast<float>(fixedY));
    if (divergenceX > g_parityWorstX)
      g_parityWorstX = divergenceX;
    if (divergenceY > g_parityWorstY)
      g_parityWorstY = divergenceY;
  }
}

void pie_MatParityReport(void)
{
  Neuron::DebugTrace("Phase 10 parity: {} points, worst divergence x={:.2f} y={:.2f} pixels, {} visibility mismatches",
                     g_parityPoints, g_parityWorstX, g_parityWorstY, g_parityVisMismatches);
}

#endif // DEBUG

//*************************************************************************
//*** create new matrix from current transformation matrix and make current
//*
//******

void pie_MatBegin(void)

{
  g_matrixIndex++;
  DEBUG_ASSERT_TEXT(g_matrixIndex < MatrixStackDepth, "pie_MatBegin past top of the stack");
  g_matrixStack[g_matrixIndex] = g_matrixStack[g_matrixIndex - 1];
#ifdef DEBUG
  g_parityStack[g_matrixIndex] = g_parityStack[g_matrixIndex - 1];
#endif
}

//*************************************************************************
//*** make current transformation matrix previous one on stack
//*
//******

void pie_MatEnd(void)

{
  g_matrixIndex--;
  DEBUG_ASSERT_TEXT(g_matrixIndex >= 0, "pie_MatEnd of the bottom of the stack");
}

//*************************************************************************
//*** matrix rotate y (yaw) current transformation matrix
//*
//******

void pie_MatRotY(int y)

{
  if (y != 0)
  {
    XMMATRIX& m = g_matrixStack[g_matrixIndex];
    m = XMMatrixMultiply(XMMatrixRotationY(y * Neuron::RadiansPerWorldAngle), m);

#ifdef DEBUG
    ParityMatrix& p = ParityTop();
    const int32 cra = COS(y), sra = SIN(y);
    int32 t;
    t = ((cra * p.a) - (sra * p.g)) >> FP12_SHIFT;
    p.g = ((sra * p.a) + (cra * p.g)) >> FP12_SHIFT;
    p.a = t;
    t = ((cra * p.b) - (sra * p.h)) >> FP12_SHIFT;
    p.h = ((sra * p.b) + (cra * p.h)) >> FP12_SHIFT;
    p.b = t;
    t = ((cra * p.c) - (sra * p.i)) >> FP12_SHIFT;
    p.i = ((sra * p.c) + (cra * p.i)) >> FP12_SHIFT;
    p.c = t;
#endif
  }
}

//*************************************************************************
//*** matrix rotate z (roll) current transformation matrix
//*
//******

void pie_MatRotZ(int z)

{
  if (z != 0)
  {
    XMMATRIX& m = g_matrixStack[g_matrixIndex];
    m = XMMatrixMultiply(XMMatrixRotationZ(z * Neuron::RadiansPerWorldAngle), m);

#ifdef DEBUG
    ParityMatrix& p = ParityTop();
    const int32 cra = COS(z), sra = SIN(z);
    int32 t;
    t = ((cra * p.a) + (sra * p.d)) >> FP12_SHIFT;
    p.d = ((cra * p.d) - (sra * p.a)) >> FP12_SHIFT;
    p.a = t;
    t = ((cra * p.b) + (sra * p.e)) >> FP12_SHIFT;
    p.e = ((cra * p.e) - (sra * p.b)) >> FP12_SHIFT;
    p.b = t;
    t = ((cra * p.c) + (sra * p.f)) >> FP12_SHIFT;
    p.f = ((cra * p.f) - (sra * p.c)) >> FP12_SHIFT;
    p.c = t;
#endif
  }
}

//*************************************************************************
//*** matrix rotate x (pitch) current transformation matrix
//*
//******

void pie_MatRotX(int x)

{
  if (x != 0)
  {
    XMMATRIX& m = g_matrixStack[g_matrixIndex];
    m = XMMatrixMultiply(XMMatrixRotationX(x * Neuron::RadiansPerWorldAngle), m);

#ifdef DEBUG
    ParityMatrix& p = ParityTop();
    const int32 cra = COS(x), sra = SIN(x);
    int32 t;
    t = ((cra * p.d) + (sra * p.g)) >> FP12_SHIFT;
    p.g = ((cra * p.g) - (sra * p.d)) >> FP12_SHIFT;
    p.d = t;
    t = ((cra * p.e) + (sra * p.h)) >> FP12_SHIFT;
    p.h = ((cra * p.h) - (sra * p.e)) >> FP12_SHIFT;
    p.e = t;
    t = ((cra * p.f) + (sra * p.i)) >> FP12_SHIFT;
    p.i = ((cra * p.i) - (sra * p.f)) >> FP12_SHIFT;
    p.f = t;
#endif
  }
}

//*************************************************************************
//*** set the translation row of the current matrix (was the pie_MATTRANS
//*** macro: j,k,l = x,y,z << FP12_SHIFT)
//*
//******

void pie_MATTRANS(int _x, int _y, int _z)
{
  g_matrixStack[g_matrixIndex].r[3] =
    XMVectorSet(static_cast<float>(_x), static_cast<float>(_y), static_cast<float>(_z), 1.0f);

#ifdef DEBUG
  ParityMatrix& p = ParityTop();
  p.j = _x << FP12_SHIFT;
  p.k = _y << FP12_SHIFT;
  p.l = _z << FP12_SHIFT;
#endif
}

//*************************************************************************
//*** translate the current matrix by a local offset (was the pie_TRANSLATE
//*** macro)
//*
//******

void pie_TRANSLATE(int _x, int _y, int _z)
{
  XMMATRIX& m = g_matrixStack[g_matrixIndex];
  m = XMMatrixMultiply(XMMatrixTranslation(static_cast<float>(_x), static_cast<float>(_y), static_cast<float>(_z)), m);

#ifdef DEBUG
  ParityMatrix& p = ParityTop();
  p.j += _x * p.a + _y * p.d + _z * p.g;
  p.k += _x * p.b + _y * p.e + _z * p.h;
  p.l += _x * p.c + _y * p.f + _z * p.i;
#endif
}

//*************************************************************************
//*** scale the rotation part of the current matrix uniformly (was
//*** Display3D.cpp's scaleMatrix writing the nine 3x3 elements directly;
//*** the scale arrives in FP12, so 4096 is 100%)
//*
//******

void pie_MatScale(SDWORD _scaleFP12)
{
  const float scale = static_cast<float>(_scaleFP12) / static_cast<float>(FP12_MULTIPLIER);
  XMMATRIX& m = g_matrixStack[g_matrixIndex];
  m = XMMatrixMultiply(XMMatrixScaling(scale, scale, scale), m);

#ifdef DEBUG
  ParityMatrix& p = ParityTop();
  p.a = (p.a * _scaleFP12) / FP12_MULTIPLIER;
  p.b = (p.b * _scaleFP12) / FP12_MULTIPLIER;
  p.c = (p.c * _scaleFP12) / FP12_MULTIPLIER;
  p.d = (p.d * _scaleFP12) / FP12_MULTIPLIER;
  p.e = (p.e * _scaleFP12) / FP12_MULTIPLIER;
  p.f = (p.f * _scaleFP12) / FP12_MULTIPLIER;
  p.g = (p.g * _scaleFP12) / FP12_MULTIPLIER;
  p.h = (p.h * _scaleFP12) / FP12_MULTIPLIER;
  p.i = (p.i * _scaleFP12) / FP12_MULTIPLIER;
#endif
}

//*************************************************************************
//*** 3D vector perspective projection
//*
//* params	v1 = 3D vector to project
//* 			v2 = pointer to 2D resultant vector
//*
//* on exit	v2 = projected vector
//*
//* returns	rotated and translated z component of v1
//*
//******

int32 pie_RotProj(iVector* v3d, iPoint* v2d)

{
  const XMVECTOR world = TransformPoint(v3d->x, v3d->y, v3d->z);
  const float rz = XMVectorGetZ(world);
  const float depth = rz * Neuron::StretchedDepthScale;

  if (depth < static_cast<float>(MIN_STRETCHED_Z))
  {
    v2d->x = LONG_WAY; //just along way off screen
    v2d->y = LONG_WAY;
  }
  else
  {
    v2d->x = psRendSurface->xcentre + static_cast<int32>(XMVectorGetX(world) * FocalScaleX() / rz);
    v2d->y = psRendSurface->ycentre - static_cast<int32>(XMVectorGetY(world) * FocalScaleY() / rz);
  }

#ifdef DEBUG
  pie_MatParityCheck(v3d->x, v3d->y, v3d->z, static_cast<float>(v2d->x), static_cast<float>(v2d->y));
#endif

  return static_cast<int32>(depth);
}

//*************************************************************************
//*** project a point already in model space to screen, clipping only at the
//*** near plane (was the pie_ROTATE_PROJECT macro, which unlike pie_RotProj
//*** did not test MIN_STRETCHED_Z)
//*
//******

void pie_RotateProjectNear(SDWORD _x, SDWORD _y, SDWORD _z, SDWORD* _sx, SDWORD* _sy)
{
  const XMVECTOR world = TransformPoint(_x, _y, _z);
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
//*** rotate and translate a point by the current matrix without projecting
//*** (was the pie_ROTATE_TRANSLATE macro)
//*
//******

void pie_RotateTranslate(SDWORD _x, SDWORD _y, SDWORD _z, SDWORD* _xs, SDWORD* _ys, SDWORD* _zs)
{
  const XMVECTOR world = TransformPoint(_x, _y, _z);
  *_xs = static_cast<SDWORD>(std::lrintf(XMVectorGetX(world)));
  *_ys = static_cast<SDWORD>(std::lrintf(XMVectorGetY(world)));
  *_zs = static_cast<SDWORD>(std::lrintf(XMVectorGetZ(world)));
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

void pie_SetGeometricOffset(int x, int y)

{
  psRendSurface->xcentre = x;
  psRendSurface->ycentre = y;
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

#ifdef DEBUG
  g_parityStack[0] = ParityMatrix{FP12_MULTIPLIER, 0, 0, 0, FP12_MULTIPLIER, 0, 0, 0, FP12_MULTIPLIER, 0, 0, 0};
#endif
}
