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

#include "Aud.h"
#include "AudioID.h"

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
    Neuron::DebugTrace("audio_ObjectDead: simple object pointer invalid\n");
    return TRUE;
  }

  /* check projectiles */
  if (psSimpleObj->type == OBJ_BULLET)
  {
    psProj = (PROJ_OBJECT*)psSimpleObj;
    if (psProj == nullptr)
    {
      Neuron::DebugTrace("audio_ObjectDead: projectile object pointer invalid\n");
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
    Neuron::DebugTrace("audio_ObjectDead: base object pointer invalid\n");
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

BOOL audio_GetIDFromStr(STRING* pWavStr, SDWORD* piID) { return audioID_GetIDFromStr(pWavStr, piID); }

/***************************************************************************/

UDWORD sound_GetGameTime(void) { return gameTime; }

/***************************************************************************/
