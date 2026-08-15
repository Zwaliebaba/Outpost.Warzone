/***************************************************************************/
/*
 * Model.h
 *
 * The IMD model format: the shape, polygon and texture-animation structures
 * the .pie loader fills in and the renderer walks.
 *
 * These lived in IvisDef.h alongside the screen surface and the interface
 * image-file structures, three families that share nothing but a header.
 * The surface went to RendMode.h and the image structures to BitImage.h,
 * beside the functions that take them.
 */
/***************************************************************************/

#ifndef _model_h
#define _model_h

#include "Frame.h"
#include "PieTypes.h"

/* BSPIMD and BSPPOLYID come from here, and gate the BSP members below. */
#include "BSPIMD.h"

//*************************************************************************
//
// texture animation structures
//
//*************************************************************************

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
  UWORD firstframe; //	When loaded as binary this contains the file number of the data file to be loaded (see  Neuron::ProcessBPIE)

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

#endif // _model_h
