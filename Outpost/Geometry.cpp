#include "pch.h"
/* Geometry.c - holds trig/vector deliverance specific stuff for 3D */
/* Alex McLean, Pumpkin Studios, EIDOS Interactive */

#include "Frame.h"
#include "Geo.h" //ivis matrix code
#include "ObjectDef.h"
#include "Map.h"
#include "Display3D.h"
#include "Geometry.h"
#include "HCI.h"
#include "Display.h"

void testAngles(void);
void processImpact(UDWORD worldX, UDWORD worldY, UBYTE severity, UDWORD tilesAcross);
void baseObjScreenCoords(BASE_OBJECT* baseObj, iPoint* pt);
SDWORD calcDirection(UDWORD x0, UDWORD y0, UDWORD x1, UDWORD y1);
UDWORD adjustDirection(SDWORD present, SDWORD difference);

void initBulletTable(void);
int inQuad(POINT* pt, QUAD* quad);

/* The arc over which bullets fly */
UBYTE sineHeightTable[SIZE_SINE_TABLE];

void initBulletTable(void)
{
  for (UDWORD i = 0; i < SIZE_SINE_TABLE; i++)
  {
    UBYTE height = static_cast<UBYTE>((AMPLITUDE_HEIGHT * sin(i * deg)));
    sineHeightTable[i] = height;
  }
}

//void	attemptScreenShake(void)

/* Angle returned is reflected in line x=0 */
SDWORD calcDirection(UDWORD x0, UDWORD y0, UDWORD x1, UDWORD y1)
{
  SDWORD angleInt = 0;
  SDWORD xDif = (x1 - x0);

  /* Watch out here - should really be y1-y0, but coordinate system is reversed in Y */
  SDWORD yDif = (y0 - y1);
  double angle = atan2(yDif, xDif);
  angle = 180 * (angle / pi);
  angleInt = static_cast<SDWORD>(angle);

  angleInt += 90;
  if (angleInt < 0)
    angleInt += 360;

  ASSERT_TEXT(angleInt >= 0 && angleInt < 360, "calcDirection: droid direction out of range");

  return (angleInt);
}

// -------------------------------------------------------------------------------------------
/*	A useful function and one that should have been written long ago, assuming of course
	that is hasn't been!!!! Alex M, 24th Sept, 1998. Returns the nearest unit
	to a given world coordinate - we can choose whether we require that the unit be
	selected or not... Makes sending the most logical unit to do something very easy. 

  NB*****THIS WON'T PICK A VTOL DROID*****
*/

DROID* getNearestDroid(UDWORD x, UDWORD y, BOOL bSelected)
{
  DROID *psDroid, *psBestUnit;
  UDWORD bestSoFar;

  /* Go thru' all the droids  - how often have we seen this - a MACRO maybe? */
  for (psDroid = apsDroidLists[selectedPlayer], psBestUnit = nullptr, bestSoFar = UDWORD_MAX; psDroid; psDroid = psDroid->psNext)
  {
    if (!vtolDroid(psDroid))
    {
      /* Clever (?) bit that reads whether we're interested in droids being selected or not */
      if ((bSelected ? psDroid->selected : TRUE))
      {
        /* Get the differences */
        UDWORD xDif = abs(static_cast<SDWORD>(psDroid->x - x));
        UDWORD yDif = abs(static_cast<SDWORD>(psDroid->y - y));
        /* Approximates the distance away - using a sqrt approximation */
        UDWORD dist = std::max(xDif, yDif) + (std::min(xDif, yDif)) / 2; // approximates, but never more than 11% out...
        /* Is this the nearest one we got so far? */
        if (dist < bestSoFar)
        {
          /* Yes, then keep a record of the distance for comparison... */
          bestSoFar = dist;
          /* ..and store away the droid responsible */
          psBestUnit = psDroid;
        }
      }
    }
  }
  return (psBestUnit);
}

// -------------------------------------------------------------------------------------------

/* Returns non-zero if a point is in a 4 sided polygon */
/* See header file for definition of QUAD */
int inQuad(POINT* pt, QUAD* quad)
{
  int i, j, c = 0;

  for (i = 0, j = 3; i < 4; j = i++)
  {
    if ((((quad->coords[i].y <= pt->y) AND (pt->y < quad->coords[j].y)) OR ((quad->coords[j].y <= pt->y) AND (pt->y < quad->coords[i].y)))
      AND (pt->x < (quad->coords[j].x - quad->coords[i].x) * (pt->y - quad->coords[i].y) / (quad->coords[j].y - quad->coords[i].y) + quad->
        coords[i].x))

      c = !c;
  }

  return c;
}

UDWORD adjustDirection(SDWORD present, SDWORD difference)
{
  SDWORD sum = present + difference;
  if (sum >= 0 AND sum <= 360)
    return static_cast<UDWORD>(sum);

  if (sum < 0)
    return static_cast<UDWORD>(360 + sum);

  if (sum > 360)
    return static_cast<UDWORD>(sum - 360);
}

/* Return a signed difference in direction : a - b
 * result is 180 .. -180
 */
SDWORD directionDiff(SDWORD a, SDWORD b)
{
  SDWORD diff = a - b;

  if (diff > 180)
    return diff - 360;
  if (diff < -180)
    return 360 + diff;

  return diff;
}

void WorldPointToScreen(iPoint* worldPt, iPoint* screenPt)
{
  iVector vec, null;
  /* Get into game context */
  /* Get the x,z translation components */
  int32 rx = player.p.x & (TILE_UNITS - 1);
  int32 rz = player.p.z & (TILE_UNITS - 1);

  /* Push identity matrix onto stack */
  iV_MatrixBegin();

  /* Set the camera position */
  pie_MATTRANS(camera.p.x, camera.p.y, camera.p.z);

  /* Rotate for the player */
  iV_MatrixRotateZ(player.r.z);
  iV_MatrixRotateX(player.r.x);
  iV_MatrixRotateY(player.r.y);

  /* Translate */
  iV_TRANSLATE(-rx, -player.p.y, rz);

  /* No rotation is necessary*/
  null.x = 0;
  null.y = 0;
  null.z = 0;

  /* Pull out coords now, because we use them twice */
  UDWORD worldX = worldPt->x;
  UDWORD worldY = worldPt->y;

  /* Get the coordinates of the object into the grid */
  vec.x = (worldX - player.p.x) - terrainMidX * TILE_UNITS;
  vec.z = terrainMidY * TILE_UNITS - (worldY - player.p.z);

  /* Which tile is it on? - In order to establish height (y coordinate in 3 space) */
  vec.y = map_Height(worldX / TILE_UNITS, worldY / TILE_UNITS);

  /* Set matrix context to local - get an identity matrix */
  iV_MatrixBegin();

  /* Translate */
  iV_TRANSLATE(vec.x, vec.y, vec.z);
  SDWORD xShift = player.p.x & (TILE_UNITS - 1);
  SDWORD zShift = player.p.z & (TILE_UNITS - 1);

  /* Translate */
  iV_TRANSLATE(xShift, 0, -zShift);

  /* Project - no rotation being done. So effectively mapping from 3 space to 2 space */
  pie_RotProj(&null, screenPt);

  /* Pop remaining matrices */
  pie_MatEnd();
  pie_MatEnd();
}

/*	Calculates the RELATIVE screen coords of a game object from its BASE_OBJECT pointer */
/*	Alex - Saturday 5th July, 1997  */
/*	Returns result in POINT pt. They're relative in the sense that even if you pass
	a pointer to an object that isn't on screen, it'll still return a result - just that
	the coords may be negative or larger than screen dimensions in either (or both) axis (axes).
	Remember also, that the Y coordinate axis is reversed for our display in that increasing Y
	implies a movement DOWN the screen, and NOT up. */

void baseObjScreenCoords(BASE_OBJECT* baseObj, iPoint* pt)
{
  iPoint worldPt;

  worldPt.x = baseObj->x;
  worldPt.y = baseObj->y;

  WorldPointToScreen(&worldPt, pt);
}

/* Get the structure pointer for a specified tile coord. NULL if no structure */
STRUCTURE* getTileStructure(UDWORD x, UDWORD y)
{
  /* No point in checking if there's no structure here! */
  if (!TILE_HAS_STRUCTURE(mapTile(x,y)))
    return (nullptr);

  /* Otherwise - see which one it is! */
  STRUCTURE* psReturn = nullptr;
  /* Get the world coords for the tile centre */
  UDWORD centreX = (x << TILE_SHIFT) + (TILE_UNITS / 2);
  UDWORD centreY = (y << TILE_SHIFT) + (TILE_UNITS / 2);

  /* Go thru' all players - drop out if match though */
  for (UDWORD i = 0; i < MAX_PLAYERS AND !psReturn; i++)
  {
    /* Got thru' all structures for this player - again drop out if match */
    for (STRUCTURE* psStructure = apsStructLists[i]; psStructure AND !psReturn; psStructure = psStructure->psNext)
    {
      /* Get structure coords */
      UDWORD strX = psStructure->x;
      UDWORD strY = psStructure->y;
      /* And extents */
      UDWORD width = psStructure->pStructureType->baseWidth * TILE_UNITS;
      UDWORD breadth = psStructure->pStructureType->baseBreadth * TILE_UNITS;
      /* Within x boundary? */

      if ((centreX > (strX - (width / 2))) AND (centreX < (strX + (width / 2))))
      {
        if ((centreY > (strY - (breadth / 2))) AND (centreY < (strY + (breadth / 2))))
          psReturn = psStructure;
      }

      /*			if((centreX > (strX-width)) AND (centreX < (strX+width)) )
            {
              if((centreY > (strY-breadth)) AND (centreY < (strY+breadth)) )
              {
                psReturn = psStructure;
              }
            }
      */
    }
  }
  /* Send back either NULL or structure */
  return (psReturn);
}

/* Sends back the feature on the specified tile - NULL if no feature */
FEATURE* getTileFeature(UDWORD x, UDWORD y)
{
  /* No point in checking if there's no feature here! */
  if (!TILE_HAS_FEATURE(mapTile(x,y)))
    return (nullptr);

  /* Otherwise - see which one it is! */
  FEATURE* psReturn = nullptr;
  /* Get the world coords for the tile centre */
  UDWORD centreX = (x << TILE_SHIFT) + (TILE_UNITS / 2);
  UDWORD centreY = (y << TILE_SHIFT) + (TILE_UNITS / 2);

  /* Go through all features for this player - again drop out if we get one */
  for (FEATURE* psFeature = apsFeatureLists[0]; psFeature AND !psReturn; psFeature = psFeature->psNext)
  {
    /* Get the features coords */
    UDWORD strX = psFeature->x;
    UDWORD strY = psFeature->y;
    /* And it's base dimensions */
    UDWORD width = psFeature->psStats->baseWidth * TILE_UNITS;
    UDWORD breadth = psFeature->psStats->baseBreadth * TILE_UNITS;
    /* Does tile centre lie within the area covered by base of feature? */
    /* First check for x */
    if ((centreX > (strX - (width / 2))) AND (centreX < (strX + (width / 2))))
    {
      /* Got a match on the x - now try y */
      if ((centreY > (strY - (breadth / 2))) AND (centreY < (strY + (breadth / 2))))
      {
        /* Got it! */
        psReturn = psFeature;
      }
    }
  }

  /* Send back either NULL or feature pointer */
  return (psReturn);
}

/*	Will return a base_object pointer to either a structure or feature - depending 
	what's on tile. Returns NULL if nothing */
BASE_OBJECT* getTileOccupier(UDWORD x, UDWORD y)
{
  /* Firsty - check there is something on it?! */
  if (!TILE_OCCUPIED(mapTile(x,y)))
  {
    /* Nothing here at all! */
    return (nullptr);
  }

  /* Now check we can see it... */
  if (TEST_TILE_VISIBLE(selectedPlayer, mapTile(x,y)) == FALSE)
    return (nullptr);

  /* Has it got a fetaure? */
  if (TILE_HAS_FEATURE(mapTile(x,y)))
  {
    /* Return the feature */
    return ((BASE_OBJECT*)getTileFeature(x, y));
  }
  /*	Otherwise check for a structure - we can do else here since a tile cannot
    have both a feature and structure simultaneously */
  if (TILE_HAS_STRUCTURE(mapTile(x,y)))
  {
    /* Send back structure pointer */
    return ((BASE_OBJECT*)getTileStructure(x, y));
  }
}

/* Will return the player who presently has a structure on the specified tile */
UDWORD getTileOwner(UDWORD x, UDWORD y)
{
  /* Arbitrary error code - player 8 (non existent) owns tile from invalid request */
  UDWORD retVal = MAX_PLAYERS;

  /* Check it has a structure - cannot have owner otherwise */
  if (!TILE_HAS_STRUCTURE(mapTile(x,y)))
    Neuron::Fatal("Asking for the owner of a tile with no structure on it!!!");
  else
  {
    /* Get a pointer to the structure */
    STRUCTURE* psStruct = getTileStructure(x, y);

    /* Did we get one - failsafe really as TILE_HAS_STRUCTURE should get it */
    if (psStruct != nullptr)
    {
      /* Pull out the player number */
      retVal = psStruct->player;
    }
  }
  /* returns eith the player number or MAX_PLAYERS to signify error */
  return (retVal);
}

void getObjectsOnTile(MAPTILE* psTile) { (void)psTile; }

// Approximates a square root - never more than 11% out...
UDWORD dirtySqrt(SDWORD x1, SDWORD y1, SDWORD x2, SDWORD y2)
{
  UDWORD xDif = abs(x1 - x2);
  UDWORD yDif = abs(y1 - y2);

  UDWORD retVal = (std::max(xDif, yDif) + (std::min(xDif, yDif) / 2));
  return (retVal);
}

//-----------------------------------------------------------------------------------
BOOL droidOnScreen(DROID* psDroid, SDWORD tolerance)
{
  if (DrawnInLastFrame(psDroid->sDisplay.frameNumber) == TRUE)
  {
    SDWORD dX = psDroid->sDisplay.screenX;
    SDWORD dY = psDroid->sDisplay.screenY;
    /* Is it on screen */
    if (dX > (0 - tolerance) AND dY > (0 - tolerance) AND dX < static_cast<SDWORD>((DISP_WIDTH + tolerance)) AND dY < static_cast<SDWORD>((
      DISP_HEIGHT + tolerance)))
      return (TRUE);
  }
  return (FALSE);
}

void processImpact(UDWORD worldX, UDWORD worldY, UBYTE severity, UDWORD tilesAcross)
{
  ASSERT_TEXT(severity<MAX_TILE_DAMAGE,"Damage is too severe");
  /* Make sure it's odd */
  if (!(tilesAcross & 0x01))
    tilesAcross -= 1;
  SDWORD tileX = ((worldX >> TILE_SHIFT) - (tilesAcross / 2 - 1));
  SDWORD tileY = ((worldY >> TILE_SHIFT) - (tilesAcross / 2 - 1));
  UDWORD maxDisplacement = ((tilesAcross / 2 + 1) * TILE_UNITS);
  maxDisplacement = static_cast<UDWORD>((float)maxDisplacement * (float)1.42);
  UDWORD maxDistance = static_cast<UDWORD>(sqrt(((float)maxDisplacement * (float)maxDisplacement)));

  if (tileX < 0)
    tileX = 0;
  if (tileY < 0)
    tileY = 0;

  for (UDWORD i = tileX; i < tileX + tilesAcross - 1; i++)
  {
    for (UDWORD j = tileY; j < tileY + tilesAcross - 1; j++)
    {
      /* Only process tiles that are on the map */
      if (tileX < static_cast<SDWORD>(mapWidth) AND tileY < static_cast<SDWORD>(mapHeight))
      {
        UDWORD xDif = abs(static_cast<SDWORD>(worldX - (i << TILE_SHIFT)));
        UDWORD yDif = abs(static_cast<SDWORD>(worldY - (j << TILE_SHIFT)));
        UDWORD distance = static_cast<UDWORD>(sqrt(((float)(xDif * xDif) + (float)(yDif * yDif))));
        float multiplier = (1 - static_cast<float>(distance) / static_cast<float>(maxDistance));
        multiplier = static_cast<float>(1.0 - ((float)distance / (float)maxDistance));
        /* Are we talking less than 15% damage? i.e - at the edge of carater? */
        if (multiplier < 0.15)
        {
          /* In which case make the crater edge have jagged edges */
          multiplier += static_cast<float>((float)(20 - rand() % 40) * 0.01);
        }

        UDWORD height = mapTile(i, j)->height;
        UDWORD damage = static_cast<UDWORD>((float)severity * multiplier);
        SDWORD newHeight = height - damage;
        if (newHeight < 0)
          newHeight = 0;
        setTileHeight(i, j, newHeight);
      }
    }
  }
}
