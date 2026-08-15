#ifndef _bspfunc_h_
#define _bspfunc_h_

#include "Model.h"
#include "BSPIMD.h"

/***************************************************************************/

extern PSBSPTREENODE InitNode(PSBSPTREENODE psBSPNode);

extern void GetPlane(iIMDShape* s, UDWORD PolygonID, PSPLANE psPlane);

#endif
