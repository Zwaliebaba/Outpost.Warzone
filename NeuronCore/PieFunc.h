/***************************************************************************/
/*
 * piefunc.h
 *
 * type defines for extended image library functions.
 *
 */
/***************************************************************************/

#ifndef _piefunc_h
#define _piefunc_h

/***************************************************************************/

#include "Frame.h"

#include "D3D9Vertex.h"

/***************************************************************************/
/*
 *	Global Definitions
 */
/***************************************************************************/

/***************************************************************************/
/*
 *	Global Variables
 */
/***************************************************************************/

/***************************************************************************/
/*
 *	Global ProtoTypes
 */
/***************************************************************************/
extern void pie_DownLoadBufferToScreen(void* srcData, UDWORD destX, UDWORD destY, UDWORD srcWidth, UDWORD srcHeight, UDWORD srcStride);
extern void pie_InitMaths(void);
extern UBYTE pie_ByteScale(UBYTE a, UBYTE b);
extern void pie_TransColouredTriangle(PIEVERTEX* vrt, UDWORD rgb, UDWORD trans);
extern void pie_RenderImageToBackBuffer(SDWORD surfaceOffsetX, SDWORD surfaceOffsetY, UWORD* pSrcData, SDWORD srcWidth, SDWORD srcHeight,
                                        SDWORD srcStride);
extern void pie_DrawViewingWindow(iVector* v, UDWORD x1, UDWORD y1, UDWORD x2, UDWORD y2, UDWORD colour);

#endif // _piedef_h
