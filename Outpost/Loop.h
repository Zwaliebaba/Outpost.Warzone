/*
 * Loop.h
 *
 * Interface to the main game loop routine.
 *
 */
#ifndef _loop_h
#define _loop_h

#include "Frame.h"

using GAMECODE = enum
{
  GAMECODE_CONTINUE,
  GAMECODE_RESTARTGAME,
  GAMECODE_QUITGAME,
  GAMECODE_PLAYVIDEO,
  GAMECODE_NEWLEVEL,
  GAMECODE_FASTEXIT,
};

// the states the loop goes through before starting a new level
using LOOP_MISSION_STATE = enum
{
  LMS_NORMAL,
  // normal state of the loop
  LMS_SETUPMISSION,
  // make the call to set up mission
  LMS_MISSIONRESULT,
  // the results box is up between missions
  LMS_NEWLEVEL,
  // start a new level
  LMS_CLEAROBJECTS,
  // make the call to destroy objects
};
extern LOOP_MISSION_STATE loopMissionState;

// this is set by scrStartMission to say what type of new level is to be started
extern SDWORD nextMissionType;

extern BOOL display3D;

extern SDWORD loopPieCount;
extern SDWORD loopTileCount;
extern SDWORD loopPolyCount;
extern SDWORD loopStateChanges;

extern GAMECODE gameLoop(void);
extern GAMECODE videoLoop(void);
extern void loop_SetVideoPlaybackMode(void);
extern void loop_ClearVideoPlaybackMode(void);
extern BOOL loop_GetVideoStatus(void);
extern SDWORD loop_GetVideoMode(void);
extern void loopFastExit(void);

// Number of units in the current list.
extern UDWORD getNumDroids(UDWORD player);
// Number of units on transporters.
extern UDWORD getNumTransporterDroids(UDWORD player);
// Number of units in the mission list.
extern UDWORD getNumMissionDroids(UDWORD player);
UDWORD getNumCommandDroids(UDWORD player);
UDWORD getNumConstructorDroids(UDWORD player);
// increase the droid counts - used by update factory to keep the counts in sync
void incNumDroids(UDWORD player);
void incNumCommandDroids(UDWORD player);
void incNumConstructorDroids(UDWORD player);

#endif
