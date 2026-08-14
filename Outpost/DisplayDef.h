/*
 * DisplayDef.h
 *
 * Definitions of the display structures
 *
 */
#ifndef _displaydef_h
#define _displaydef_h

#include "IMD.h"
#include "PieClip.h"
#define DISP_WIDTH		(pie_GetVideoBufferWidth()) 
#define DISP_HEIGHT		(pie_GetVideoBufferHeight())
#define DISP_HARDBITDEPTH	(16)
#define DISP_BITDEPTH	(8)
#define	BOUNDARY_X			(16)
#define BOUNDARY_Y			(16)

typedef struct _screen_disp_data
{
	iIMDShape	*imd;
//	BOOL		drawnThisFrame;		// for sorting - have we drawn the imd already?
	UDWORD		frameNumber;		// last frame it was drawn
//	UDWORD		animFrame;			// anim Frame
	UDWORD		screenX,screenY;
	UDWORD		screenR; 
} SCREEN_DISP_DATA;



#endif

