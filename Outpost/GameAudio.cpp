#include "pch.h"
/***************************************************************************/
/*
 * GameAudio.cpp
 *
 * The game's AudioWorld provider (was Aud.cpp, whose functions the engine
 * declared in NeuronCore/Aud.h and called by name - the one dependency edge
 * that pointed from engine to game). The same knowledge now flows through
 * the AudioWorld the game hands to Neuron::AudioSystem::Init.
 *
 * The coordinate convention is unchanged from 1997: the audio space is
 * x east, y north, z height, with world y inverted on the way in - "QSOUND
 * axes", which the mixer maps onto X3DAudio. The inversion lives here and
 * nowhere else.
 */
/***************************************************************************/

#include "Frame.h"
#include "Base.h"
#include "Map.h"
#include "Disp2D.h"
#include "Display3D.h"
#include "RenderTypes.h"
#include "GTime.h"

#include "GameAudio.h"
#include "AudioID.h"

/***************************************************************************/

extern BOOL display3D;
extern UDWORD mapX;
extern UDWORD mapY;
extern iView player;

/***************************************************************************/

namespace
{

bool ObjectDead(void* _object)
{
  auto* simpleObject = static_cast<SIMPLE_OBJECT*>(_object);

  if (simpleObject == nullptr)
  {
    Neuron::DebugTrace("GameAudio: object pointer invalid\n");
    return true;
  }

  /* projectiles die at impact, not through BASE_OBJECT::died */
  if (simpleObject->type == OBJ_BULLET)
    return static_cast<PROJ_OBJECT*>(_object)->state == PROJ_POSTIMPACT;

  return static_cast<BASE_OBJECT*>(_object)->died != 0;
}

/***************************************************************************/

void ObjectPosition(void* _object, SDWORD& _x, SDWORD& _y, SDWORD& _z)
{
  auto* baseObject = static_cast<BASE_OBJECT*>(_object);

  _x = baseObject->x;
  _z = map_TileHeight(baseObject->x >> TILE_SHIFT, baseObject->y >> TILE_SHIFT);

  /* invert y to match QSOUND axes */
  _y = (GetHeightOfMap() << TILE_SHIFT) - baseObject->y;
}

/***************************************************************************/

void StaticPosition(SDWORD _worldX, SDWORD _worldY, SDWORD& _x, SDWORD& _y, SDWORD& _z)
{
  _x = _worldX;
  _z = map_TileHeight(_worldX >> TILE_SHIFT, _worldY >> TILE_SHIFT);

  /* invert y to match QSOUND axes */
  _y = (GetHeightOfMap() << TILE_SHIFT) - _worldY;
}

/***************************************************************************/

/* The listener follows the player: the camera in 3D, the map position in
 * the 2D map view. The orientation is the camera's rotation about the
 * vertical axis in both, which is what the old update always fed the mixer.
 */
void ListenerPose(SDWORD& _x, SDWORD& _y, SDWORD& _z, SDWORD& _degrees)
{
  if (display3D == TRUE)
  {
    /* player's y and z interchanged */
    _x = player.p.x + ((visibleXTiles / 2) << TILE_SHIFT);
    _y = player.p.z + ((visibleYTiles / 2) << TILE_SHIFT);
    _z = player.p.y;

    /* invert y to match QSOUND axes */
    _y = (GetHeightOfMap() << TILE_SHIFT) - _y;
  }
  else
  {
    _x = mapX << TILE_SHIFT;
    _y = mapY << TILE_SHIFT;
    _z = 0;
  }

  _degrees = player.r.y / DEG_1;
}

} // namespace

/***************************************************************************/

Neuron::AudioWorld GameAudioWorld()
{
  Neuron::AudioWorld world;

  world.objectDead = ObjectDead;
  world.objectPosition = ObjectPosition;
  world.staticPosition = StaticPosition;
  world.listenerPose = ListenerPose;
  world.trackIdForName = AudioIdFromName; /* AudioID.h's, which already has this shape */
  world.gameTimeMs = [] { return gameTime; };

  return world;
}

/***************************************************************************/
