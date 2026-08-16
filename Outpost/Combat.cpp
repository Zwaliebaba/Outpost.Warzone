#include "pch.h"
#include <directxmath.h>
#include <cmath>
/*
 * Combat.c
 *
 * Combat mechanics routines.
 *
 */

#include <math.h>

/* Turn on the damage printf's from combExplodeBullet */
/* Turn on LOS printf's */
/* Turn on Missed printf's */
#include "Frame.h"

#include "Objects.h"
#include "Combat.h"
#include "Stats.h"
#include "Visibility.h"
#include "GTime.h"
#include "Map.h"
#include "Move.h"
#include "Findpath.h"
#include "MessageDef.h"
#include "MiscIMD.h"
#include "Projectile.h"
#include "AudioSystem.h"
#include "AudioID.h"
#include "Geometry.h"
#include "CmdDroid.h"
#include "MapGrid.h"
#include "Order.h"
#include "AI.h"
#include "Action.h"

#define	EXPLOSION_AUDIO	0

/* The buffer to store LOS points */
static TILE_COORD* aLOSPoints;

/* Number of tiles that missed bullets scatter from target */

/* minimum miss distance */
#define MIN_MISSDIST	(TILE_UNITS/6)

/* The number of tiles of clear space needed for indirect fire */
#define INDIRECT_LOSDIST 2

// maximum random pause for firing
#define RANDOM_PAUSE	500

// visibility level below which the to hit chances are reduced
#define VIS_ATTACK_MOD_LEVEL	150

/* direction array for missed bullets */
using BUL_DIR = struct _bul_dir
{
  SDWORD x, y;
};
#define BUL_MAXSCATTERDIR 8
static BUL_DIR aScatterDir[BUL_MAXSCATTERDIR] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},};

/* Initialise the combat system */
BOOL combInitialise(void) { return TRUE; }

/* Shutdown the combat system */
BOOL combShutdown(void) { return TRUE; }

/* Fire a weapon at something */
void combFire(WEAPON* psWeap, BASE_OBJECT* psAttacker, BASE_OBJECT* psTarget)
{
  WEAPON_STATS* psStats;
  UDWORD xDiff, yDiff, distSquared;
  UDWORD dice, damLevel;
  SDWORD missDir, missDist, missX, missY;
  SDWORD hitMod, hitInc, fireChance;
  UDWORD firePause;
  float targetDir, dirDiff;
  SDWORD longRange;
  DROID* psDroid;
  SDWORD level, cmdLevel;
  BOOL bMissVisible;

  /* Get the stats for the weapon */
  psStats = asWeaponStats + psWeap->nStat;

  //check valid weapon/prop combination
  if (!validTarget(psAttacker, psTarget))
    return;

  /*see if reload-able weapon and out of ammo*/
  if (psStats->reloadTime AND !psWeap->ammo)
  {
    if (gameTime - psWeap->lastFired < psStats->reloadTime)
      return;
    //reset the ammo level
    psWeap->ammo = psStats->numRounds;
  }

  /* See when the weapon last fired to control it's rate of fire */
  firePause = weaponFirePause(psStats, psAttacker->player);

  // increase the pause if heavily damaged
  switch (psAttacker->type)
  {
  case OBJ_DROID:
    psDroid = (DROID*)psAttacker;
    damLevel = PERCENT(psDroid->body, psDroid->originalBody);

    break;
  case OBJ_STRUCTURE:
    damLevel = PERCENT(((STRUCTURE *)psAttacker)->body, structureBody((STRUCTURE *)psAttacker));
    break;
  default:
    damLevel = 100;
    break;
  }

  if (damLevel < HEAVY_DAMAGE_LEVEL)
    firePause += firePause;

  if (gameTime - psWeap->lastFired <= firePause)
  {
    /* Too soon to fire again */
    return;
  }

  // add a random delay to the fire
  fireChance = gameTime - (psWeap->lastFired + firePause);
  if (rand() % RANDOM_PAUSE > fireChance)
    return;

  /* Check we can see the target */
  if ((psAttacker->type == OBJ_DROID) && !vtolDroid((DROID*)psAttacker) && (proj_Direct(psStats) ||
    actionInsideMinRange(psDroid, psDroid->psActionTarget)))
  {
    if (!visibleObjWallBlock(psAttacker, psTarget))
    {
      // Can't see the target - can't hit it with direct fire 
      return;
    }
  }
  else if ((psAttacker->type == OBJ_STRUCTURE) && (((STRUCTURE*)psAttacker)->pStructureType->height == 1) && proj_Direct(psStats))
  {
    // a bunker can't shoot through walls
    if (!visibleObjWallBlock(psAttacker, psTarget))
    {
      // Can't see the target - can't hit it with direct fire 
      return;
    }
  }
  else if (proj_Direct(psStats))
  {
    if (!visibleObject(psAttacker, psTarget))
    {
      // Can't see the target - can't hit it with direct fire 
      return;
    }
  }
  else
  {
    if (!psTarget->visible[psAttacker->player])
    {
      // Can't get an indirect LOS - can't hit it with the weapon 
      return;
    }
  }

  /*	if ( proj_Direct(psStats) ||
       ((psAttacker->type == OBJ_DROID) &&
        !proj_Direct(psStats) &&
         actionInsideMinRange(psDroid, psDroid->psActionTarget)) )
    {
      switch (psAttacker->type)
      {
      case OBJ_DROID:
        if (!visibleObjWallBlock(psAttacker, psTarget))
        {
          // Can't see the target - can't hit it with direct fire 
          DBP3(("directLOS failed\n"));
          return;
        }
        break;
      default:
        if (!visibleObject(psAttacker, psTarget))
        {
          // Can't see the target - can't hit it with direct fire 
          DBP3(("directLOS failed\n"));
          return;
        }
        break;
      }
    }
    else
    {
      if (!psTarget->visible[psAttacker->player])
      {
        // Can't get an indirect LOS - can't hit it with the weapon 
        DBP3(("indirectLOS failed\n"));
        return;
      }
    }*/

  // if the turret doesn't turn, check the attacker is in alignment with the
  // target
  if (psAttacker->type == OBJ_DROID && !psStats->rotate)
  {
    targetDir = calcDirection(psAttacker->x, psAttacker->y, psTarget->x, psTarget->y);
    dirDiff = std::fabs(DirectX::XMScalarModAngle(targetDir - psAttacker->direction));
    if (dirDiff > DirectX::XMConvertToRadians(static_cast<float>(FIXED_TURRET_DIR)))
      return;
  }

  // base modifier 100% of original
  hitMod = 100;
  // base hit increment of zero
  hitInc = 0;

  // apply upgrades - do these when know if its longHit or shortHit
  //hitMod = hitMod * (asWeaponUpgrade[psAttacker->player]

  // add the attackers experience modifier
  if (psAttacker->type == OBJ_DROID)
  {
    level = getDroidLevel((DROID*)psAttacker);
    cmdLevel = cmdGetCommanderLevel((DROID*)psAttacker);
    if (level > cmdLevel)
      hitInc += 5 * level;
    else
      hitInc += 5 * cmdLevel;
  }

  // subtract the defenders experience modifier
  /*	if (psTarget->type == OBJ_DROID)
    {
      level = getDroidLevel((DROID *)psTarget);
      cmdLevel = cmdGetCommanderLevel((DROID *)psTarget);
      if (level > cmdLevel)
      {
        hitInc -= 5 * level;
      }
      else
      {
        hitInc -= 5 * cmdLevel;
      }
    }*/

  // fire while moving modifiers
  if (psAttacker->type == OBJ_DROID && ((DROID*)psAttacker)->sMove.Status != MOVEINACTIVE)
  {
    switch (psStats->fireOnMove)
    {
    case FOM_NO:
      // Can't fire while moving
      return;
      break;
    case FOM_PARTIAL:
      hitMod = 50 * hitMod / 100;
      break;
    case FOM_YES:
      // can fire while moving
      break;
    }
  }

  // visibility modifiers
  if (psTarget->visible[psAttacker->player] < VIS_ATTACK_MOD_LEVEL)
    hitMod = 50 * hitMod / 100;


  /* Now see if the target is in range  - also check not too near*/
  xDiff = abs(psAttacker->x - psTarget->x);
  yDiff = abs(psAttacker->y - psTarget->y);
  distSquared = xDiff * xDiff + yDiff * yDiff;
  longRange = proj_GetLongRange(psStats, static_cast<SDWORD>(psAttacker->z) - static_cast<SDWORD>(psTarget->z));
  if (distSquared <= (psStats->shortRange * psStats->shortRange) AND distSquared >= (psStats->minRange * psStats->minRange))
  {
    /* note when the weapon fired */
    psWeap->lastFired = gameTime;
    /*reduce ammo if salvo*/
    if (psStats->reloadTime)
      psWeap->ammo--;

    /* Can do a short range shot - see if it hits */
    /************************************************/
    /* NEED TO TAKE ACCOUNT OF ECM, BODY SHAPE ETC. */
    /************************************************/
    HIT_ROLL(dice);
    if (dice <= (weaponShortHit(psStats, psAttacker->player) * hitMod / 100) + hitInc)
    {
      /* Kerrrbaaang !!!!! a hit */
      if (!proj_SendProjectile(psWeap, psAttacker, psAttacker->player, psTarget->x, psTarget->y, psTarget->z, psTarget, FALSE))
      {
        /* Out of memory - we can safely ignore this */
        return;
      }
    }
    else
      goto missed;
  }
  else if (static_cast<SDWORD>(distSquared) <= longRange * longRange && ((distSquared >= psStats->minRange * psStats->minRange) || ((
    psAttacker->type == OBJ_DROID) && !proj_Direct(psStats) && actionInsideMinRange(psDroid, psDroid->psActionTarget))))
  {
    /* note when the weapon fired */
    psWeap->lastFired = gameTime;
    /*reduce ammo if salvo*/
    if (psStats->reloadTime)
      psWeap->ammo--;

    /* Can do a long range shot - see if it hits */
    /************************************************/
    /* NEED TO TAKE ACCOUNT OF ECM, BODY SHAPE ETC. */
    /************************************************/
    HIT_ROLL(dice);
    if (dice <= (weaponLongHit(psStats, psAttacker->player) * hitMod / 100) + hitInc)
    {
      /* Kerrrbaaang !!!!! a hit */
      if (!proj_SendProjectile(psWeap, psAttacker, psAttacker->player, psTarget->x, psTarget->y, psTarget->z, psTarget, FALSE))
      {
        /* Out of memory - we can safely ignore this */
        return;
      }
    }
    else
      goto missed;
  }
  else
  {
    /* Out of range */
    return;
  }

  return;

missed:
  /* Deal with a missed shot */

  // Approximate the distance between the attacker and target
  xDiff = ABSDIF(psAttacker->x, psTarget->x);
  yDiff = ABSDIF(psAttacker->y, psTarget->y);
  missDist = (xDiff > yDiff ? xDiff + (yDiff >> 1) : yDiff + (xDiff >> 1));

  // Calculate where the shot will end up
  missDist = missDist >> 2;
  if (missDist < MIN_MISSDIST)
    missDist = MIN_MISSDIST;
  missDir = rand() % BUL_MAXSCATTERDIR;
  missX = aScatterDir[missDir].x * (rand() % missDist) + psTarget->x;
  missY = aScatterDir[missDir].y * (rand() % missDist) + psTarget->y;

  // decide if a miss is visible
  bMissVisible = FALSE;
  if (psTarget->player == selectedPlayer)
    bMissVisible = TRUE;

  /* Fire off the bullet to the miss location */
  if (!proj_SendProjectile(psWeap, psAttacker, psAttacker->player, missX, missY, psTarget->z, nullptr, bMissVisible))
  {
    /* Out of memory */
    return;
  }
}

/*checks through the target players list of structures and droids to see 
if any support a counter battery sensor*/
void counterBatteryFire(BASE_OBJECT* psAttacker, BASE_OBJECT* psTarget)
{
  STRUCTURE* psStruct;
  DROID* psDroid;
  BASE_OBJECT* psViewer;
  SDWORD sensorRange;
  SDWORD xDiff, yDiff;

  /*if a null target is passed in ignore - this will be the case when a 'miss'
  projectile is sent - we may have to cater for these at some point*/
  // also ignore cases where you attack your own player
  if ((psTarget == nullptr) || ((psAttacker != nullptr) && (psAttacker->player == psTarget->player)))
    return;

  gridStartIterate(psTarget->x, psTarget->y);
  for (psViewer = gridIterate(); psViewer != nullptr; psViewer = gridIterate())
  {
    if (psViewer->player != psTarget->player)
    {
      //ignore non target players' objects
      continue;
    }
    sensorRange = 0;
    if (psViewer->type == OBJ_STRUCTURE)
    {
      psStruct = (STRUCTURE*)psViewer;
      //check if have a sensor of correct type
      if (structCBSensor(psStruct) OR structVTOLCBSensor(psStruct))
        sensorRange = psStruct->pStructureType->pSensor->range;
    }
    else if (psViewer->type == OBJ_DROID)
    {
      psDroid = (DROID*)psViewer;
      //must be a CB sensor
      /*if (asSensorStats[psDroid->asBits[COMP_SENSOR].nStat].type == 
        INDIRECT_CB_SENSOR OR asSensorStats[psDroid->asBits[COMP_SENSOR].
        nStat].type == VTOL_CB_SENSOR)*/
      if (cbSensorDroid(psDroid)) { sensorRange = asSensorStats[psDroid->asBits[COMP_SENSOR].nStat].range; }
    }
    //check sensor distance from target
    if (sensorRange)
    {
      xDiff = static_cast<SDWORD>(psViewer->x) - static_cast<SDWORD>(psTarget->x);
      yDiff = static_cast<SDWORD>(psViewer->y) - static_cast<SDWORD>(psTarget->y);
      if (xDiff * xDiff + yDiff * yDiff < sensorRange * sensorRange)
      {
        //inform viewer of target
        if (psViewer->type == OBJ_DROID)
          orderDroidObj((DROID*)psViewer, DORDER_OBSERVE, psAttacker);
        else if (psViewer->type == OBJ_STRUCTURE)
          ((STRUCTURE*)psViewer)->psTarget = psAttacker;
      }
    }
  }
}
