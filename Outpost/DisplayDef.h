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
#define DISP_HARDBITDEPTH	(32)
#define DISP_BITDEPTH	(32)
#define	BOUNDARY_X			(16)
#define BOUNDARY_Y			(16)

using SCREEN_DISP_DATA = struct _screen_disp_data
{
  iIMDShape* imd;
  //	BOOL		drawnThisFrame;		// for sorting - have we drawn the imd already?
  UDWORD frameNumber; // last frame it was drawn
  //	UDWORD		animFrame;			// anim Frame
  UDWORD screenX, screenY;
  UDWORD screenR;
};

#endif
