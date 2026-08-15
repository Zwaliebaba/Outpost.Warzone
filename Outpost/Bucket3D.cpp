#include "pch.h"
/* 
	bucket3D.c - stores object render calls in a linked list renders after bucket sorting objects 

				-------------------------------------------------
				Jeremy Sallis, Pumpkin Studios, EIDOS INTERACTIVE
				-------------------------------------------------
*/

/* Includes direct access to matrix code */
#include "RenderTypes.h"
#include "Model.h"
#include "PieState.h"
#include "RendMode.h"
#include "Geo.h"//matrix code
/* Includes from PUMPKIN stuff */
#include "Frame.h"
#include "ObjectDef.h"
#include "Map.h"
#include "Display3D.h"
#include "MiscIMD.h"
#include "Effects.h"
#include "Component.h"
#include "Bucket3D.h"
#include "Message.h"
#include "Console.h"
#include "Atmos.h"

#define NUM_BUCKETS		8000
#define NUM_OBJECTS		4000
#define BUCKET_OFFSET	0
#define BUCKET_RANGE	32000

#define BUCKET_CLIP
#define CLIP_LEFT	(0)
#define CLIP_RIGHT	((SDWORD)DISP_WIDTH)
#define CLIP_TOP	(0)
#define CLIP_BOTTOM ((SDWORD)DISP_HEIGHT)
//scale depth = 1<<FP12_SHIFT>>STRETCHED_Z_SHIFT<<xpshift
#define SCALE_DEPTH (FP12_MULTIPLIER)

using BUCKET_TAG = struct _bucket_tag
{
  struct _bucket_tag* psNextTag;
  RENDER_TYPE objectType; //type of object held
  void* pObject; //pointer to the object
  SDWORD actualZ;
};

BUCKET_TAG tagResource[NUM_OBJECTS];
BUCKET_TAG* bucketArray[NUM_BUCKETS];
UDWORD resourceCounter;
SDWORD zMax;
SDWORD zMin;
SDWORD worldMax, worldMin;
#ifdef _DEBUG
UDWORD rejected;
#endif

/* function prototypes */
SDWORD bucketCalculateZ(RENDER_TYPE objectType, void* pObject);
SDWORD bucketCalculateState(RENDER_TYPE objectType, void* pObject);
void testRender(void);

/* code */
#ifdef _DEBUG
UDWORD getBucketRejected(void) { return (rejected); }
#endif
/* reset object list */
BOOL bucketSetupList(void)
{
  UDWORD i;

  zMax = SDWORD_MIN;
  zMin = SDWORD_MAX;
  worldMax = SDWORD_MIN;
  worldMin = SDWORD_MAX;
  //reset resource
  resourceCounter = 0;
  //reset buckets
  for (i = 0; i < NUM_BUCKETS; i++)
    bucketArray[i] = nullptr;
#ifdef _DEBUG
  rejected = 0;
#endif
  return TRUE;
}

/* add an object to the current render list */
extern BOOL bucketAddTypeToList(RENDER_TYPE objectType, void* pObject)
{
  BUCKET_TAG* newTag;
  DROID* psDroid;
  STRUCTURE* psStructure;
  SDWORD z;

  /*	switch (objectType)
    {
    case RENDER_STRUCTURE:
    case RENDER_DROID:
      return TRUE;
      break;
    case RENDER_EFFECT:
    case RENDER_EXPLOSION:
    case RENDER_GRAVITON:
    case RENDER_SMOKE:
    case RENDER_PROJECTILE:
    case RENDER_PROJECTILE_TRANSPARENT:
    case RENDER_PARTICLE:
    case RENDER_FEATURE:
    case RENDER_PROXMSG:
    case RENDER_SHADOW:
    case RENDER_ANIMATION:
    case RENDER_WATERTILE:
    case RENDER_MIST:
    case RENDER_DELIVPOINT:
    case RENDER_TILE:
      break;
    }*/

  //get next Tag
  newTag = &tagResource[resourceCounter];
  if (resourceCounter >= NUM_OBJECTS)
  {
    /* Just get out if there's too much to render already...! */
    return (TRUE);
  }
  resourceCounter++;
  DEBUG_ASSERT_TEXT(resourceCounter <= NUM_OBJECTS, "bucketAddTypeToList: too many objects");

  //put the object data into the tag
  newTag->objectType = objectType;
  newTag->pObject = pObject;
  if ((objectType == RENDER_EFFECT) && ((static_cast<EFFECT*>(pObject)->group == EFFECT_EXPLOSION) || (static_cast<EFFECT*>(pObject)->group
    == EFFECT_CONSTRUCTION) || (static_cast<EFFECT*>(pObject)->group == EFFECT_SMOKE) || (static_cast<EFFECT*>(pObject)->group ==
    EFFECT_FIREWORK)))
    z = bucketCalculateZ(objectType, pObject);
  else if (objectType == RENDER_SHADOW)
    z = bucketCalculateZ(objectType, pObject);
  else if (objectType == RENDER_PROJECTILE_TRANSPARENT)
    z = bucketCalculateZ(objectType, pObject);
  else if (objectType == RENDER_PROXMSG)
    z = bucketCalculateZ(objectType, pObject);

  else if (objectType == RENDER_WATERTILE)
    z = bucketCalculateZ(objectType, pObject);
    /*	else if(objectType == RENDER_PROJECTILE)//rendered solid so sort by state
    {
      z = bucketCalculateZ(objectType, pObject);
    }
  */ else
    z = bucketCalculateState(objectType, pObject);

  if (z < 0)
  {
    /* Object will not be render - has been clipped! */
    if (objectType == RENDER_DROID)
    {
      /* Won't draw selection boxes */
      psDroid = static_cast<DROID*>(pObject);
      psDroid->sDisplay.frameNumber = 0;
    }
    else if (objectType == RENDER_STRUCTURE)
    {
      /* Won't draw selection boxes */
      psStructure = static_cast<STRUCTURE*>(pObject);
      psStructure->sDisplay.frameNumber = 0;
    }

#ifdef _DEBUG
    rejected++;
#endif
    return TRUE;
  }

  /* Maintain biggest*/
  if (z > worldMax)
    worldMax = z;
  else if (z < worldMin)
    worldMin = z;

  /* get min and max */
  if (z > zMax)
    zMax = z;
  else if (z < zMin)
    zMin = z;

  z = (z * NUM_BUCKETS) / BUCKET_RANGE;

  if (z >= NUM_BUCKETS)
    z = NUM_BUCKETS - 1;

  //add tag to bucketArray
  newTag->psNextTag = bucketArray[z];
  newTag->actualZ = z;
  bucketArray[z] = newTag;
  return TRUE;
}

/* render Objects in list */
extern BOOL bucketRenderCurrentList(void)
{
  SDWORD z;
  BUCKET_TAG* thisTag;

  for (z = NUM_BUCKETS - 1; z >= 0; z--) // render from back to front
  {
    thisTag = bucketArray[z];
    while (thisTag != nullptr)
    {
      switch (thisTag->objectType)
      {
      case RENDER_PARTICLE:
        renderParticle(static_cast<ATPART*>(thisTag->pObject));
        break;
      case RENDER_EFFECT:
        renderEffect(static_cast<EFFECT*>(thisTag->pObject));
        break;
      case RENDER_DROID:
        renderDroid(static_cast<DROID*>(thisTag->pObject));
        break;
      case RENDER_SHADOW:
        renderShadow(static_cast<DROID*>(thisTag->pObject), getImdFromIndex(MI_SHADOW));
        break;
      case RENDER_STRUCTURE:
        renderStructure(static_cast<STRUCTURE*>(thisTag->pObject));
        break;
      case RENDER_FEATURE:
        renderFeature(static_cast<FEATURE*>(thisTag->pObject));
        break;
      case RENDER_PROXMSG:
        renderProximityMsg(static_cast<PROXIMITY_DISPLAY*>(thisTag->pObject));
        break;
      case RENDER_MIST:
#ifdef MIST
        drawTerrainMist(((TILE_BUCKET*)thisTag->pObject)->i, ((TILE_BUCKET*)thisTag->pObject)->j);
#endif
        break;
      case RENDER_TILE:
        drawTerrainTile(static_cast<TILE_BUCKET*>(thisTag->pObject)->i, static_cast<TILE_BUCKET*>(thisTag->pObject)->j);
        break;

      case RENDER_WATERTILE:
        drawTerrainWaterTile(static_cast<TILE_BUCKET*>(thisTag->pObject)->i, static_cast<TILE_BUCKET*>(thisTag->pObject)->j);
        break;

      case RENDER_PROJECTILE:
      case RENDER_PROJECTILE_TRANSPARENT:
        renderProjectile(static_cast<PROJ_OBJECT*>(thisTag->pObject));
        break;
      case RENDER_ANIMATION:
        renderAnimComponent(static_cast<COMPONENT_OBJECT*>(thisTag->pObject));
        break;
      //				case RENDER_GRAVITON:
      //				case RENDER_SMOKE:
      case RENDER_DELIVPOINT:
        renderDeliveryPoint(static_cast<FLAG_POSITION*>(thisTag->pObject));
        break;
      default:
        break;
      }
      thisTag = thisTag->psNextTag;
    }
    //reset the bucket array as we go
    bucketArray[z] = nullptr;
  }

  //reset the tag array
  resourceCounter = 0;
  zMax = SDWORD_MIN;
  zMin = SDWORD_MAX;
  worldMax = SDWORD_MIN;
  worldMin = SDWORD_MAX;
  return TRUE;
}

SDWORD bucketCalculateZ(RENDER_TYPE objectType, void* pObject)
{
  SDWORD z = 0, radius;
  SDWORD px, pz;
  iPoint pixel;
  iVector position;
  UDWORD droidSize;
  DROID* psDroid;
  BODY_STATS* psBStats;
  SIMPLE_OBJECT* psSimpObj;
  COMPONENT_OBJECT* psCompObj;
  iIMDShape* pImd;

  pie_MatBegin();

  switch (objectType)
  {
  case RENDER_PARTICLE:
    px = player.p.x & (TILE_UNITS - 1);
    pz = player.p.z & (TILE_UNITS - 1);

    /* Translate */
    pie_TRANSLATE(px, 0, -pz);

    position.x = static_cast<UDWORD>(std::lrintf(((ATPART*)pObject)->position.x));
    position.y = static_cast<UDWORD>(std::lrintf(((ATPART*)pObject)->position.y));
    position.z = static_cast<UDWORD>(std::lrintf(((ATPART*)pObject)->position.z));

    position.x = position.x - player.p.x - terrainMidX * TILE_UNITS;
    position.z = static_cast<SDWORD>(terrainMidY * TILE_UNITS - (position.z - player.p.z));
    position.y = position.y;

    /* 16 below is HACK!!! */
    z = pie_RotProj(&position, &pixel) - 16;
#ifdef BUCKET_CLIP
    if (z > 0)
    {
      //particle use the image radius
      radius = static_cast<ATPART*>(pObject)->imd->radius;
      radius *= SCALE_DEPTH;
      radius /= z;
      if ((pixel.x + radius < CLIP_LEFT) || (pixel.x - radius > CLIP_RIGHT) || (pixel.y + radius < CLIP_TOP) || (pixel.y - radius >
        CLIP_BOTTOM))
        z = -1;
    }
#endif
    break;
  case RENDER_PROJECTILE: //not depth sorted 
  case RENDER_PROJECTILE_TRANSPARENT:
    static_cast<PROJ_OBJECT*>(pObject)->psWStats;
    /* these guys should never be added to the list anyway */
    if (static_cast<PROJ_OBJECT*>(pObject)->psWStats->weaponSubClass == WSC_FLAME OR static_cast<PROJ_OBJECT*>(pObject)->psWStats->
      weaponSubClass == WSC_COMMAND OR static_cast<PROJ_OBJECT*>(pObject)->psWStats->weaponSubClass == WSC_EMP)
    {
      /* We don't do projectiles from these guys, cos there's an effect instead */
      z = -1;
    }
    else
    {
      //the weapon stats holds the reference to which graphic to use
      pImd = static_cast<PROJ_OBJECT*>(pObject)->psWStats->pInFlightGraphic;

      px = player.p.x & (TILE_UNITS - 1);
      pz = player.p.z & (TILE_UNITS - 1);

      /* Translate */
      pie_TRANSLATE(px, 0, -pz);

      psSimpObj = static_cast<SIMPLE_OBJECT*>(pObject);
      position.x = (psSimpObj->x - player.p.x) - terrainMidX * TILE_UNITS;
      position.z = terrainMidY * TILE_UNITS - (psSimpObj->y - player.p.z);

      position.y = psSimpObj->z;

      z = pie_RotProj(&position, &pixel);
#ifdef BUCKET_CLIP
      if (z > 0)
      {
        //particle use the image radius
        radius = pImd->radius;
        radius *= SCALE_DEPTH;
        radius /= z;
        if ((pixel.x + radius < CLIP_LEFT) || (pixel.x - radius > CLIP_RIGHT) || (pixel.y + radius < CLIP_TOP) || (pixel.y - radius >
          CLIP_BOTTOM))
          z = -1;
      }
#endif
    }
    break;
  case RENDER_STRUCTURE: //not depth sorted
    px = player.p.x & (TILE_UNITS - 1);
    pz = player.p.z & (TILE_UNITS - 1);

    /* Translate */
    pie_TRANSLATE(px, 0, -pz);

    psSimpObj = static_cast<SIMPLE_OBJECT*>(pObject);
    position.x = (psSimpObj->x - player.p.x) - terrainMidX * TILE_UNITS;
    position.z = terrainMidY * TILE_UNITS - (psSimpObj->y - player.p.z);

    //if((objectType == RENDER_STRUCTURE) AND (((STRUCTURE*)pObject)->
    //	pStructureType->type >= REF_DEFENSE) AND
    //	(((STRUCTURE*)pObject)->pStructureType->type<=REF_TOWER4))
    if ((objectType == RENDER_STRUCTURE) AND ((static_cast<STRUCTURE*>(pObject)->pStructureType->type == REF_DEFENSE) OR (static_cast<
      STRUCTURE*>(pObject)->pStructureType->type == REF_WALL) OR (static_cast<STRUCTURE*>(pObject)->pStructureType->type ==
      REF_WALLCORNER)))
    {
      position.y = psSimpObj->z + 64;
#ifdef BUCKET_CLIP
      radius = static_cast<STRUCTURE*>(pObject)->sDisplay.imd->radius; //walls guntowers and tank traps clip tightly
#endif
    }
    else
    {
      position.y = psSimpObj->z;
#ifdef BUCKET_CLIP
      radius = (static_cast<STRUCTURE*>(pObject)->sDisplay.imd->radius) * 2; //other building clipping not so close
#endif
    }

    z = pie_RotProj(&position, &pixel);
#ifdef BUCKET_CLIP
    if (z > 0)
    {
      //particle use the image radius
      radius *= SCALE_DEPTH;
      radius /= z;
      if ((pixel.x + radius < CLIP_LEFT) || (pixel.x - radius > CLIP_RIGHT) || (pixel.y + radius < CLIP_TOP) || (pixel.y - radius >
        CLIP_BOTTOM))
        z = -1;
    }
#endif
    break;
  case RENDER_FEATURE: //not depth sorted
    px = player.p.x & (TILE_UNITS - 1);
    pz = player.p.z & (TILE_UNITS - 1);

    /* Translate */
    pie_TRANSLATE(px, 0, -pz);

    psSimpObj = static_cast<SIMPLE_OBJECT*>(pObject);
    position.x = (psSimpObj->x - player.p.x) - terrainMidX * TILE_UNITS;
    position.z = terrainMidY * TILE_UNITS - (psSimpObj->y - player.p.z);

    position.y = psSimpObj->z + 2;

    z = pie_RotProj(&position, &pixel);
#ifdef BUCKET_CLIP
    if (z > 0)
    {
      //particle use the image radius
      radius = static_cast<FEATURE*>(pObject)->sDisplay.imd->radius;
      radius *= SCALE_DEPTH;
      radius /= z;
      if ((pixel.x + radius < CLIP_LEFT) || (pixel.x - radius > CLIP_RIGHT) || (pixel.y + radius < CLIP_TOP) || (pixel.y - radius >
        CLIP_BOTTOM))
        z = -1;
    }
#endif
    break;
  case RENDER_ANIMATION: //not depth sorted
    px = player.p.x & (TILE_UNITS - 1);
    pz = player.p.z & (TILE_UNITS - 1);

    /* Translate */
    pie_TRANSLATE(px, 0, -pz);

    psCompObj = static_cast<COMPONENT_OBJECT*>(pObject);
    psSimpObj = static_cast<SIMPLE_OBJECT*>(psCompObj->psParent);
    position.x = (psSimpObj->x - player.p.x) - terrainMidX * TILE_UNITS;
    position.z = terrainMidY * TILE_UNITS - (psSimpObj->y - player.p.z);
    position.y = psSimpObj->z;

    /* object offset translation */
    position.x += psCompObj->psShape->ocen.x;
    position.y += psCompObj->psShape->ocen.z;
    position.z -= psCompObj->psShape->ocen.y;

    /* object (animation) translations - ivis z and y flipped */
    pie_TRANSLATE(psCompObj->position.x, psCompObj->position.z, psCompObj->position.y);

    /* object (animation) rotations */
    pie_MatRotY(-psCompObj->orientation.z);
    pie_MatRotZ(-psCompObj->orientation.y);
    pie_MatRotX(-psCompObj->orientation.x);

    z = pie_RotProj(&position, &pixel);
#ifdef BUCKET_CLIP
    /*	Don't do this for animations
    if (z > 0)
    {
      //particle use the image radius
      radius = psCompObj->psShape->radius;
      radius *= SCALE_DEPTH;
      radius /= z;
      if ((pixel.x + radius < CLIP_LEFT) || (pixel.x - radius > CLIP_RIGHT)
        || (pixel.y + radius < CLIP_TOP) || (pixel.y - radius > CLIP_BOTTOM))
      {
        z = -1;
      }
    }
    */
#endif
    break;
  case RENDER_DROID:
  case RENDER_SHADOW:
    psDroid = static_cast<DROID*>(pObject);
    px = player.p.x & (TILE_UNITS - 1);
    pz = player.p.z & (TILE_UNITS - 1);

    /* Translate */
    pie_TRANSLATE(px, 0, -pz);

    psSimpObj = static_cast<SIMPLE_OBJECT*>(pObject);
    position.x = (psSimpObj->x - player.p.x) - terrainMidX * TILE_UNITS;
    position.z = terrainMidY * TILE_UNITS - (psSimpObj->y - player.p.z);
    position.y = psSimpObj->z;
    if (objectType == RENDER_SHADOW)
      position.y += 4;

    psBStats = asBodyStats + psDroid->asBits[COMP_BODY].nStat;
    droidSize = psBStats->pIMD->radius;
    z = pie_RotProj(&position, &pixel) - (droidSize * 2);
#ifdef BUCKET_CLIP
    if (z > 0)
    {
      //particle use the image radius
      radius = droidSize;
      radius *= SCALE_DEPTH;
      radius /= z;
      if ((pixel.x + radius < CLIP_LEFT) || (pixel.x - radius > CLIP_RIGHT) || (pixel.y + radius < CLIP_TOP) || (pixel.y - radius >
        CLIP_BOTTOM))
        z = -1;
    }
#endif
    break;
  case RENDER_PROXMSG:
    px = player.p.x & (TILE_UNITS - 1);
    pz = player.p.z & (TILE_UNITS - 1);

    /* Translate */
    pie_TRANSLATE(px, 0, -pz);
    if (static_cast<PROXIMITY_DISPLAY*>(pObject)->type == POS_PROXDATA)
    {
      position.x = (static_cast<VIEW_PROXIMITY*>(((VIEWDATA*)((PROXIMITY_DISPLAY*)pObject)->psMessage->pViewData)->pData)->x - player.p.x) -
        terrainMidX * TILE_UNITS;
      position.z = terrainMidY * TILE_UNITS - (static_cast<VIEW_PROXIMITY*>(((VIEWDATA*)((PROXIMITY_DISPLAY*)pObject)->psMessage->pViewData)
        ->pData)->y - player.p.z);
      position.y = static_cast<VIEW_PROXIMITY*>(((VIEWDATA*)((PROXIMITY_DISPLAY*)pObject)->psMessage->pViewData)->pData)->z;
    }
    else if (static_cast<PROXIMITY_DISPLAY*>(pObject)->type == POS_PROXOBJ)
    {
      position.x = (((BASE_OBJECT*)static_cast<PROXIMITY_DISPLAY*>(pObject)->psMessage->pViewData)->x - player.p.x) - terrainMidX *
        TILE_UNITS;
      position.z = terrainMidY * TILE_UNITS - (((BASE_OBJECT*)static_cast<PROXIMITY_DISPLAY*>(pObject)->psMessage->pViewData)->y - player.p.
        z);
      position.y = ((BASE_OBJECT*)static_cast<PROXIMITY_DISPLAY*>(pObject)->psMessage->pViewData)->z;
    }
    z = pie_RotProj(&position, &pixel);
#ifdef BUCKET_CLIP
    if (z > 0)
    {
      //particle use the image radius
      pImd = getImdFromIndex(MI_BLIP_ENEMY); //use MI_BLIP_ENEMY as all are same radius
      radius = pImd->radius;
      radius *= SCALE_DEPTH;
      radius /= z;
      if ((pixel.x + radius < CLIP_LEFT) || (pixel.x - radius > CLIP_RIGHT) || (pixel.y + radius < CLIP_TOP) || (pixel.y - radius >
        CLIP_BOTTOM))
        z = -1;
    }
#endif
    break;
  case RENDER_TILE:
    z = static_cast<TILE_BUCKET*>(pObject)->depth;
    break;

  case RENDER_WATERTILE:
    z = static_cast<TILE_BUCKET*>(pObject)->depth;
    break;

  case RENDER_EFFECT:
    px = player.p.x & (TILE_UNITS - 1);
    pz = player.p.z & (TILE_UNITS - 1);

    /* Translate */
    pie_TRANSLATE(px, 0, -pz);

    position.x = static_cast<SDWORD>(((EFFECT*)pObject)->position.x - player.p.x) - terrainMidX * TILE_UNITS;
    position.z = static_cast<SDWORD>(terrainMidY * TILE_UNITS - (((EFFECT*)pObject)->position.z - player.p.z));
    position.y = static_cast<SDWORD>(((EFFECT*)pObject)->position.y);

    /* 16 below is HACK!!! */
    z = pie_RotProj(&position, &pixel) - 16;
#ifdef BUCKET_CLIP
    if (z > 0)
    {
      //particle use the image radius
      pImd = static_cast<EFFECT*>(pObject)->imd;
      if (pImd != nullptr)
      {
        radius = pImd->radius;
        radius *= SCALE_DEPTH;
        radius /= z;
        if ((pixel.x + radius < CLIP_LEFT) || (pixel.x - radius > CLIP_RIGHT) || (pixel.y + radius < CLIP_TOP) || (pixel.y - radius >
          CLIP_BOTTOM))
          z = -1;
      }
    }
#endif
    break;

  case RENDER_DELIVPOINT:
    px = player.p.x & (TILE_UNITS - 1);
    pz = player.p.z & (TILE_UNITS - 1);

    /* Translate */
    pie_TRANSLATE(px, 0, -pz);
    position.x = (static_cast<FLAG_POSITION*>(pObject)->coords.x - player.p.x) - terrainMidX * TILE_UNITS;
    position.z = terrainMidY * TILE_UNITS - (static_cast<FLAG_POSITION*>(pObject)->coords.y - player.p.z);
    position.y = static_cast<FLAG_POSITION*>(pObject)->coords.z;

    z = pie_RotProj(&position, &pixel);
#ifdef BUCKET_CLIP
    if (z > 0)
    {
      //particle use the image radius
      radius = pAssemblyPointIMDs[static_cast<FLAG_POSITION*>(pObject)->factoryType][static_cast<FLAG_POSITION*>(pObject)->factoryInc]->
        radius;
      radius *= SCALE_DEPTH;
      radius /= z;
      if ((pixel.x + radius < CLIP_LEFT) || (pixel.x - radius > CLIP_RIGHT) || (pixel.y + radius < CLIP_TOP) || (pixel.y - radius >
        CLIP_BOTTOM))
        z = -1;
    }
#endif
    break;

  default:
    break;
  }

  pie_MatEnd();

  return z;
}

SDWORD bucketCalculateState(RENDER_TYPE objectType, void* pObject)
{
  SDWORD z = 0;
  iIMDShape* pie;

#ifdef BUCKET_CLIP
  if (bucketCalculateZ(objectType, pObject) < 0)
    return -1;
#endif

  switch (objectType)
  {
  case RENDER_EFFECT:
    switch (static_cast<EFFECT*>(pObject)->group)
    {
    case EFFECT_WAYPOINT:
    case EFFECT_EXPLOSION:
    case EFFECT_CONSTRUCTION:
      pie = static_cast<EFFECT*>(pObject)->imd;
      z = NUM_BUCKETS - pie->texpage;
      break;

    case EFFECT_SMOKE:
    case EFFECT_GRAVITON:
    case EFFECT_BLOOD:
    case EFFECT_STRUCTURE:
    case EFFECT_DESTRUCTION: default:
      z = NUM_BUCKETS - 42;
      break;
    }
    break;
  case RENDER_DROID:
    pie = BODY_IMD(static_cast<DROID*>(pObject), 0);
    z = NUM_BUCKETS - pie->texpage;
    break;
  case RENDER_STRUCTURE:
    pie = static_cast<STRUCTURE*>(pObject)->sDisplay.imd;
    z = NUM_BUCKETS - pie->texpage;
    break;
  case RENDER_FEATURE:
    pie = static_cast<FEATURE*>(pObject)->sDisplay.imd;
    z = NUM_BUCKETS - pie->texpage;
    break;
  case RENDER_PROXMSG:
    z = NUM_BUCKETS - 40;
    break;
  case RENDER_TILE:
    z = NUM_BUCKETS - 1;
    break;

  case RENDER_WATERTILE:
    z = NUM_BUCKETS - 1;
    break;

  case RENDER_PROJECTILE:
    pie = static_cast<PROJ_OBJECT*>(pObject)->psWStats->pInFlightGraphic;
    z = NUM_BUCKETS - pie->texpage;
    break;
  case RENDER_ANIMATION:
    pie = static_cast<COMPONENT_OBJECT*>(pObject)->psShape;
    z = NUM_BUCKETS - pie->texpage;
    break;
  case RENDER_DELIVPOINT:
    pie = pAssemblyPointIMDs[static_cast<FLAG_POSITION*>(pObject)->factoryType][static_cast<FLAG_POSITION*>(pObject)->factoryInc];
    z = NUM_BUCKETS - pie->texpage;
    break;
  default:
    break;
  }

  z *= (BUCKET_RANGE / NUM_BUCKETS); //stretch the dummy depth so its right when its compressed into the bucket array
  return z;
}

void testRender(void)
{
  pie_SetDepthBufferStatus(DEPTH_CMP_ALWAYS_WRT_OFF);
  //render test line
  pie_Line(CLIP_LEFT, CLIP_TOP, CLIP_RIGHT, CLIP_TOP, 2);
  pie_Line(CLIP_RIGHT, CLIP_TOP, CLIP_RIGHT, CLIP_BOTTOM, 2);
  pie_Line(CLIP_RIGHT, CLIP_BOTTOM, CLIP_LEFT, CLIP_BOTTOM, 2);
  pie_Line(CLIP_LEFT, CLIP_BOTTOM, CLIP_LEFT, CLIP_TOP, 2);

  pie_SetDepthBufferStatus(DEPTH_CMP_LEQ_WRT_ON);
}
