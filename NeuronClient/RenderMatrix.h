/***************************************************************************/
/*
 * RenderMatrix.h
 *
 * The renderer's transform stack and world -> screen projection, on
 * DirectXMath. Call sites compose local transforms onto the current matrix
 * directly, pre-multiplied:
 *
 *   Neuron::MatrixPush();
 *   DirectX::XMMATRIX& world = Neuron::WorldMatrix();
 *   world = DirectX::XMMatrixTranslation(x, y, z) * world;
 *   ...
 *   Neuron::MatrixPop();
 *
 * A reference names the stack slot that was current when it was taken, so
 * take it after MatrixPush and do not carry it across MatrixPop.
 *
 */
/***************************************************************************/
#ifndef _renderMatrix_h
#define _renderMatrix_h

#include <directxmath.h>

#include "RenderTypes.h"
#include "RendMode.h"

namespace Neuron
{
  // The renderer's depth scale. The fixed-point pipeline computed depth as
  // FP12 z >> STRETCHED_Z_SHIFT, i.e. world z times this factor, and
  // MIN_STRETCHED_Z, MAX_Z and the depth-sort ranges are calibrated to it.
  inline constexpr float StretchedDepthScale = static_cast<float>(FP12_MULTIPLIER >> STRETCHED_Z_SHIFT);

  // The current model -> camera transform: the top of the matrix stack.
  extern DirectX::XMMATRIX& WorldMatrix(void);

  extern void MatrixPush(void);
  extern void MatrixPop(void);

  // World -> screen through the current matrix. Returns the stretched depth
  // (world z through the matrix, times StretchedDepthScale); writes LONG_WAY
  // to both coordinates when the point is at or behind the near limit.
  // Integer in and out because that is what every caller's world state is;
  // stage E revisits the boundary with the angle units.
  extern SDWORD ProjectToScreen(SDWORD _x, SDWORD _y, SDWORD _z, SDWORD* _sx, SDWORD* _sy);

  extern void SetGeometricOffset(int _x, int _y);

  // Reset the stack and make the first matrix identity.
  extern void MatrixInit(void);
}

#endif
