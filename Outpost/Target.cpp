#include "pch.h"
// Screen coordinate based targeting system. Maintains a list of on screen objects
// and provides various functions for aquiring targets from this list.
//

#include <stdio.h>
#include <assert.h>
#include <Frame.h>
#include <GTime.h>
#include <AnimObj.h>

#include "StatsDef.h"
#include "Base.h"
#include "FeatureDef.h"
#include "Weapons.h"
#include "StructureDef.h"
#include "Display.h"
#include "Visibility.h"
#include "ObjectDef.h"
#include "Droid.h"

#include "Geo.h"
#include "IMD.h"
#include "RendMode.h"

#include "Target.h"

extern UDWORD selectedPlayer;

#define TARGET_RESOURCES		// Allow targeting of resources.
#define TARGET_WALLS			// Allow targeting of walls.

#define MAX_TARGETS	32

using TARGET = struct
{
  UWORD Type;
  BASE_OBJECT* psObj;
};

static BOOL FoundCurrent;
static UWORD NumTargets;
static UWORD TargetCurrent;
static UDWORD TargetCurrentID;
static TARGET TargetList[MAX_TARGETS];
static UDWORD TargetEndTime;
static BASE_OBJECT* TargetingObject;
static UWORD TargetingType;

extern UDWORD currentGameFrame;

// Initialise the targeting system.
//
void targetInitialise(void)
{
  TargetCurrent = 0;
  TargetCurrentID = UDWORD_MAX;
}

// Reset the target list, call once per frame.
//
void targetOpenList(BASE_OBJECT* psTargeting)
{
  NumTargets = 0;
  FoundCurrent = FALSE;
  TargetingObject = psTargeting;
}

void targetCloseList(void) { if (!FoundCurrent) {} }

//// Aquire a new target.
////
//BASE_OBJECT *targetAquireNew(void)
//{
//	BASE_OBJECT *psObj;
//
//	// First try and target the nearest threat.
//	if( (psObj=targetAquireNearestObj(TargetingObject,TARGET_TYPE_THREAT)) != NULL) {
//		DBPRINTF(("Targeting threat\n"));
//		return psObj;
//	}
//
//	// if that failed then attempt to target the nearest object.
//	if( (psObj=targetAquireNearestObj(TargetingObject,TARGET_TYPE_ANY)) != NULL) {
//		DBPRINTF(("Targeting any\n"));
//		return psObj;
//	}
//
//	TargetCurrent = 0;
//	TargetCurrentID = UDWORD_MAX;
//
//	return NULL;
//}

/*
	DROID_WEAPON,		// Weapon droid
	DROID_SENSOR,		// Sensor droid
	DROID_ECM,			// ECM droid
	DROID_CONSTRUCT,	// Constructor droid
	DROID_PERSON,		// person
	DROID_CYBORG,		// cyborg-type thang
	DROID_TRANSPORTER,	// guess what this is!
	DROID_COMMAND,		// Command droid
	DROID_REPAIR,		// Repair droid
	DROID_DEFAULT,		// Default droid
	DROID_ANY,			// Any droid, Only used as a parameter for intGotoNextDroidType(droidtype).
*/

// Tell the targeting system what type of droid is doing the targeting.
//
void targetSetTargetable(UWORD DroidType) { TargetingType = DroidType; }

void targetAdd(BASE_OBJECT* psObj) { UNUSEDPARAMETER(psObj); }

// Call whenever an object is removed. probably don't need this as
// the list is rebuilt every frame any way.
//
void targetKilledObject(BASE_OBJECT* psObj) { UNUSEDPARAMETER(psObj); }

//// Aquire the target nearest to the specified screen coords.
////
//BASE_OBJECT *targetAquireNearestScreen(SWORD x,SWORD y,UWORD TargetType)
//{
//	UWORD i;
//	UWORD Nearesti = 0;
//	UDWORD NearestDsq = UDWORD_MAX;
//	UDWORD dx,dy;
//	UDWORD Dsq;
//	BASE_OBJECT *NearestObj = NULL;
//	BASE_OBJECT *psObj;
//
//	for(i=0; i<NumTargets; i++) {
//		psObj = TargetList[i].psObj;
//		dx = abs(psObj->sDisplay.screenX - x);
//		dy = abs(psObj->sDisplay.screenY - y);
//		Dsq = dx*dx+dy*dy;
//		if(Dsq < NearestDsq) {
//			if(TargetList[i].Type & TargetType) {
//				NearestDsq = Dsq;
//				Nearesti = i;
//				NearestObj = psObj;
//			}
//		}
//	}
//
//	if(NearestObj != NULL) {
//		TargetCurrent = Nearesti;
//		if(TargetCurrentID != NearestObj->id) {
//			TargetCurrentID = NearestObj->id;
//			targetStartAnim();
//		}
//	}
//
//	return NearestObj;
//}

// Aquire the target nearest the vector from x,y to the top of the screen.
//
BASE_OBJECT* targetAquireNearestView(SWORD x, SWORD y, UWORD TargetType)
{
  UWORD i;
  UWORD Nearesti = 0;
  UDWORD NearestDx = UDWORD_MAX;
  UDWORD dx, dy;
  BASE_OBJECT* NearestObj = nullptr;
  BASE_OBJECT* psObj;

  for (i = 0; i < NumTargets; i++)
  {
    psObj = TargetList[i].psObj;
    dx = abs(static_cast<SDWORD>(psObj->sDisplay.screenX - x));
    dy = abs(static_cast<SDWORD>(psObj->sDisplay.screenY - y));
    dx += dy / 2;
    if (dx < NearestDx)
    {
      if (TargetList[i].Type & TargetType)
      {
        NearestDx = dx;
        Nearesti = i;
        NearestObj = psObj;
      }
    }
  }

  if (NearestObj != nullptr)
  {
    TargetCurrent = Nearesti;
    if (TargetCurrentID != NearestObj->id)
    {
      TargetCurrentID = NearestObj->id;
      targetStartAnim();
    }
  }
  else
    TargetCurrentID = UDWORD_MAX;

  return NearestObj;
}

//// Aquire the target nearest to the specified object.
////
//BASE_OBJECT *targetAquireNearestObj(BASE_OBJECT *psObj,UWORD TargetType)
//{
//	if(psObj != NULL) {
//		return targetAquireNearestScreen(psObj->sDisplay.screenX,psObj->sDisplay.screenY,TargetType);
//	} else {
//		return NULL;
//	}
//}

// Aquire the target nearest to the specified object.
//
BASE_OBJECT* targetAquireNearestObjView(BASE_OBJECT* psObj, UWORD TargetType)
{
  if (psObj != nullptr)
  {
    return targetAquireNearestView(static_cast<SWORD>(psObj->sDisplay.screenX), static_cast<SWORD>(psObj->sDisplay.screenY), TargetType);
  }
  return nullptr;
}

//// Aquire the next target in the target list.
////
//BASE_OBJECT *targetAquireNext(UWORD TargetType)
//{
//	BASE_OBJECT *Target = NULL;
//
//	if(NumTargets) {
//		UWORD OldCurrent = TargetCurrent;
//
//		TargetCurrent++;
//
//		while( (Target == NULL) && (TargetCurrent < NumTargets) ) {
//			// Target is of required type?
//			if(TargetList[TargetCurrent].Type & TargetType) {
//				Target = TargetList[TargetCurrent].psObj;
//			}
//
//			TargetCurrent++;
//		}
//
//
//		if(Target == NULL) {
//			TargetCurrent = 0;
//
//			while( (Target == NULL) && (TargetCurrent < OldCurrent) ) {
//				// Target is of required type?
//				if(TargetList[TargetCurrent].Type & TargetType) {
//					Target = TargetList[TargetCurrent].psObj;
//				}
//
//				TargetCurrent++;
//			}
//		}
//
//		if(TargetCurrent >= NumTargets) {
//			TargetCurrent = 0;
//		}
//	}
//
//	if(Target != NULL) {
//		if(TargetCurrentID != Target->id) {
//			TargetCurrentID = Target->id;
//			targetStartAnim();
//		}
//	}
//
//	return Target;
//}

// Get the currently targeted object.
//
BASE_OBJECT* targetGetCurrent(void)
{
  if (TargetCurrentID != UDWORD_MAX)
    return TargetList[TargetCurrent].psObj;

  return nullptr;
}

// Start the box zoom animation.
//
void targetStartAnim(void) { TargetEndTime = gameTime + GAME_TICKS_PER_SEC / 2; }

// Display a marker over the current target.
//
void targetMarkCurrent(void)
{
  SWORD x, y;
  SWORD Offset;
  SWORD x0, y0, x1, y1;

  if (TargetCurrentID == UDWORD_MAX)
    return;

  x = static_cast<SWORD>(TargetList[TargetCurrent].psObj->sDisplay.screenX);
  y = static_cast<SWORD>(TargetList[TargetCurrent].psObj->sDisplay.screenY);

  // Make it zoom in.
  if (TargetEndTime > gameTime)
    Offset = static_cast<SWORD>(16 + (TargetEndTime - gameTime) / 2);
  else
    Offset = 16;

  x0 = static_cast<SWORD>(x - Offset);
  y0 = static_cast<SWORD>(y - Offset);
  x1 = static_cast<SWORD>(x + Offset);
  y1 = static_cast<SWORD>(y + Offset);

  iV_Line(x0, y0, x0 + 8, y0,COL_YELLOW);
  iV_Line(x0, y0, x0, y0 + 8,COL_YELLOW);

  iV_Line(x1, y0, x1 - 8, y0,COL_YELLOW);
  iV_Line(x1, y0, x1, y0 + 8,COL_YELLOW);

  iV_Line(x1, y1, x1 - 8, y1,COL_YELLOW);
  iV_Line(x1, y1, x1, y1 - 8,COL_YELLOW);

  iV_Line(x0, y1, x0 + 8, y1,COL_YELLOW);
  iV_Line(x0, y1, x0, y1 - 8,COL_YELLOW);
}
