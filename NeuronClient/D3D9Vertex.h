/***************************************************************************/
/*
 * D3D9Vertex.h
 *
 * The pre-transformed vertex the renderer feeds Direct3D.
 *
 * Direct3D 6 declared this for us as D3DTLVERTEX. Direct3D 9 dropped the
 * fixed vertex structs in favour of flexible vertex formats, so the same
 * layout is declared here along with the FVF that describes it. The name is
 * kept because the drawing code refers to it several hundred times, and the
 * layout is unchanged, so the vertex filling code did not have to move.
 */
/***************************************************************************/

#ifndef _d3d9vertex_h
#define _d3d9vertex_h

#pragma warning (disable : 4201 4214 4115 4514)
#include <d3d9.h>
#pragma warning (default : 4201 4214 4115)

using D3DTLVERTEX = struct _d3dtlvertex
{
  float sx, sy, sz, rhw; // screen space position and 1/w
  D3DCOLOR color; // diffuse
  D3DCOLOR specular; // specular, used to carry fog
  float tu, tv; // texture coordinates
};

#define D3DFVF_TLVERTEX (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX1)

/* d3d.h spelled a float literal this way and the drawing code still does.
 * Direct3D 9 dropped it along with the fixed point D3DVALUE it came from.
 */
#define D3DVAL(val) ((float)(val))

#endif	/* _d3d9vertex_h */
