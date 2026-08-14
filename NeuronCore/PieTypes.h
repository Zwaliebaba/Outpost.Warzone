/***************************************************************************/
/*
 * pieTypes.h
 *
 * type defines for simple pies.
 *
 */
/***************************************************************************/

#ifndef _pieTypes_h
#define _pieTypes_h

#include "Frame.h"

/***************************************************************************/
/***************************************************************************/
/*
 *	Global Definitions
 */
/***************************************************************************/

#define PI 	  					3.141592654

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
using iBitmap = uint8;
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
using iPalette = iColour[256];
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
  iColour* pPal;
  iBool bColourKeyed;
};
using iVertex = struct
{
  int32 x, y, z, u, v;
  uint8 g;
};
using PIEVECTORF = struct
{
  float x, y, z;
};
using iView = struct
{
  iVector p, r;
};

#endif // _pieTypes_h
