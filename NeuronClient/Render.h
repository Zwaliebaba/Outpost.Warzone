/***************************************************************************/

#ifndef _RENDER_H_
#define _RENDER_H_

/***************************************************************************/

#include "D3D9Vertex.h"
#include "Frame.h"
#include "PieState.h"
#include "RenderTypes.h"

/***************************************************************************/

extern BOOL InitD3D(void);
extern void BeginFrameD3D(void);
extern void EndFrameD3D(void);
extern void ShutDownD3D(void);
extern void BeginSceneD3D(void);
extern void EndSceneD3D(void);
extern void D3D_PIEPolygon(SDWORD numVerts, PIEVERTEX* pVrts);
extern void D3DDrawPoly(int nVerts, D3DTLVERTEX* psVert);
extern void D3DSetTranslucencyMode(TRANSLUCENCY_MODE transMode);

extern void D3DSetColourKeying(BOOL bKeyingOn);
extern void D3DSetDepthWrite(BOOL bWriteEnable);
extern void D3DSetDepthCompare(D3DCMPFUNC depthCompare);

extern void D3DReInit(void);
extern void D3DTestCooperativeLevel(BOOL bGotFocus);

/* Re-apply every render state the device holds. Called after the device is
 * reset, which throws all of them away.
 */
extern void D3DApplyRenderStates(void);

extern LPDIRECT3DDEVICE9 d3d_GetDevice(void);

/***************************************************************************/

#endif	/* _RENDER_H_ */

/***************************************************************************/
