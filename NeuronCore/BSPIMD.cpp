#include "pch.h"
#include "Frame.h"	// just for the typedef's
#include "PieTypes.h"
#include "PieMatrix.h"
#include "IvisDef.h"// this can have the #define for BSPIMD in it
#include "IMD.h"// this has the #define for BSPPOLYID_TERMINATE
#include "Ivi.h"

#ifdef BSPIMD		// covers the whole file

#define BSP_BACKFACECULL	// if defined we perform the backface cull in the BSP (we get this for free (!))

#include "BSPIMD.h"

#include "BSPFunc.h"

#include <stdio.h>
#include <assert.h>

// from imddraw.c
void DrawTriangleList(BSPPOLYID PolygonNumber);

// Local prototypes
_inline int IsPointOnPlane(PSPLANE psPlane, iVector* vP);
static iVector* IMDvec(int Vertex);
static void TraverseTreeAndRender(PSBSPTREENODE psNode);

iIMDShape* BSPimd = nullptr; // This is a global ... it is used in imddraw.c (for speed)

// Local static variables
static iVector* BSPScrPos = nullptr;

static iVector* CurrentVertexList = nullptr;
static int CurrentVertexListCount = 0;

extern BOOL NoCullBSP; // Oh yes... a global externaly referenced variable....

// General routines that work on both PC & PSX

/***************************************************************************/
/*
 * Calculates whether point is on same side, opposite side or in plane;
 *
 * returns OPPOSITE_SIDE if opposite,
 *         IN_PLANE if contained in plane,
 *         SAME_SIDE if same side
 *       Also returns pvDot - the dot product 
 * - inputs vP vector to the point
 * - psPlane structure containing the plane equation
 */
/***************************************************************************/
_inline int IsPointOnPlane(PSPLANE psPlane, iVector* vP)
{
  iVectorf vecP;
  float Dot;

  /* validate input */
#ifdef BSP_MAXDEBUG
#endif

  /* subtract point on plane from input point to get position vector */
  vecP.x = static_cast<float>(vP->x - psPlane->vP.x);
  vecP.y = static_cast<float>(vP->y - psPlane->vP.y);
  vecP.z = static_cast<float>(vP->z - psPlane->vP.z);

  /* get dot product of result with plane normal (a,b,c of plane) */
  Dot = static_cast<float>(((vecP.x * psPlane->a) + (vecP.y * psPlane->b) + (vecP.z * psPlane->c)));

  /* if result is -ve, return -1 */
  if ((abs(static_cast<int>(Dot))) < (1.0f / 100.0f))
    return IN_PLANE;
  if (Dot < 0)
    return OPPOSITE_SIDE;
  return SAME_SIDE;
}

/*
	This is the main BSP Traversal routine. It Zaps through the tree (recursively) - and draws all the polygons
	for the IMD in the correct order ... pretty clever eh ..
*/
static void TraverseTreeAndRender(PSBSPTREENODE psNode)
{
  /* is viewer on same side? */
  // so we just do the list the opposite way around - this affects the BACKFACE culling as well 
  if (IsPointOnPlane(&psNode->Plane, BSPScrPos) == SAME_SIDE)
  {
    /* recurse on opposite side, render this node on same side, 
     * recurse on same side.
     */

    if (psNode->link[LEFT] != nullptr)
      TraverseTreeAndRender(psNode->link[LEFT]);
    if (psNode->TriSameDir != BSPPOLYID_TERMINATE)
      DrawTriangleList(psNode->TriSameDir);
#ifndef BSP_BACKFACECULL
    if (psNode->TriOppoDir != BSPPOLYID_TERMINATE)
      DrawTriangleList(psNode->TriOppoDir);
#warning	LETS_FORCE_AN_ERROR_TO_MAKE_SURE_THAT_THIS_CODE_ISNT_COMPILED
#endif
    if (psNode->link[RIGHT] != nullptr)
      TraverseTreeAndRender(psNode->link[RIGHT]);
  }
  else
  /* viewer in plane or on opposite side */
  {
    /* recurse on same side, render this node on opposite side
     * recurse on opposite side.
     */
    if (psNode->link[RIGHT] != nullptr)
      TraverseTreeAndRender(psNode->link[RIGHT]);
    if (psNode->TriOppoDir != BSPPOLYID_TERMINATE)
      DrawTriangleList(psNode->TriOppoDir);

#ifndef BSP_BACKFACECULL
    if (psNode->TriSameDir != BSPPOLYID_TERMINATE)
      DrawTriangleList(psNode->TriSameDir);
#endif
    if (psNode->link[LEFT] != nullptr)
      TraverseTreeAndRender(psNode->link[LEFT]);
  }
}

/*
	These routines are used by the IMD Load_BSP routine 
*/
using PSTRIANGLE = iIMDPoly*;

static iVectorf* iNormalise(iVectorf* v);
static iVectorf* iCrossProduct(iVectorf* psD, iVectorf* psA, iVectorf* psB);
static void GetTriangleNormal(PSTRIANGLE psTri, iVectorf* psN, int pA, int pB, int pC);
PSBSPTREENODE InitNode(PSBSPTREENODE psBSPNode);

static float GetDist(PSTRIANGLE psTri, int pA, int pB);

// little routine for getting an imd vector structure in the IMD from the vertex ID
static _inline iVector* IMDvec(int Vertex)
{
#ifdef BSP_MAXDEBUG
  assert((Vertex>=0)&&(Vertex<CurrentVertexListCount));
#endif
  return (CurrentVertexList + Vertex);
}

/*
 Its easy enough to calculate the plane equation if there is only 3 points
 ... but if there is four things get a little tricky ...

In theory you should be able the pick any 3 of the points to calculate the equation, however
in practise mathematically inacurraceys mean that  you need the three points that are the furthest apart

*/
void GetPlane(iIMDShape* s, UDWORD PolygonID, PSPLANE psPlane)
{
  iVectorf Result;
  iIMDPoly* psTri;
  /* validate input */

  psTri = &(s->polys[PolygonID]);
  CurrentVertexList = s->points;
#ifdef BSP_MAXDEBUG
  CurrentVertexListCount = s->npoints; // for error detection
#endif

  if (psTri->npnts == 4)
  {
    int pA, pB, pC;
    float ShortDist, Dist;

    ShortDist = 999.0f;
    pA = 0;
    pB = 1;
    pC = 2;

    Dist = GetDist(psTri, 0, 1); // Distance between two points in the quad
    if (Dist < ShortDist)
    {
      ShortDist = Dist;
      pA = 0;
      pB = 2;
      pC = 3;
    }

    Dist = GetDist(psTri, 0, 2);
    if (Dist < ShortDist)
    {
      ShortDist = Dist;
      pA = 0;
      pB = 1;
      pC = 3;
    }

    Dist = GetDist(psTri, 0, 3);
    if (Dist < ShortDist)
    {
      ShortDist = Dist;
      pA = 0;
      pB = 1;
      pC = 2;
    }

    Dist = GetDist(psTri, 1, 2);
    if (Dist < ShortDist)
    {
      ShortDist = Dist;
      pA = 0;
      pB = 1;
      pC = 3;
    }

    Dist = GetDist(psTri, 1, 3);
    if (Dist < ShortDist)
    {
      ShortDist = Dist;
      pA = 0;
      pB = 1;
      pC = 2;
    }

    Dist = GetDist(psTri, 2, 3);
    if (Dist < ShortDist)
    {
      ShortDist = Dist;
      pA = 0;
      pB = 1;
      pC = 2;
    }

    GetTriangleNormal(psTri, &Result, pA, pB, pC);
  }
  else
    GetTriangleNormal(psTri, &Result, 0, 1, 2); // for a triangle there is no choice ...

  /* normalise normal */
  iNormalise(&Result);

  // This result MUST be cast to a fract and not called using MAKEFRACT
  //
  //  This is because on a playstation we are casting from FRACT->FRACT (so no conversion is needed)
  //    ... and on a PC we are converting from DOUBLE->FLOAT  (so a cast is needed)
  psPlane->a = static_cast<float>(Result.x);
  psPlane->b = static_cast<float>(Result.y);
  psPlane->c = static_cast<float>(Result.z);
  /* since plane eqn is ax + by + cz + d = 0,
   * d = -(ax + by + cz).
   */
  // Because we are multiplying a FRACT by a const we can use normal '*'
  psPlane->d = -((psPlane->a * IMDvec(psTri->pindex[0])->x) + (psPlane->b * IMDvec(psTri->pindex[0])->y) + (psPlane->c *
    IMDvec(psTri->pindex[0])->z));
  /* store first triangle vertex as point on plane for later point /
   * plane classification in IsPointOnPlane
   */
  memcpy(&psPlane->vP, IMDvec(psTri->pindex[0]), sizeof(iVector));
}

/***************************************************************************/
/*
 * iCrossProduct
 *
 * Calculates cross product of a and b.
 */
/***************************************************************************/

static iVectorf* iCrossProduct(iVectorf* psD, iVectorf* psA, iVectorf* psB)
{
  psD->x = (psA->y * psB->z) - (psA->z * psB->y);
  psD->y = (psA->z * psB->x) - (psA->x * psB->z);
  psD->z = (psA->x * psB->y) - (psA->y * psB->x);

  return psD;
}

static float GetDist(PSTRIANGLE psTri, int pA, int pB)
{
  float vx, vy, vz;
  float sum_square, dist;

  vx = static_cast<float>(IMDvec(psTri->pindex[pA])->x - IMDvec(psTri->pindex[pB])->x);
  vy = static_cast<float>(IMDvec(psTri->pindex[pA])->y - IMDvec(psTri->pindex[pB])->y);
  vz = static_cast<float>(IMDvec(psTri->pindex[pA])->z - IMDvec(psTri->pindex[pB])->z);

  sum_square = ((vx * vx) + (vy * vy) + (vz * vz));
  dist = static_cast<float>(std::sqrt(sum_square));
  return dist;
}

static void GetTriangleNormal(PSTRIANGLE psTri, iVectorf* psN, int pA, int pB, int pC)
{
  iVectorf vecA, vecB;

  /* validate input */

  /* get triangle edge vectors */
  vecA.x = static_cast<float>(IMDvec(psTri->pindex[pA])->x - IMDvec(psTri->pindex[pB])->x);
  vecA.y = static_cast<float>(IMDvec(psTri->pindex[pA])->y - IMDvec(psTri->pindex[pB])->y);
  vecA.z = static_cast<float>(IMDvec(psTri->pindex[pA])->z - IMDvec(psTri->pindex[pB])->z);

  vecB.x = static_cast<float>(IMDvec(psTri->pindex[pA])->x - IMDvec(psTri->pindex[pC])->x);
  vecB.y = static_cast<float>(IMDvec(psTri->pindex[pA])->y - IMDvec(psTri->pindex[pC])->y);
  vecB.z = static_cast<float>(IMDvec(psTri->pindex[pA])->z - IMDvec(psTri->pindex[pC])->z);

  /* normal is cross product */
  iCrossProduct(psN, &vecA, &vecB);
}

/***************************************************************************/
/*
 * Normalises the vector v
 */
/***************************************************************************/

static iVectorf* iNormalise(iVectorf* v)
{
  float vx, vy, vz, mod, sum_square;

  vx = static_cast<float>(v->x);
  vy = static_cast<float>(v->y);
  vz = static_cast<float>(v->z);

  if ((vx == 0) && (vy == 0) && (vz == 0))
    return v;

  sum_square = ((vx * vx) + (vy * vy) + (vz * vz));
  mod = static_cast<float>(std::sqrt(sum_square));

  v->x = (vx / mod);
  v->y = (vy / mod);
  v->z = (vz / mod);

  return v;
}

PSBSPTREENODE InitNode(PSBSPTREENODE psBSPNode)
{
  PSBNODE psBNode;

  /* create node triangle lists */
  psBSPNode->TriSameDir = BSPPOLYID_TERMINATE;
  psBSPNode->TriOppoDir = BSPPOLYID_TERMINATE;

  psBNode = (PSBNODE)psBSPNode;

  psBNode->link[LEFT] = nullptr;
  psBNode->link[RIGHT] = nullptr;

  return psBSPNode;
}

// PC Specific drawing routines

// Calculate the real camera position based on the coordinates of the camera and the camera
// distance - the result is stores in CameraLoc ,,  up is +ve Y
void GetRealCameraPos(OBJPOS* Camera, SDWORD Distance, iVector* CameraLoc)
{
  int Yinc;

  //  as pitch is negative ... we need to subtract the value from y to go up the axis
  CameraLoc->y = Camera->y - ((SIN(Camera->pitch) * Distance) >> FP12_SHIFT);

  Yinc = ((COS(Camera->pitch) * Distance) >> FP12_SHIFT);

  CameraLoc->x = Camera->x + ((SIN(-Camera->yaw) * (-Yinc)) >> FP12_SHIFT);
  CameraLoc->z = Camera->z + ((COS(-Camera->yaw) * (Yinc)) >> FP12_SHIFT);
}

iIMDPoly* BSPScrVertices = nullptr;

void DrawBSPIMD(iIMDShape* IMDdef, iVector* pPos, iIMDPoly* ScrVertices)
{
  BSPScrVertices = ScrVertices; // this is wrong ... use vrt entry in IMDdef
  BSPScrPos = pPos;
  BSPimd = IMDdef;

  TraverseTreeAndRender(IMDdef->BSPNode);
}

#endif			// #ifdef BSPIMD
