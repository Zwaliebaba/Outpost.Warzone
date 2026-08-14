#include "pch.h"
/***************************************************************************/
/*
 * Projectile functions
 *
 * Gareth Jones 11/7/97
 *
 */
/***************************************************************************/

#include "Frame.h"
#include "GTime.h"
#include "Objects.h"
#include "Move.h"
#include "Action.h"
#include "Combat.h"
#include "Effects.h"
#include "Map.h"
#include "AudioID.h"
#include "Audio.h"
#include "HashTabl.h"
#include "AnimID.h"
#include "Projectile.h"
#include "Visibility.h"
#include "Script.h"
#include "ScriptTabs.h"
#include "ScriptCB.h"
#include "Group.h"
#include "CmdDroid.h"
#include "Feature.h"
#include "PieState.h"
#include "Loop.h"
#include "PieMatrix.h"

#include "Scores.h"
#include "Display3D.h"
#include "Display.h"
#include "Findpath.h"
#include "MultiPlay.h"
#include "MultiStat.h"

/***************************************************************************/
/* max number of slots in hash table - prime numbers are best because hash
 * function used here is modulous of object pointer with table size -
 * prime number nearest 100 is 97.
 * Table of nearest primes in Binstock+Rex, "Practical Algorithms" p 91.
 */

#define	PROJ_HASH_TABLE_SIZE	97
#define PROJ_INIT				200

#define PROJ_EXT				10

#define	ACC_GRAVITY				1000
#define	DIRECT_PROJ_SPEED		500

#define	PROJ_MAX_PITCH			30

#define	CHECK_PROJ_ABOVE_GROUND	1

#define NOMINAL_DAMAGE	5

/*#define GFX_VISIBLE(psObj)		 ((psObj->psSource != NULL) AND psObj->psSource->visible[selectedPlayer]) OR \
								 ((psObj->psDest != NULL) AND psObj->psDest->visible[selectedPlayer] )*/

/***************************************************************************/

static HASHTABLE* g_pProjObjTable;

// the last unit that did damage - used by script functions
BASE_OBJECT* g_pProjLastAttacker;

extern BOOL godMode;

/***************************************************************************/

static UDWORD establishTargetRadius(BASE_OBJECT* psTarget);
static void proj_InFlightDirectFunc(PROJ_OBJECT* psObj);
static void proj_InFlightIndirectFunc(PROJ_OBJECT* psObj);
static void proj_ImpactFunc(PROJ_OBJECT* psObj);
static void proj_PostImpactFunc(PROJ_OBJECT* psObj);

static void proj_MachineGunInFlightFunc(PROJ_OBJECT* psObj);

static void proj_checkBurnDamage(BASE_OBJECT* apsList, PROJ_OBJECT* psProj, FIRE_BOX* pFireBox);
static BOOL objectDamage(BASE_OBJECT* psObj, UDWORD damage, UDWORD weaponClass, UDWORD weaponSubClass);
/***************************************************************************/
BOOL gfxVisible(PROJ_OBJECT* psObj)
{
  BOOL bVisible;

  bVisible = FALSE;

  // already know it is visible
  if (psObj->bVisible)
    return TRUE;

  // you fired it
  if (psObj->player == selectedPlayer)
    return (TRUE);

  // always see in this mode
  if (godMode)
    return (TRUE);

  // you can see the source
  if ((psObj->psSource != nullptr) AND (psObj->psSource->visible[selectedPlayer]))
    bVisible = TRUE;

  // you can see the destination
  if ((psObj->psDest != nullptr) AND (psObj->psDest->visible[selectedPlayer]))
    bVisible = TRUE;

  // someone elses structure firing at something you can't see
  if ((psObj->psSource != nullptr) AND (psObj->psSource->type == OBJ_STRUCTURE) AND (psObj->psSource->player != selectedPlayer) AND ((psObj
    ->psDest == nullptr) OR (!psObj->psDest->visible[selectedPlayer])))
    bVisible = FALSE;

  // something you cannot see firing at a structure that isn't yours
  if ((psObj->psDest != nullptr) AND (psObj->psDest->type == OBJ_STRUCTURE) AND (psObj->psDest->player != selectedPlayer) AND ((psObj->
    psSource == nullptr) OR (!psObj->psSource->visible[selectedPlayer])))
    bVisible = FALSE;

  return (bVisible);
}

/***************************************************************************/

BOOL proj_InitSystem(void)
{
  /* allocate object hashtable */
  hashTable_Create(&g_pProjObjTable, PROJ_HASH_TABLE_SIZE, PROJ_INIT, PROJ_EXT, sizeof(PROJ_OBJECT));

  return TRUE;
}

/***************************************************************************/

void proj_FreeAllProjectiles(void)
{
  if (g_pProjObjTable)
    hashTable_Clear(g_pProjObjTable);
}

/***************************************************************************/

BOOL proj_Shutdown(void)
{
  /* destroy hash table */
  hashTable_Destroy(g_pProjObjTable);

  g_pProjObjTable = nullptr;

  return TRUE;
}

/***************************************************************************/

PROJ_OBJECT* proj_GetFirst(void) { return static_cast<PROJ_OBJECT*>(hashTable_GetFirst(g_pProjObjTable)); }

/***************************************************************************/

PROJ_OBJECT* proj_GetNext(void) { return static_cast<PROJ_OBJECT*>(hashTable_GetNext(g_pProjObjTable)); }

/***************************************************************************/

// update the kills after a target is destroyed
void proj_UpdateKills(PROJ_OBJECT* psObj)
{
  DROID* psDroid; //, *psSensor;
  BASE_OBJECT* psSensor; //, *psTarget;

  if ((psObj->psSource == nullptr) || ((psObj->psDest != nullptr) && (psObj->psDest->type == OBJ_FEATURE)))
    return;

  if (bMultiPlayer)
  {
    sendDestroyExtra(psObj->psDest, psObj->psSource);
    updateMultiStatsKills(psObj->psDest, psObj->psSource->player);
  }

  if (psObj->psSource->type == OBJ_DROID) /* update droid kills */
  {
    psDroid = (DROID*)psObj->psSource;
    psDroid->numKills++;
    cmdDroidUpdateKills(psDroid);
    //can't assume the sensor object is a droid - it might be a structure
    /*if (orderStateObj(psDroid, DORDER_FIRESUPPORT, (BASE_OBJECT **)&psSensor))
    {
      psSensor->numKills ++;
    }*/
    if (orderStateObj(psDroid, DORDER_FIRESUPPORT, &psSensor))
    {
      if (psSensor->type == OBJ_DROID)
        ((DROID*)psSensor)->numKills++;
    }
  }
  else if (psObj->psSource->type == OBJ_STRUCTURE)
  {
    // see if there was a command droid designating this target
    psDroid = cmdDroidGetDesignator(psObj->psSource->player);
    if (psDroid != nullptr)
    {
      if ((psDroid->action == DACTION_ATTACK) && (psDroid->psActionTarget == psObj->psDest))
        psDroid->numKills++;
    }
  }
}

/***************************************************************************/

BOOL proj_SendProjectile(WEAPON* psWeap, BASE_OBJECT* psAttacker, SDWORD player, UDWORD tarX, UDWORD tarY, UDWORD tarZ,
                         BASE_OBJECT* psTarget, BOOL bVisible)
{
  PROJ_OBJECT* psObj;
  SDWORD tarHeight, srcHeight, iMinSq;
  SDWORD altChange, dx, dy, dz, iVelSq, iVel;
  FRACT_D fR, fA, fS, fT, fC; // 52.12 fixed point on PSX, float on PC.
  iVector muzzle;
  SDWORD iRadSq, iPitchLow, iPitchHigh, iTemp;
  UDWORD heightVariance;
  WEAPON_STATS* psWeapStats = &asWeaponStats[psWeap->nStat];

  DEBUG_ASSERT_TEXT(PTRVALID(psWeapStats,sizeof(WEAPON_STATS)), "proj_SendProjectile: invalid weapon stats");

  /* get unused projectile object from hashtable*/
  psObj = static_cast<PROJ_OBJECT*>(hashTable_GetElement(g_pProjObjTable));

  /* get muzzle offset */
  if (psAttacker == nullptr)
  {
    // if there isn't an attacker just start at the target position
    // NB this is for the script function to fire the las sats
    muzzle.x = static_cast<SDWORD>(tarX);
    muzzle.y = static_cast<SDWORD>(tarY);
    muzzle.z = static_cast<SDWORD>(tarZ);
  }
  else if (psAttacker->type == OBJ_DROID)
  {
    calcDroidMuzzleLocation((DROID*)psAttacker, &muzzle);
    /*update attack runs for VTOL droid's each time a shot is fired*/
    updateVtolAttackRun((DROID*)psAttacker);
  }
  else if (psAttacker->type == OBJ_STRUCTURE)
    calcStructureMuzzleLocation((STRUCTURE*)psAttacker, &muzzle);
  else // incase anything wants a projectile
  {
    muzzle.x = psAttacker->x;
    muzzle.y = psAttacker->y;
    muzzle.z = psAttacker->z;
    /* GJ - horrible hack to get droid tower projectiles at correct height */
  }

  /* Initialise the structure */
  psObj->type = OBJ_BULLET;
  psObj->state = PROJ_INFLIGHT;
  psObj->psWStats = asWeaponStats + psWeap->nStat;
  psObj->x = static_cast<UWORD>(muzzle.x);
  psObj->y = static_cast<UWORD>(muzzle.y);
  psObj->z = static_cast<UWORD>(muzzle.z);
  psObj->startX = muzzle.x;
  psObj->startY = muzzle.y;
  psObj->tarX = tarX;
  psObj->tarY = tarY;
  psObj->targetRadius = establishTargetRadius(psTarget); // New - needed to backtrack FX
  psObj->psSource = psAttacker;
  psObj->psDest = psTarget;
  psObj->born = gameTime;
  psObj->player = static_cast<UBYTE>(player);
  psObj->bVisible = FALSE;
  psObj->airTarget = static_cast<UBYTE>(((psTarget != NULL) && (psTarget->type == OBJ_DROID) && (vtolDroid((DROID*)psTarget))) || ((psTarget
    == NULL) && ((SDWORD)tarZ > map_Height(tarX, tarY))));

  if (psTarget)
  {
    scoreUpdateVar(WD_SHOTS_ON_TARGET);
    heightVariance = 0;
    switch (psTarget->type)
    {
    case OBJ_DROID:
    case OBJ_FEATURE:
      if (((DROID*)psTarget)->droidType == DROID_PERSON)
        heightVariance = rand() % 8;
      else
        heightVariance = rand() % 24;
      break;
    case OBJ_STRUCTURE:
      heightVariance = (rand() % psTarget->sDisplay.imd->ymax);
      break;
    }
    tarHeight = psTarget->z + heightVariance;
  }
  else
  {
    tarHeight = static_cast<SDWORD>(tarZ);
    scoreUpdateVar(WD_SHOTS_OFF_TARGET);
  }

  srcHeight = muzzle.z;
  altChange = tarHeight - srcHeight;

  psObj->srcHeight = srcHeight;
  psObj->altChange = altChange;

  dx = static_cast<SDWORD>(psObj->tarX) - muzzle.x;
  dy = static_cast<SDWORD>(psObj->tarY) - muzzle.y;
  dz = tarHeight - muzzle.z;

  /* roll never set */
  psObj->roll = 0;

  fR = static_cast<FRACT_D>(atan2(dx, dy));
  if (fR < 0.0)
    fR += static_cast<FRACT_D>(2 * PI);
  psObj->direction = static_cast<UWORD>((RAD_TO_DEG(fR)));

  /* get target distance */
  iRadSq = dx * dx + dy * dy + dz * dz;
  fR = trigIntSqrt(iRadSq);
  iMinSq = static_cast<SDWORD>(psWeapStats->minRange * psWeapStats->minRange);

  if (proj_Direct(psObj->psWStats) || (!proj_Direct(psWeapStats) && (iRadSq <= iMinSq)))
  {
    fR = static_cast<FRACT_D>(atan2(dz, fR));
    if (fR < 0.0)
      fR += static_cast<FRACT_D>(2 * PI);
    psObj->pitch = static_cast<SWORD>((RAD_TO_DEG(fR)));

    /* set function pointer */
    psObj->pInFlightFunc = proj_InFlightDirectFunc;
  }
  else
  {
    /* indirect */
    iVelSq = psObj->psWStats->flightSpeed * psObj->psWStats->flightSpeed;

    fA = ACC_GRAVITY * MAKEFRACT_D(iRadSq) / (2 * iVelSq);
    fC = 4 * fA * (MAKEFRACT_D(dz) + fA);
    fS = MAKEFRACT_D(iRadSq) - fC;

    /* target out of range - increase velocity to hit target */
    if (fS < MAKEFRACT_D(0))
    {
      /* set optimal pitch */
      psObj->pitch = PROJ_MAX_PITCH;

      /* increase velocity: tan angle could be hard-coded but is variable here */

      fS = trigSin(PROJ_MAX_PITCH);
      fC = trigCos(PROJ_MAX_PITCH);
      fT = FRACTdiv_D(fS, fC);
      fS = ACC_GRAVITY * (MAKEFRACT_D(1) + FRACTmul_D(fT, fT));
      fS = FRACTdiv_D(fS, (2 * (FRACTmul_D(fR,fT) - MAKEFRACT_D(dz))));
      {
        FRACT_D Tmp;
        Tmp = FRACTmul_D(fR, fR);
        iVel = MAKEINT_D(trigIntSqrt(MAKEINT_D(FRACTmul_D(fS, Tmp))));
      }
    }
    else
    {
      /* set velocity to stats value */
      iVel = psObj->psWStats->flightSpeed;

      /* get floating point square root */
      fS = trigIntSqrt(MAKEINT_D(fS));

      fT = static_cast<FRACT_D>(atan2(fR + fS, 2 * fA));
      /* make sure angle positive */
      if (fT < 0)
        fT += static_cast<FRACT_D>(2 * PI);
      iPitchLow = MAKEINT_D(RAD_TO_DEG(fT));

      fT = static_cast<FRACT_D>(atan2(fR - fS, 2 * fA));
      /* make sure angle positive */
      if (fT < 0)
        fT += static_cast<FRACT_D>(2 * PI);
      iPitchHigh = MAKEINT_D(RAD_TO_DEG(fT));
      /* swap pitches if wrong way round */
      if (iPitchLow > iPitchHigh)
      {
        iTemp = iPitchHigh;
        iPitchLow = iPitchHigh;
        iPitchHigh = iTemp;
      }

      /* chooselow pitch unless -ve */
      if (iPitchLow > 0)
        psObj->pitch = static_cast<SWORD>(iPitchLow);
      else
        psObj->pitch = static_cast<SWORD>(iPitchHigh);
    }

    /* if droid set muzzle pitch */
    if (psAttacker != nullptr)
    {
      if (psAttacker->type == OBJ_DROID)
        ((DROID*)psAttacker)->turretPitch = psObj->pitch;
      else if (psAttacker->type == OBJ_STRUCTURE)
        ((STRUCTURE*)psAttacker)->turretPitch = psObj->pitch;
    }

    psObj->vXY = MAKEINT_D(iVel * trigCos(psObj->pitch));
    psObj->vZ = MAKEINT_D(iVel * trigSin(psObj->pitch));

    /* set function pointer */
    psObj->pInFlightFunc = proj_InFlightIndirectFunc;
  }

  /* put object in hashtable */
  hashTable_InsertElement(g_pProjObjTable, psObj, (int)psObj, UNUSED_KEY);

  /* play firing audio */
  // only play if either object is visible, i know it's a bit of a hack, but it avoids the problem
  // of having to calculate real visibility values for each projectile.
  //	if(  ((psObj->psSource != NULL) && psObj->psSource->visible[selectedPlayer] ) ||
  //		 ((psObj->psDest != NULL) && psObj->psDest->visible[selectedPlayer]     )    )
  //	if(GFX_VISIBLE(psObj))
  if (bVisible || gfxVisible(psObj))
  {
    // note that the projectile is visible
    psObj->bVisible = TRUE;

    if (psObj->psWStats->iAudioFireID != NO_SOUND)
    {
      if (psObj->psSource)
      {
        /* firing sound emitted from source */
        audio_PlayObjDynamicTrack(psObj->psSource, psObj->psWStats->iAudioFireID, nullptr);
        /* GJ HACK: move howitzer sound with shell */
        if (psObj->psWStats->weaponSubClass == WSC_HOWITZERS) { audio_PlayObjDynamicTrack(psObj, ID_SOUND_HOWITZ_FLIGHT, nullptr); }
      }
      else
      {
        //don't play the sound for a LasSat in multiPlayer
        if (!(bMultiPlayer AND psWeapStats->weaponSubClass == WSC_LAS_SAT))
          audio_PlayObjStaticTrack(psObj, psObj->psWStats->iAudioFireID);
      }
    }
  }

  if ((psAttacker != nullptr) && !proj_Direct(psWeapStats))
  {
    //check for Counter Battery Sensor in range of target
    counterBatteryFire(psAttacker, psTarget);
  }

  return TRUE;
}

/***************************************************************************/

void proj_InFlightDirectFunc(PROJ_OBJECT* psObj)
{
  WEAPON_STATS* psStats;
  SDWORD timeSoFar;
  SDWORD dx, dy, dz, iX, iY, dist, xdiff, ydiff;
  SDWORD rad;
  iVector pos;

  DEBUG_ASSERT_TEXT(PTRVALID(psObj, sizeof(PROJ_OBJECT)), "proj_InFlightDirectFunc: invalid projectile pointer");

  psStats = psObj->psWStats;
  DEBUG_ASSERT_TEXT(PTRVALID(psStats, sizeof(WEAPON_STATS)), "proj_InFlightDirectFunc: Invalid weapon stats pointer");

  timeSoFar = gameTime - psObj->born;

  //we want a delay between Las-Sats firing and actually hitting in multiPlayer
  if (bMultiPlayer)
  {
    if (psStats->weaponSubClass == WSC_LAS_SAT)
    {
      //magic number but that's how long the audio countdown message lasts!
      if (timeSoFar < 8 * GAME_TICKS_PER_SEC)
        return;
    }
  }

  /*
    if(psObj->psDest)
    {
      erraticX = 128-rand()%256;
      erraticY = 128-rand()%256;
  
      dx = ( (SDWORD)psObj->psDest->x+erraticX) -(SDWORD)psObj->x;
      dy = ( (SDWORD)psObj->psDest->y+erraticY) -(SDWORD)psObj->y;
  
    }
    else
    {
  */

  /* If it's homing and it has a target (not a miss)... */
  if (psStats->movementModel == MM_HOMINGDIRECT AND psObj->psDest)
  {
    dx = static_cast<SDWORD>(psObj->psDest->x) - static_cast<SDWORD>(psObj->startX);
    dy = static_cast<SDWORD>(psObj->psDest->y) - static_cast<SDWORD>(psObj->startY);
    dz = static_cast<SDWORD>(psObj->psDest->z) - static_cast<SDWORD>(psObj->srcHeight);
  }
  else
  {
    dx = static_cast<SDWORD>(psObj->tarX) - static_cast<SDWORD>(psObj->startX);
    dy = static_cast<SDWORD>(psObj->tarY) - static_cast<SDWORD>(psObj->startY);
    dz = static_cast<SDWORD>(psObj->srcHeight + psObj->altChange) - static_cast<SDWORD>(psObj->srcHeight);
  }

  rad = static_cast<SDWORD>(iSQRT(dx*dx + dy*dy));

  if (rad == 0)
    rad = 1;

  dist = timeSoFar * psStats->flightSpeed / GAME_TICKS_PER_SEC;

  iX = psObj->startX + (dist * dx / rad);
  iY = psObj->startY + (dist * dy / rad);

  /* impact if about to go off map else update coordinates */
  if (worldOnMap(iX, iY) == FALSE)
  {
    psObj->state = PROJ_IMPACT;
    DBPRINTF(("**** projectile off map - removed ****\n"));
    return;
  }
  psObj->x = static_cast<UWORD>(iX);
  psObj->y = static_cast<UWORD>(iY);

  psObj->z = static_cast<UWORD>(psObj->srcHeight + (dist * dz / rad));

  if (psStats->weaponSubClass == WSC_FLAME)
  {
    //		if(GFX_VISIBLE(psObj))
    if (gfxVisible(psObj))
    {
      effectGiveAuxVar(PERCENT(dist, rad));
      pos.x = psObj->x;
      pos.y = psObj->z - 8;
      pos.z = psObj->y;
      addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_FLAMETHROWER,FALSE, nullptr, 0);
    }
  }

  else if (psStats->weaponSubClass == WSC_COMMAND OR psStats->weaponSubClass == WSC_ELECTRONIC OR psStats->weaponSubClass == WSC_EMP)
  {
    if (gfxVisible(psObj))
    //		if(GFX_VISIBLE(psObj))
    {
      effectGiveAuxVar((PERCENT(dist, rad) / 2));
      pos.x = psObj->x;
      pos.y = psObj->z - 8;
      pos.z = psObj->y;
      addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_LASER,FALSE, nullptr, 0);
    }
  }
  /*
  else
  if(psStats->weaponSubClass == WSC_ROCKET OR psStats->weaponSubClass == WSC_MISSILE OR
        psStats->weaponSubClass == WSC_SLOWROCKET OR psStats->weaponSubClass == WSC_SLOWMISSILE)
  {
    if(GFX_VISIBLE(psObj))
    {
      pos.x = psObj->x;
      pos.y = psObj->z-8;
      pos.z = psObj->y;
      addEffect(&pos,EFFECT_EXPLOSION,EXPLOSION_TYPE_FLARE,FALSE,NULL,0);
    }
  }
  */
  if (psStats->weaponSubClass == WSC_ROCKET OR psStats->weaponSubClass == WSC_MISSILE OR psStats->weaponSubClass == WSC_SLOWROCKET OR
    psStats->weaponSubClass == WSC_SLOWMISSILE)
  {
    if (gfxVisible(psObj))
    //		if(GFX_VISIBLE(psObj))
    {
      pos.x = psObj->x;
      pos.y = psObj->z + 8;
      pos.z = psObj->y;
      addEffect(&pos, EFFECT_SMOKE, SMOKE_TYPE_TRAIL,FALSE, nullptr, 0);
    }
  }

  /* See if effect has finished */
  if ((psStats->movementModel == MM_HOMINGDIRECT) AND psObj->psDest)
  {
    xdiff = static_cast<SDWORD>(psObj->x) - static_cast<SDWORD>(psObj->psDest->x);
    ydiff = static_cast<SDWORD>(psObj->y) - static_cast<SDWORD>(psObj->psDest->y);
    if ((xdiff * xdiff + ydiff * ydiff) < (static_cast<SDWORD>(psObj->targetRadius) * static_cast<SDWORD>(psObj->targetRadius)))
      psObj->state = PROJ_IMPACT;
  }
  else if (dist > (rad - static_cast<SDWORD>(psObj->targetRadius)))
  {
    /* It's damage time */
    //		psObj->x = psObj->tarX;		// leave it there, but use tarX and tarY for damage
    psObj->state = PROJ_IMPACT;
  }

#if CHECK_PROJ_ABOVE_GROUND
  /* check not trying to travel through terrain - if so count as a miss */
  if (mapObjIsAboveGround((BASE_OBJECT*)psObj) == FALSE)
  {
    psObj->state = PROJ_IMPACT;
    /* miss registered if NULL target */
    psObj->psDest = nullptr;
    return;
  }
#endif

  /* add smoke trail to indirect weapons firing directly */
  if (!proj_Direct(psStats) AND gfxVisible(psObj)) //GFX_VISIBLE(psObj) )
  {
    pos.x = psObj->x;
    pos.y = psObj->z + 8;
    pos.z = psObj->y;
    addEffect(&pos, EFFECT_SMOKE, SMOKE_TYPE_TRAIL,FALSE, nullptr, 0);
  }
}

/***************************************************************************/

void proj_InFlightIndirectFunc(PROJ_OBJECT* psObj)
{
  WEAPON_STATS* psStats;
  SDWORD iTime, iRad, iDist, dx, dy, dz, iX, iY;
  iVector pos;
  FRACT fVVert;
  BOOL bOver = FALSE;

  DEBUG_ASSERT_TEXT(PTRVALID(psObj, sizeof(PROJ_OBJECT)), "proj_InFlightIndirectFunc: invalid projectile pointer");

  psStats = psObj->psWStats;
  DEBUG_ASSERT_TEXT(PTRVALID(psStats, sizeof(WEAPON_STATS)), "proj_InFlightIndirectFunc: Invalid weapon stats pointer");

  iTime = gameTime - psObj->born;

  dx = static_cast<SDWORD>(psObj->tarX) - static_cast<SDWORD>(psObj->startX);
  dy = static_cast<SDWORD>(psObj->tarY) - static_cast<SDWORD>(psObj->startY);

  iRad = fastRoot(dx, dy);

  iDist = iTime * psObj->vXY / GAME_TICKS_PER_SEC;

  iX = psObj->startX + (iDist * dx / iRad);
  iY = psObj->startY + (iDist * dy / iRad);

  /* impact if about to go off map else update coordinates */
  if (worldOnMap(iX, iY) == FALSE)
  {
    psObj->state = PROJ_IMPACT;
    DBPRINTF(("**** projectile off map - removed ****\n"));
    return;
  }
  psObj->x = static_cast<UWORD>(iX);
  psObj->y = static_cast<UWORD>(iY);

  dz = (psObj->vZ - (iTime * ACC_GRAVITY / GAME_TICKS_PER_SEC / 2)) * iTime / GAME_TICKS_PER_SEC;
  psObj->z = static_cast<UWORD>(psObj->srcHeight + dz);

  fVVert = MAKEFRACT(psObj->vZ - (iTime*ACC_GRAVITY/GAME_TICKS_PER_SEC));
  psObj->pitch = static_cast<SWORD>((RAD_TO_DEG(atan2(fVVert, psObj->vXY))));

  if (psStats->weaponSubClass == WSC_FLAME)
  {
    if (gfxVisible(psObj))
    //		if(GFX_VISIBLE(psObj))
    {
      effectGiveAuxVar(PERCENT(iDist, iRad));
      pos.x = psObj->x;
      pos.y = psObj->z - 8;
      pos.z = psObj->y;
      addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_FLAMETHROWER,FALSE, nullptr, 0);
    }
  }
  else if (psStats->weaponSubClass == WSC_COMMAND OR psStats->weaponSubClass == WSC_ELECTRONIC OR psStats->weaponSubClass == WSC_EMP)
  {
    if (gfxVisible(psObj))
    //		if(GFX_VISIBLE(psObj))
    {
      effectGiveAuxVar((PERCENT(iDist, iRad) / 2));
      pos.x = psObj->x;
      pos.y = psObj->z - 8;
      pos.z = psObj->y;
      addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_LASER,FALSE, nullptr, 0);
    }
  }
  /*
  else
  if(psStats->weaponSubClass == WSC_ROCKET)
  {
    if(GFX_VISIBLE(psObj))
    {
      pos.x = psObj->x;
      pos.y = psObj->z-8;
      pos.z = psObj->y;
      addEffect(&pos,EFFECT_EXPLOSION,EXPLOSION_TYPE_FLARE,FALSE,NULL,0);
    }
  }
  else
  {
    if(GFX_VISIBLE(psObj))
    {
      pos.x = psObj->x; 
      pos.y = psObj->z; 
      pos.z = psObj->y;
      addEffect(&pos,EFFECT_SMOKE,SMOKE_TYPE_TRAIL,FALSE,NULL,0);
    }
  }
  */

  /* See if effect has finished */
  if (iDist > (iRad - static_cast<SDWORD>(psObj->targetRadius)))
  {
    pos.x = psObj->x;
    pos.z = psObj->y;
    pos.y = map_Height(pos.x, pos.z) + 8;

    /* It's damage time */
    //		psObj->x = psObj->tarX;		 // leave it where it is, but use tarX, tarY for damage
    psObj->z = static_cast<UWORD>(pos.y + 8); //map_Height(psObj->x,psObj->y) + 8;	// bring up the impact explosion
    psObj->state = PROJ_IMPACT;
    bOver = TRUE;
  }

#if CHECK_PROJ_ABOVE_GROUND
  /* check not trying to travel through terrain - if so count as a miss */
  if (mapObjIsAboveGround((BASE_OBJECT*)psObj) == FALSE)
  {
    psObj->state = PROJ_IMPACT;
    /* miss registered if NULL target */
    psObj->psDest = nullptr;
    bOver = TRUE;
  }
#endif

  /* Add smoke particle at projectile location (in world coords) */
  /* Add a trail graphic */
  /* If it's indirect and not a flamethrower - add a smoke trail! */
  /* MAKE IT ADD A 'TRAIL GRAPHIC'? */
  if (psStats->weaponSubClass != WSC_FLAME AND psStats->weaponSubClass != WSC_ENERGY AND psStats->weaponSubClass != WSC_COMMAND AND psStats
    ->weaponSubClass != WSC_ELECTRONIC AND psStats->weaponSubClass != WSC_EMP AND !bOver)
  {
    if (gfxVisible(psObj))
    //		if(GFX_VISIBLE(psObj))// AND psStats->pTrailGraphic )
    {
      pos.x = psObj->x;
      pos.y = psObj->z + 4;
      pos.z = psObj->y;
      addEffect(&pos, EFFECT_SMOKE, SMOKE_TYPE_TRAIL,FALSE, nullptr, 0);
    }
  }
}

/***************************************************************************/

void proj_ImpactFunc(PROJ_OBJECT* psObj)
{
  WEAPON_STATS* psStats;
  SDWORD i, iAudioImpactID;
  DROID *psCurrD, *psNextD;
  STRUCTURE *psCurrS, *psNextS;
  FEATURE *psCurrF, *psNextF;
  UDWORD dice;
  SDWORD tarX0, tarY0, tarX1, tarY1;
  SDWORD radSquared, xDiff, yDiff;
  BOOL bKilled; //,bMultiTemp;
  iVector position, scatter;
  UDWORD damage; //optimisation - were all being calculated twice on PC

  DEBUG_ASSERT_TEXT(PTRVALID(psObj, sizeof(PROJ_OBJECT)), "proj_ImpactFunc: invalid projectile pointer");

  psStats = psObj->psWStats;
  DEBUG_ASSERT_TEXT(PTRVALID(psStats, sizeof(WEAPON_STATS)), "proj_ImpactFunc: Invalid weapon stats pointer");

  /* play impact audio */
  if (gfxVisible(psObj))
  {
    if (psStats->iAudioImpactID == NO_SOUND)
    {
      // ID_SOUND_RICOCHET_1 is -1 on PSX so code below must be PC only.
      /* play richochet if MG */
      if (psObj->psDest != nullptr && psObj->psWStats->weaponSubClass == WSC_MGUN && ONEINTHREE)
      {
        iAudioImpactID = ID_SOUND_RICOCHET_1 + (rand() % 3);
        audio_PlayStaticTrack(psObj->psDest->x, psObj->psDest->y, iAudioImpactID);
      }
    }
    else
    {
      if (psObj->psDest == nullptr) { audio_PlayStaticTrack(psObj->tarX, psObj->tarY, psStats->iAudioImpactID); }
      else { audio_PlayStaticTrack(psObj->psDest->x, psObj->psDest->y, psStats->iAudioImpactID); }
    }
  }
  // note the attacker if any
  g_pProjLastAttacker = psObj->psSource;

  if (gfxVisible(psObj))
  //	if(GFX_VISIBLE(psObj))
  {
    /* Set stuff burning if it's a flame droid... */
    /* Shouldn't need to do this check but the stats aren't all at a value yet... */ // FIXME
    if (psStats->incenRadius AND psStats->incenTime)
    {
      position.x = psObj->tarX;
      position.z = psObj->tarY;
      position.y = map_Height(position.x, position.z);
      effectGiveAuxVar(psStats->incenRadius);
      effectGiveAuxVarSec(psStats->incenTime);
      addEffect(&position, EFFECT_FIRE, FIRE_TYPE_LOCALISED,FALSE, nullptr, 0);
    }
    // may want to add both a fire effect and the las sat effect
    if (psStats->weaponSubClass == WSC_LAS_SAT)
    {
      position.x = psObj->tarX;
      position.z = psObj->tarY;
      position.y = map_Height(position.x, position.z);
      addEffect(&position, EFFECT_SAT_LASER, SAT_LASER_STANDARD,FALSE, nullptr, 0);
      if (clipXY(psObj->tarX, psObj->tarY))
        shakeStart();
    }
  }
  /* Nothings been killed */
  bKilled = FALSE;
  if (psObj->psDest != nullptr)
  {
    DEBUG_ASSERT_TEXT(PTRVALID(psObj->psDest, sizeof(BASE_OBJECT)), "proj_ImpactFunc: Invalid destination object pointer");
  }

  if (psObj->psDest == nullptr)
  //		 (psObj->x != psObj->psDest->x) || (psObj->y != psObj->psDest->y) )
  {
    /* The bullet missed or the target was destroyed in flight */
    /* So show the MISS effect */
    position.x = psObj->x;
    position.z = psObj->y;
    position.y = psObj->z; //map_Height(psObj->x, psObj->y) + 24;//jps
    scatter.x = psStats->radius;
    scatter.y = 0;
    scatter.z = psStats->radius;

    // you got to have at least 1 in the numExplosions field or you'll see nowt..:-)
    if (psObj->airTarget)
    {
      if ((psStats->surfaceToAir & SHOOT_IN_AIR) && !(psStats->surfaceToAir & SHOOT_ON_GROUND))
      {
        if (psStats->facePlayer)
        {
          if (gfxVisible(psObj))
            //					if(GFX_VISIBLE(psObj))
            addMultiEffect(&position, &scatter, EFFECT_EXPLOSION, EXPLOSION_TYPE_SPECIFIED,TRUE, psStats->pTargetMissGraphic,
                           psStats->numExplosions, psStats->lightWorld, psStats->effectSize);
        }
        else
        {
          if (gfxVisible(psObj))
            //					if(GFX_VISIBLE(psObj))
            addMultiEffect(&position, &scatter, EFFECT_EXPLOSION, EXPLOSION_TYPE_NOT_FACING,TRUE, psStats->pTargetMissGraphic,
                           psStats->numExplosions, psStats->lightWorld, psStats->effectSize);
        }
        /* Add some smoke to flak */
        addMultiEffect(&position, &scatter, EFFECT_SMOKE, SMOKE_TYPE_DRIFTING,FALSE, nullptr, 3, 0, 0);
      }
    }
    else if (TERRAIN_TYPE(mapTile(psObj->x/TILE_UNITS,psObj->y/TILE_UNITS)) == TER_WATER)
    {
      if (psStats->facePlayer)
      {
        if (gfxVisible(psObj))
          //				if(GFX_VISIBLE(psObj))
          addMultiEffect(&position, &scatter, EFFECT_EXPLOSION, EXPLOSION_TYPE_SPECIFIED,TRUE, psStats->pWaterHitGraphic,
                         psStats->numExplosions, psStats->lightWorld, psStats->effectSize);
      }
      else
      {
        if (gfxVisible(psObj))
          //				if(GFX_VISIBLE(psObj))
          addMultiEffect(&position, &scatter, EFFECT_EXPLOSION, EXPLOSION_TYPE_NOT_FACING,TRUE, psStats->pWaterHitGraphic,
                         psStats->numExplosions, psStats->lightWorld, psStats->effectSize);
      }
    }
    else
    {
      if (psStats->facePlayer)
      {
        if (gfxVisible(psObj))
          //				if(GFX_VISIBLE(psObj))
          addMultiEffect(&position, &scatter, EFFECT_EXPLOSION, EXPLOSION_TYPE_SPECIFIED,TRUE, psStats->pTargetMissGraphic,
                         psStats->numExplosions, psStats->lightWorld, psStats->effectSize);
      }
      else
      {
        if (gfxVisible(psObj))
          //				if(GFX_VISIBLE(psObj))
          addMultiEffect(&position, &scatter, EFFECT_EXPLOSION, EXPLOSION_TYPE_NOT_FACING,TRUE, psStats->pTargetMissGraphic,
                         psStats->numExplosions, psStats->lightWorld, psStats->effectSize);
      }
    }
  }
  else
  {
    position.x = psObj->x;
    position.z = psObj->y;
    position.y = psObj->z; //map_Height(psObj->x, psObj->y) + 24;
    scatter.x = psStats->radius;
    scatter.y = 0;
    scatter.z = psStats->radius;
    if (psStats->facePlayer)
    {
      if (gfxVisible(psObj))
        //			if(GFX_VISIBLE(psObj))
        addMultiEffect(&position, &scatter, EFFECT_EXPLOSION, EXPLOSION_TYPE_SPECIFIED,TRUE, psStats->pTargetHitGraphic,
                       psStats->numExplosions, psStats->lightWorld, psStats->effectSize);
    }
    else
    {
      if (gfxVisible(psObj))
        //			if(GFX_VISIBLE(psObj))
        addMultiEffect(&position, &scatter, EFFECT_EXPLOSION, EXPLOSION_TYPE_NOT_FACING,TRUE, psStats->pTargetHitGraphic,
                       psStats->numExplosions, psStats->lightWorld, psStats->effectSize);
    }
    /* Do damage to the target */
    if (proj_Direct(psStats))
    {
      /*Check for Electronic Warfare damage where we know the subclass 
            of the weapon*/
      if (psStats->weaponSubClass == WSC_ELECTRONIC) // AND psObj->psDest->
      //type == OBJ_STRUCTURE)
      {
        if (psObj->psSource)
        {
          //if (electronicDamage((STRUCTURE *)psObj->psDest, calcDamage(
          if (electronicDamage(psObj->psDest, calcDamage(weaponDamage(psStats, psObj->player), psStats->weaponEffect, psObj->psDest),
                               psObj->player))
          {
            if (psObj->psSource->type == OBJ_DROID)
            {
              //the structure has lost all resistance so quit
              ((DROID*)psObj->psSource)->order = DORDER_NONE;
              actionDroid((DROID*)(psObj->psSource), DACTION_NONE);
            }
            /*else
            {
              ASSERT((FALSE, "proj_ImpactFunc: EW Weapon not attached to a droid"));
            }*/
            else if (psObj->psSource->type == OBJ_STRUCTURE)
            {
              //the droid has lost all resistance - init the structures target
              ((STRUCTURE*)psObj->psSource)->psTarget = nullptr;
            }
          }
        }
      }
      else
      {
        /* Just assume a direct fire weapon hits the target */

        damage = calcDamage(weaponDamage(psStats, psObj->player), psStats->weaponEffect, psObj->psDest);
        if (bMultiPlayer)
        {
          if (psObj->psSource && myResponsibility(psObj->psSource->player))
            updateMultiStatsDamage(psObj->psSource->player, psObj->psDest->player, damage);
        }
        //			else
        DBP1(("Damage to object %d, player %d\n", psObj->psDest->id, psObj->psDest->player));
        /*the damage depends on the weapon effect and the target propulsion type or structure strength*/
        bKilled = objectDamage(psObj->psDest, damage, psStats->weaponClass, psStats->weaponSubClass);

        if (bKilled)
          proj_UpdateKills(psObj);

        // do the attacked callback
      }
    }
    else
    {
      /* See if the target is still close to the impact point */
      /* Should do a better test than this but as we don't have */
      /* a world size for objects we'll just see if it is within */
      /* a tile of the bullet */
      /***********************************************************/
      /*  MIGHT WANT TO CHANGE THIS                              */
      /***********************************************************/
      if ((psObj->psDest->type == OBJ_STRUCTURE) || (psObj->psDest->type == OBJ_FEATURE) || ((static_cast<SDWORD>(psObj->psDest->x) >=
          static_cast<SDWORD>(psObj->x) - TILE_UNITS) && (static_cast<SDWORD>(psObj->psDest->x) <= static_cast<SDWORD>(psObj->x) +
          TILE_UNITS)
        && (static_cast<SDWORD>(psObj->psDest->y) >= static_cast<SDWORD>(psObj->y) - TILE_UNITS) && (static_cast<SDWORD>(psObj->psDest->y)
          <=
          static_cast<SDWORD>(psObj->y) + TILE_UNITS)))
      {
        damage = calcDamage(weaponDamage(psStats, psObj->player), psStats->weaponEffect, psObj->psDest);
        if (bMultiPlayer)
        {
          if (psObj->psSource && myResponsibility(psObj->psSource->player))
          {
            updateMultiStatsDamage(psObj->psSource->player,
                                   //psObj->psDest->player,	calcDamage(psStats->damage, 
                                   psObj->psDest->player, damage);
          }
        }
        //				else

        DBP1(("Damage to object %d, player %d\n", psObj->psDest->id, psObj->psDest->player));
        /*bKilled = psObj->psDest->damage(psObj->psDest, calcDamage(
          //psStats->damage, psStats->weaponEffect, psObj->psDest),
          weaponDamage(psStats,psObj->player), psStats->
          weaponEffect, psObj->psDest), psStats->weaponClass);*/
        bKilled = objectDamage(psObj->psDest, damage, psStats->weaponClass, psStats->weaponSubClass);

        if (bKilled)
          proj_UpdateKills(psObj);
        // do the attacked callback
      }
    }
  }

  if ((psStats->radius == 0) && (psStats->incenTime == 0))
  {
    position.x = psObj->x;
    position.z = psObj->y;
    position.y = psObj->z; //map_Height(psObj->x, psObj->y) + 24;//jps
    scatter.x = psStats->radius;
    scatter.y = 0;
    scatter.z = psStats->radius;

    if (TERRAIN_TYPE(mapTile(psObj->x/TILE_UNITS,psObj->y/TILE_UNITS)) == TER_WATER)
    {
      if (psStats->facePlayer)
      {
        if (gfxVisible(psObj))
          //				if(GFX_VISIBLE(psObj))
          addMultiEffect(&position, &scatter, EFFECT_EXPLOSION, EXPLOSION_TYPE_SPECIFIED,TRUE, psStats->pWaterHitGraphic,
                         psStats->numExplosions, psStats->lightWorld, psStats->effectSize);
      }
      else
      {
        if (gfxVisible(psObj))
          //				if(GFX_VISIBLE(psObj))
          addMultiEffect(&position, &scatter, EFFECT_EXPLOSION, EXPLOSION_TYPE_NOT_FACING,TRUE, psStats->pWaterHitGraphic,
                         psStats->numExplosions, psStats->lightWorld, psStats->effectSize);
      }
    }
    else
    {
      if (psStats->facePlayer)
      {
        if (gfxVisible(psObj))
          //				if(GFX_VISIBLE(psObj))
          addMultiEffect(&position, &scatter, EFFECT_EXPLOSION, EXPLOSION_TYPE_SPECIFIED,TRUE, psStats->pTargetHitGraphic,
                         psStats->numExplosions, psStats->lightWorld, psStats->effectSize);
      }
      else
      {
        if (gfxVisible(psObj))
          //				if(GFX_VISIBLE(psObj))
          addMultiEffect(&position, &scatter, EFFECT_EXPLOSION, EXPLOSION_TYPE_NOT_FACING,TRUE, psStats->pTargetHitGraphic,
                         psStats->numExplosions, psStats->lightWorld, psStats->effectSize);
      }
    }

    /* This was just a simple bullet - release it and return */
    if (hashTable_RemoveElement(g_pProjObjTable, psObj, (int)psObj, UNUSED_KEY) == FALSE)
      DBPRINTF(("proj_ImpactFunc: couldn't remove projectile from table\n"));
    return;
  }

  if (psStats->radius != 0)
  {
    /* An area effect bullet */
    psObj->state = PROJ_POSTIMPACT;

    /* Note when it exploded for the explosion effect */
    psObj->born = gameTime;

    /* Work out the bounding box for the blast radius */
    tarX0 = static_cast<SDWORD>(psObj->x) - static_cast<SDWORD>(psStats->radius);
    tarY0 = static_cast<SDWORD>(psObj->y) - static_cast<SDWORD>(psStats->radius);
    tarX1 = static_cast<SDWORD>(psObj->x) + static_cast<SDWORD>(psStats->radius);
    tarY1 = static_cast<SDWORD>(psObj->y) + static_cast<SDWORD>(psStats->radius);

    /* Store the radius squared */
    radSquared = psStats->radius * psStats->radius;

    /* Do damage to everything in range */
    for (i = 0; i < MAX_PLAYERS; i++)
    {
      for (psCurrD = apsDroidLists[i]; psCurrD; psCurrD = psNextD)
      {
        /* have to store the next pointer as psCurrD could be destroyed */
        psNextD = psCurrD->psNext;

        if (vtolDroid(psCurrD) && (psCurrD->sMove.Status != MOVEINACTIVE))
        {
          // skip VTOLs in the air
          continue;
        }

        /* see if psCurrD is hit (don't hit main target twice) */
        if (((BASE_OBJECT*)psCurrD != psObj->psDest) && (static_cast<SDWORD>(psCurrD->x) >= tarX0) && (static_cast<SDWORD>(psCurrD->x) <=
          tarX1) && (static_cast<SDWORD>(psCurrD->y) >= tarY0) && (static_cast<SDWORD>(psCurrD->y) <= tarY1))
        {
          /* Within the bounding box, now check the radius */
          xDiff = psCurrD->x - psObj->x;
          yDiff = psCurrD->y - psObj->y;
          if ((xDiff * xDiff + yDiff * yDiff) <= radSquared)
          {
            HIT_ROLL(dice);
            if (dice < weaponRadiusHit(psStats, psObj->player))
            {
              DBP1(("Damage to object %d, player %d\n", psCurrD->id, psCurrD->player));
              damage = calcDamage(weaponRadDamage(psStats, psObj->player), psStats->weaponEffect, (BASE_OBJECT*)psCurrD);
              if (bMultiPlayer)
              {
                if (psObj->psSource && myResponsibility(psObj->psSource->player))
                {
                  updateMultiStatsDamage(psObj->psSource->player, psCurrD->player, damage);
                }
                turnOffMultiMsg(TRUE);
              }

              /*bKilled = psCurrD->damage(psCurrD, calcDamage(
                //psStats->radiusDamage, psStats->weaponEffect, 
                weaponRadDamage(psStats,psObj->player), 
                psStats->weaponEffect, 
                (BASE_OBJECT *)psCurrD), psStats->weaponClass);*/
              bKilled = droidDamage(psCurrD, damage, psStats->weaponClass, psStats->weaponSubClass);

              turnOffMultiMsg(FALSE); // multiplay msgs back on.

              if (bKilled)
                proj_UpdateKills(psObj);
            }
          }
        }
      }
      for (psCurrS = apsStructLists[i]; psCurrS; psCurrS = psNextS)
      {
        /* have to store the next pointer as psCurrD could be destroyed */
        psNextS = psCurrS->psNext;

        /* see if psCurrS is hit (don't hit main target twice) */
        if (((BASE_OBJECT*)psCurrS != psObj->psDest) && (static_cast<SDWORD>(psCurrS->x) >= tarX0) && (static_cast<SDWORD>(psCurrS->x) <=
          tarX1) && (static_cast<SDWORD>(psCurrS->y) >= tarY0) && (static_cast<SDWORD>(psCurrS->y) <= tarY1))
        {
          /* Within the bounding box, now check the radius */
          xDiff = psCurrS->x - psObj->x;
          yDiff = psCurrS->y - psObj->y;
          if ((xDiff * xDiff + yDiff * yDiff) <= radSquared)
          {
            HIT_ROLL(dice);
            if (dice < weaponRadiusHit(psStats, psObj->player))
            {
              damage = calcDamage(weaponRadDamage(psStats, psObj->player), psStats->weaponEffect, (BASE_OBJECT*)psCurrS);

              if (bMultiPlayer)
              {
                if (psObj->psSource && myResponsibility(psObj->psSource->player))
                  updateMultiStatsDamage(psObj->psSource->player, psCurrS->player, damage);
              }
              bKilled = structureDamage(psCurrS, damage, psStats->weaponClass, psStats->weaponSubClass);

              if (bKilled)
                proj_UpdateKills(psObj);
            }
          }
        }
        // Missed by old method, but maybe in landed within the building's footprint(baseplate)
        else if (ptInStructure(psCurrS, psObj->x, psObj->y) AND (BASE_OBJECT*)psCurrS != psObj->psDest)
        {
          damage = NOMINAL_DAMAGE;
          if (bMultiPlayer)
          {
            if (psObj->psSource && myResponsibility(psObj->psSource->player))
              updateMultiStatsDamage(psObj->psSource->player, psCurrS->player, damage);
          }
          bKilled = structureDamage(psCurrS, damage, psStats->weaponClass, psStats->weaponSubClass);

          if (bKilled)
            proj_UpdateKills(psObj);
        }
      }
    }

    for (psCurrF = apsFeatureLists[0]; psCurrF; psCurrF = psNextF)
    {
      /* have to store the next pointer as psCurrD could be destroyed */
      psNextF = psCurrF->psNext;

      //ignore features that are not damageable
      if (!psCurrF->psStats->damageable)
        continue;
      /* see if psCurrS is hit (don't hit main target twice) */
      if (((BASE_OBJECT*)psCurrF != psObj->psDest) && (static_cast<SDWORD>(psCurrF->x) >= tarX0) && (static_cast<SDWORD>(psCurrF->x) <=
        tarX1) && (static_cast<SDWORD>(psCurrF->y) >= tarY0) && (static_cast<SDWORD>(psCurrF->y) <= tarY1))
      {
        /* Within the bounding box, now check the radius */
        xDiff = psCurrF->x - psObj->x;
        yDiff = psCurrF->y - psObj->y;
        if ((xDiff * xDiff + yDiff * yDiff) <= radSquared)
        {
          HIT_ROLL(dice);
          if (dice < weaponRadiusHit(psStats, psObj->player))
          {
            DBP1(("Damage to object %d, player %d\n", psCurrF->id, psCurrF->player));
            //(void)psCurrF->damage(psCurrF, calcDamage(psStats->radiusDamage, 
            /*(void)psCurrF->damage(psCurrF, calcDamage(weaponRadDamage(
              psStats, psObj->player), psStats->weaponEffect, 
              (BASE_OBJECT *)psCurrF), psStats->weaponClass);*/
            bKilled = featureDamage(
              psCurrF, calcDamage(weaponRadDamage(psStats, psObj->player), psStats->weaponEffect, (BASE_OBJECT*)psCurrF),
              psStats->weaponClass, psStats->weaponSubClass);
            if (bKilled)
              proj_UpdateKills(psObj);
          }
        }
      }
    }
  }

  if (psStats->incenTime != 0)
  {
    /* Incendiary round */
    /* Incendiary damage gets done in the bullet update routine */
    /* Just note when the incendiary started burning            */
    psObj->state = PROJ_POSTIMPACT;
    psObj->born = gameTime;
  }
  /* Something was blown up */
}

/***************************************************************************/

void proj_PostImpactFunc(PROJ_OBJECT* psObj)
{
  WEAPON_STATS* psStats;
  SDWORD i, age;
  FIRE_BOX flame;

  DEBUG_ASSERT_TEXT(PTRVALID(psObj, sizeof(PROJ_OBJECT)), "proj_PostImpactFunc: invalid projectile pointer");

  psStats = psObj->psWStats;
  DEBUG_ASSERT_TEXT(PTRVALID(psStats, sizeof(WEAPON_STATS)), "proj_PostImpactFunc: Invalid weapon stats pointer");

  age = static_cast<SDWORD>(gameTime) - static_cast<SDWORD>(psObj->born);

  /* Time to finish postimpact effect? */
  if (age > static_cast<SDWORD>(psStats->radiusLife) && age > static_cast<SDWORD>(psStats->incenTime))
  {
    if (hashTable_RemoveElement(g_pProjObjTable, psObj, (int)psObj, UNUSED_KEY) == FALSE)
      DBPRINTF(("proj_PostImpactFunc: couldn't remove projectile from table\n"));
    return;
  }

  /* Burning effect */
  if (psStats->incenTime > 0)
  {
    /* See if anything is in the fire and burn it */

    /* Calculate the fire's bounding box */
    flame.x1 = static_cast<SWORD>(psObj->x - psStats->incenRadius);
    flame.y1 = static_cast<SWORD>(psObj->y - psStats->incenRadius);
    flame.x2 = static_cast<SWORD>(psObj->x + psStats->incenRadius);
    flame.y2 = static_cast<SWORD>(psObj->y + psStats->incenRadius);
    flame.rad = static_cast<SWORD>(psStats->incenRadius * psStats->incenRadius);

    for (i = 0; i < MAX_PLAYERS; i++)
    {
      /* Don't damage your own droids - unrealistic, but better */
      if (i != psObj->player)
      {
        proj_checkBurnDamage((BASE_OBJECT*)apsDroidLists[i], psObj, &flame);
        proj_checkBurnDamage((BASE_OBJECT*)apsStructLists[i], psObj, &flame);
      }
    }
  }
}

/***************************************************************************/

void proj_Update(PROJ_OBJECT* psObj)
{
  DEBUG_ASSERT_TEXT(PTRVALID(psObj, sizeof(PROJ_OBJECT)), "proj_Update: Invalid bullet pointer");

  /* See if any of the stored objects have died
   * since the projectile was created
   */
  if (psObj->psSource && psObj->psSource->died)
    psObj->psSource = nullptr;
  if (psObj->psDest && psObj->psDest->died)
    psObj->psDest = nullptr;

  switch (psObj->state)
  {
  case PROJ_INFLIGHT:
    (psObj->pInFlightFunc)(psObj);
    break;

  case PROJ_IMPACT:
    proj_ImpactFunc(psObj);
    break;

  case PROJ_POSTIMPACT:
    proj_PostImpactFunc(psObj);
    break;
  }
}

/***************************************************************************/

void proj_UpdateAll(void)
{
  PROJ_OBJECT* psObj;

  psObj = static_cast<PROJ_OBJECT*>(hashTable_GetFirst(g_pProjObjTable));

  while (psObj != nullptr)
  {
    proj_Update(psObj);

    psObj = static_cast<PROJ_OBJECT*>(hashTable_GetNext(g_pProjObjTable));
  }
}

/***************************************************************************/

void proj_checkBurnDamage(BASE_OBJECT* apsList, PROJ_OBJECT* psProj, FIRE_BOX* pFireBox)
{
  BASE_OBJECT *psCurr, *psNext;
  SDWORD xDiff, yDiff;
  WEAPON_STATS* psStats;
  UDWORD damageSoFar;
  SDWORD damageToDo;
  BOOL bKilled;

  // note the attacker if any
  g_pProjLastAttacker = psProj->psSource;

  psStats = psProj->psWStats;
  for (psCurr = apsList; psCurr; psCurr = psNext)
  {
    /* have to store the next pointer as psCurr could be destroyed */
    psNext = psCurr->psNext;

    if ((psCurr->type == OBJ_DROID) && vtolDroid((DROID*)psCurr) && ((DROID*)psCurr)->sMove.Status != MOVEINACTIVE)
    {
      // can't set flying vtols on fire
      continue;
    }

    /* see if psCurr is hit (don't hit main target twice) */
    if ((static_cast<SDWORD>(psCurr->x) >= pFireBox->x1) && (static_cast<SDWORD>(psCurr->x) <= pFireBox->x2) && (static_cast<SDWORD>(psCurr
      ->y) >= pFireBox->y1) && (static_cast<SDWORD>(psCurr->y) <= pFireBox->y2))
    {
      /* Within the bounding box, now check the radius */
      xDiff = psCurr->x - psProj->x;
      yDiff = psCurr->y - psProj->y;
      if ((xDiff * xDiff + yDiff * yDiff) <= pFireBox->rad)
      {
        /* The object is in the fire */
        psCurr->inFire |= IN_FIRE;

        if ((psCurr->burnStart == 0) || (psCurr->inFire & BURNING))
        {
          /* This is the first turn the object is in the fire */
          psCurr->burnStart = gameTime;
          psCurr->burnDamage = 0;
        }
        else
        {
          /* Calculate how much damage should have
             been done up till now */
          damageSoFar = (gameTime - psCurr->burnStart)
            //* psStats->incenDamage
            * weaponIncenDamage(psStats, psProj->player) / GAME_TICKS_PER_SEC;
          damageToDo = static_cast<SDWORD>(damageSoFar) - static_cast<SDWORD>(psCurr->burnDamage);
          if (damageToDo > 0)
          {
            DBP1(("Burn damage of %d to object %d, player %d\n", damageToDo, psCurr->id, psCurr->player));

            //						else

            bKilled = objectDamage(psCurr, damageToDo, psStats->weaponClass, psStats->weaponSubClass);
            psCurr->burnDamage += damageToDo;

            if (bKilled)
              proj_UpdateKills(psProj);
          }
          /* The damage could be negative if the object
             is being burn't by another fire
             with a higher burn damage */
        }
      }
    }
  }
}

/***************************************************************************/

// return whether a weapon is direct or indirect
BOOL proj_Direct(WEAPON_STATS* psStats)
{
  switch (psStats->movementModel)
  {
  case MM_DIRECT:
  case MM_HOMINGDIRECT:
  case MM_ERRATICDIRECT:
  case MM_SWEEP:
    return TRUE;
    break;
  case MM_INDIRECT:
  case MM_HOMINGINDIRECT:
    return FALSE;
    break;
  default: DEBUG_ASSERT_TEXT(FALSE, "proj_Direct: unknown movement model");
    break;
  }

  return TRUE;
}

/***************************************************************************/

// return the maximum range for a weapon
SDWORD proj_GetLongRange(WEAPON_STATS* psStats, SDWORD dz)
{
  dz;
  return static_cast<SDWORD>(psStats->longRange);
}

/***************************************************************************/
UDWORD establishTargetRadius(BASE_OBJECT* psTarget)
{
  UDWORD radius;
  STRUCTURE* psStructure;
  FEATURE* psFeat;

  radius = 0;

  if (psTarget == nullptr) // Can this happen?
    return (0);
  switch (psTarget->type)
  {
  case OBJ_DROID:
    radius = TILE_UNITS / 4; // how will we arrive at this?
    break;
  case OBJ_STRUCTURE:
    psStructure = (STRUCTURE*)psTarget;
    radius = ((std::max(psStructure->pStructureType->baseBreadth, psStructure->pStructureType->baseWidth)) * TILE_UNITS) / 2;
    break;
  case OBJ_FEATURE:
    //			radius = TILE_UNITS/4;	// how will we arrive at this?
    psFeat = (FEATURE*)psTarget;
    radius = ((std::max(psFeat->psStats->baseBreadth, psFeat->psStats->baseWidth)) * TILE_UNITS) / 2;
    break;
  }

  return (radius);
}

/***************************************************************************/

/*the damage depends on the weapon effect and the target propulsion type or 
structure strength*/
UDWORD calcDamage(UDWORD baseDamage, WEAPON_EFFECT weaponEffect, BASE_OBJECT* psTarget)
{
  UDWORD damage;

  //default value
  damage = baseDamage;

  if (psTarget->type == OBJ_STRUCTURE)
  {
    damage = baseDamage * asStructStrengthModifier[weaponEffect][((STRUCTURE*)psTarget)->pStructureType->strength] / 100;

    //a little fail safe!
    if (damage == 0 AND baseDamage != 0)
      damage = 1;

#if(0)
    {
      UDWORD Mod;
      UDWORD PropType = ((STRUCTURE*)psTarget)->pStructureType->strength;
      UDWORD damage1;

      Mod = asStructStrengthModifier[weaponEffect][PropType];

      damage1 = baseDamage * Mod / 100;
    }
#endif
  }
  else if (psTarget->type == OBJ_DROID)
  {
    damage = baseDamage * asWeaponModifier[weaponEffect][(asPropulsionStats + ((DROID*)psTarget)->asBits[COMP_PROPULSION].nStat)->
      propulsionType] / 100;

    //a little fail safe!
    if (damage == 0 AND baseDamage != 0)
      damage = 1;

#if(0)
    {
      UDWORD Mod;
      UDWORD PropType = (asPropulsionStats + ((DROID*)psTarget)->asBits[COMP_PROPULSION].nStat)->propulsionType;
      UDWORD damage1;

      Mod = asWeaponModifier[weaponEffect][PropType];

      damage1 = baseDamage * Mod / 100;

      DBPRINTF(("damage1=%d damage=%d baseDamage=%d mod=%d (weaponEffect=%d proptype=%d) \n", damage1, damage, baseDamage, Mod, weaponEffect
        , PropType);
    }
#endif
  }

  return damage;
}

BOOL objectDamage(BASE_OBJECT* psObj, UDWORD damage, UDWORD weaponClass, UDWORD weaponSubClass)
{
  switch (psObj->type)
  {
  case OBJ_DROID:
    return droidDamage((DROID*)psObj, damage, weaponClass, weaponSubClass);
    break;
  case OBJ_STRUCTURE:
    return structureDamage((STRUCTURE*)psObj, damage, weaponClass, weaponSubClass);
    break;
  case OBJ_FEATURE:
    return featureDamage((FEATURE*)psObj, damage, weaponClass, weaponSubClass);
    break;
  default: DEBUG_ASSERT_TEXT(FALSE, "objectDamage - unknown object type");
  }
  return FALSE;
}

#define HIT_THRESHOLD	(GAME_TICKS_PER_SEC/6)	// got to be over 5 frames per sec.
/* Returns true if an object has just been hit by an electronic warfare weapon*/
BOOL justBeenHitByEW(BASE_OBJECT* psObj)
{
  DROID* psDroid;
  FEATURE* psFeature;
  STRUCTURE* psStructure;

  if (gamePaused())
    return (FALSE); // Don't shake when paused...!

  switch (psObj->type)
  {
  case OBJ_DROID:
    psDroid = (DROID*)psObj;
    if ((gameTime - psDroid->timeLastHit) < HIT_THRESHOLD AND psDroid->lastHitWeapon == WSC_ELECTRONIC)
      return (TRUE);
  case OBJ_FEATURE:
    psFeature = (FEATURE*)psObj;
    if ((gameTime - psFeature->timeLastHit) < HIT_THRESHOLD)
      return (TRUE);
    break;
  case OBJ_STRUCTURE:
    psStructure = (STRUCTURE*)psObj;
    if ((gameTime - psStructure->timeLastHit) < HIT_THRESHOLD AND psStructure->lastHitWeapon == WSC_ELECTRONIC)
      return (TRUE);
    break;
  default: DBERROR(("Weird object type in justBeenHitByEW"));
    break;
  }

  return (FALSE);
}

void objectShimmy(BASE_OBJECT* psObj)
{
  if (justBeenHitByEW(psObj))
  {
    iV_MatrixRotateX(SKY_SHIMMY);
    iV_MatrixRotateY(SKY_SHIMMY);
    iV_MatrixRotateZ(SKY_SHIMMY);
    if (psObj->type == OBJ_DROID) { iV_TRANSLATE(1-rand()%3, 0, 1-rand()%3); }
  }
}
