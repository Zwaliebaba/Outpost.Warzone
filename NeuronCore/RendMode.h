// vid.c 0.1 10-01-96.22-11-96
#ifndef _rendmode_h_
#define _rendmode_h_
#include "PieTypes.h"
#include "PieDef.h"
#include "Render2D.h"
#include "BitImage.h"
#include "TextDraw.h"

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

//*************************************************************************

using iSurface = struct iSurface
{
  uint32 flags;
  int xcentre;
  int ycentre;
  int xpshift;
  int ypshift;
  iClip clip;
  uint8* buffer;
  int width;
  int height;
  int32 size;
};

//*************************************************************************

extern iSurface rendSurface;
extern iSurface* psRendSurface;

//*************************************************************************

//*************************************************************************

namespace Neuron
{
  extern void RenderAssign(iSurface* s);
}

//*************************************************************************

namespace Neuron
{
  extern int GetDisplayWidth(void);
  extern int GetDisplayHeight(void);
}

//*************************************************************************

//*************************************************************************
#endif
