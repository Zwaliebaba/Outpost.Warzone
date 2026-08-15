#ifndef _imd_
#define _imd_

#include "IvisDef.h"

#define IMD_NAME				"IMD"
#define PIE_NAME				"PIE"  // Pumpkin image export data file
#define IMD_VER				1
#define PIE_VER				2

#ifdef BSPIMD

#define BSPPOLYID_MAXPOLYID (65534)
#define BSPPOLYID_TERMINATE (BSPPOLYID_MAXPOLYID+1)	// This is used as a terminator

#include "BSPIMD.h"
#include "Frame.h"

#endif

//*************************************************************************

#define IMD_MAX_POINTS	256
#define IMD_MAX_POLYS	256

//*************************************************************************

// polygon flags	b0..b7: col, b24..b31: anim index

#define IMD_TEX			0x00000200
#define IMD_TEXANIM		0x00004000	// IMD_TEX must be set also
#define IMD_PSXTEX		0x00008000	// - use playstation texture allocation method

// shape override flags

// Are any of these really used anymore ?

#define IMD_XEFFECT			0x1	// if this is set DO NOT PROCESS as a normal 3d pie ! (its a psx effect)
#define IMD_BINARY	0x10		// This Pie file was loaded as binary ... when freeing up do not free the pointers ... treat as one big chunk
#define IMD_NOCULLSOME 0x20		// Some polys in the object are drawn without culling.

#define IMD_XTEX			0x00000200
#define IMD_XTRANS		0x00000800

// extended draw routines flags


//*************************************************************************

namespace Neuron
{
  extern BOOL setImagePath(char* path);
  extern iIMDShape* IMDLoad(char* filename, iBool palkeep);
  extern iIMDShape* ProcessIMD(UBYTE** ppFileData, UBYTE* FileDataEnd, UBYTE* IMDpath, UBYTE* PCXpath, iBool palkeep);
  iIMDShape* ProcessBPIE(iIMDShape*, UDWORD size);

  extern iBool IMDSave(char* filename, iIMDShape* s, BOOL PieIMD);

  extern void IMDRelease(iIMDShape* s);
}

// How high up do we want to stop looking 
#define DROID_VIS_UPPER	100

// How low do we stop looking? 
#define DROID_VIS_LOWER	10

/* not for PIEDRAW
namespace Neuron
{
  extern void PIEDraw(iIMDShape *s,int frame);
  extern void IMDDrawTexturedHeightScaled(iIMDShape *shape, float scale);
}

// utils *****************************************************************

namespace Neuron
{
  extern void IMDDrawTextureRaise(iIMDShape *shape, float scale);
  extern void IMDDrawTexturedShade(iIMDShape *shape, int lightLevel);
}
*/

/* PIE registry, implemented in IMDLoad.c.  Declared here because the game
   calls tpInit and tpAddPIE; C let those calls stand without a visible
   declaration, C++ does not. */
extern void tpInit(void);
extern void tpAddPIE(char* FileName, iIMDShape* pIMD);
extern int tpGetNumPIEs(void);
extern iIMDShape* tpGetPIE(int Index);
extern char* tpGetPIEName(int Index);

#endif
