/***************************************************************************/
/*
 * RenderTypes.h
 *
 * The render layer's value types: sized integers, geometry, colour, and the
 * PIE vertex/state/image structures the draw path passes around.
 *
 * Absorbed PieDef.h, which held the PIE structures and the draw constants;
 * PieDef.h's function declarations went to RenderModel.h and its PIEPOLY to
 * RenderModel.cpp, which was its only user.
 *
 */
/***************************************************************************/

#ifndef _renderTypes_h
#define _renderTypes_h

#include <directxmath.h>

#include "Frame.h"

/***************************************************************************/
/***************************************************************************/
/*
 *	Global Macros
 */
/***************************************************************************/

/***************************************************************************/
/*
 *	Global Type Definitions
 */
/***************************************************************************/
using int8 = signed char;
using int16 = signed short;
using int32 = int;
using uint8 = unsigned char;
using uint16 = unsigned short;
using uint32 = unsigned int;

//*************************************************************************
//
// Simple derived types
//
//*************************************************************************
using iClip = struct
{
  int left, top, right, bottom;
};
/* One image pixel, packed A8R8G8B8. This was uint8 - a palette index - until
 * the palette removal's stage 2: the conversion tool expanded indices through the
 * game palette at load, so everything downstream holds and moves true-colour
 * pixels. Alpha is 0xff except for the old "index 0 / black is transparent"
 * convention, which the loader turns into alpha 0. */
using iBitmap = UDWORD;
using iColour = struct
{
  uint8 r, g, b;
};
using iBool = int;
using iPoint = struct
{
  int32 x, y;
};
using iSprite = struct
{
  int width, height;
  iBitmap* bmp;
};
using iRGB8 = struct
{
  uint8 r, g, b, p;
};
using iRGB16 = struct
{
  uint16 r, g, b, p;
};
using iRGB32 = struct
{
  uint32 r, g, b, p;
};
using iPoint8 = struct
{
  int8 x, y;
};
using iPoint16 = struct
{
  int16 x, y;
};
using iPoint32 = struct
{
  int32 x, y;
};

using iVector = struct
{
  int32 x, y, z;
};
using iVectorf = struct
{
  double x, y, z;
};
using iTexture = struct
{
  int xshift, width, height;
  iBitmap* bmp;
  iBool bColourKeyed;
};
using iVertex = struct
{
  int32 x, y, z, u, v;
  uint8 g;
};
using iView = struct
{
  iVector p;
  DirectX::XMFLOAT3 r; /* radians */
};


/***************************************************************************/
/*
 *	Draw constants and macros (from PieDef.h)
 */
/***************************************************************************/

#define FP12_SHIFT				12
#define FP12_MULTIPLIER				(1<<12)
#define STRETCHED_Z_SHIFT		10		//stretchs z range for (1000 to 4000) to (8000 to 32000)
#define	MAX_Z					(32000.0f)			//raised to 32000 from 6000 when stretched
#define	INV_MAX_Z				(0.00003125f)			//1/32000
#define MIN_STRETCHED_Z			256
#define	LONG_WAY				(1<<15)
#define	LONG_TEST				(1<<14)
#define INTERFACE_DEPTH			(MAX_Z - 1.0f)
#define INTERFACE_DEPTH_3DFX	(65535)
#define INV_INTERFACE_DEPTH_3DFX	(1.0f/65535.0f)
#define BUTTON_DEPTH			(2000) //will be stretched to 16000

#define TEXTURE_SIZE			(256.0f)
#define INV_TEX_SIZE			(0.00390625f)

#define MAX_FILE_PATH		256
#define pie_MAX_POLY_SIZE	16

//Effects
#define pie_MAX_BRIGHT_LEVEL 255
#define pie_BRIGHT_LEVEL_200 200
#define pie_BRIGHT_LEVEL_180 180
#define pie_DROID_BRIGHT_LEVEL 192

//Render style flags for all pie draw functions
#define pie_FLAG_MASK			0xffff
#define pie_FLAT				0x1
#define pie_TRANSLUCENT			0x2
#define pie_ADDITIVE			0x4
#define pie_NO_BILINEAR			0x8
#define pie_HEIGHT_SCALED		0x10
#define pie_RAISE				0x20
#define pie_BUTTON				0x40

#define pie_RAISE_SCALE			256

#define pie_BAND				0x80
#define pie_BAND_RED			0x90
#define pie_BAND_GREEN			0xa0
#define pie_BAND_YELLOW			0xb0
#define pie_BAND_BLUE			0xc0

#define pie_DRAW_DISC			0x800
#define pie_DRAW_DISC_RED		0x900
#define pie_DRAW_DISC_GREEN		0xa00
#define pie_DRAW_DISC_YELLOW	0xb00
#define pie_DRAW_DISC_BLUE		0xc00

#define pie_GLOW				0x8000
#define pie_GLOW_RED			0x9000
#define pie_GLOW_GREEN			0xa000
#define pie_GLOW_YELLOW			0xb000
#define pie_GLOW_BLUE			0xc000
#define pie_GLOW_STRENGTH		63

#define pie_MAX_POINTS	256
#define pie_MAX_POLYS	256
#define pie_MAX_POLY_VERTS 10

#define pie_FILLRED 16
#define pie_FILLGREEN 16
#define pie_FILLBLUE 128
#define pie_FILLTRANS 128

#define MAX_UB_LIGHT		((UBYTE)255)
#define MIN_UB_LIGHT		((UBYTE)0)
#define MAX_LIGHT 0xffffffff

/***************************************************************************/
/*
 *	Global Definitions (MACROS)
 */
/***************************************************************************/
#define pie_MIN(a,b)	(((a) < (b)) ? (a) : (b))
#define pie_MAX(a,b)	(((a) > (b)) ? (a) : (b))
#define pie_ABS(a)		(((a) < 0) ? (-(a)) : (a))

#define pie_ADDLIGHT(l,x)						\
(((l)->byte.r > (MAX_UB_LIGHT - (x))) ? ((l)->byte.r = MAX_UB_LIGHT) : ((l)->byte.r +=(x)));		\
(((l)->byte.g > (MAX_UB_LIGHT - (x))) ? ((l)->byte.g = MAX_UB_LIGHT) : ((l)->byte.g +=(x)));		\
(((l)->byte.b > (MAX_UB_LIGHT - (x))) ? ((l)->byte.b = MAX_UB_LIGHT) : ((l)->byte.b +=(x)));

#define pie_SUBTRACTLIGHT(l,x)						\
(((l->byte.r) < (x)) ? ((l->byte.r) = MIN_UB_LIGHT) : ((l->byte.r) -=(x)));		\
(((l->byte.g) < (x)) ? ((l->byte.g) = MIN_UB_LIGHT) : ((l->byte.g) -=(x)));		\
(((l->byte.b) < (x)) ? ((l->byte.b) = MIN_UB_LIGHT) : ((l->byte.b) -=(x)));

/***************************************************************************/
/*
 *	PIE structures (from PieDef.h)
 */
/***************************************************************************/

using PIELIGHTBYTES = struct
{
  UBYTE b, g, r, a;
}; //for byte fields in a DWORD
using PIELIGHT = union
{
  PIELIGHTBYTES byte;
  UDWORD argb;
};
using PIEVERTLIGHT = struct
{
  UBYTE r, g, b, a;
};
using PIEVERTEX = struct
{
  SDWORD sx, sy, sz;
  UWORD tu, tv;
  PIELIGHT light, specular;
};
using PIEPIXEL = struct
{
  float d3dx, d3dy, d3dz;
};
using PIERECT = struct
{
  SWORD x, y, w, h;
}; //screen rectangle
using PIEIMAGE = struct
{
  SDWORD texPage;
  SWORD tu, tv, tw, th;
}; //an area of texture
using PIESTYLE = struct
{
  UDWORD pieFlag;
  PIELIGHT colour, specular;
  UBYTE light, trans, scale, height;
}; //render style for pie draw functions

using fixed = int32;

#endif // _renderTypes_h
