/***************************************************************************/
/*
 * pieMatrix.h
 *
 * matrix functions for pumpkin image library.
 *
 * Phase 10 stage B: the transform stack is DirectXMath (an XMMATRIX stack)
 * behind the pie_* names below, which are transitional shims -- stage C
 * rewrites their call sites onto native XMMATRIX composition and deletes
 * them. The names keep their legacy spellings, macros included, so the ~390
 * call sites do not churn twice. See Docs/Phase10Plan.md.
 *
 */
/***************************************************************************/
#ifndef _renderMatrix_h
#define _renderMatrix_h

#include <directxmath.h>

#include "RenderTypes.h"
#include "RendMode.h"

/***************************************************************************/
/*
 *	Global Variables
 */
/***************************************************************************/

extern SDWORD aSinTable[];

//*************************************************************************

namespace Neuron
{
  // 16-bit binary angles (DEG_360 == 65536 == a full circle) to radians.
  // Transitional like the shims: dies in stage E with the last binary-angle
  // state.
  inline constexpr float RadiansPerWorldAngle = DirectX::XM_2PI / 65536.0f;

  // The renderer's depth scale. The fixed-point pipeline computed depth as
  // FP12 z >> STRETCHED_Z_SHIFT, i.e. world z times this factor, and
  // MIN_STRETCHED_Z, MAX_Z and the depth-sort ranges are calibrated to it.
  inline constexpr float StretchedDepthScale = static_cast<float>(FP12_MULTIPLIER >> STRETCHED_Z_SHIFT);

  // The current model -> camera transform: the top of the matrix stack.
  // Compose local transforms onto it directly, pre-multiplied:
  //   XMMATRIX& world = Neuron::WorldMatrix();
  //   world = XMMatrixTranslation(x, y, z) * world;
  // A reference names the stack slot that was current when it was taken, so
  // take it after MatrixPush and do not carry it across MatrixPop.
  extern DirectX::XMMATRIX& WorldMatrix(void);

  extern void MatrixPush(void);
  extern void MatrixPop(void);

  // World -> screen through the current matrix. Returns the stretched depth
  // (world z through the matrix, times StretchedDepthScale); writes LONG_WAY
  // to both coordinates when the point is at or behind the near limit.
  extern SDWORD ProjectToScreen(float _x, float _y, float _z, SDWORD* _sx, SDWORD* _sy);

  extern void SetGeometricOffset(int _x, int _y);
}

//*************************************************************************
// The sine table survives stage B for the game code that still indexes it
// directly; stage C moves those call sites to XMScalarSinCos and deletes it.

#define SIN(X)					aSinTable[(uint16)(X) >> 4]
#define COS(X)					aSinTable[((uint16)(X) >> 4) + 1024]

//*************************************************************************
// Transitional shims for what used to be macros over the fixed-point stack.

extern void pie_MATTRANS(int _x, int _y, int _z);
extern void pie_TRANSLATE(int _x, int _y, int _z);
extern void pie_MatScale(SDWORD _scaleFP12);
extern void pie_RotateProjectNear(SDWORD _x, SDWORD _y, SDWORD _z, SDWORD* _sx, SDWORD* _sy);
extern void pie_RotateTranslate(SDWORD _x, SDWORD _y, SDWORD _z, SDWORD* _xs, SDWORD* _ys, SDWORD* _zs);

// The locals the old pie_ROTATE_PROJECT macro needed; nothing to declare now.
#define pie_SETUP_ROTATE_PROJECT

// Templates because the call sites bind outputs of mixed signedness; the old
// macros assigned through whatever lvalue they were handed.
template <class TX, class TY>
inline void pie_ROTATE_PROJECT(SDWORD _x, SDWORD _y, SDWORD _z, TX& _sx, TY& _sy)
{
  SDWORD sx, sy;
  pie_RotateProjectNear(_x, _y, _z, &sx, &sy);
  _sx = static_cast<TX>(sx);
  _sy = static_cast<TY>(sy);
}

template <class TX, class TY, class TZ>
inline void pie_ROTATE_TRANSLATE(SDWORD _x, SDWORD _y, SDWORD _z, TX& _xs, TY& _ys, TZ& _zs)
{
  SDWORD xs, ys, zs;
  pie_RotateTranslate(_x, _y, _z, &xs, &ys, &zs);
  _xs = static_cast<TX>(xs);
  _ys = static_cast<TY>(ys);
  _zs = static_cast<TZ>(zs);
}

//*************************************************************************

extern void pie_MatInit(void);

//*************************************************************************

extern void pie_MatBegin(void);
extern void pie_MatEnd(void);
extern void pie_MatRotX(int x);
extern void pie_MatRotY(int y);
extern void pie_MatRotZ(int z);
extern int32 pie_RotProj(iVector* v3d, iPoint* v2d);

//*************************************************************************

extern void pie_VectorNormalise(iVector* v);
extern void pie_SurfaceNormal(iVector* p1, iVector* p2, iVector* p3, iVector* v);
extern void pie_SetGeometricOffset(int x, int y);

// PIEVERTEX structure contains much infomation that is not required on the playstation ... and hence is not currently used
extern BOOL pie_PieClockwise(PIEVERTEX* s);

#endif
