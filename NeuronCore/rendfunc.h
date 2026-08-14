/***************************************************************************/
/*
 * rendfunc.h
 *
 * render functions for base render library.
 *
 */
/***************************************************************************/

#ifndef _rendFunc_h
#define _rendFunc_h


/***************************************************************************/

#include "frame.h"
#include "piedef.h"

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
//*************************************************************************
// functions accessed dirtectly from rendmode
//*************************************************************************
extern void	SetTransFilter(UDWORD rgb,UDWORD tablenumber);
extern void iV_SetMousePointer(IMAGEFILE *ImageFile,UWORD ImageID);
extern void iV_DrawMousePointer(int x,int y);


extern UDWORD iV_GetMouseFrame(void);

//*************************************************************************
// functions accessed indirectly from rendmode
//*************************************************************************
extern void (*iV_pBox)(int x0, int y0, int x1, int y1, uint32 colour);
extern void (*iV_pBoxFill)(int x0, int y0, int x1, int y1, uint32 colour);


//*************************************************************************
#endif // _rendFunc_h
