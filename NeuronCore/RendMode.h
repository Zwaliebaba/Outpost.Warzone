// vid.c 0.1 10-01-96.22-11-96
#ifndef _rendmode_h_
#define _rendmode_h_
#include "IvisDef.h"
#include "Ivi.h"
#include "PieBlitFunc.h"
#include "BitImage.h"
#include "TextDraw.h"

//*************************************************************************
//patch

#define	iV_Line					pie_Line
#define	iV_Box					pie_Box
#define	iV_BoxFill				pie_BoxFillIndex
#define	iV_TransBoxFill			pie_TransBoxFill
#define	iV_UniTransBoxFill		pie_UniTransBoxFill
#define	iV_DrawImage			pie_ImageFileID
#define	iV_DrawImageRect		pie_ImageFileIDTile
#define	iV_DrawTransImage		pie_ImageFileID
#define	iV_DrawTransImageRect	pie_ImageFileIDTile
#define	iV_DrawStretchImage		pie_ImageFileIDStretch
#define	iV_DrawImageDef				pie_ImageDef
#define iV_UploadDisplayBuffer		pie_UploadDisplayBuffer

//*************************************************************************

// polygon flags	b0..b7: col, b24..b31: anim index

#define PIE_TEXTURED		0x00000200
#define PIE_COLOURKEYED		0x00000800
#define PIE_NO_CULL			0x00002000
//#define PIE_TEXANIM			0x00004000	// PIE_TEX must be set also
#define PIE_PSXTEX			0x00008000	// - use playstation texture allocation method
#define PIE_BSPFRESH		0x00010000	// Freshly created by the BSP 
#define PIE_NOHALFPSXTEX	0x00020000
#define PIE_ALPHA			0x00040000

//*************************************************************************

#define REND_SURFACE_UNDEFINED	0
#define REND_SURFACE_SCREEN		1
#define REND_SURFACE_USR		2

#define REND_MAX_X			pie_GetVideoBufferWidth()
#define iV_SCREEN_Y_MAX		pie_GetVideoBufferHeight()
#define iV_SCREEN_SIZE_MAX	(iV_SCREEN_X_MAX * iV_SCREEN_Y_MAX)
#define iV_SCREEN_WIDTH		(rendSurface.width)
#define iV_SCREEN_HEIGHT	(rendSurface.height)
#define iV_SCREEN_BUFFER	(rendSurface.buffer)

//*************************************************************************

extern iSurface rendSurface;
extern iSurface* psRendSurface;

//*************************************************************************

//*************************************************************************

extern void iV_RenderAssign(iSurface* s);
extern void iV_SurfaceDestroy(iSurface* s);
extern iSurface* iV_SurfaceCreate(uint32 flags, int width, int height, int xp, int yp, uint8* buffer);

//*************************************************************************

extern int iV_GetDisplayWidth(void);
extern int iV_GetDisplayHeight(void);

//*************************************************************************

extern void iV_DrawMousePointer(int x, int y);
extern void iV_SetMousePointer(IMAGEFILE* ImageFile, UWORD ImageID);
//*************************************************************************
#endif
