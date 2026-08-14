#include "pch.h"
/*
 * MapGrid.c
 *
 * Functions for storing objects in a grid over the map.
 * The objects are stored in every grid over which they might
 * have some influence.
 *
 */

#include "Frame.h"
#include "Objects.h"
#include "Map.h"
#include "MapGrid.h"

// The number of world units per grid
#define GRID_UNITS	(GRID_SIZE * TILE_UNITS)

// Initial and extension sizes for the grid heap
#define GRID_HEAPINIT	(GRID_MAXAREA)
#define GRID_HEAPEXT	4

UDWORD gridWidth, gridHeight;

// The heap for the grid arrays
OBJ_HEAP* psGridHeap;

// The map grid 
GRID_ARRAY* apsMapGrid[GRID_MAXAREA];
#define GridIndex(a,b) (((b)*gridWidth) + (a))

// which grid to garbage collect on next
SDWORD garbageX, garbageY;

// the current state of the iterator
GRID_ARRAY* psIterateGrid;
SDWORD iterateEntry;

// what to do when calculating the coverage of an object
using COVERAGE_MODE = enum _coverage_mode
{
  GRID_ADDOBJECT,
  GRID_REMOVEOBJECT,
};

// Function prototypes
BOOL gridIntersect(SDWORD x1, SDWORD y1, SDWORD x2, SDWORD y2, SDWORD cx, SDWORD cy, SDWORD Rad);
void gridAddArrayObject(SDWORD x, SDWORD y, BASE_OBJECT* psObj);
void gridRemoveArrayObject(SDWORD x, SDWORD y, BASE_OBJECT* psObj);
void gridCompactArray(SDWORD x, SDWORD y);
SDWORD gridObjRange(BASE_OBJECT* psObj);

// initialise the grid system
BOOL gridInitialise(void)
{
  if (!HEAP_CREATE(&psGridHeap, sizeof(GRID_ARRAY), GRID_HEAPINIT, GRID_HEAPEXT))
    return FALSE;

  memset(apsMapGrid, 0, sizeof(GRID_ARRAY*) * GRID_MAXAREA);

  garbageX = 0;
  garbageY = 0;

  psIterateGrid = nullptr;
  iterateEntry = 0;

  return TRUE;
}

//clear the grid of everything on it
void gridClear(void)
{
  GRID_ARRAY *psCurr, *psNext;
  SDWORD x, y;

  DBPRINTF(("gridClear %d %d\n",gridWidth,gridHeight));
  for (x = 0; x < gridWidth; x += 1)
  {
    for (y = 0; y < gridHeight; y += 1)
    {
      for (psCurr = apsMapGrid[GridIndex(x, y)]; psCurr; psCurr = psNext)
      {
        psNext = psCurr->psNext;
        HEAP_FREE(psGridHeap, psCurr);
      }
      apsMapGrid[GridIndex(x, y)] = nullptr;
    }
  }
}

// reset the grid system
void gridReset(void)
{
  STRUCTURE* psStruct;
  DROID* psDroid;
  FEATURE* psFeature;
  UBYTE inc;

  // Setup the grid dimensions.
  gridWidth = (mapWidth + GRID_SIZE - 1) / GRID_SIZE;
  gridHeight = (mapHeight + GRID_SIZE - 1) / GRID_SIZE;

  gridClear();

  //put all the existing objects into the grid
  for (inc = 0; inc < MAX_PLAYERS; inc++)
  {
    for (psDroid = apsDroidLists[inc]; psDroid != nullptr; psDroid = psDroid->psNext)
      gridAddObject((BASE_OBJECT*)psDroid);
    for (psStruct = apsStructLists[inc]; psStruct != nullptr; psStruct = psStruct->psNext)
      gridAddObject((BASE_OBJECT*)psStruct);
    for (psFeature = apsFeatureLists[inc]; psFeature != nullptr; psFeature = psFeature->psNext)
      gridAddObject((BASE_OBJECT*)psFeature);
  }
}

// shutdown the grid system
void gridShutDown(void)
{
  gridClear();

  HEAP_DESTROY(psGridHeap);
}

// find the grid's that are covered by the object and either
// add or remove the object
void gridCalcCoverage(BASE_OBJECT* psObj, SDWORD objx, SDWORD objy, COVERAGE_MODE mode)
{
  SDWORD range, x, y, minx, maxx, miny, maxy;

  range = gridObjRange(psObj);

  // calculate the range of grids that could be covered by the object
  objx = (objx & ~TILE_MASK) + TILE_UNITS / 2;
  objy = (objy & ~TILE_MASK) + TILE_UNITS / 2;
  minx = objx - range;
  maxx = objx + range;
  miny = objy - range;
  maxy = objy + range;

  minx = (minx >> TILE_SHIFT) / GRID_SIZE;
  maxx = (maxx >> TILE_SHIFT) / GRID_SIZE;
  miny = (miny >> TILE_SHIFT) / GRID_SIZE;
  maxy = (maxy >> TILE_SHIFT) / GRID_SIZE;

  // see which ones are covered by the object
  for (x = minx; x <= maxx; x++)
  {
    //		if ( (x >= 0) && (x < GRID_WIDTH) )
    if ((x >= 0) && (x < gridWidth))
    {
      for (y = miny; y <= maxy; y++)
      {
        //				if ( (y >= 0) && (y < GRID_HEIGHT) &&
        if ((y >= 0) && (y < gridHeight) && gridIntersect(x * GRID_UNITS, y * GRID_UNITS, (x + 1) * GRID_UNITS, (y + 1) * GRID_UNITS, objx,
                                                          objy, range))
        {
          switch (mode)
          {
          case GRID_ADDOBJECT:
            gridAddArrayObject(x, y, psObj);
            break;
          case GRID_REMOVEOBJECT:
            gridRemoveArrayObject(x, y, psObj);
            break;
          }
        }
      }
    }
  }
}

// add an object to the grid system
void gridAddObject(BASE_OBJECT* psObj) { gridCalcCoverage(psObj, psObj->x, psObj->y, GRID_ADDOBJECT); }

// move an object within the grid
// oldX,oldY are the old position of the object in world coords
void gridMoveObject(BASE_OBJECT* psObj, SDWORD oldX, SDWORD oldY)
{
  if (((psObj->x >> TILE_SHIFT) == (static_cast<UDWORD>(oldX) >> TILE_SHIFT)) && ((psObj->y >> TILE_SHIFT) == (static_cast<UDWORD>(oldY) >>
    TILE_SHIFT)))
  {
    // havn't changed the tile the object is on, don't bother updating
    return;
  }

  gridCalcCoverage(psObj, oldX, oldY, GRID_REMOVEOBJECT);
  gridCalcCoverage(psObj, psObj->x, psObj->y, GRID_ADDOBJECT);
}

// remove an object from the grid system
void gridRemoveObject(BASE_OBJECT* psObj)
{
  gridCalcCoverage(psObj, psObj->x, psObj->y, GRID_REMOVEOBJECT);

#if defined(DEBUG)
  {
    GRID_ARRAY* psCurr;
    SDWORD i, x, y;

    for (x = 0; x < gridWidth; x++)
    {
      for (y = 0; y < gridHeight; y++)
      {
        for (psCurr = apsMapGrid[GridIndex(x, y)]; psCurr; psCurr = psCurr->psNext)
        {
          for (i = 0; i < MAX_GRID_ARRAY_CHUNK; i++)
          {
            if (psCurr->apsObjects[i] == psObj)
            {
              DEBUG_ASSERT_TEXT(FALSE, "gridRemoveObject: grid out of sync");
              psCurr->apsObjects[i] = nullptr;
            }
          }
        }
      }
    }
  }
#endif
}

// initialise the grid system to start iterating through units that
void gridStartIterate(SDWORD x, SDWORD y)
{
  //	ASSERT(( (x >= 0) && (x < GRID_WIDTH*GRID_UNITS) &&
  //			 (y >= 0) && (y < GRID_WIDTH*GRID_UNITS),
  DEBUG_ASSERT_TEXT((x >= 0) && (x < gridWidth*GRID_UNITS) && (y >= 0) && (y < gridHeight*GRID_UNITS), "gridStartIterate: coords off grid");

  x = x / GRID_UNITS;
  y = y / GRID_UNITS;

  psIterateGrid = apsMapGrid[GridIndex(x, y)];
  iterateEntry = 0;
}

// get the next object that could affect a location,
// should only be called after gridStartIterate
BASE_OBJECT* gridIterate(void)
{
  BASE_OBJECT* psRet;

  while (psIterateGrid != nullptr)
  {
    if (psIterateGrid->apsObjects[iterateEntry] != nullptr)
      break;

    iterateEntry += 1;
    if (iterateEntry >= MAX_GRID_ARRAY_CHUNK)
    {
      psIterateGrid = psIterateGrid->psNext;
      iterateEntry = 0;
    }
  }

  if (psIterateGrid)
  {
    psRet = psIterateGrid->apsObjects[iterateEntry];
    iterateEntry += 1;
    if (iterateEntry >= MAX_GRID_ARRAY_CHUNK)
    {
      psIterateGrid = psIterateGrid->psNext;
      iterateEntry = 0;
    }
  }
  else
    psRet = nullptr;

  return psRet;
}

// compact some of the grid arrays
void gridGarbageCollect(void)
{
  gridCompactArray(garbageX, garbageY);

  garbageX += 1;
  if (garbageX >= gridWidth)
  {
    garbageX = 0;
    garbageY += 1;

    if (garbageY >= gridHeight)
    {
      garbageX = 0;
      garbageY = 0;
    }
  }
}

// add an object to a grid array
void gridAddArrayObject(SDWORD x, SDWORD y, BASE_OBJECT* psObj)
{
  GRID_ARRAY *psPrev, *psCurr, *psNew;
  SDWORD i;

  // see if there is an empty slot in the currently allocated array
  psPrev = nullptr;
  for (psCurr = apsMapGrid[GridIndex(x, y)]; psCurr; psCurr = psCurr->psNext)
  {
    for (i = 0; i < MAX_GRID_ARRAY_CHUNK; i++)
    {
      if (psCurr->apsObjects[i] == nullptr)
      {
        // found an empty slot
        psCurr->apsObjects[i] = psObj;
        return;
      }
    }
    psPrev = psCurr;
  }

  // allocate a new array chunk
  if (!HEAP_ALLOC(psGridHeap, &psNew))
  {
    DBPRINTF(("help - %d\n", psObj->id));
    return;
  }

  // store the object
  memset(psNew, 0, sizeof(GRID_ARRAY));
  psNew->apsObjects[0] = psObj;

  // add the chunk to the end of the list
  if (psPrev == nullptr)
    apsMapGrid[GridIndex(x, y)] = psNew;
  else
    psPrev->psNext = psNew;
}

// remove an object from a grid array
void gridRemoveArrayObject(SDWORD x, SDWORD y, BASE_OBJECT* psObj)
{
  GRID_ARRAY* psCurr;
  SDWORD i;

  for (psCurr = apsMapGrid[GridIndex(x, y)]; psCurr; psCurr = psCurr->psNext)
  {
    for (i = 0; i < MAX_GRID_ARRAY_CHUNK; i++)
    {
      if (psCurr->apsObjects[i] == psObj)
      {
        psCurr->apsObjects[i] = nullptr;
        return;
      }
    }
  }
}

// Compact a grid array to remove any NULL objects
void gridCompactArray(SDWORD x, SDWORD y)
{
  GRID_ARRAY *psDone, *psMove, *psPrev, *psNext;
  SDWORD done, move;

  psDone = psMove = apsMapGrid[GridIndex(x, y)];
  done = move = 0;

  // move every entry down in the array
  psPrev = nullptr;
  while (psMove != nullptr)
  {
    if (psMove->apsObjects[move] != nullptr)
    {
      psDone->apsObjects[done] = psMove->apsObjects[move];

      done += 1;
      if (done >= MAX_GRID_ARRAY_CHUNK)
      {
        psPrev = psDone;
        psDone = psDone->psNext;
        done = 0;
      }
    }

    move += 1;
    if (move >= MAX_GRID_ARRAY_CHUNK)
    {
      psMove = psMove->psNext;
      move = 0;
    }
  }

  // if the compacted array finishes half way through, NULL the rest of it
  if ((psDone != nullptr) && (done != 0))
  {
    memset(&psDone->apsObjects[done], 0, sizeof(BASE_OBJECT*) * (MAX_GRID_ARRAY_CHUNK - done));

    psPrev = psDone;
    psDone = psDone->psNext;
  }

  // now release any unused chunks
  if (psPrev == nullptr)
    apsMapGrid[GridIndex(x, y)] = nullptr;
  else
    psPrev->psNext = nullptr;
  while (psDone)
  {
    psNext = psDone->psNext;
    HEAP_FREE(psGridHeap, psDone);
    psDone = psNext;
  }
}

// Display all the grid's an object is a member of
void gridDisplayCoverage(BASE_OBJECT* psObj)
{
  UNUSEDPARAMETER(psObj);
#ifdef DEBUG
  {
    SDWORD x, y, i;
    GRID_ARRAY* psCurr;

    DBPRINTF(("Grid coverage for object %d (%d,%d) - range %d\n", psObj->id, psObj->x,psObj->y, gridObjRange(psObj)));
    for (x = 0; x < gridWidth; x++)
    {
      for (y = 0; y < gridHeight; y++)
      {
        psCurr = apsMapGrid[GridIndex(x, y)];
        i = 0;
        while (psCurr != nullptr)
        {
          if (psCurr->apsObjects[i] == psObj)
          {
            DBPRINTF(("    %d,%d  [ %d,%d -> %d,%d ]\n", x,y, x*GRID_UNITS,y*GRID_UNITS, (x+1)*GRID_UNITS,(y+1)*GRID_UNITS));
          }

          i += 1;
          if (i >= MAX_GRID_ARRAY_CHUNK)
          {
            psCurr = psCurr->psNext;
            i = 0;
          }
        }
      }
    }
  }
#endif
}

// Fast circle rectangle intersection, taken from Graphics Gems I, p51
// by Clifford A Shaffer
/* Return TRUE iff rectangle R intersects circle with centerpoint C and
   radius Rad. */
BOOL gridIntersect(SDWORD x1, SDWORD y1, SDWORD x2, SDWORD y2, SDWORD cx, SDWORD cy, SDWORD Rad)
{
  SDWORD Rad2;

  Rad2 = Rad * Rad;

  /* Translate coordinates, placing C at the origin. */
  x1 -= cx;
  y1 -= cy;
  x2 -= cx;
  y2 -= cy;

  if (x2 < 0) /* R to left of circle center */
  {
    if (y2 < 0) /* R in lower left corner */
      return ((x2 * x2 + y2 * y2) < Rad2);
    if (y1 > 0) /* R in upper left corner */
      return ((x2 * x2 + y1 * y1) < Rad2);
    /* R due West of circle */
    return (abs(x2) < Rad);
  }
  if (x1 > 0) /* R to right of circle center */
  {
    if (y2 < 0) /* R in lower right corner */
      return ((x1 * x1 + y2 * y2) < Rad2);
    if (y1 > 0) /* R in upper right corner */
      return ((x1 * x1 + y1 * y1) < Rad2);
    /* R due East of circle */
    return (x1 < Rad);
  }
  /* R on circle vertical centerline */
  if (y2 < 0) /* R due South of circle */
    return (abs(y2) < Rad);
  if (y1 > 0) /* R due North of circle */
    return (y1 < Rad);
  /* R contains circle centerpoint */
  return (TRUE);
}

// Get the range of effect of an object
SDWORD gridObjRange(BASE_OBJECT* psObj)
{
  /*	SDWORD	range;
  
    switch (psObj->type)
    {
    case OBJ_DROID:
      range = ((DROID *)psObj)->sensorRange;
      break;
    case OBJ_STRUCTURE:
      range = ((STRUCTURE *)psObj)->sensorRange;
      if (structCBSensor((STRUCTURE *)psObj) ||
        structVTOLCBSensor((STRUCTURE *)psObj))
      {
        range = MAP_MAXWIDTH > MAP_MAXHEIGHT ?
          MAP_MAXWIDTH*TILE_UNITS : MAP_MAXHEIGHT*TILE_UNITS;
      }
      break;
    case OBJ_FEATURE:
      range = 0;
      break;
    default:
      range = 0;
      break;
    }
  
    if (range < NAYBOR_RANGE)
    {
      range = NAYBOR_RANGE;
    }
  
    return range;*/

  return (TILE_UNITS * 20);
}
