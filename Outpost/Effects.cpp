#include "pch.h"
#include <directxmath.h>
#include "Effects.h"
#include "AudioSystem.h"
#include "AudioID.h"
#include "Bucket3D.h"
#include "Display3D.h"
#include "Frame.h"
#include "Input.h"
#include "GTime.h"
#include "Game.h"
#include "Geo.h"  
#include "HCI.h"
#include "Model.h"
#include "Lighting.h"
#include "Loop.h"
#include "Map.h"
#include "MiscIMD.h"
#include "Mission.h"
#include "MultiPlay.h"
#include "RenderTypes.h"
#include "RenderModel.h"

#define DOLIGHTS

extern UWORD OffScreenEffects;

/* Our list of all game world effects */
EFFECT asEffectsList[MAX_EFFECTS];

#define FIREWORK_EXPLODE_HEIGHT			400
#define STARBURST_RADIUS				150
#define STARBURST_DIAMETER				300

#define	EFFECT_SMOKE_ADDITIVE			1
#define	EFFECT_STEAM_ADDITIVE			8
#define	EFFECT_WAYPOINT_ADDITIVE		32
#define	EFFECT_EXPLOSION_ADDITIVE		164
#define EFFECT_PLASMA_ADDITIVE			224
#define	EFFECT_SMOKE_TRANSPARENCY		130
#define	EFFECT_STEAM_TRANSPARENCY		128
#define	EFFECT_WAYPOINT_TRANSPARENCY	128
#define	EFFECT_BLOOD_TRANSPARENCY		128
#define	EFFECT_EXPLOSION_TRANSPARENCY	164

#define	EFFECT_DROID_DIVISION			101
#define	EFFECT_STRUCTURE_DIVISION		103

#define	DROID_UPDATE_INTERVAL			500
#define	STRUCTURE_UPDATE_INTERVAL		1250
#define	BASE_FLAME_SIZE					80
#define	BASE_LASER_SIZE					10
#define BASE_PLASMA_SIZE				0
#define	DISCOVERY_SIZE					60
#define	FLARE_SIZE						100
#define SHOCKWAVE_SPEED	(GAME_TICKS_PER_SEC)
#define	MAX_SHOCKWAVE_SIZE				500

/* Tick counts for updates on a particular interval */
static UDWORD lastUpdateDroids[EFFECT_DROID_DIVISION];
static UDWORD lastUpdateStructures[EFFECT_STRUCTURE_DIVISION];

/* Current next slot to use - cyclic */
static UDWORD freeEffect;

static UDWORD numEffects;
static UDWORD activeEffects;
static UDWORD missCount;
static UDWORD skipped, skippedEffects, letThrough;
static UDWORD auxVar; // dirty filthy hack - don't look for what this does.... //FIXME
static UDWORD auxVarSec; // dirty filthy hack - don't look for what this does.... //FIXME
static UDWORD aeCalls;
static UDWORD specifiedSize;
static UDWORD ellSpec;
static POINT powerHack[NUM_POWER_MODULES] = // don't even ask
  {{-90, 90}, {-90, -90}, {90, -90}, {90, 90}};
// ----------------------------------------------------------------------------------------
/* PROTOTYPES */
/* externals */

/* Don't even ask what this fellow does... */
void effectResetUpdates(void);
void effectGiveAuxVar(UDWORD var);
void effectGiveAuxVarSec(UDWORD var);
UDWORD getFreeEffect(void);

void initEffectsSystem(void);
void drawEffects(void);
void processEffects(void);
void addEffect(iVector* pos, EFFECT_GROUP group, EFFECT_TYPE type, BOOL specified, iIMDShape* imd, BOOL lit);
void addMultiEffect(iVector* basePos, iVector* scatter, EFFECT_GROUP group, EFFECT_TYPE type, BOOL specified, iIMDShape* imd, UDWORD number,
                    BOOL lit, UDWORD size);
UDWORD getNumEffects(void);
void renderEffect(EFFECT* psEffect); // MASTER Fn
// ----------------------------------------------------------------------------------------
// ---- Update functions - every group type of effect has one of these */
void updateEffect(EFFECT* psEffect); //MASTER Fn
void updateWaypoint(EFFECT* psEffect);
void updateExplosion(EFFECT* psEffect);
void updatePolySmoke(EFFECT* psEffect);
void updateGraviton(EFFECT* psEffect);
void updateConstruction(EFFECT* psEffect);
void updateBlood(EFFECT* psEffect);
void updateDestruction(EFFECT* psEffect);
void updateFire(EFFECT* psEffect);
void updateSatLaser(EFFECT* psEffect);
void updateFirework(EFFECT* psEffect);

// ----------------------------------------------------------------------------------------
// ---- The render functions - every group type of effect has a distinct one
static void renderExplosionEffect(EFFECT* psEffect);
static void renderSmokeEffect(EFFECT* psEffect);
static void renderGravitonEffect(EFFECT* psEffect);
static void renderConstructionEffect(EFFECT* psEffect);
static void renderWaypointEffect(EFFECT* psEffect);
static void renderBloodEffect(EFFECT* psEffect);
static void renderDestructionEffect(EFFECT* psEffect);
static void renderFirework(EFFECT* psEffect);
/* There is no render destruction effect! */

// ----------------------------------------------------------------------------------------
// ---- The set up functions - every type has one
static void effectSetupSmoke(EFFECT* psEffect);
static void effectSetupGraviton(EFFECT* psEffect);
static void effectSetupExplosion(EFFECT* psEffect);
static void effectSetupConstruction(EFFECT* psEffect);
static void effectSetupWayPoint(EFFECT* psEffect);
static void effectSetupBlood(EFFECT* psEffect);
static void effectSetupDestruction(EFFECT* psEffect);
static void effectSetupFire(EFFECT* psEffect);
static void effectSetUpSatLaser(EFFECT* psEffect);
static void effectSetUpFirework(EFFECT* psEffect);
BOOL validatePie(EFFECT_GROUP group, EFFECT_TYPE type, iIMDShape* pie);
// ----------------------------------------------------------------------------------------
//void	initPerimeterSmoke			( EFFECT *psEffect );
void initPerimeterSmoke(iIMDShape* pImd, UDWORD x, UDWORD y, UDWORD z);
// ----------------------------------------------------------------------------------------
void effectStructureUpdates(void);
void effectDroidUpdates(void);

UDWORD EffectGetNumFrames(EFFECT* psEffect);
UDWORD IMDGetNumFrames(iIMDShape* Shape);

/* The fraction of a second that the last game frame took */
static float fraction;

// ----------------------------------------------------------------------------------------
BOOL essentialEffect(EFFECT_GROUP group, EFFECT_TYPE type)
{
  switch (group)
  {
  case EFFECT_FIRE:
  case EFFECT_WAYPOINT:
  case EFFECT_DESTRUCTION:
  case EFFECT_SAT_LASER:
  case EFFECT_STRUCTURE:
    return (TRUE);
    break;
  case EFFECT_EXPLOSION:
    {
      if (type == EXPLOSION_TYPE_LAND_LIGHT)
        return (TRUE);
      return (FALSE);
    }
  default:
    return (FALSE);
    break;
  }
}

BOOL utterlyReject(EFFECT_GROUP group, EFFECT_TYPE type)
{
  switch (group)
  {
  case EFFECT_BLOOD:
  case EFFECT_DUST_BALL:
  case EFFECT_CONSTRUCTION:
    return (TRUE);
  default:
    return (FALSE);
    break;
  }
}

// ----------------------------------------------------------------------------------------
/*	Simply sets the free pointer to the start - actually this isn't necessary
	as it will work just fine anyway. This WOULD be necessary were we to change
	the system so that it seeks FREE slots rather than the oldest one. This is because
	different effects last for different times and the oldest effect may have 
	just got going (if it was a long effect).
*/
void initEffectsSystem(void)
{
  /* Set position to first */
  freeEffect = 0;

  /* None are active */
  numEffects = 0;

  activeEffects = 0;

  missCount = 0;

  skipped = letThrough = 0;

  for (UDWORD i = 0; i < MAX_EFFECTS; i++)
  {
    /* Get a pointer - just cos our macro requires it, speeds not an issue here */
    EFFECT* psEffect = &asEffectsList[i];
    /* Clear all the control bits */
    psEffect->control = static_cast<UBYTE>(0);
    /* All effects are initially inactive */
    asEffectsList[i].status = ES_INACTIVE;
  }
}

// ----------------------------------------------------------------------------------------
void effectSetLandLightSpec(LAND_LIGHT_SPEC spec) { ellSpec = spec; }
// ----------------------------------------------------------------------------------------
void effectSetSize(UDWORD size) { specifiedSize = size; }
// ----------------------------------------------------------------------------------------
void addMultiEffect(iVector* basePos, iVector* scatter, EFFECT_GROUP group, EFFECT_TYPE type, BOOL specified, iIMDShape* imd, UDWORD number,
                    BOOL lit, UDWORD size)
{
  iVector scatPos;

  if (number == 0)
    return;
  /* Set up the scaling for specified ones */
  specifiedSize = size;

  /* If there's only one, make sure it's in the centre */
  if (number == 1)
  {
    scatPos.x = basePos->x;
    scatPos.y = basePos->y;
    scatPos.z = basePos->z;
    addEffect(&scatPos, group, type, specified, imd, lit);
  }
  else
  {
    /* Fix for jim */
    scatter->x /= 10;
    scatter->y /= 10;
    scatter->z /= 10;

    /* There are multiple effects - so scatter them around according to parameter */
    for (UDWORD i = 0; i < number; i++)
    {
      scatPos.x = basePos->x + (scatter->x ? (scatter->x - (rand() % (2 * scatter->x))) : 0);
      scatPos.y = basePos->y + (scatter->y ? (scatter->y - (rand() % (2 * scatter->y))) : 0);
      scatPos.z = basePos->z + (scatter->z ? (scatter->z - (rand() % (2 * scatter->z))) : 0);
      addEffect(&scatPos, group, type, specified, imd, lit);
    }
  }
}

// ----------------------------------------------------------------------------------------
UDWORD getNumActiveEffects(void) { return (activeEffects); }
// ----------------------------------------------------------------------------------------
UDWORD getMissCount(void) { return (missCount); }

UDWORD getNumSkippedEffects(void) { return (skippedEffects); }

UDWORD getNumEvenEffects(void) { return (letThrough); }
// ----------------------------------------------------------------------------------------

UDWORD Reject1;

void addEffect(iVector* pos, EFFECT_GROUP group, EFFECT_TYPE type, BOOL specified, iIMDShape* imd, BOOL lit)
{
  UDWORD essentialCount;
  UDWORD i;
  BOOL bSmoke;

  aeCalls++;

  if (gamePaused())
    return;

  /* Quick optimsation to reject every second non-essential effect if it's off grid */
  //	if(clipXY((UDWORD)MAKEINT(pos->x),(UDWORD)MAKEINT(pos->z)) == FALSE)
  if (clipXY(static_cast<UDWORD>(pos->x), static_cast<UDWORD>(pos->z)) == FALSE)
  {
    /* 	If effect is essentail - then let it through */
    if (!essentialEffect(group, type))
    {
      /* Some we can get rid of right away */
      if (utterlyReject(group, type))
      {
        skipped++;
        return;
      }
      /* Smoke gets culled more than most off grid effects */
      if (group == EFFECT_SMOKE)
        bSmoke = TRUE;
      else
        bSmoke = FALSE;
      /* Others intermittently (50/50 for most and 25/100 for smoke */
      if (bSmoke ? (aeCalls & 0x03) : (aeCalls & 0x01))
      {
        /* Do one */
        skipped++;
        return;
      }
      letThrough++;
    }
  }

  for (i = freeEffect, essentialCount = 0; (asEffectsList[i].control & EFFECT_ESSENTIAL) AND essentialCount < MAX_EFFECTS; i++)
  {
    /* Check for wrap around */
    if (i >= (MAX_EFFECTS - 1))
    {
      /* Go back to the first one */
      i = 0;
    }
    essentialCount++;
    missCount++;
  }

  /* Check the list isn't just full of essential effects */
  if (essentialCount >= MAX_EFFECTS)
  {
    /* All of the effects are essential!?!? */
    return;
  }
  freeEffect = i;

  /* Store away it's position - into FRACTS */
  asEffectsList[freeEffect].position.x = static_cast<float>(pos->x);
  asEffectsList[freeEffect].position.y = static_cast<float>(pos->y);
  asEffectsList[freeEffect].position.z = static_cast<float>(pos->z);

  /* Now, note group and type */
  asEffectsList[freeEffect].group = static_cast<UBYTE>(group);
  asEffectsList[freeEffect].type = static_cast<UBYTE>(type);

  /* Set when it entered the world */
  asEffectsList[freeEffect].birthTime = asEffectsList[freeEffect].lastFrame = gameTime;

  if (group == EFFECT_GRAVITON AND (type == GRAVITON_TYPE_GIBLET OR type == GRAVITON_TYPE_EMITTING_DR))
    asEffectsList[freeEffect].frameNumber = lit;

  else
  {
    /* Starts off on frame zero */
    asEffectsList[freeEffect].frameNumber = 0;
  }

  /*	
    See what kind of effect it is - the add fucnction is different for each,
    although some things are shared
  */
  asEffectsList[freeEffect].imd = nullptr;
  if (lit)
    SET_LITABS(asEffectsList[freeEffect]);

  if (specified)
  {
    /* We're specifying what the imd is - override */
    asEffectsList[freeEffect].imd = imd;
    //		else
    asEffectsList[freeEffect].size = static_cast<UWORD>(specifiedSize);
  }

  /* Do all the effect type specific stuff */
  switch (group)
  {
  case EFFECT_SMOKE:
    effectSetupSmoke(&asEffectsList[freeEffect]);
    break;
  case EFFECT_GRAVITON:
    effectSetupGraviton(&asEffectsList[freeEffect]);
    break;
  case EFFECT_EXPLOSION:
    effectSetupExplosion(&asEffectsList[freeEffect]);
    break;
  case EFFECT_CONSTRUCTION:
    effectSetupConstruction(&asEffectsList[freeEffect]);
    break;
  case EFFECT_WAYPOINT:
    effectSetupWayPoint(&asEffectsList[freeEffect]);
    break;
  case EFFECT_BLOOD:
    effectSetupBlood(&asEffectsList[freeEffect]);
    break;
  case EFFECT_DESTRUCTION:
    effectSetupDestruction(&asEffectsList[freeEffect]);
    break;
  case EFFECT_FIRE:
    effectSetupFire(&asEffectsList[freeEffect]);
    break;
  case EFFECT_SAT_LASER:
    effectSetUpSatLaser(&asEffectsList[freeEffect]);
    break;
  case EFFECT_FIREWORK:
    effectSetUpFirework(&asEffectsList[freeEffect]);
    break;
  default: DEBUG_ASSERT_TEXT(FALSE, "Weirdy group type for an effect");
    break;
  }

  /* Make the effect active */
  asEffectsList[freeEffect].status = ES_ACTIVE;

  /* As of yet, it hasn't bounced (or whatever)... */
  if (type != EXPLOSION_TYPE_LAND_LIGHT)
    asEffectsList[freeEffect].specific = 0;

  /* Looks like we didn't establish an imd for the effect */
  /*
  ASSERT((asEffectsList[freeEffect].imd != NULL OR group == EFFECT_DESTRUCTION OR group == EFFECT_FIRE OR group == EFFECT_SAT_LASER,
    "null effect imd"));
  */

#ifdef DEBUG
  if (validatePie(group, type, asEffectsList[freeEffect].imd) == FALSE)
    DEBUG_ASSERT_TEXT(FALSE, "No PIE found or specified for an effect");
#endif

  /* No more slots available? */
  if (freeEffect++ >= (MAX_EFFECTS - 1))
  {
    /* Go back to the first one */
    freeEffect = 0;
  }
}

#ifdef DEBUG
// ----------------------------------------------------------------------------------------
BOOL validatePie(EFFECT_GROUP group, EFFECT_TYPE type, iIMDShape* pie)
{
  /* If we haven't got a pie */
  if (pie == nullptr)
  {
    if (group == EFFECT_DESTRUCTION OR group == EFFECT_FIRE OR group == EFFECT_SAT_LASER)
    {
      /* Ok in these cases */
      return (TRUE);
    }
    return (FALSE);
  }
  return (TRUE);
}

// ----------------------------------------------------------------------------------------
#endif
/* Calls all the update functions for each different currently active effect */
void processEffects(void)
{
  /* Establish how long the last game frame took */
  fraction = static_cast<float>(frameTime) / GAME_TICKS_PER_SEC;
  UDWORD num = 0;
  missCount = 0;

  for (UDWORD i = 0; i < MAX_EFFECTS; i++)
  {
    /* Is it active */
    switch (asEffectsList[i].status)
    {
    /* The effect is active */
    case ES_ACTIVE:
      /* So process it */
      updateEffect(&asEffectsList[i]);
      num++;
      break;
    case ES_DORMANT:
      /* Might be useful? */
      break;
    default:
      break;
    }
  }

  /* Add any droid effects */
  effectDroidUpdates();

  /* Add any structure effects */
  effectStructureUpdates();

  activeEffects = num;
  skippedEffects = skipped;
}

// ----------------------------------------------------------------------------------------
/*
drawEffects:-
This will either draw all the effects that are on the grid in a oner or
more likely add them to the bucket.
*/
void drawEffects(void)
{
  /* Reset counter */
  numEffects = 0;

  /* Traverse the list */
  for (UDWORD i = 0; i < MAX_EFFECTS; i++)
  {
    /* Don't bother unless it's active */
    if (asEffectsList[i].status == ES_ACTIVE)
    {
      /* One more is active */
      numEffects++;
      /* Is it on the grid */
      if (clipXY(static_cast<UDWORD>(std::lrintf(asEffectsList[i].position.x)), static_cast<UDWORD>(std::lrintf(asEffectsList[i].position.z))))
      {
#ifndef BUCKET
        /* Draw it right now */
        renderEffect(&asEffectsList[i]);
#else
        /* Add it to the bucket */
        bucketAddTypeToList(RENDER_EFFECT, &asEffectsList[i]);
#endif
      }
    }
  }
}

// ----------------------------------------------------------------------------------------
/* The general update function for all effects - calls a specific one for each */
void updateEffect(EFFECT* psEffect)
{
  /* What type of effect are we dealing with? */
  switch (psEffect->group)
  {
  case EFFECT_EXPLOSION:
    updateExplosion(psEffect);
    break;

  case EFFECT_WAYPOINT:
    if (!gamePaused())
      updateWaypoint(psEffect);
    break;

  case EFFECT_CONSTRUCTION:
    if (!gamePaused())
      updateConstruction(psEffect);
    break;

  case EFFECT_SMOKE:
    if (!gamePaused())
      updatePolySmoke(psEffect);
    break;

  case EFFECT_STRUCTURE:
    break;

  case EFFECT_GRAVITON:
    if (!gamePaused())
      updateGraviton(psEffect);
    break;

  case EFFECT_BLOOD:
    if (!gamePaused())
      updateBlood(psEffect);
    break;

  case EFFECT_DESTRUCTION:
    if (!gamePaused())
      updateDestruction(psEffect);
    break;

  case EFFECT_FIRE:
    if (!gamePaused())
      updateFire(psEffect);
    break;

  case EFFECT_SAT_LASER:
    if (!gamePaused())
      updateSatLaser(psEffect);
    break;
  case EFFECT_FIREWORK:
    if (!gamePaused())
      updateFirework(psEffect);
    break;
  default: Neuron::Fatal("Weirdy class of effect passed to updateEffect");
    break;
  }
}

// ----------------------------------------------------------------------------------------
// ALL THE UPDATE FUNCTIONS 
// ----------------------------------------------------------------------------------------
/* Update the waypoint effects.*/
void updateWaypoint(EFFECT* psEffect)
{
  if (!(keyDown(KEY_LCTRL) || keyDown(KEY_RCTRL) || keyDown(KEY_LSHIFT) || keyDown(KEY_RSHIFT))) { KILL_EFFECT(psEffect); }
}

// ----------------------------------------------------------------------------------------
void updateFirework(EFFECT* psEffect)
{
  iVector dv;
  UDWORD drop;

  /* Move it */
  DirectX::XMStoreFloat3(&psEffect->position,
    DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&psEffect->position),
                         DirectX::XMVectorScale(DirectX::XMLoadFloat3(&psEffect->velocity), fraction)));

  if (psEffect->type == FIREWORK_TYPE_LAUNCHER)
  {
    UDWORD height = std::lrintf(psEffect->position.y);
    if (height > psEffect->size)
    {
      dv.x = std::lrintf(psEffect->position.x);
      dv.z = std::lrintf(psEffect->position.z);
      dv.y = std::lrintf(psEffect->position.y) + (psEffect->radius / 2);
      addEffect(&dv, EFFECT_EXPLOSION, EXPLOSION_TYPE_MEDIUM,FALSE, nullptr, 0);
      AudioSystem::PlayStaticTrack(std::lrintf(psEffect->position.x), std::lrintf(psEffect->position.z), ID_SOUND_EXPLOSION);

      for (UDWORD dif = 0; dif < (psEffect->radius * 2); dif += 20)
      {
        if (dif < psEffect->radius)
          drop = psEffect->radius - dif;
        else
          drop = dif - psEffect->radius;
        UDWORD radius = static_cast<UDWORD>(sqrt((psEffect->radius * psEffect->radius) - (drop * drop)));
        //val = getStaticTimeValueRange(720,360);	// grab an angle - 4 seconds cyclic
        for (UDWORD val = 0; val <= 180; val += 20)
        {
          float valSin, valCos;
          DirectX::XMScalarSinCos(&valSin, &valCos, DirectX::XMConvertToRadians(static_cast<float>(val)));
          SDWORD xDif = static_cast<SDWORD>(std::lrintf(radius * valSin));
          SDWORD yDif = static_cast<SDWORD>(std::lrintf(radius * valCos));
          dv.x = std::lrintf(psEffect->position.x) + xDif;
          dv.z = std::lrintf(psEffect->position.z) + yDif;
          dv.y = std::lrintf(psEffect->position.y) + dif;
          effectGiveAuxVar(100);
          addEffect(&dv, EFFECT_FIREWORK, FIREWORK_TYPE_STARBURST,FALSE, nullptr, 0);
          dv.x = std::lrintf(psEffect->position.x) - xDif;
          dv.z = std::lrintf(psEffect->position.z) - yDif;
          dv.y = std::lrintf(psEffect->position.y) + dif;
          effectGiveAuxVar(100);
          addEffect(&dv, EFFECT_FIREWORK, FIREWORK_TYPE_STARBURST,FALSE, nullptr, 0);
          dv.x = std::lrintf(psEffect->position.x) + xDif;
          dv.z = std::lrintf(psEffect->position.z) - yDif;
          dv.y = std::lrintf(psEffect->position.y) + dif;
          effectGiveAuxVar(100);
          addEffect(&dv, EFFECT_FIREWORK, FIREWORK_TYPE_STARBURST,FALSE, nullptr, 0);
          dv.x = std::lrintf(psEffect->position.x) - xDif;
          dv.z = std::lrintf(psEffect->position.z) + yDif;
          dv.y = std::lrintf(psEffect->position.y) + dif;
          effectGiveAuxVar(100);
          addEffect(&dv, EFFECT_FIREWORK, FIREWORK_TYPE_STARBURST,FALSE, nullptr, 0);

          //   			dv.z = dv.z - (2*yDif);	// buildings are level!
        }
      }
      KILL_EFFECT(psEffect);
    }
    else
    {
      /* Add an effect at the firework's position */
      dv.x = std::lrintf(psEffect->position.x);
      dv.y = std::lrintf(psEffect->position.y);
      dv.z = std::lrintf(psEffect->position.z);

      /* Add a trail graphic */
      addEffect(&dv, EFFECT_SMOKE, SMOKE_TYPE_TRAIL,FALSE, nullptr, 0);
    }
  }
  else // must be a startburst
  {
    /* Time to update the frame number on the smoke sprite */
    if (gameTime - psEffect->lastFrame > psEffect->frameDelay)
    {
      /* Store away last frame change time */
      psEffect->lastFrame = gameTime;

      /* Are we on the last frame? */
      if (++psEffect->frameNumber >= EffectGetNumFrames(psEffect))
      {
        /* Does the anim wrap around? */
        if (TEST_CYCLIC(psEffect))
          psEffect->frameNumber = 0;
        else
        {
          /* Kill it off */
          KILL_EFFECT(psEffect);
          return;
        }
      }
    }

    /* If it doesn't get killed by frame number, then by age */
    if (TEST_CYCLIC(psEffect))
    {
      /* Has it overstayed it's welcome? */
      if (gameTime - psEffect->birthTime > psEffect->lifeSpan)
      {
        /* Kill it */
        KILL_EFFECT(psEffect);
      }
    }
  }
}

// ----------------------------------------------------------------------------------------
void updateSatLaser(EFFECT* psEffect)
{
  iVector dv;
  UDWORD i;
  LIGHT light;

  // Do these here cause there used by the lighting code below this if.
  UDWORD xPos = std::lrintf(psEffect->position.x);
  UDWORD startHeight = std::lrintf(psEffect->position.y);
  UDWORD endHeight = startHeight + 1064;
  UDWORD yPos = std::lrintf(psEffect->position.z);

  if (psEffect->baseScale)
  {
    psEffect->baseScale = 0;

    iIMDShape* pie = getImdFromIndex(MI_FLAME);

    /* Add some big explosions....! */

    for (i = 0; i < 16; i++)
    {
      dv.x = xPos + (200 - rand() % 400);
      dv.z = yPos + (200 - rand() % 400);
      dv.y = startHeight + rand() % 100;
      addEffect(&dv, EFFECT_EXPLOSION, EXPLOSION_TYPE_MEDIUM,FALSE, nullptr, 0);
    }
    /* Add a sound effect */
    AudioSystem::PlayStaticTrack(std::lrintf(psEffect->position.x), std::lrintf(psEffect->position.z), ID_SOUND_EXPLOSION);

    /* Add a shockwave */
    dv.x = xPos;
    dv.z = yPos;
    dv.y = startHeight + SHOCK_WAVE_HEIGHT;
    addEffect(&dv, EFFECT_EXPLOSION, EXPLOSION_TYPE_SHOCKWAVE,FALSE, nullptr, 0);

    /* Now, add the column of light */
    for (i = startHeight; i < endHeight; i += 56)
    {
      UDWORD radius = 80;
      /* Add 36 around in a circle..! */
      for (UDWORD val = 0; val <= 180; val += 30)
      {
        float valSin, valCos;
        DirectX::XMScalarSinCos(&valSin, &valCos, DirectX::XMConvertToRadians(static_cast<float>(val)));
        SDWORD xDif = static_cast<SDWORD>(std::lrintf(radius * valSin));
        SDWORD yDif = static_cast<SDWORD>(std::lrintf(radius * valCos));
        dv.x = xPos + xDif + i / 64;
        dv.z = yPos + yDif;
        dv.y = startHeight + i;
        effectGiveAuxVar(100);
        addEffect(&dv, EFFECT_EXPLOSION, EXPLOSION_TYPE_MEDIUM,FALSE, nullptr, 0);
        dv.x = xPos - xDif + i / 64;
        dv.z = yPos - yDif;
        dv.y = startHeight + i;
        effectGiveAuxVar(100);
        addEffect(&dv, EFFECT_EXPLOSION, EXPLOSION_TYPE_MEDIUM,FALSE, nullptr, 0);
        dv.x = xPos + xDif + i / 64;
        dv.z = yPos - yDif;
        dv.y = startHeight + i;
        effectGiveAuxVar(100);
        addEffect(&dv, EFFECT_EXPLOSION, EXPLOSION_TYPE_MEDIUM,FALSE, nullptr, 0);
        dv.x = xPos - xDif + i / 64;
        dv.z = yPos + yDif;
        dv.y = startHeight + i;
        effectGiveAuxVar(100);
        addEffect(&dv, EFFECT_EXPLOSION, EXPLOSION_TYPE_MEDIUM,FALSE, nullptr, 0);
      }
    }
  }

  if (gameTime - psEffect->birthTime < 1000)
  {
    light.position.x = xPos;
    light.position.y = startHeight;
    light.position.z = yPos;
    light.range = 800;
    light.colour = LIGHT_BLUE;
    processLight(&light);
  }
  else { KILL_EFFECT(psEffect); }
}

// ----------------------------------------------------------------------------------------
/* The update function for the explosions */
void updateExplosion(EFFECT* psEffect)
{
  LIGHT light;
  UDWORD percent;
  float scaling;

  if (TEST_LIT(psEffect))
  {
    if (psEffect->lifeSpan)
    {
      percent = PERCENT(gameTime-psEffect->birthTime, psEffect->lifeSpan);
      if (percent > 100)
        percent = 100;
      else
      {
        if (percent > 50)
          percent = 100 - percent;
      }
    }
    else
      percent = 100;

    UDWORD range = percent;
    light.position.x = std::lrintf(psEffect->position.x);
    light.position.y = std::lrintf(psEffect->position.y);
    light.position.z = std::lrintf(psEffect->position.z);
    light.range = (3 * range) / 2;
    light.colour = LIGHT_RED;
    processLight(&light);
  }

#ifdef DOLIGHTS
#endif

  if (psEffect->type == EXPLOSION_TYPE_SHOCKWAVE)
  {
    psEffect->size += std::lrintf(fraction * SHOCKWAVE_SPEED);
    scaling = static_cast<float>(psEffect->size) / MAX_SHOCKWAVE_SIZE;
    psEffect->frameNumber = std::lrintf(scaling * EffectGetNumFrames(psEffect));
#ifdef DOLIGHTS
    light.position.x = std::lrintf(psEffect->position.x);
    light.position.y = std::lrintf(psEffect->position.y);
    light.position.z = std::lrintf(psEffect->position.z);
    light.range = psEffect->size + 200;
    light.colour = LIGHT_YELLOW;
    processLight(&light);
#endif
    if (psEffect->size > MAX_SHOCKWAVE_SIZE OR light.range > 600)
    {
      /* Kill it off */
      KILL_EFFECT(psEffect);
      return;
    }
  }

  /* Time to update the frame number on the explosion */
  else if (gameTime - psEffect->lastFrame > psEffect->frameDelay)
  {
    psEffect->lastFrame = gameTime;
    /* Are we on the last frame? */

    if (++psEffect->frameNumber >= EffectGetNumFrames(psEffect))
    {
      if (psEffect->type != EXPLOSION_TYPE_LAND_LIGHT)
      {
        /* Kill it off */
        KILL_EFFECT(psEffect);
        return;
      }
      psEffect->frameNumber = 0;
    }
  }

  if (!gamePaused())
  {
    /* Tesla explosions are the only ones that rise, or indeed move */
    if (psEffect->type == EXPLOSION_TYPE_TESLA)
      psEffect->position.y += (std::lrintf(psEffect->velocity.y) * fraction);
  }
}

// ----------------------------------------------------------------------------------------
/* The update function for blood */
void updateBlood(EFFECT* psEffect)
{
  /* Time to update the frame number on the blood */
  if (gameTime - psEffect->lastFrame > psEffect->frameDelay)
  {
    psEffect->lastFrame = gameTime;
    /* Are we on the last frame? */
    if (++psEffect->frameNumber >= EffectGetNumFrames(psEffect))
    {
      /* Kill it off */
      KILL_EFFECT(psEffect);
      return;
    }
  }
  /* Move it about in the world */
  DirectX::XMStoreFloat3(&psEffect->position,
    DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&psEffect->position),
                         DirectX::XMVectorScale(DirectX::XMLoadFloat3(&psEffect->velocity), fraction)));
}

// ----------------------------------------------------------------------------------------
/* Processes all the drifting smoke 
	Handles the smoke puffing out the factory as well */
void updatePolySmoke(EFFECT* psEffect)
{
  /* Time to update the frame number on the smoke sprite */
  if (gameTime - psEffect->lastFrame > psEffect->frameDelay)
  {
    /* Store away last frame change time */
    psEffect->lastFrame = gameTime;

    /* Are we on the last frame? */
    if (++psEffect->frameNumber >= EffectGetNumFrames(psEffect))
    {
      /* Does the anim wrap around? */
      if (TEST_CYCLIC(psEffect))
      {
        /* Does it change drift direction? */
        if (psEffect->type == SMOKE_TYPE_DRIFTING)
        {
          /* Make it change direction */
          psEffect->velocity.x = static_cast<float>(rand()%20);
          psEffect->velocity.z = static_cast<float>(10-rand()%20);
          psEffect->velocity.y = static_cast<float>(10+rand()%20);
        }
        /* Reset the frame */
        psEffect->frameNumber = 0;
      }
      else
      {
        /* Kill it off */
        KILL_EFFECT(psEffect);
        return;
      }
    }
  }

  /* Update position */
  DirectX::XMStoreFloat3(&psEffect->position,
    DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&psEffect->position),
                         DirectX::XMVectorScale(DirectX::XMLoadFloat3(&psEffect->velocity), fraction)));

  /* If it doesn't get killed by frame number, then by age */
  if (TEST_CYCLIC(psEffect))
  {
    /* Has it overstayed it's welcome? */
    if (gameTime - psEffect->birthTime > psEffect->lifeSpan)
    {
      /* Kill it */
      KILL_EFFECT(psEffect);
    }
  }
}

// ----------------------------------------------------------------------------------------
/* 
	Gravitons just fly up for a bit and then drop down and are
	killed off when they hit the ground
*/
void updateGraviton(EFFECT* psEffect)
{
  float accel;
  iVector dv;
  UDWORD groundHeight;
  MAPTILE* psTile;

  LIGHT light;
#ifdef DOLIGHTS
  if (psEffect->type != GRAVITON_TYPE_GIBLET)
  {
    light.position.x = std::lrintf(psEffect->position.x);
    light.position.y = std::lrintf(psEffect->position.y);
    light.position.z = std::lrintf(psEffect->position.z);
    light.range = 128;
    light.colour = LIGHT_YELLOW;
    processLight(&light);
  }
#endif

  if (gamePaused())
  {
    /* Only update the lights if it's paused */
    return;
  }
  /* Move it about in the world */
  DirectX::XMStoreFloat3(&psEffect->position,
    DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&psEffect->position),
                         DirectX::XMVectorScale(DirectX::XMLoadFloat3(&psEffect->velocity), fraction)));
  /* If it's bounced/drifted off the map then kill it */
  if ((static_cast<UDWORD>(std::lrintf(psEffect->position.x)) / TILE_UNITS >= mapWidth) OR static_cast<UDWORD>(std::lrintf(psEffect->position.z)) /
    TILE_UNITS >= mapHeight)
  {
    KILL_EFFECT(psEffect);
    return;
  }

  groundHeight = map_Height(static_cast<UDWORD>(std::lrintf(psEffect->position.x)), static_cast<UDWORD>(std::lrintf(psEffect->position.z)));

  /* If it's going up and it's still under the landscape, then remove it... */
  if (psEffect->position.y < groundHeight AND std::lrintf(psEffect->velocity.y) > 0)
  {
    KILL_EFFECT(psEffect);
    return;
  }

  /* Does it emit a trail? And is it high enough? */
  if ((psEffect->type == GRAVITON_TYPE_EMITTING_DR) OR (psEffect->type == GRAVITON_TYPE_EMITTING_ST) AND (psEffect->position.y > (
    groundHeight + 10)))
  {
    /* Time to add another trail 'thing'? */
    if (gameTime > psEffect->lastFrame + psEffect->frameDelay)
    {
      /* Store away last update */
      psEffect->lastFrame = gameTime;

      /* Add an effect at the gravitons's position */
      dv.x = std::lrintf(psEffect->position.x);
      dv.y = std::lrintf(psEffect->position.y);
      dv.z = std::lrintf(psEffect->position.z);

      /* Add a trail graphic */
      addEffect(&dv, EFFECT_SMOKE, SMOKE_TYPE_TRAIL,FALSE, nullptr, 0);
    }
  }

  else if (psEffect->type == GRAVITON_TYPE_GIBLET AND (psEffect->position.y > (groundHeight + 5)))
  {
    /* Time to add another trail 'thing'? */
    if (gameTime > psEffect->lastFrame + psEffect->frameDelay)
    {
      /* Store away last update */
      psEffect->lastFrame = gameTime;

      /* Add an effect at the gravitons's position */
      dv.x = std::lrintf(psEffect->position.x);
      dv.y = std::lrintf(psEffect->position.y);
      dv.z = std::lrintf(psEffect->position.z);
      addEffect(&dv, EFFECT_BLOOD, BLOOD_TYPE_NORMAL,FALSE, nullptr, 0);
    }
  }

  /* Spin it round a bit */
  psEffect->rotation.x += std::lrintf(static_cast<float>(psEffect->spin.x) * fraction);
  psEffect->rotation.y += std::lrintf(static_cast<float>(psEffect->spin.y) * fraction);
  psEffect->rotation.z += std::lrintf(static_cast<float>(psEffect->spin.z) * fraction);

  /* Update velocity (and retarding of descent) according to present frame rate */
  accel = (GRAVITON_GRAVITY * fraction);
  psEffect->velocity.y += accel;

  /* If it's bounced/drifted off the map then kill it */
  if ((std::lrintf(psEffect->position.x) <= TILE_UNITS) OR std::lrintf(psEffect->position.z) <= TILE_UNITS)
  {
    KILL_EFFECT(psEffect);
    return;
  }

  /* Are we below it? - Hit the ground? */
  if ((std::lrintf(psEffect->position.y) < static_cast<SDWORD>(groundHeight)))
  {
    psTile = mapTile((std::lrintf(psEffect->position.x)) >> TILE_SHIFT, (std::lrintf(psEffect->position.z)) >> TILE_SHIFT);
    if (TERRAIN_TYPE(psTile) == TER_WATER)
    {
      KILL_EFFECT(psEffect);
      return;
    }
    /* Are we falling - rather than rising? */
    if (std::lrintf(psEffect->velocity.y) < 0)
    {
      /* Has it sufficient energy to keep bouncing? */
      if (abs(std::lrintf(psEffect->velocity.y)) > 16 AND psEffect->specific <= 2)
      {
        psEffect->specific++;
        /* Half it's velocity */
        psEffect->velocity.y /= static_cast<float>(-2); // only y gets flipped
        /* Set it at ground level - may have gone through */
        psEffect->position.y = static_cast<float>(groundHeight);
      }
      else
      {
        /* Giblets don't blow up when they hit the ground! */
        if (psEffect->type != GRAVITON_TYPE_GIBLET)
        {
          /* Remove the graviton and add an explosion */
          dv.x = std::lrintf(psEffect->position.x);
          dv.y = std::lrintf(psEffect->position.y + 10);
          dv.z = std::lrintf(psEffect->position.z);
          addEffect(&dv, EFFECT_EXPLOSION, EXPLOSION_TYPE_VERY_SMALL,FALSE, nullptr, 0);
        }
        KILL_EFFECT(psEffect);
      }
    }
  }
}

// ----------------------------------------------------------------------------------------
/* updateDestruction
This isn't really an on-screen effect itself - it just spawns other ones....
  */
void updateDestruction(EFFECT* psEffect)
{
  iVector pos;
  UDWORD effectType;
  UDWORD widthScatter, breadthScatter, heightScatter;
  SDWORD iX, iY;
  LIGHT light;
  float div;
  UDWORD height;

  UDWORD percent = PERCENT(gameTime-psEffect->birthTime, psEffect->lifeSpan);
  if (percent > 100)
    percent = 100;
  UDWORD range = 50 - abs(static_cast<SDWORD>(50 - percent));
#ifdef DOLIGHTS
  light.position.x = std::lrintf(psEffect->position.x);
  light.position.y = std::lrintf(psEffect->position.y);
  light.position.z = std::lrintf(psEffect->position.z);
  if (psEffect->type == DESTRUCTION_TYPE_STRUCTURE)
    light.range = range * 10;
  else
    light.range = range * 4;
  if (psEffect->type == DESTRUCTION_TYPE_POWER_STATION)
  {
    light.range *= 3;
    light.colour = LIGHT_WHITE;
  }
  else
    light.colour = LIGHT_RED;
  processLight(&light);
#endif

  if (gameTime > (psEffect->birthTime + psEffect->lifeSpan))
  {
    /* Kill it - it's too old */
    KILL_EFFECT(psEffect);
    return;
  }

  if (psEffect->type == DESTRUCTION_TYPE_SKYSCRAPER)
  {
    if ((gameTime - psEffect->birthTime) > ((9 * psEffect->lifeSpan) / 10))
    {
      pos.x = std::lrintf(psEffect->position.x);
      pos.z = std::lrintf(psEffect->position.z);
      pos.y = std::lrintf(psEffect->position.y);
      addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_LARGE,FALSE, nullptr, 0);
      KILL_EFFECT(psEffect);
      return;
    }

    div = static_cast<float>(gameTime - psEffect->birthTime) / psEffect->lifeSpan;
    if (div > 1.0f)
      div = 1.0f;
    div = 1.0f - div;
    height = std::lrintf(div * psEffect->imd->ymax);
  }
  else
    height = 16;

  /* Time to add another effect? */
  if ((gameTime - psEffect->lastFrame) > psEffect->frameDelay)
  {
    psEffect->lastFrame = gameTime;
    switch (psEffect->type)
    {
    case DESTRUCTION_TYPE_SKYSCRAPER:
      widthScatter = TILE_UNITS;
      breadthScatter = TILE_UNITS;
      heightScatter = TILE_UNITS;
      break;

    case DESTRUCTION_TYPE_POWER_STATION:
    case DESTRUCTION_TYPE_STRUCTURE:
      widthScatter = TILE_UNITS / 2;
      breadthScatter = TILE_UNITS / 2;
      heightScatter = TILE_UNITS / 4;
      break;

    case DESTRUCTION_TYPE_DROID:
    case DESTRUCTION_TYPE_WALL_SECTION:
    case DESTRUCTION_TYPE_FEATURE:
      widthScatter = TILE_UNITS / 6;
      breadthScatter = TILE_UNITS / 6;
      heightScatter = TILE_UNITS / 6;
      break;
    default: DEBUG_ASSERT_TEXT(FALSE, "Weirdy destruction type effect");
      break;
    }

    /* Find a position to dump it at */
    pos.x = std::lrintf(psEffect->position.x) + widthScatter - rand() % (2 * widthScatter);
    pos.z = std::lrintf(psEffect->position.z) + breadthScatter - rand() % (2 * breadthScatter);
    pos.y = std::lrintf(psEffect->position.y) + height + rand() % heightScatter;

    if (psEffect->type == DESTRUCTION_TYPE_SKYSCRAPER)
      pos.y = std::lrintf(psEffect->position.y) + height;

    /* Choose an effect */
    effectType = rand() % 15;
    switch (effectType)
    {
    case 0:
      addEffect(&pos, EFFECT_SMOKE, SMOKE_TYPE_DRIFTING,FALSE, nullptr, 0);
      break;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
      if (psEffect->type == DESTRUCTION_TYPE_SKYSCRAPER)
        addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_LARGE,FALSE, nullptr, 0);
        /* Only structures get the big explosions */
      else if (psEffect->type == DESTRUCTION_TYPE_STRUCTURE)
        addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_MEDIUM,FALSE, nullptr, 0);
      else
        addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_SMALL,FALSE, nullptr, 0);
      break;
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
      if (psEffect->type == DESTRUCTION_TYPE_STRUCTURE)
        addEffect(&pos, EFFECT_GRAVITON, GRAVITON_TYPE_EMITTING_ST,TRUE, getRandomDebrisImd(), 0);
      else
        addEffect(&pos, EFFECT_GRAVITON, GRAVITON_TYPE_EMITTING_DR,TRUE, getRandomDebrisImd(), 0);
      break;
    case 11:
      addEffect(&pos, EFFECT_SMOKE, SMOKE_TYPE_DRIFTING,FALSE, nullptr, 0);
      break;
    case 12:
    case 13:
      addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_SMALL,FALSE, nullptr, 0);
      break;
    case 14:
      /* Add sound effect, but only if we're less than 3/4 of the way thru' destruction */
      if (gameTime < ((3 * (psEffect->birthTime + psEffect->lifeSpan) / 4)))
      {
        iX = std::lrintf(psEffect->position.x);
        iY = std::lrintf(psEffect->position.z);
        AudioSystem::PlayStaticTrack(iX, iY, ID_SOUND_EXPLOSION);
      }
      break;
    }
  }
}

// ----------------------------------------------------------------------------------------
/* 
updateConstruction:-
Moves the construction graphic about - dust cloud or whatever....
*/
void updateConstruction(EFFECT* psEffect)
{
  /* Time to update the frame number on the construction sprite */
  if (gameTime - psEffect->lastFrame > psEffect->frameDelay)
  {
    psEffect->lastFrame = gameTime;
    /* Are we on the last frame? */
    if (++psEffect->frameNumber >= EffectGetNumFrames(psEffect))
    {
      /* Is it a cyclic sprite? */
      if (TEST_CYCLIC(psEffect))
        psEffect->frameNumber = 0;
      else
      {
        KILL_EFFECT(psEffect);
        return;
      }
    }
  }

  /* Move it about in the world */
  DirectX::XMStoreFloat3(&psEffect->position,
    DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&psEffect->position),
                         DirectX::XMVectorScale(DirectX::XMLoadFloat3(&psEffect->velocity), fraction)));

  /* If it doesn't get killed by frame number, then by height */
  if (TEST_CYCLIC(psEffect))
  {
    /* Has it hit the ground */
    if (static_cast<UDWORD>(std::lrintf(psEffect->position.y)) <= map_Height(static_cast<UDWORD>(std::lrintf(psEffect->position.x)),
                                                                         static_cast<UDWORD>(std::lrintf(psEffect->position.z))))
    {
      KILL_EFFECT(psEffect);
      return;
    }

    if (gameTime - psEffect->birthTime > psEffect->lifeSpan) { KILL_EFFECT(psEffect); }
  }
}

// ----------------------------------------------------------------------------------------
/* Update fire sequences */
void updateFire(EFFECT* psEffect)
{
  iVector pos;
  LIGHT light;

  UDWORD percent = PERCENT(gameTime-psEffect->birthTime, psEffect->lifeSpan);
  if (percent > 100)
    percent = 100;
#ifdef DOLIGHTS
  light.position.x = std::lrintf(psEffect->position.x);
  light.position.y = std::lrintf(psEffect->position.y);
  light.position.z = std::lrintf(psEffect->position.z);
  light.range = (percent * psEffect->radius * 3) / 100;
  light.colour = LIGHT_RED;
  processLight(&light);
#endif

  /* Time to update the frame number on the construction sprite */
  if (gameTime - psEffect->lastFrame > psEffect->frameDelay)
  {
    psEffect->lastFrame = gameTime;
    pos.x = (std::lrintf(psEffect->position.x) + ((rand() % psEffect->radius) - (rand() % (2 * psEffect->radius))));
    pos.z = (std::lrintf(psEffect->position.z) + ((rand() % psEffect->radius) - (rand() % (2 * psEffect->radius))));
    pos.y = map_Height(pos.x, pos.z);

    if (psEffect->type == FIRE_TYPE_SMOKY_BLUE)
      addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_FLAMETHROWER,FALSE, nullptr, 0);
    else
      addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_SMALL,FALSE, nullptr, 0);

    if (psEffect->type == FIRE_TYPE_SMOKY OR psEffect->type == FIRE_TYPE_SMOKY_BLUE)
    {
      pos.x = (std::lrintf(psEffect->position.x) + ((rand() % psEffect->radius / 2) - (rand() % (2 * psEffect->radius / 2))));
      pos.z = (std::lrintf(psEffect->position.z) + ((rand() % psEffect->radius / 2) - (rand() % (2 * psEffect->radius / 2))));
      pos.y = map_Height(pos.x, pos.z);
      addEffect(&pos, EFFECT_SMOKE, SMOKE_TYPE_DRIFTING_HIGH,FALSE, nullptr, 0);
    }
    else
    {
      pos.x = (std::lrintf(psEffect->position.x) + ((rand() % psEffect->radius) - (rand() % (2 * psEffect->radius))));
      pos.z = (std::lrintf(psEffect->position.z) + ((rand() % psEffect->radius) - (rand() % (2 * psEffect->radius))));
      pos.y = map_Height(pos.x, pos.z);
      addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_SMALL,FALSE, nullptr, 0);
    }
  }

  if (gameTime - psEffect->birthTime > psEffect->lifeSpan) { KILL_EFFECT(psEffect); }
}

// ----------------------------------------------------------------------------------------
// ALL THE RENDER FUNCTIONS
// ----------------------------------------------------------------------------------------
/* 
renderEffect:-
Calls the appropriate render routine for each type of effect 
*/
void renderEffect(EFFECT* psEffect)
{
  /* What type of effect are we dealing with? */
  switch (psEffect->group)
  {
  case EFFECT_WAYPOINT:
    renderWaypointEffect(psEffect);
    break;

  case EFFECT_EXPLOSION:
    renderExplosionEffect(psEffect);
    break;

  case EFFECT_CONSTRUCTION:
    renderConstructionEffect(psEffect);
    break;

  case EFFECT_SMOKE:
    renderSmokeEffect(psEffect);
    break;

  case EFFECT_GRAVITON:
    renderGravitonEffect(psEffect);
    break;

  case EFFECT_BLOOD:
    renderBloodEffect(psEffect);
    break;

  case EFFECT_STRUCTURE:
    break;

  case EFFECT_DESTRUCTION:
    /*	There is no display func for a destruction effect - 
      it merely spawn other effects over time */
    renderDestructionEffect(psEffect);
    break;
  case EFFECT_FIRE:
    /* Likewise */
    break;
  case EFFECT_SAT_LASER:
    /* Likewise */
    break;
  case EFFECT_FIREWORK:
    renderFirework(psEffect);
    break;
  default: Neuron::Fatal("Weirdy class of effect passed to renderEffect");
    break;
  }
}

// ----------------------------------------------------------------------------------------
/* drawing func for wapypoints . AJL. */
void renderWaypointEffect(EFFECT* psEffect)
{
  iVector dv;
  UDWORD specular;

  dv.x = (static_cast<UDWORD>(std::lrintf(psEffect->position.x)) - player.p.x) - terrainMidX * TILE_UNITS;
  dv.y = static_cast<UDWORD>(std::lrintf(psEffect->position.y));
  dv.z = terrainMidY * TILE_UNITS - (static_cast<UDWORD>(std::lrintf(psEffect->position.z)) - player.p.z);
  Neuron::MatrixPush(); /* Push the indentity matrix */
  Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(static_cast<float>(dv.x), static_cast<float>(dv.y), static_cast<float>(dv.z)) * Neuron::WorldMatrix();
  SDWORD rx = player.p.x & (TILE_UNITS - 1); /* Get the x,z translation components */
  SDWORD rz = player.p.z & (TILE_UNITS - 1);
  Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(static_cast<float>(rx), 0.0f, static_cast<float>(-rz)) * Neuron::WorldMatrix(); /* Translate */

  // set up lighting
  UDWORD brightness = lightDoFogAndIllumination(pie_MAX_BRIGHT_LEVEL, getCentreX() - std::lrintf(psEffect->position.x),
                                                getCentreZ() - std::lrintf(psEffect->position.z), &specular);

  pie_Draw3DShape(psEffect->imd, 0, 0, brightness, specular, 0, 0);
  Neuron::MatrixPop();
}

// ----------------------------------------------------------------------------------------
void renderFirework(EFFECT* psEffect)
{
  iVector dv;
  UDWORD specular;

  /* these don't get rendered */
  if (psEffect->type == FIREWORK_TYPE_LAUNCHER)
    return;

  dv.x = (static_cast<UDWORD>(std::lrintf(psEffect->position.x)) - player.p.x) - terrainMidX * TILE_UNITS;
  dv.y = static_cast<UDWORD>(std::lrintf(psEffect->position.y));
  dv.z = terrainMidY * TILE_UNITS - (static_cast<UDWORD>(std::lrintf(psEffect->position.z)) - player.p.z);
  Neuron::MatrixPush(); /* Push the indentity matrix */
  Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(static_cast<float>(dv.x), static_cast<float>(dv.y), static_cast<float>(dv.z)) * Neuron::WorldMatrix();
  SDWORD rx = player.p.x & (TILE_UNITS - 1); /* Get the x,z translation components */
  SDWORD rz = player.p.z & (TILE_UNITS - 1);
  Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(static_cast<float>(rx), 0.0f, static_cast<float>(-rz)) * Neuron::WorldMatrix(); /* Translate */

  Neuron::WorldMatrix() = DirectX::XMMatrixRotationY(-player.r.y * Neuron::RadiansPerWorldAngle) * Neuron::WorldMatrix();
  Neuron::WorldMatrix() = DirectX::XMMatrixRotationX(-player.r.x * Neuron::RadiansPerWorldAngle) * Neuron::WorldMatrix();

  UDWORD brightness = lightDoFogAndIllumination(pie_MAX_BRIGHT_LEVEL, getCentreX() - std::lrintf(psEffect->position.x),
                                                getCentreZ() - std::lrintf(psEffect->position.z), &specular);

  scaleMatrix(psEffect->size);
  pie_Draw3DShape(psEffect->imd, psEffect->frameNumber, 0, brightness, 0, pie_ADDITIVE, EFFECT_EXPLOSION_ADDITIVE);
  Neuron::MatrixPop();
}

// ----------------------------------------------------------------------------------------
/* drawing func for blood. */
void renderBloodEffect(EFFECT* psEffect)
{
  iVector dv;
  UDWORD specular;

  dv.x = (static_cast<UDWORD>(std::lrintf(psEffect->position.x)) - player.p.x) - terrainMidX * TILE_UNITS;
  dv.y = static_cast<UDWORD>(std::lrintf(psEffect->position.y));
  dv.z = terrainMidY * TILE_UNITS - (static_cast<UDWORD>(std::lrintf(psEffect->position.z)) - player.p.z);
  Neuron::MatrixPush(); /* Push the indentity matrix */
  Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(static_cast<float>(dv.x), static_cast<float>(dv.y), static_cast<float>(dv.z)) * Neuron::WorldMatrix();
  SDWORD rx = player.p.x & (TILE_UNITS - 1); /* Get the x,z translation components */
  SDWORD rz = player.p.z & (TILE_UNITS - 1);
  Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(static_cast<float>(rx), 0.0f, static_cast<float>(-rz)) * Neuron::WorldMatrix(); /* Translate */
  Neuron::WorldMatrix() = DirectX::XMMatrixRotationY(-player.r.y * Neuron::RadiansPerWorldAngle) * Neuron::WorldMatrix();
  Neuron::WorldMatrix() = DirectX::XMMatrixRotationX(-player.r.x * Neuron::RadiansPerWorldAngle) * Neuron::WorldMatrix();
  scaleMatrix(psEffect->size);

  // set up lighting
  UDWORD brightness = lightDoFogAndIllumination(pie_MAX_BRIGHT_LEVEL, getCentreX() - std::lrintf(psEffect->position.x),
                                                getCentreZ() - std::lrintf(psEffect->position.z), &specular);

  pie_Draw3DShape(getImdFromIndex(MI_BLOOD), psEffect->frameNumber, 0, brightness, specular, pie_TRANSLUCENT, EFFECT_BLOOD_TRANSPARENCY);
  Neuron::MatrixPop();
}

// ----------------------------------------------------------------------------------------
void renderDestructionEffect(EFFECT* psEffect)
{
  iVector dv;
  SDWORD percent;
  UDWORD specular;

  if (psEffect->type != DESTRUCTION_TYPE_SKYSCRAPER)
    return;

  dv.x = (static_cast<UDWORD>(std::lrintf(psEffect->position.x)) - player.p.x) - terrainMidX * TILE_UNITS;
  dv.y = static_cast<UDWORD>(std::lrintf(psEffect->position.y));
  dv.z = terrainMidY * TILE_UNITS - (static_cast<UDWORD>(std::lrintf(psEffect->position.z)) - player.p.z);
  Neuron::MatrixPush(); /* Push the indentity matrix */
  Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(static_cast<float>(dv.x), static_cast<float>(dv.y), static_cast<float>(dv.z)) * Neuron::WorldMatrix();
  SDWORD rx = player.p.x & (TILE_UNITS - 1); /* Get the x,z translation components */
  SDWORD rz = player.p.z & (TILE_UNITS - 1);
  Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(static_cast<float>(rx), 0.0f, static_cast<float>(-rz)) * Neuron::WorldMatrix(); /* Translate */

  float div = static_cast<float>(gameTime - psEffect->birthTime) / psEffect->lifeSpan;
  if (div > 1.0)
    div = 1.0; //temporary!
  {
    div = 1.0 - div;
    percent = static_cast<SDWORD>(div * pie_RAISE_SCALE);
  }

  //get fog value
  UDWORD brightness = lightDoFogAndIllumination(pie_MAX_BRIGHT_LEVEL, getCentreX() - std::lrintf(psEffect->position.x),
                                                getCentreZ() - std::lrintf(psEffect->position.z), &specular);

  if (!gamePaused())
  {
    Neuron::WorldMatrix() = DirectX::XMMatrixRotationX(SKY_SHIMMY * Neuron::RadiansPerWorldAngle) * Neuron::WorldMatrix();
    Neuron::WorldMatrix() = DirectX::XMMatrixRotationY(SKY_SHIMMY * Neuron::RadiansPerWorldAngle) * Neuron::WorldMatrix();
    Neuron::WorldMatrix() = DirectX::XMMatrixRotationZ(SKY_SHIMMY * Neuron::RadiansPerWorldAngle) * Neuron::WorldMatrix();
  }
  pie_Draw3DShape(psEffect->imd, 0, 0, brightness, 0,pie_RAISE, percent);

  Neuron::MatrixPop();
}

// ----------------------------------------------------------------------------------------
BOOL rejectLandLight(LAND_LIGHT_SPEC type)
{
  UDWORD timeSlice = gameTime % 2000;
  if (timeSlice < 400)
  {
    if (type == LL_MIDDLE)
      return (FALSE);
    return (TRUE);
    // reject all expect middle
  }
  if (timeSlice < 800)
  {
    if (type == LL_OUTER)
      return (TRUE);
    return (FALSE);
    // reject only outer
  }
  if (timeSlice < 1200)
    return (FALSE); //reject none
  if (timeSlice < 1600)
  {
    if (type == LL_OUTER)
      return (TRUE);
    return (FALSE);
    // reject only outer
  }
  if (type == LL_MIDDLE)
    return (FALSE);
  return (TRUE);
  // reject all expect middle
}

// ----------------------------------------------------------------------------------------
/* Renders the standard explosion effect */
void renderExplosionEffect(EFFECT* psEffect)
{
  iVector dv;
  SDWORD percent;
  UDWORD specular;
  UDWORD timeSlice;

  if (psEffect->type == EXPLOSION_TYPE_LAND_LIGHT)
  {
    if (rejectLandLight(static_cast<LAND_LIGHT_SPEC>(psEffect->specific)))
      return;
  }

  dv.x = (static_cast<UDWORD>(std::lrintf(psEffect->position.x)) - player.p.x) - terrainMidX * TILE_UNITS;
  dv.y = static_cast<UDWORD>(std::lrintf(psEffect->position.y));
  dv.z = terrainMidY * TILE_UNITS - (static_cast<UDWORD>(std::lrintf(psEffect->position.z)) - player.p.z);
  Neuron::MatrixPush(); /* Push the indentity matrix */
  Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(static_cast<float>(dv.x), static_cast<float>(dv.y), static_cast<float>(dv.z)) * Neuron::WorldMatrix();
  SDWORD rx = player.p.x & (TILE_UNITS - 1); /* Get the x,z translation components */
  SDWORD rz = player.p.z & (TILE_UNITS - 1);
  Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(static_cast<float>(rx), 0.0f, static_cast<float>(-rz)) * Neuron::WorldMatrix(); /* Translate */

  /* Bit in comments - doesn't quite work yet? */
  if (TEST_FACING(psEffect))
  {
    /* Always face the viewer! */
    /*		TEST_FLIPPED_Y(psEffect) ? pie_MatRotY(-player.r.y+iV_DEG(180)) :*/
    Neuron::WorldMatrix() = DirectX::XMMatrixRotationY(-player.r.y * Neuron::RadiansPerWorldAngle) * Neuron::WorldMatrix();
    /*		TEST_FLIPPED_X(psEffect) ? pie_MatRotX(-player.r.x+iV_DEG(180)) :*/
    Neuron::WorldMatrix() = DirectX::XMMatrixRotationX(-player.r.x * Neuron::RadiansPerWorldAngle) * Neuron::WorldMatrix();
  }

  /* Tesla explosions diminish in size */
  if (psEffect->type == EXPLOSION_TYPE_TESLA)
  {
    percent = std::lrintf(PERCENT((gameTime - psEffect->birthTime), psEffect->lifeSpan));
    if (percent < 0)
      percent = 0;
    if (percent > 45)
      percent = 45;
    scaleMatrix(psEffect->size - percent);
  }
  else if (psEffect->type == EXPLOSION_TYPE_PLASMA)
  {
    percent = (std::lrintf(PERCENT((gameTime - psEffect->birthTime), psEffect->lifeSpan))) / 3;
    scaleMatrix(BASE_PLASMA_SIZE + percent);
  }
  else
    scaleMatrix(psEffect->size);
  //get fog value
  UDWORD brightness = lightDoFogAndIllumination(pie_MAX_BRIGHT_LEVEL, getCentreX() - std::lrintf(psEffect->position.x),
                                                getCentreZ() - std::lrintf(psEffect->position.z), &specular);

  if (psEffect->type == EXPLOSION_TYPE_PLASMA)
    pie_Draw3DShape(psEffect->imd, psEffect->frameNumber, 0, brightness, 0, pie_ADDITIVE, EFFECT_PLASMA_ADDITIVE);
  else if (psEffect->type == EXPLOSION_TYPE_KICKUP)
  {
    /* not transparent */
    pie_Draw3DShape(psEffect->imd, psEffect->frameNumber, 0, pie_TRANSLUCENT, 128, 0, 0);
  }
  else
    pie_Draw3DShape(psEffect->imd, psEffect->frameNumber, 0, brightness, 0, pie_ADDITIVE, EFFECT_EXPLOSION_ADDITIVE);

  Neuron::MatrixPop();
}

// ----------------------------------------------------------------------------------------
void renderGravitonEffect(EFFECT* psEffect)
{
  iVector vec;
  UDWORD specular;

  /* Establish world position */
  vec.x = (static_cast<UDWORD>(std::lrintf(psEffect->position.x)) - player.p.x) - terrainMidX * TILE_UNITS;
  vec.y = static_cast<UDWORD>(std::lrintf(psEffect->position.y));
  vec.z = terrainMidY * TILE_UNITS - (static_cast<UDWORD>(std::lrintf(psEffect->position.z)) - player.p.z);

  /* Push matrix */
  Neuron::MatrixPush();

  /* Move to position */
  Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(static_cast<float>(vec.x), static_cast<float>(vec.y), static_cast<float>(vec.z)) * Neuron::WorldMatrix();

  /* Offset from camera */
  SDWORD rx = player.p.x & (TILE_UNITS - 1);
  SDWORD rz = player.p.z & (TILE_UNITS - 1);

  /* Move to camera reference */
  Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(static_cast<float>(rx), 0.0f, static_cast<float>(-rz)) * Neuron::WorldMatrix();

  Neuron::WorldMatrix() = DirectX::XMMatrixRotationX(psEffect->rotation.x * Neuron::RadiansPerWorldAngle) * Neuron::WorldMatrix();
  Neuron::WorldMatrix() = DirectX::XMMatrixRotationY(psEffect->rotation.y * Neuron::RadiansPerWorldAngle) * Neuron::WorldMatrix();
  Neuron::WorldMatrix() = DirectX::XMMatrixRotationZ(psEffect->rotation.z * Neuron::RadiansPerWorldAngle) * Neuron::WorldMatrix();

  /* Buildings emitted by gravitons are chunkier */
  if (psEffect->type == GRAVITON_TYPE_EMITTING_ST)
  {
    /* Twice as big - 150 percent */
    scaleMatrix(psEffect->size);
  }
  else
    scaleMatrix(100);
  UDWORD brightness = lightDoFogAndIllumination(pie_MAX_BRIGHT_LEVEL, getCentreX() - std::lrintf(psEffect->position.x),
                                                getCentreZ() - std::lrintf(psEffect->position.z), &specular);

  pie_Draw3DShape(psEffect->imd, psEffect->frameNumber, 0, brightness, specular, 0, 0);

  /* Pop the matrix */
  Neuron::MatrixPop();
}

// ----------------------------------------------------------------------------------------
/* 
renderConstructionEffect:-
Renders the standard construction effect */
void renderConstructionEffect(EFFECT* psEffect)
{
  iVector vec, null;
  UDWORD translucency;
  UDWORD specular;

  /* No rotation about arbitrary axis */
  null.x = null.y = null.z = 0;

  /* Establish world position */
  vec.x = (static_cast<UDWORD>(std::lrintf(psEffect->position.x)) - player.p.x) - terrainMidX * TILE_UNITS;
  vec.y = static_cast<UDWORD>(std::lrintf(psEffect->position.y));
  vec.z = terrainMidY * TILE_UNITS - (static_cast<UDWORD>(std::lrintf(psEffect->position.z)) - player.p.z);

  /* Push matrix */
  Neuron::MatrixPush();

  /* Move to position */
  Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(static_cast<float>(vec.x), static_cast<float>(vec.y), static_cast<float>(vec.z)) * Neuron::WorldMatrix();

  /* Offset from camera */
  SDWORD rx = player.p.x & (TILE_UNITS - 1);
  SDWORD rz = player.p.z & (TILE_UNITS - 1);

  /* Move to camera reference */
  Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(static_cast<float>(rx), 0.0f, static_cast<float>(-rz)) * Neuron::WorldMatrix();

  /* Bit in comments doesn't quite work yet? */
  if (TEST_FACING(psEffect))
  {
    /*		TEST_FLIPPED_Y(psEffect) ? pie_MatRotY(-player.r.y+iV_DEG(180)) :*/
    Neuron::WorldMatrix() = DirectX::XMMatrixRotationY(-player.r.y * Neuron::RadiansPerWorldAngle) * Neuron::WorldMatrix();
    /*		TEST_FLIPPED_X(psEffect) ? pie_MatRotX(-player.r.x+iV_DEG(180)) :*/
    Neuron::WorldMatrix() = DirectX::XMMatrixRotationX(-player.r.x * Neuron::RadiansPerWorldAngle) * Neuron::WorldMatrix();
  }

  /* Scale size according to age */
  SDWORD percent = std::lrintf(PERCENT((gameTime - psEffect->birthTime), psEffect->lifeSpan));
  if (percent < 0)
    percent = 0;
  if (percent > 100)
    percent = 100;

  /* Make imds be transparent on 3dfx */
  if (percent < 50)
    translucency = percent * 2;
  else
    translucency = (100 - percent) * 2;
  translucency += 10;
  UDWORD size = 2 * translucency;
  if (size > 90)
    size = 90;
  scaleMatrix(size);

  // set up lighting
  UDWORD brightness = lightDoFogAndIllumination(pie_MAX_BRIGHT_LEVEL, getCentreX() - std::lrintf(psEffect->position.x),
                                                getCentreZ() - std::lrintf(psEffect->position.z), &specular);
  pie_Draw3DShape(psEffect->imd, psEffect->frameNumber, 0, brightness, specular, pie_TRANSLUCENT, static_cast<UBYTE>(translucency));

  /* Pop the matrix */
  Neuron::MatrixPop();
}

// ----------------------------------------------------------------------------------------
/*
renderSmokeEffect:-
Renders the standard smoke effect - it is now scaled in real-time as well 
*/
void renderSmokeEffect(EFFECT* psEffect)
{
  UDWORD transparency = 0;
  iVector vec;
  UDWORD specular;

  /* Establish world position */
  vec.x = (static_cast<UDWORD>(std::lrintf(psEffect->position.x)) - player.p.x) - terrainMidX * TILE_UNITS;
  vec.y = static_cast<UDWORD>(std::lrintf(psEffect->position.y));
  vec.z = terrainMidY * TILE_UNITS - (static_cast<UDWORD>(std::lrintf(psEffect->position.z)) - player.p.z);

  /* Push matrix */
  Neuron::MatrixPush();

  /* Move to position */
  Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(static_cast<float>(vec.x), static_cast<float>(vec.y), static_cast<float>(vec.z)) * Neuron::WorldMatrix();

  /* Offset from camera */
  SDWORD rx = player.p.x & (TILE_UNITS - 1);
  SDWORD rz = player.p.z & (TILE_UNITS - 1);

  /* Move to camera reference */
  Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(static_cast<float>(rx), 0.0f, static_cast<float>(-rz)) * Neuron::WorldMatrix();

  /* Bit in comments doesn't quite work yet? */
  if (TEST_FACING(psEffect))
  {
    /* Always face the viewer! */
    Neuron::WorldMatrix() = DirectX::XMMatrixRotationY(-player.r.y * Neuron::RadiansPerWorldAngle) * Neuron::WorldMatrix();
    Neuron::WorldMatrix() = DirectX::XMMatrixRotationX(-player.r.x * Neuron::RadiansPerWorldAngle) * Neuron::WorldMatrix();
  }

  /* Small smoke - used for the droids */

  if (TEST_SCALED(psEffect))
  {
    UDWORD percent;
#ifdef HARDWARE_TEST//test additive
    percent = (std::lrintf(PERCENT((gameTime - psEffect->birthTime), psEffect->lifeSpan))); if (percent < 10 AND psEffect->type ==
      SMOKE_TYPE_TRAIL)
    {
      scaleMatrix((3 * percent / 10 * psEffect->baseScale) / 100);
      transparency = (EFFECT_SMOKE_ADDITIVE * (100 - 10)) / 100;
    }
    else
    {
      scaleMatrix((4 * percent * psEffect->baseScale) / 100);
      transparency = (EFFECT_SMOKE_ADDITIVE * (100 - percent)) / 100;
    }
#else//Constant alpha
    percent = (std::lrintf(PERCENT((gameTime - psEffect->birthTime), psEffect->lifeSpan)));
    scaleMatrix(percent + psEffect->baseScale);
    transparency = (EFFECT_SMOKE_TRANSPARENCY * (100 - percent)) / 100;
#endif
  }

  // set up lighting
  UDWORD brightness = lightDoFogAndIllumination(pie_MAX_BRIGHT_LEVEL, getCentreX() - std::lrintf(psEffect->position.x),
                                                getCentreZ() - std::lrintf(psEffect->position.z), &specular);

  transparency = (transparency * 3) / 2; //JPS smoke strength increased for d3d 12 may 99

  /* Make imds be transparent on 3dfx */
  if (psEffect->type == SMOKE_TYPE_STEAM)
    pie_Draw3DShape(psEffect->imd, psEffect->frameNumber, 0, brightness, specular, pie_TRANSLUCENT,
                    static_cast<UBYTE>((EFFECT_STEAM_TRANSPARENCY)) / 2);
  else
  {
    if (psEffect->type == SMOKE_TYPE_TRAIL)
      pie_Draw3DShape(psEffect->imd, psEffect->frameNumber, 0, brightness, specular, pie_TRANSLUCENT,
                      static_cast<UBYTE>((2 * transparency) / 3));
    else
      pie_Draw3DShape(psEffect->imd, psEffect->frameNumber, 0, brightness, specular, pie_TRANSLUCENT, static_cast<UBYTE>(transparency) / 2);
  }

  /* Pop the matrix */
  Neuron::MatrixPop();
}

// ----------------------------------------------------------------------------------------
// ALL THE SETUP FUNCTIONS
// ----------------------------------------------------------------------------------------
void effectSetUpFirework(EFFECT* psEffect)
{
  if (psEffect->type == FIREWORK_TYPE_LAUNCHER)
  {
    psEffect->velocity.x = 200 - rand() % 400;
    psEffect->velocity.z = 200 - rand() % 400;
    psEffect->velocity.y = 400 + rand() % 200; //height
    psEffect->lifeSpan = GAME_TICKS_PER_SEC * 3;
    psEffect->radius = 80 + rand() % 150;
    UDWORD camExtra = 0;
    if (getCampaignNumber() != 1)
      camExtra += rand() % 200;
    psEffect->size = 300 + rand() % 300; //height it goes off
    psEffect->imd = getImdFromIndex(MI_FIREWORK); // not actually drawn
  }
  else
  {
    psEffect->velocity.x = 20 - rand() % 40;
    psEffect->velocity.z = 20 - rand() % 40;
    psEffect->velocity.y = 0 - (20 + rand() % 40); //height
    psEffect->lifeSpan = GAME_TICKS_PER_SEC * 4;

    /* setup the imds */
    switch (rand() % 3)
    {
    case 0:
      psEffect->imd = getImdFromIndex(MI_FIREWORK);
      psEffect->size = 45; //size of graphic
      break;
    case 1:
      psEffect->imd = getImdFromIndex(MI_SNOW);
      SET_CYCLIC(psEffect);
      psEffect->size = 60; //size of graphic

      break;
    default:
      psEffect->imd = getImdFromIndex(MI_FLAME);
      psEffect->size = 40; //size of graphic

      break;
    }
  }

  psEffect->frameDelay = (EXPLOSION_FRAME_DELAY * 2);
}

// ----------------------------------------------------------------------------------------
void effectSetupSmoke(EFFECT* psEffect)
{
  /* everything except steam drifts about */
  if (psEffect->type == SMOKE_TYPE_STEAM)
  {
    /* Only upwards */
    psEffect->velocity.x = 0.0f;
    psEffect->velocity.z = 0.0f;
  }
  else if (psEffect->type == SMOKE_TYPE_BILLOW)
  {
    psEffect->velocity.x = static_cast<float>(10-rand()%20);
    psEffect->velocity.z = static_cast<float>(10-rand()%20);
  }
  else
  {
    psEffect->velocity.x = static_cast<float>(rand()%20);
    psEffect->velocity.z = static_cast<float>(10-rand()%20);
  }

  /* Steam isn't cyclic  - it doesn't grow with time either */
  if (psEffect->type != SMOKE_TYPE_STEAM)
  {
    SET_CYCLIC(psEffect);
    SET_SCALED(psEffect);
  }

  switch (psEffect->type)
  {
  case SMOKE_TYPE_DRIFTING:
    psEffect->imd = getImdFromIndex(MI_SMALL_SMOKE);
    psEffect->lifeSpan = static_cast<UWORD>(NORMAL_SMOKE_LIFESPAN);
    psEffect->velocity.y = static_cast<float>(35+rand()%30);
    psEffect->baseScale = 40;
    break;
  case SMOKE_TYPE_DRIFTING_HIGH:
    psEffect->imd = getImdFromIndex(MI_SMALL_SMOKE);
    psEffect->lifeSpan = static_cast<UWORD>(NORMAL_SMOKE_LIFESPAN);
    psEffect->velocity.y = static_cast<float>(40+rand()%45);
    psEffect->baseScale = 25;
    break;
  case SMOKE_TYPE_DRIFTING_SMALL:
    psEffect->imd = getImdFromIndex(MI_SMALL_SMOKE);
    psEffect->lifeSpan = static_cast<UWORD>(SMALL_SMOKE_LIFESPAN);
    psEffect->velocity.y = static_cast<float>(25+rand()%35);
    psEffect->baseScale = 17;
    break;
  case SMOKE_TYPE_BILLOW:
    psEffect->imd = getImdFromIndex(MI_SMALL_SMOKE);
    psEffect->lifeSpan = static_cast<UWORD>(SMALL_SMOKE_LIFESPAN);
    psEffect->velocity.y = static_cast<float>(10+rand()%20);
    psEffect->baseScale = 80;
    break;
  case SMOKE_TYPE_STEAM:
    psEffect->imd = getImdFromIndex(MI_SMALL_STEAM);
    psEffect->velocity.y = static_cast<float>(rand()%5);
    break;
  case SMOKE_TYPE_TRAIL:
    psEffect->imd = getImdFromIndex(MI_TRAIL);
    psEffect->lifeSpan = TRAIL_SMOKE_LIFESPAN;
    psEffect->velocity.y = static_cast<float>(5+rand()%10);
    psEffect->baseScale = 25;
    break;
  default: DEBUG_ASSERT_TEXT(FALSE, "Weird smoke type");
    break;
  }

  /* It always faces you */
  SET_FACING(psEffect);

  psEffect->frameDelay = static_cast<UWORD>(SMOKE_FRAME_DELAY);
  /* Randomly flip gfx for variation */
  if (ONEINTWO)
    SET_FLIPPED_X(psEffect);
  if (ONEINTWO)
    SET_FLIPPED_Y(psEffect);
}

// ----------------------------------------------------------------------------------------
void effectSetUpSatLaser(EFFECT* psEffect)
{
  /* Does nothing at all..... Runs only for one frame! */
  psEffect->baseScale = 1;
}

// ----------------------------------------------------------------------------------------
void effectSetupGraviton(EFFECT* psEffect)
{
  switch (psEffect->type)
  {
  case GRAVITON_TYPE_GIBLET:
    psEffect->velocity.x = GIBLET_INIT_VEL_X;
    psEffect->velocity.z = GIBLET_INIT_VEL_Z;
    psEffect->velocity.y = GIBLET_INIT_VEL_Y;
    break;
  case GRAVITON_TYPE_EMITTING_ST:
    psEffect->velocity.x = GRAVITON_INIT_VEL_X;
    psEffect->velocity.z = GRAVITON_INIT_VEL_Z;
    psEffect->velocity.y = (5 * GRAVITON_INIT_VEL_Y) / 4;
    psEffect->size = static_cast<UWORD>(120 + rand() % 30);
    break;
  case GRAVITON_TYPE_EMITTING_DR:
    psEffect->velocity.x = GRAVITON_INIT_VEL_X / 2;
    psEffect->velocity.z = GRAVITON_INIT_VEL_Z / 2;
    psEffect->velocity.y = GRAVITON_INIT_VEL_Y;
    break;
  default: DEBUG_ASSERT_TEXT(FALSE, "Weirdy type of graviton");
    break;
  }

  psEffect->rotation.x = DEG((rand()%360));
  psEffect->rotation.z = DEG((rand()%360));
  psEffect->rotation.y = DEG((rand()%360));

  psEffect->spin.x = DEG((rand()%100)+20);
  psEffect->spin.z = DEG((rand()%100)+20);
  psEffect->spin.y = DEG((rand()%100)+20);

  /* Gravitons are essential */
  SET_ESSENTIAL(psEffect);

  if (psEffect->type == GRAVITON_TYPE_GIBLET)
    psEffect->frameDelay = static_cast<UWORD>(GRAVITON_BLOOD_DELAY);
  else
    psEffect->frameDelay = static_cast<UWORD>(GRAVITON_FRAME_DELAY);
}

// ----------------------------------------------------------------------------------------
void effectSetupExplosion(EFFECT* psEffect)
{
  /* Get an imd if it's not established */
  if (psEffect->imd == nullptr)
  {
    switch (psEffect->type)
    {
    case EXPLOSION_TYPE_SMALL:
      psEffect->imd = getImdFromIndex(MI_EXPLOSION_SMALL);
      psEffect->size = static_cast<UBYTE>((6 * EXPLOSION_SIZE) / 5);
      break;
    case EXPLOSION_TYPE_VERY_SMALL:
      psEffect->imd = getImdFromIndex(MI_EXPLOSION_SMALL);
      psEffect->size = static_cast<UBYTE>((BASE_FLAME_SIZE + auxVar));
      break;
    case EXPLOSION_TYPE_MEDIUM:
      psEffect->imd = getImdFromIndex(MI_EXPLOSION_MEDIUM);
      psEffect->size = static_cast<UBYTE>(EXPLOSION_SIZE);
      break;
    case EXPLOSION_TYPE_LARGE:
      psEffect->imd = getImdFromIndex(MI_EXPLOSION_MEDIUM);
      psEffect->size = static_cast<UBYTE>(EXPLOSION_SIZE) * 2;
      break;
    case EXPLOSION_TYPE_FLAMETHROWER:
      psEffect->imd = getImdFromIndex(MI_FLAME);
      psEffect->size = static_cast<UBYTE>((BASE_FLAME_SIZE + auxVar));
      break;
    case EXPLOSION_TYPE_LASER:
      psEffect->imd = getImdFromIndex(MI_FLAME); // change this
      psEffect->size = static_cast<UBYTE>((BASE_LASER_SIZE + auxVar));
      break;
    case EXPLOSION_TYPE_DISCOVERY:
      psEffect->imd = getImdFromIndex(MI_TESLA); // change this
      psEffect->size = DISCOVERY_SIZE;
      break;
    case EXPLOSION_TYPE_FLARE:
      psEffect->imd = getImdFromIndex(MI_MFLARE);
      psEffect->size = FLARE_SIZE;
      break;
    case EXPLOSION_TYPE_TESLA:
      psEffect->imd = getImdFromIndex(MI_TESLA);
      psEffect->size = TESLA_SIZE;
      psEffect->velocity.y = static_cast<float>(TESLA_SPEED);
      break;
    case EXPLOSION_TYPE_KICKUP:
      psEffect->imd = getImdFromIndex(MI_KICK);
      psEffect->size = 100;
      break;
    case EXPLOSION_TYPE_PLASMA:
      psEffect->imd = getImdFromIndex(MI_PLASMA);
      psEffect->size = BASE_PLASMA_SIZE;
      psEffect->velocity.y = 0.0f;
      break;
    case EXPLOSION_TYPE_LAND_LIGHT:
      psEffect->imd = getImdFromIndex(MI_LANDING);
      psEffect->size = 120;
      psEffect->specific = ellSpec;
      psEffect->velocity.y = 0.0f;
      SET_ESSENTIAL(psEffect); // Landing lights are permanent and cyclic
      break;
    case EXPLOSION_TYPE_SHOCKWAVE:
      psEffect->imd = getImdFromIndex(MI_SHOCK); //resGetData("IMD","blbhq.pie");
      psEffect->size = 50;
      psEffect->velocity.y = 0.0f;
      break;
    default:
      break;
    }
  }

  if (psEffect->type == EXPLOSION_TYPE_FLAMETHROWER)
    psEffect->frameDelay = 45;
    /* Set how long it lasts */
  else if (psEffect->type == EXPLOSION_TYPE_LASER)
    psEffect->frameDelay = static_cast<UWORD>((EXPLOSION_FRAME_DELAY / 2));
  else if (psEffect->type == EXPLOSION_TYPE_TESLA)
    psEffect->frameDelay = EXPLOSION_TESLA_FRAME_DELAY;
  else if (psEffect->type == EXPLOSION_TYPE_PLASMA)
    psEffect->frameDelay = EXPLOSION_PLASMA_FRAME_DELAY;
  else if (psEffect->type == EXPLOSION_TYPE_LAND_LIGHT)
    psEffect->frameDelay = 120;
  else
    psEffect->frameDelay = static_cast<UWORD>(EXPLOSION_FRAME_DELAY);

  if (psEffect->type == EXPLOSION_TYPE_SHOCKWAVE)
    psEffect->lifeSpan = GAME_TICKS_PER_SEC;
  else
    psEffect->lifeSpan = (psEffect->frameDelay * psEffect->imd->numFrames);

  if ((psEffect->type != EXPLOSION_TYPE_NOT_FACING) AND (psEffect->type != EXPLOSION_TYPE_SHOCKWAVE))
    SET_FACING(psEffect);
  /* Randomly flip x and y for variation */
  if (ONEINTWO)
    SET_FLIPPED_X(psEffect);
  if (ONEINTWO)
    SET_FLIPPED_Y(psEffect);
}

// ----------------------------------------------------------------------------------------
void effectSetupConstruction(EFFECT* psEffect)
{
  psEffect->velocity.x = 0.0f; //(1-rand()%3);
  psEffect->velocity.z = 0.0f; //(1-rand()%3);
  psEffect->velocity.y = static_cast<float>(0-rand()%3);
  psEffect->frameDelay = static_cast<UWORD>(CONSTRUCTION_FRAME_DELAY);
  psEffect->imd = getImdFromIndex(MI_CONSTRUCTION);
  psEffect->lifeSpan = CONSTRUCTION_LIFESPAN;

  /* These effects always face you */
  SET_FACING(psEffect);

  /* It's a cyclic anim - dies on age */
  SET_CYCLIC(psEffect);

  /* Randomly flip the construction graphics in x and y for variation */
  if (ONEINTWO)
    SET_FLIPPED_X(psEffect);
  if (ONEINTWO)
    SET_FLIPPED_Y(psEffect);
}

// ----------------------------------------------------------------------------------------
#if (0)
void effectSetupDust(EFFECT* psEffect)
{
  psEffect->velocity.x = 0.0f; //(1-rand()%3);
  psEffect->velocity.z = 0.0f; //(1-rand()%3);
  psEffect->velocity.y = static_cast<float>(0-rand()%3);
  psEffect->frameDelay = (UWORD)CONSTRUCTION_FRAME_DELAY;
  psEffect->imd = getImdFromIndex(MI_BLOOD);
  psEffect->lifeSpan = CONSTRUCTION_LIFESPAN;

  /* These effects always face you */
  SET_FACING(psEffect);

  /* It's a cyclic anim - dies on age */
  SET_CYCLIC(psEffect);

  /* Randomly flip the construction graphics in x and y for variation */
  if (ONEINTWO) { SET_FLIPPED_X(psEffect); }
  if (ONEINTWO) { SET_FLIPPED_Y(psEffect); }
}
#endif

void effectSetupFire(EFFECT* psEffect)
{
  psEffect->frameDelay = 300; // needs to be investigated...
  psEffect->radius = auxVar; // needs to be investigated
  psEffect->lifeSpan = static_cast<UWORD>(auxVarSec);
  psEffect->birthTime = gameTime;
  SET_ESSENTIAL(psEffect);
}

// ----------------------------------------------------------------------------------------
void effectSetupWayPoint(EFFECT* psEffect)
{
  psEffect->imd = pProximityMsgIMD;

  /* These effects musnt make way for others */
  SET_ESSENTIAL(psEffect);
}

// ----------------------------------------------------------------------------------------
void effectSetupBlood(EFFECT* psEffect)
{
  psEffect->frameDelay = BLOOD_FRAME_DELAY;
  psEffect->velocity.y = static_cast<float>(BLOOD_FALL_SPEED);
  psEffect->imd = getImdFromIndex(MI_BLOOD);
  psEffect->size = static_cast<UBYTE>(BLOOD_SIZE);
}

// ----------------------------------------------------------------------------------------
void effectSetupDestruction(EFFECT* psEffect)
{
  if (psEffect->type == DESTRUCTION_TYPE_SKYSCRAPER)
  {
    psEffect->lifeSpan = (3 * GAME_TICKS_PER_SEC) / 2 + (rand() % GAME_TICKS_PER_SEC);
    psEffect->frameDelay = DESTRUCTION_FRAME_DELAY / 2;
  }
  else if (psEffect->type == DESTRUCTION_TYPE_DROID)
  {
    /* It's all over quickly for droids */
    psEffect->lifeSpan = DROID_DESTRUCTION_DURATION;
    psEffect->frameDelay = DESTRUCTION_FRAME_DELAY;
  }
  else if (psEffect->type == DESTRUCTION_TYPE_WALL_SECTION OR psEffect->type == DESTRUCTION_TYPE_FEATURE)
  {
    psEffect->lifeSpan = STRUCTURE_DESTRUCTION_DURATION / 4;
    psEffect->frameDelay = DESTRUCTION_FRAME_DELAY / 2;
  }
  else if (psEffect->type == DESTRUCTION_TYPE_POWER_STATION)
  {
    psEffect->lifeSpan = STRUCTURE_DESTRUCTION_DURATION / 2;
    psEffect->frameDelay = DESTRUCTION_FRAME_DELAY / 4;
  }
  else
  {
    /* building's destruction is longer */
    psEffect->lifeSpan = STRUCTURE_DESTRUCTION_DURATION;
    psEffect->frameDelay = DESTRUCTION_FRAME_DELAY / 2;
  }
}

#define FX_PER_EDGE 6
#define	SMOKE_SHIFT	(16 - (rand()%32))
// ----------------------------------------------------------------------------------------
void initPerimeterSmoke(iIMDShape* pImd, UDWORD x, UDWORD y, UDWORD z)
{
  SDWORD i;
  SDWORD shift;
  iVector base;
  iVector pos;

  base.x = x;
  base.y = y;
  base.z = z;

  SDWORD varStart = pImd->xmin - 16;
  SDWORD varEnd = pImd->xmax + 16;
  SDWORD varStride = 24; //(varEnd-varStart)/FX_PER_EDGE;

  SDWORD inStart = pImd->zmin - 16;
  SDWORD inEnd = pImd->zmax + 16;

  for (i = varStart; i < varEnd; i += varStride)
  {
    shift = SMOKE_SHIFT;
    pos.x = base.x + i + shift;
    pos.y = base.y;
    pos.z = base.z + inStart + shift;
    if (rand() % 6 == 1)
      addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_SMALL,FALSE, nullptr, 0);
    else
      addEffect(&pos, EFFECT_SMOKE, SMOKE_TYPE_BILLOW,FALSE, nullptr, 0);

    pos.x = base.x + i + shift;
    pos.y = base.y;
    pos.z = base.z + inEnd + shift;
    if (rand() % 6 == 1)
      addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_SMALL,FALSE, nullptr, 0);
    else
      addEffect(&pos, EFFECT_SMOKE, SMOKE_TYPE_BILLOW,FALSE, nullptr, 0);
  }

  varStart = pImd->zmin - 16;
  varEnd = pImd->zmax + 16;
  varStride = 24; //(varEnd-varStart)/FX_PER_EDGE;

  inStart = pImd->xmin - 16;
  inEnd = pImd->xmax + 16;

  for (i = varStart; i < varEnd; i += varStride)
  {
    pos.x = base.x + inStart + shift;
    pos.y = base.y;
    pos.z = base.z + i + shift;
    if (rand() % 6 == 1)
      addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_SMALL,FALSE, nullptr, 0);
    else
      addEffect(&pos, EFFECT_SMOKE, SMOKE_TYPE_BILLOW,FALSE, nullptr, 0);

    pos.x = base.x + inEnd + shift;
    pos.y = base.y;
    pos.z = base.z + i + shift;
    if (rand() % 6 == 1)
      addEffect(&pos, EFFECT_EXPLOSION, EXPLOSION_TYPE_SMALL,FALSE, nullptr, 0);
    else
      addEffect(&pos, EFFECT_SMOKE, SMOKE_TYPE_BILLOW,FALSE, nullptr, 0);
  }
}

// ----------------------------------------------------------------------------------------
UDWORD getNumEffects(void) { return (numEffects); }

// ----------------------------------------------------------------------------------------
UDWORD EffectGetNumFrames(EFFECT* psEffect) { return psEffect->imd->numFrames; }

UDWORD IMDGetNumFrames(iIMDShape* Shape) { return Shape->numFrames; }

UDWORD IMDGetAnimInterval(iIMDShape* Shape) { return Shape->animInterval; }

void effectGiveAuxVar(UDWORD var) { auxVar = var; }

void effectGiveAuxVarSec(UDWORD var) { auxVarSec = var; }

// ----------------------------------------------------------------------------------------
/* Runs all the spot effect stuff for the droids - adding of dust and the like... */
void effectDroidUpdates(void)
{
  iVector pos;

  /* Go through all players */
  for (UDWORD i = 0; i < MAX_PLAYERS; i++)
  {
    /* Now go through all their droids */
    for (DROID* psDroid = apsDroidLists[i]; psDroid; psDroid = psDroid->psNext)
    {
      /* Gets it's group number */
      UDWORD partition = psDroid->id % EFFECT_DROID_DIVISION;
      /* Right frame to process? */
      if (partition == frameGetFrameNumber() % EFFECT_DROID_DIVISION AND ONEINFOUR)
      {
        /* Sufficent time since last update? - The EQUALS comparison is needed */
        if (gameTime >= (lastUpdateDroids[partition] + DROID_UPDATE_INTERVAL))
        {
          /* Store away when we last processed this group */
          lastUpdateDroids[partition] = gameTime;

          /*	Now add some dust at it's arse end if it's moving or skidding. 
            The check that it's not 0 is probably not sufficient.						
          */
          if (static_cast<SDWORD>(psDroid->sMove.speed) != 0)
          {
            /* Present direction is important */
            float dirSin, dirCos;
            DirectX::XMScalarSinCos(&dirSin, &dirCos,
                                    DirectX::XMConvertToRadians(static_cast<float>(static_cast<SWORD>(psDroid->direction))));
            SDWORD xBehind = static_cast<SDWORD>(std::lrintf(50 * dirSin));
            SDWORD yBehind = static_cast<SDWORD>(std::lrintf(50 * dirCos));
            pos.x = psDroid->x - xBehind;
            pos.z = psDroid->y - yBehind;
            pos.y = map_Height(pos.x, pos.z);
          }
        }
      }
    }
  }
}

// ----------------------------------------------------------------------------------------
/* Runs all the structure effect stuff - steam puffing out etc */
void effectStructureUpdates(void)
{
  iVector eventPos;

  /* Go thru' all players */
  for (UDWORD i = 0; i < MAX_PLAYERS; i++)
  {
    for (STRUCTURE* psStructure = apsStructLists[i]; psStructure; psStructure = psStructure->psNext)
    {
      /* Find it's group */
      UDWORD partition = psStructure->id % EFFECT_STRUCTURE_DIVISION;
      /* Is it the right frame? */
      if (partition == frameGetFrameNumber() % EFFECT_STRUCTURE_DIVISION)
      {
        /* Is it the right time? */
        if (gameTime > (lastUpdateStructures[partition] + STRUCTURE_UPDATE_INTERVAL))
        {
          /* Store away the last update time */
          lastUpdateStructures[partition] = gameTime;
          // -------------------------------------------------------------------------------
          /* Factories puff out smoke, power stations puff out tesla stuff */
          if ((psStructure->pStructureType->type == REF_FACTORY) OR (psStructure->pStructureType->type == REF_POWER_GEN))
          {
            if ((bMultiPlayer && isHumanPlayer(psStructure->player)) || (psStructure->player == 0))
            {
              if (psStructure->status == SS_BUILT)
              {
                if (psStructure->visible[selectedPlayer])
                {
                  /*	We're a factory, so better puff out a bit of steam 
							Complete hack with the magic numbers - just for IAN demo 
						*/
                  if (psStructure->pStructureType->type == REF_FACTORY)
                  {
                    if (psStructure->sDisplay.imd->nconnectors == 1)
                    {
                      eventPos.x = psStructure->x + psStructure->sDisplay.imd->connectors->x;
                      eventPos.z = psStructure->y - psStructure->sDisplay.imd->connectors->y;
                      eventPos.y = psStructure->z + psStructure->sDisplay.imd->connectors->z;
                      addEffect(&eventPos, EFFECT_SMOKE, SMOKE_TYPE_STEAM,FALSE, nullptr, 0);

                      if (selectedPlayer == psStructure->player)
                        AudioSystem::PlayObjectTrack(psStructure, ID_SOUND_STEAM, nullptr);
                    }
                  }
                  else if (psStructure->pStructureType->type == REF_POWER_GEN)
                  {
                    POWER_GEN* psPowerGen = (POWER_GEN*)psStructure->pFunctionality;
                    eventPos.x = psStructure->x;
                    eventPos.z = psStructure->y;
                    if (psStructure->sDisplay.imd->nconnectors > 0)
                      eventPos.y = psStructure->z + psStructure->sDisplay.imd->connectors->z;
                    else
                      eventPos.y = psStructure->z;
                    UDWORD capacity = psPowerGen->capacity;
                    /* Add an effect over the central spire - if 
							connected to Res Extractor and it is active*/
                    //look through the list to see if any connected Res Extr
                    BOOL active = FALSE;
                    for (i = 0; i < NUM_POWER_MODULES; i++)
                    {
                      if (psPowerGen->apResExtractors[i] AND ((RES_EXTRACTOR*)psPowerGen->apResExtractors[i]->pFunctionality)->active)
                      {
                        active = TRUE;
                        break;
                      }
                    }
                    /*
							if (((POWER_GEN*)psStructure->pFunctionality)->
								apResExtractors[0] AND ((RES_EXTRACTOR *)((POWER_GEN*)
								psStructure->pFunctionality)->apResExtractors[0]->
								pFunctionality)->active)
							*/
                    {
                      eventPos.y = psStructure->z + 48;
                      addEffect(&eventPos, EFFECT_EXPLOSION, EXPLOSION_TYPE_TESLA,FALSE, nullptr, 0);
                      if (selectedPlayer == psStructure->player)
                        AudioSystem::PlayObjectTrack(psStructure, ID_SOUND_POWER_SPARK, nullptr);
                    }
                    /*	Work out how many spires it has. This is a particularly unpleasant
								hack and I'm not proud of it, but it needs to done. Honest. AM
							*/
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

UDWORD getFreeEffect(void) { return (freeEffect); }

// ----------------------------------------------------------------------------------------
void effectResetUpdates(void)
{
  UDWORD i;

  for (i = 0; i < EFFECT_DROID_DIVISION; i++)
    lastUpdateDroids[i] = 0;
  for (i = 0; i < EFFECT_STRUCTURE_DIVISION; i++)
    lastUpdateStructures[i] = 0;
}

// -----------------------------------------------------------------------------------
BOOL fireOnLocation(UDWORD x, UDWORD y)
{
  UDWORD i;
  BOOL bOnFire;

  for (i = 0, bOnFire = FALSE; i < MAX_EFFECTS AND !bOnFire; i++)
  {
    if ((asEffectsList[i].status == ES_ACTIVE) AND asEffectsList[i].group == EFFECT_FIRE)
    {
      UDWORD posX = std::lrintf(asEffectsList[i].position.x);
      UDWORD posY = std::lrintf(asEffectsList[i].position.z);
      if ((posX == x) AND (posY == y))
        bOnFire = TRUE;
    }
  }
  return (bOnFire);
}

// -----------------------------------------------------------------------------------
void addFireworksEffect(void) {}
// -----------------------------------------------------------------------------------

// ----------------------------------------------------------------------------------------
