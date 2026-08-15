/***************************************************************************/
/*
 * ivisdef.h
 *
 * type defines for all ivis library functions.
 *
 */
/***************************************************************************/

#ifndef _ivisdef_h
#define _ivisdef_h

#include "Frame.h"
#include "PieTypes.h"

// on the psx we need piepsx defined ... on the pc we need it when creating binary pie's

/***************************************************************************/
/***************************************************************************/
/*
 *	Global Definitions
 */
/***************************************************************************/
#define BSPIMD	// now defined for all versions (optional BSP handled on all formats)
#define iV_DDX

#define iV_SCANTABLE_MAX	1024

// texture animation defines
#define iV_IMD_ANIM_LOOP	0
#define iV_IMD_ANIM_RUN		1
#define iV_IMD_ANIM_FRAMES	8

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
//*************************************************************************
//
// Basic types (now defined in pieTypes.h)
//
//*************************************************************************

//*************************************************************************
//
// Simple derived types (now defined in pieTypes.h)
//
//*************************************************************************

//*************************************************************************
//
// screen surface structure
//
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
  int32 scantable[iV_SCANTABLE_MAX]; // currently uses 4k per structure (!)
  int width;
  int height;
  int32 size;
};

//*************************************************************************
//
// texture animation structures
//
//*************************************************************************
using iTexAnimFrame = struct
{
  int npoints;
  iPoint frame[iV_IMD_ANIM_FRAMES];
};

using iTexAnim = struct
{
  int nFrames;
  int playbackRate;
  int textureWidth;
  int textureHeight;
};

//*************************************************************************
//
// imd structures
//
//*************************************************************************
#ifdef BSPIMD
using BSPPOLYID = uint16; // lets hope this can work as a byte ... that will limit it to 255 polygons in 1 imd
#endif
#include "BSPIMD.h" //structure defintions only

using VERTEXID = int; // Size of the entry for vertex id in the imd polygon structure (32bits on pc 16bits on PSX)

using iIMDPoly = struct
{
  uint32 flags;
  int32 zcentre;
  int npnts;
  iVector normal;
  VERTEXID* pindex;
  iVertex* vrt;
  iTexAnim* pTexAnim; // warning.... this is not used on the playstation version !
#ifdef BSPIMD
  BSPPOLYID BSP_NextPoly; // the polygon number for the next in the BSP list ... or BSPPOLYID_TERMINATE for no more
#endif
};

// PlayStation special effect structure ... loaded as a PIE (type 9) and cast to iIMDShape 
using iIMDShapeEffect = struct iIMDShapeEffect
{
  uint32 flags; // This 'flags' can be used to check if the file is a 3d PIE file or a special effect
  void* ImageFile;
  // ( cast this to (IMAGEFILE*) when using it). - // When loaded as a binary this contains the hashed value of the text starting frame
  UWORD firstframe; //	When loaded as binary this contains the file number of the data file to be loaded (see  iV_ProcessBPIE)

  UWORD numframes;
  UWORD xsize;
  UWORD ysize;
};

#define TRACER_SINGLE 0	// iIMDShapeProjectile types.
#define TRACER_DOUBLE 1

// PlayStation special effect structure ... loaded as a PIE (type 10) and cast to iIMDShape 
using iIMDShapeProjectile = struct iIMDShapeProjectile
{
  uint32 flags; // This 'flags' can be used to check if the file is a 3d PIE file or a special effect

  uint8 Type;
  uint8 Radius;
  uint8 Seperation;
  uint8 Pad0;
  uint8 LRed, LGreen, LBlue, Pad1;
  uint8 TRed, TGreen, TBlue, Pad2;
};

// PC version
using iIMDShape = struct iIMDShape
{
  uint32 flags;
  int32 texpage;
  int32 oradius, sradius, radius, visRadius, xmin, xmax, ymin, ymax, zmin, zmax;

  iVector ocen;
  UWORD numFrames;
  UWORD animInterval;
  int npoints;
  int npolys; // After BSP this number is not updated - it stays the number of pre-bsp polys
  int nconnectors; // After BSP this number is not updated - it stays the number of pre-bsp polys

  iVector* points;
  iIMDPoly* polys;
  // After BSP this is not changed - it stays the original chunk of polys - not all are now used,and others not in this array are, see BSPNode for a tree of all the post BSP polys
  iVector* connectors;
  // After BSP this is not changed - it stays the original chunk of polys - not all are now used,and others not in this array are, see BSPNode for a tree of all the post BSP polys

  int ntexanims;
  iTexAnim** texanims;

  struct iIMDShape* next; // next pie in multilevel pies (NULL for non multilevel !)

#ifdef BSPIMD
  PSBSPTREENODE BSPNode; // Start of the BSP tree;
#endif
};

//*************************************************************************
//
// immitmap image structures
//
//*************************************************************************

using CLUTHEADER = struct
{
  UBYTE Type[4];
  UWORD Version;
  UWORD ClutSize;
  UWORD NumCluts;
  UWORD Pad;
};

using IMAGEHEADER = struct
{
  UBYTE Type[4];
  UWORD Version;
  UWORD NumImages;
  UWORD BitDepth;
  UWORD NumTPages;
  UBYTE TPageFiles[16][16];
  UBYTE PalFile[16];
};

using IMAGEDEF = struct
{
  //	UDWORD HashValue
  UWORD TPageID;
  UWORD PalID;
  UWORD Tu, Tv;
  UWORD Width;
  UWORD Height;
  SWORD XOffset;
  SWORD YOffset;
};

using IMAGEFILE = struct
{
  IMAGEHEADER Header;
  iSprite* TexturePages;
  UWORD NumCluts;
  UWORD TPageIDs[16];
  UWORD ClutIDs[48];
  IMAGEDEF* ImageDefs;
};

/***************************************************************************/
/*
 *	Global Variables
 */
/***************************************************************************/

#endif // _ivisdef_h
