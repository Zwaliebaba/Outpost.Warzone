/***************************************************************************/
/*
 * RenderModel.h
 *
 * The draw entry points implemented in RenderModel.cpp and Render2D.cpp.
 *
 * These were declared in PieDef.h, which was a types header; the types moved
 * to RenderTypes.h and the declarations came here, beside the code.
 */
/***************************************************************************/

#ifndef _renderModel_h
#define _renderModel_h

#include "Frame.h"
#include "RenderTypes.h"
#include "Model.h"

extern void pie_Draw3DShape(iIMDShape* shape, int frame, int team, UDWORD colour, UDWORD specular, int pieFlag, int pieData);
extern void pie_DrawImage(PIEIMAGE* image, PIERECT* dest, PIESTYLE* style);
extern void pie_DrawImage270(PIEIMAGE* image, PIERECT* dest, PIESTYLE* style);

//PIEVERTEX line draw for all hardware modes
extern void pie_DrawLine(SDWORD x0, SDWORD y0, SDWORD x1, SDWORD y1, UDWORD colour, BOOL bclip);
//PIEVERTEX poly draw for all hardware modes
extern void pie_DrawPoly(SDWORD numVrts, PIEVERTEX* aVrts, SDWORD texPage, void* psEffects);

extern void pie_GetResetCounts(SDWORD* pPieCount, SDWORD* pTileCount, SDWORD* pPolyCount, SDWORD* pStateCount);

#endif // _renderModel_h
