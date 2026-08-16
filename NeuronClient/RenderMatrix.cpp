#include "pch.h"
/***************************************************************************/
/*
 * RenderMatrix.cpp
 *
 * The renderer's transform stack and world -> screen projection, on
 * DirectXMath. What used to be here -- the 4.12 fixed-point SDMATRIX
 * stack, the 5,120-entry sine table and the pie_* shims over both -- is
 * gone with Phase 10 stage C; the call sites compose XMMATRIX natively.
 *
 */
/***************************************************************************/

#include <cmath>

#include "RenderTypes.h"
#include "RenderMatrix.h"
#include "RendMode.h"

using namespace DirectX;

namespace
{
  constexpr int MatrixStackDepth = 8;

  alignas(16) XMMATRIX g_matrixStack[MatrixStackDepth];
  int g_matrixIndex = 0;

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
  const XMVECTOR world = XMVector3Transform(
    XMVectorSet(static_cast<float>(_x), static_cast<float>(_y), static_cast<float>(_z), 1.0f),
    g_matrixStack[g_matrixIndex]);
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

//*************************************************************************
//*** reset the stack and make the first matrix identity
//*
//******

void Neuron::MatrixInit(void)
{
  g_matrixIndex = 0;
  g_matrixStack[0] = XMMatrixIdentity();
}
