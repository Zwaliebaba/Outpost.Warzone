#include "pch.h"
/*
 * RayCast.c
 *
 * raycasting routine that gives intersection points with map tiles
 *
 */

#include <cmath>
#include <math.h>
#include <stdio.h>
#include <directxmath.h>

#include "Frame.h"

#include "Objects.h"
#include "Map.h"

#include "RayCast.h"
#include "Display3D.h"
#include "Effects.h"

// accuracy for the raycast lookup tables
#define RAY_ACC		12

#define RAY_ACCMUL	(1<<RAY_ACC)

// Get the tile from tile coords
#define RAY_TILE(x,y) mapTile((x),(y))

// control the type of clip method
// 0 - clip on ray length (faster but it doesn't always work :-)
// 1 - clip on coordinates (accurate but possibly a bit slower)
#define RAY_CLIP	1

// ray point
using RAY_POINT = struct _ray_point
{
  SDWORD x, y;
};

/* x and y increments for each ray angle */
static SDWORD rayDX[NUM_RAYS], rayDY[NUM_RAYS];
static SDWORD rayHDist[NUM_RAYS], rayVDist[NUM_RAYS];
static SDWORD rayFPTan[NUM_RAYS], rayFPInvTan[NUM_RAYS];
static SDWORD rayFPInvCos[NUM_RAYS], rayFPInvSin[NUM_RAYS];

#define MAX_FRACT (0x7fffffff)
#define angle_PSX2WORLD(ang) ((((ang)%4096)*360)/4096)

/* Initialise the ray tables */
BOOL rayInitialise(void)
{
  SDWORD i;
  float angle = 0.0f;
  float val;

  for (i = 0; i < NUM_RAYS; i++)
  {
    // Set up the fixed offset tables for calculating the intersection points
    val = static_cast<float>(tan(angle));

    rayDX[i] = static_cast<SDWORD>((TILE_UNITS * RAY_ACCMUL * val));

    if (i <= NUM_RAYS / 4 || (i >= 3 * NUM_RAYS / 4))
      rayDX[i] = -rayDX[i];

    if (val == 0)
      val = static_cast<float>(1); // Horrible hack to avoid divide by zero.

    rayDY[i] = static_cast<SDWORD>((TILE_UNITS * RAY_ACCMUL / val));
    if (i >= NUM_RAYS / 2)
      rayDY[i] = -rayDY[i];

    // These are used to calculate the initial intersection
    rayFPTan[i] = std::lrintf(val * static_cast<float>(RAY_ACCMUL));
    rayFPInvTan[i] = std::lrintf(static_cast<float>(RAY_ACCMUL) / val);

    // Set up the trig tables for calculating the offset distances
    val = static_cast<float>(sin(angle));
    if (val == 0)
      val = static_cast<float>(1);
    rayFPInvSin[i] = std::lrintf(static_cast<float>(RAY_ACCMUL) / val);
    if (i >= NUM_RAYS / 2)
      rayVDist[i] = std::lrintf(static_cast<float>(-TILE_UNITS) / val);
    else
      rayVDist[i] = std::lrintf(static_cast<float>(TILE_UNITS) / val);

    val = static_cast<float>(cos(angle));
    if (val == 0)
      val = static_cast<float>(1);
    rayFPInvCos[i] = std::lrintf(static_cast<float>(RAY_ACCMUL) / val);
    if (i < NUM_RAYS / 4 || i > 3 * NUM_RAYS / 4)
      rayHDist[i] = std::lrintf(static_cast<float>(TILE_UNITS) / val);
    else
      rayHDist[i] = std::lrintf(static_cast<float>(-TILE_UNITS) / val);

    angle += RAY_ANGLE;
  }

  return TRUE;
}

//
////#ifdef WIN32
//

//
//
//	// Stack in the DCache.
//

/* cast a ray from x,y (world coords) at angle ray (0-360)
 * The ray angle starts at zero along the positive y axis and
 * increases towards -ve X.
 *
 * Sorry about the wacky angle set up but that was what I thought
 * warzone used, but turned out not to be after I wrote it.
 */
void rayCast(UDWORD x, UDWORD y, UDWORD ray, UDWORD length, RAY_CALLBACK callback)
{
  SDWORD hdInc = 0, vdInc = 0; // increases in x and y distance per intersection
  SDWORD hDist, vDist; // distance to current horizontal and vertical intersections
  RAY_POINT sVert, sHoriz;
  SDWORD vdx = 0, hdy = 0; // vertical x increment, horiz y inc

  // every table below has NUM_RAYS entries; an index past them reads garbage trig
  DEBUG_ASSERT_TEXT(ray < NUM_RAYS, "rayCast: ray index out of range");
#if RAY_CLIP == 0
  SDWORD newLen, clipLen; // ray length after clipping
#endif

  // Clipping is done with the position offset by TILE_UNITS/4 to account 
  // for the rounding errors when the intersection length is calculated.
  // Bit of a hack but I'm pretty sure it doesn't let through anything
  // that should be clippped.

#if RAY_CLIP == 0
  // Initial clip length is just the length of the ray
  clipLen = (SDWORD)length;
#endif

  // initialise the horizontal intersection calculations
  // and clip to the top and bottom of the map
  // (no horizontal intersection for a horizontal ray)
  if (ray != NUM_RAYS / 4 && ray != 3 * NUM_RAYS / 4)
  {
    if (ray < NUM_RAYS / 4 || ray > 3 * NUM_RAYS / 4)
    {
      // intersection
      sHoriz.y = (y & ~TILE_MASK) + TILE_UNITS;
      hdy = TILE_UNITS;

#if RAY_CLIP == 0
      // clipping
      newLen = (((mapHeight << TILE_SHIFT) - ((SDWORD)y + TILE_UNITS / 4)) * rayFPInvCos[ray]) >> RAY_ACC; if (newLen < clipLen)
      {
        clipLen = newLen;
      }
#endif
    }
    else
    {
      // intersection
      sHoriz.y = (y & ~TILE_MASK) - 1;
      hdy = -TILE_UNITS;

#if RAY_CLIP == 0
      // clipping
      newLen = ((TILE_UNITS / 4 - (SDWORD)y) * rayFPInvCos[ray]) >> RAY_ACC; if (newLen < clipLen) { clipLen = newLen; }
#endif
    }

    // Horizontal x is kept in fixed point form until passed to the callback
    // to avoid rounding errors
    // Horizontal y is in integer form all the time
    sHoriz.x = (x << RAY_ACC) + ((static_cast<SDWORD>(y) - sHoriz.y) * rayFPTan[ray]);

    // Set up the distance calculations
    hDist = ((sHoriz.y - static_cast<SDWORD>(y)) * rayFPInvCos[ray]) >> RAY_ACC;
    hdInc = rayHDist[ray];
  }
  else
  {
    // ensure no horizontal intersections are calculated
    hDist = length;
  }

  // initialise the vertical intersection calculations
  // and clip to the left and right of the map
  // (no vertical intersection for a vertical ray)
  if (ray != 0 && ray != NUM_RAYS / 2)
  {
    if (ray >= NUM_RAYS / 2)
    {
      // intersection
      sVert.x = (x & ~TILE_MASK) + TILE_UNITS;
      vdx = TILE_UNITS;

#if RAY_CLIP == 0
      // clipping
      newLen = ((((SDWORD)x + TILE_UNITS / 4) - (mapWidth << TILE_SHIFT)) * rayFPInvSin[ray]) >> RAY_ACC; if (newLen < clipLen)
      {
        clipLen = newLen;
      }
#endif
    }
    else
    {
      // intersection
      sVert.x = (x & ~TILE_MASK) - 1;
      vdx = -TILE_UNITS;

#if RAY_CLIP == 0
      // clipping
      newLen = (((SDWORD)x - TILE_UNITS / 4) * rayFPInvSin[ray]) >> RAY_ACC; if (newLen < clipLen) { clipLen = newLen; }
#endif
    }

    // Vertical y is kept in fixed point form until passed to the callback
    // to avoid rounding errors
    // Vertical x is in integer form all the time
    sVert.y = (y << RAY_ACC) + (static_cast<SDWORD>(x) - sVert.x) * rayFPInvTan[ray];

    // Set up the distance calculations
    vDist = ((static_cast<SDWORD>(x) - sVert.x) * rayFPInvSin[ray]) >> RAY_ACC;
    vdInc = rayVDist[ray];
  }
  else
  {
    // ensure no vertical intersections are calculated
    vDist = length;
  }

  DEBUG_ASSERT_TEXT(hDist != 0 && vDist != 0, "rayCast: zero distance");
  DEBUG_ASSERT_TEXT((hDist == static_cast<SDWORD>(length) || hdInc > 0) &&
    (vDist == static_cast<SDWORD>(length) || vdInc > 0), "rayCast: negative (or 0) distance increment");

#if RAY_CLIP == 0
  while (hDist < clipLen || vDist < clipLen)
  {
    // choose the next closest intersection
    if (hDist < vDist)
    {
      // pass through the current intersection, converting x from fixed point
      if (!callback(sHoriz.x >> RAY_ACC, sHoriz.y, hDist))
      {
        // callback doesn't want any more points so return
        return;
      }

      // update for the next intersection
      sHoriz.x += rayDX[ray];
      sHoriz.y += hdy;
      hDist += hdInc;
    }
    else
    {
      // pass through the current intersection, converting y from fixed point
      if (!callback(sVert.x, sVert.y >> RAY_ACC, vDist))
      {
        // callback doesn't want any more points so return
        return;
      }

      // update for the next intersection
      sVert.x += vdx;
      sVert.y += rayDY[ray];
      vDist += vdInc;
    }
    DEBUG_ASSERT_TEXT(hDist != 0 && vDist != 0, "rayCast: zero distance");
  }
#elif RAY_CLIP == 1
  while (hDist < static_cast<SDWORD>(length) || vDist < static_cast<SDWORD>(length))
  {
    // choose the next closest intersection
    if (hDist < vDist)
    {
      // clip to the edge of the map
      if (sHoriz.x < 0 || (sHoriz.x >> RAY_ACC) >= static_cast<SDWORD>(mapWidth << TILE_SHIFT) || sHoriz.y < 0 || sHoriz.y >= static_cast<
        SDWORD>(mapHeight << TILE_SHIFT))
        return;

      // pass through the current intersection, converting x from fixed point
      if (!callback(sHoriz.x >> RAY_ACC, sHoriz.y, hDist))
      {
        // callback doesn't want any more points so return
        return;
      }

      // update for the next intersection
      sHoriz.x += rayDX[ray];
      sHoriz.y += hdy;
      hDist += hdInc;
    }
    else
    {
      // clip to the edge of the map
      if (sVert.x < 0 || sVert.x >= static_cast<SDWORD>(mapWidth << TILE_SHIFT) || sVert.y < 0 || (sVert.y >> RAY_ACC) >= static_cast<
        SDWORD>(mapHeight << TILE_SHIFT))
        return;

      // pass through the current intersection, converting y from fixed point
      if (!callback(sVert.x, sVert.y >> RAY_ACC, vDist))
      {
        // callback doesn't want any more points so return
        return;
      }

      // update for the next intersection
      sVert.x += vdx;
      sVert.y += rayDY[ray];
      vDist += vdInc;
    }
    DEBUG_ASSERT_TEXT(hDist != 0 && vDist != 0, "rayCast: zero distance");
  }
#endif
}

// Calculate the angle to cast a ray between two points
UDWORD rayPointsToAngle(SDWORD x1, SDWORD y1, SDWORD x2, SDWORD y2)
{
  SDWORD xdiff, ydiff;
  SDWORD angle;

  xdiff = x2 - x1;
  ydiff = y1 - y2;

  angle = static_cast<SDWORD>((NUM_RAYS / 2) * atan2(xdiff, ydiff) / DirectX::XM_PI);

  angle += NUM_RAYS / 2;
  angle = angle % NUM_RAYS;

  DEBUG_ASSERT_TEXT(angle >= 0 && angle < NUM_RAYS, "rayPointsToAngle: angle out of range");

  return static_cast<UDWORD>(angle);
}

/* Distance of a point from a line.
 * NOTE: This is not 100% accurate - it approximates to get the square root
 *
 * This is based on Graphics Gems II setion 1.3
 */
SDWORD rayPointDist(SDWORD x1, SDWORD y1, SDWORD x2, SDWORD y2, SDWORD px, SDWORD py)
{
  SDWORD a, lxd, lyd, dist;

  lxd = x2 - x1;
  lyd = y2 - y1;

  a = (py - y1) * lxd - (px - x1) * lyd;
  if (a < 0)
    a = -a;
  if (lxd < 0)
    lxd = -lxd;
  if (lyd < 0)
    lyd = -lyd;

  if (lxd < lyd)
    dist = a / (lxd + lyd - lxd / 2);
  else
    dist = a / (lxd + lyd - lyd / 2);

  return dist;
}

//-----------------------------------------------------------------------------------
/*	Gets the maximum terrain height along a certain direction to the edge of the grid
	from wherever you specify, as well as the distance away 
*/

/* Nasty global vars - put into a structure? */
//-----------------------------------------------------------------------------------
SDWORD gHeight;
float gPitch;
UDWORD gStartTileX;
UDWORD gStartTileY;

SDWORD gHighestHeight, gHOrigHeight;
SDWORD gHMinDist;
float gHPitch;

//-----------------------------------------------------------------------------------
UDWORD getTileTallObj(UDWORD x, UDWORD y)
{
  UDWORD i, j;
  UDWORD TallObj = 0;

  x = x >> TILE_SHIFT;
  y = y >> TILE_SHIFT;

  for (j = y; j < y + 2; j++)
  {
    for (i = x; i < x + 2; i++)
      TallObj |= TILE_HAS_TALLSTRUCTURE(mapTile(i,j));
  }

  return TallObj;
}

//-----------------------------------------------------------------------------------
static BOOL getTileHighestCallback(SDWORD x, SDWORD y, SDWORD dist)
{
  SDWORD heightDif;
  UDWORD height;
  if (clipXY(x, y))
  {
    height = map_Height(x, y);
    if ((height > gHighestHeight) AND (dist >= gHMinDist))
    {
      heightDif = height - gHOrigHeight;
      gHPitch = atan2f(static_cast<float>(heightDif), static_cast<float>(6*TILE_UNITS)); //MAKEFRACT(dist-(TILE_UNITS*3))));
      gHighestHeight = height;
    }
  }
  else
    return (FALSE);

  return (TRUE);
}

//-----------------------------------------------------------------------------------
/* Will return false when we've hit the edge of the grid */
static BOOL getTileHeightCallback(SDWORD x, SDWORD y, SDWORD dist)
{
  SDWORD height, heightDif;
  float newPitch;
  BOOL HasTallStructure = FALSE;
#ifdef TEST_RAY
  iVector pos;
#endif

  /* Are we still on the grid? */
  if (clipXY(x, y))
  {
    HasTallStructure = TILE_HAS_TALLSTRUCTURE(mapTile(x>>TILE_SHIFT,y>>TILE_SHIFT));

    if ((dist > TILE_UNITS) || HasTallStructure)
    {
      // Only do it the current tile is > TILE_UNITS away from the starting tile. Or..
      // there is a tall structure  on the current tile and the current tile is not the starting tile.
      //		if( (dist>TILE_UNITS) ||
      //			( (HasTallStructure = TILE_HAS_TALLSTRUCTURE(mapTile(x>>TILE_SHIFT,y>>TILE_SHIFT))) &&
      /* Get height at this intersection point */
      height = map_Height(x, y);

      if (HasTallStructure)
        height += 300; //TALLOBJECT_ADJUST;

      if (height <= gHeight)
        heightDif = 0;
      else
        heightDif = height - gHeight;

      /* Work out the angle to this point from start point */
      newPitch = atan2f(static_cast<float>(heightDif), static_cast<float>(dist));

      /* Is this the steepest we've found? */
      if (newPitch > gPitch)
      {
        /* Yes, then keep a record of it */
        gPitch = newPitch;
      }
      //---

#ifdef TEST_RAY
      pos.x = x; pos.y = height; pos.z = y; addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_SMALL,FALSE,NULL, 0);
#endif
    }
  }
  else
  {
    /* We've hit edge of grid - so exit!! */
    return (FALSE);
  }

  /* Not at edge yet - so exit */
  return (TRUE);
}

/* The ray tables step in whole degrees, so quantise a radian direction to a ray index */
UDWORD rayIndex(float direction)
{
  return static_cast<UDWORD>(((std::lround(DirectX::XMConvertToDegrees(direction)) % NUM_RAYS) + NUM_RAYS) % NUM_RAYS);
}

void getBestPitchToEdgeOfGrid(UDWORD x, UDWORD y, float direction, float* pitch)
{
  /* Set global var to clear */
  gPitch = 0.0f;
  gHeight = map_Height(x, y);
  gStartTileX = x >> TILE_SHIFT;
  gStartTileY = y >> TILE_SHIFT;
  rayCast(x, y, rayIndex(direction), 5430, getTileHeightCallback);
  *pitch = gPitch;
}

//-----------------------------------------------------------------------------------
void getPitchToHighestPoint(UDWORD x, UDWORD y, float direction, UDWORD thresholdDistance, float* pitch)
{
  gHPitch = 0.0f;
  gHOrigHeight = map_Height(x, y);
  gHighestHeight = map_Height(x, y);
  gHMinDist = thresholdDistance;
  rayCast(x, y, rayIndex(direction), 3000, getTileHighestCallback);
  *pitch = gHPitch;
}

//-----------------------------------------------------------------------------------
