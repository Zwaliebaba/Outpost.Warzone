#include "pch.h"
/***************************************************************************/
/*
 * Aud.c
 *
 * Warzone audio wrapper functions
 *
 * Gareth Jones 16/12/97
 */
/***************************************************************************/

#include "Frame.h"
#include "Base.h"
#include "Map.h"
#include "Disp2D.h"
#include "Display3D.h"
#include "PieDef.h"
#include "GTime.h"

#include "Cluster.h"
#include "Aud.h"
#include "AudioID.h"
#include "Findpath.h"

/***************************************************************************/

extern BOOL display3D;
extern UDWORD mapX;
extern UDWORD mapY;
extern iView player;
extern UDWORD distance;

/* Map Position of top right hand corner of the screen */
extern UDWORD viewX;
extern UDWORD viewY;

/***************************************************************************/

BOOL audio_ObjectDead(void* psObj)
{
  auto psSimpleObj = static_cast<SIMPLE_OBJECT*>(psObj);
  BASE_OBJECT* psBaseObj;
  PROJ_OBJECT* psProj;

  /* check is valid simple object pointer */
  if (psSimpleObj == nullptr)
  {
    DBPRINTF(("audio_ObjectDead: simple object pointer invalid\n"));
    return TRUE;
  }

  /* check projectiles */
  if (psSimpleObj->type == OBJ_BULLET)
  {
    psProj = (PROJ_OBJECT*)psSimpleObj;
    if (psProj == nullptr)
    {
      DBPRINTF(("audio_ObjectDead: projectile object pointer invalid\n"));
      return TRUE;
    }
    if (psProj->state == PROJ_POSTIMPACT)
      return TRUE;
    return FALSE;
  }
  /* check base object */
  psBaseObj = static_cast<BASE_OBJECT*>(psObj);

  /* check is valid pointer */
  if (psBaseObj == nullptr)
  {
    DBPRINTF(("audio_ObjectDead: base object pointer invalid\n"));
    return TRUE;
  }
  return psBaseObj->died;
}

/***************************************************************************/

void audio_Get2DPlayerPos(SDWORD* piX, SDWORD* piY, SDWORD* piZ)
{
  *piX = mapX << TILE_SHIFT;
  *piY = mapY << TILE_SHIFT;
  *piZ = 0;
}

/***************************************************************************/

void audio_Get3DPlayerPos(SDWORD* piX, SDWORD* piY, SDWORD* piZ)
{
  /* player's y and z interchanged */
  *piX = player.p.x + ((visibleXTiles / 2) << TILE_SHIFT);
  *piY = player.p.z + ((visibleYTiles / 2) << TILE_SHIFT);
  *piZ = player.p.y;

  /* invert y to match QSOUND axes */
  *piY = (GetHeightOfMap() << TILE_SHIFT) - *piY;
}

/***************************************************************************/
/*
 * get player direction vector - always 0 in 2D
 */
/***************************************************************************/

void audio_Get2DPlayerRotAboutVerticalAxis(SDWORD* piA) { *piA = static_cast<SWORD>(0); }

/***************************************************************************/
/*
 * get player direction vector - angle about vertical (y) ivis axis
 */
/***************************************************************************/

void audio_Get3DPlayerRotAboutVerticalAxis(SDWORD* piA) { *piA = player.r.y / DEG_1; }

/***************************************************************************/

BOOL audio_Display3D(void) { return display3D; }

/***************************************************************************/
/*
 * audio_GetStaticPos
 *
 * Get QSound axial position from world (x,y)
 */
/***************************************************************************/

void audio_GetStaticPos(SDWORD iWorldX, SDWORD iWorldY, SDWORD* piX, SDWORD* piY, SDWORD* piZ)
{
  *piX = iWorldX;
  *piZ = map_TileHeight(iWorldX >> TILE_SHIFT, iWorldY >> TILE_SHIFT);
  /* invert y to match QSOUND axes */
  *piY = (GetHeightOfMap() << TILE_SHIFT) - iWorldY;
}

/***************************************************************************/

void audio_GetObjectPos(void* psObj, SDWORD* piX, SDWORD* piY, SDWORD* piZ)
{
  auto psBaseObj = static_cast<BASE_OBJECT*>(psObj);

  /* check is valid pointer */

  *piX = psBaseObj->x;
  *piZ = map_TileHeight(psBaseObj->x >> TILE_SHIFT, psBaseObj->y >> TILE_SHIFT);

  /* invert y to match QSOUND axes */
  *piY = (GetHeightOfMap() << TILE_SHIFT) - psBaseObj->y;
}

/***************************************************************************/

UWORD audio_GetScreenWidth(void) { return static_cast<UWORD>(DISP_WIDTH); }

/***************************************************************************/
/*
 * audio_GetClusterCentre
 *
 * returns FALSE if no droids moving
 */
/***************************************************************************/

BOOL audio_GetClusterCentre(void* psClusterObj, SDWORD* piX, SDWORD* piY, SDWORD* piZ)
{
  SDWORD iClusterID, iNumObj;
  auto psDroid = static_cast<DROID*>(psClusterObj);
  BOOL bDroidInClusterMoving = FALSE;

  /* check valid pointer */

  iNumObj = *piX = *piY = *piZ = 0;

  /* clustGetClusterID returns 0 if cluster is empty or no droids moving */
  iClusterID = clustGetClusterID(static_cast<BASE_OBJECT*>(psClusterObj));
  if (iClusterID == 0)
  {
    DBPRINTF(("audio_GetClusterCentre: empty cluster!\n"));
    return FALSE;
  }
  clustInitIterate(iClusterID);
  do
  {
    psDroid = (DROID*)clustIterate();
    if (psDroid != nullptr && psDroid->sMove.Status != MOVEINACTIVE)
    {
      iNumObj++;
      *piX += psDroid->x;
      *piY += psDroid->y;
      *piZ += psDroid->z;
      bDroidInClusterMoving = TRUE;
    }
  }
  while (psDroid != nullptr);

  /* get average */
  if (bDroidInClusterMoving == TRUE)
  {
    *piX /= iNumObj;
    *piY /= iNumObj;
    *piZ /= iNumObj;

    /* invert y to match QSOUND axes */
    *piY = (GetHeightOfMap() << TILE_SHIFT) - *piY;
  }

  return bDroidInClusterMoving;
}

/***************************************************************************/
/*
 * audio_GetNewClusterObject
 *
 * get next droid in cluster if current object dead
 */
/***************************************************************************/

BOOL audio_GetNewClusterObject(void** psClusterObj, SDWORD iClusterID)
{
  auto psDroid = static_cast<DROID*>(*psClusterObj);

  /* check valid pointer */

  /* return if droid not dead */
  if (!psDroid->died)
    return FALSE;

  if (iClusterID == 0)
  {
    DBPRINTF(("audio_GetNewClusterObject: empty cluster!\n"));
    return FALSE;
  }
  /* find next undying droid in cluster */
  clustInitIterate(iClusterID);
  do
  {
    psDroid = (DROID*)clustIterate();
    if (psDroid != nullptr && !psDroid->died)
    {
      *psClusterObj = psDroid;
      return TRUE;
    }
  }
  while (psDroid != nullptr);

  return FALSE;
}

/***************************************************************************/

BOOL audio_ClusterEmpty(void* psClusterObj)
{
  /* clustGetClusterID returns 0 if cluster is empty */
  if (clustGetClusterID(static_cast<BASE_OBJECT*>(psClusterObj)) == 0)
    return TRUE;
  return FALSE;
}

/***************************************************************************/

SDWORD audio_GetClusterIDFromObj(void* psClusterObj) { return clustGetClusterID(static_cast<BASE_OBJECT*>(psClusterObj)); }

/***************************************************************************/

BOOL audio_GetIDFromStr(STRING* pWavStr, SDWORD* piID) { return audioID_GetIDFromStr(pWavStr, piID); }

/***************************************************************************/

UDWORD sound_GetGameTime(void) { return gameTime; }

/***************************************************************************/
