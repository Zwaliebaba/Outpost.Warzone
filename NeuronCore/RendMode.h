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

#define iV_RenderBegin			pie_LocalRenderBegin
#define iV_RenderEnd			pie_LocalRenderEnd
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
#define iV_DownloadDisplayBuffer	pie_DownloadDisplayBuffer
#define iV_ScaleBitmapRGB			pie_ScaleBitmapRGB

//*************************************************************************

#define iV_MODE_4101		0x4101			// DDX 640x480x256
#define REND_D3D_RGB		0x133			// Direct3D 640x480x16bit RGB renderer (mmx)
#define REND_D3D_HAL		0x143			// Direct3D 640x480x16bit hardware
#define REND_D3D_REF		0x153			// Direct3D 640x480x16bit hardware
#define REND_GLIDE_3DFX		0x200			// 3dfx Glide API
#define REND_16BIT			0x400			// 16bit software mode for video
#define iV_MODE_SURFACE		0x10000			// off-screen surface
#define REND_PSX			0x20000			// PlayStation - added by tjc
#define REND_UNDEFINED		-1				// undefined mode

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

extern void iV_RenderAssign(int mode, iSurface* s);
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
