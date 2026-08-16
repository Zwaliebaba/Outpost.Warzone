#include "pch.h"
/*
 * Levels.c
 *
 * Control the data loading for game levels
 *
 */

#include "ctype.h"

// levLoadData printf's
#define DEBUG_GROUP0
#include "Frame.h"
#include "ListMacs.h"
#include "FrameResource.h"
#include "Init.h"
#include "Objects.h"
#include "HCI.h"
#include "Levels.h"
#include "Mission.h"
#include "Manifest.h"
#include "Game.h"
#include "Lighting.h"
#include "PieState.h"
#include "Data.h"

#include "Script.h"
#include "ScriptTabs.h"
#include "PieMode.h"

// minimum type number for a type instruction
#define MULTI_TYPE_START	10

#define CURRENT_DATAID		LEVEL_MAXFILES

static UBYTE currentLevelName[32];

// the current level descriptions
LEVEL_DATASET* psLevels;

// the currently loaded data set
LEVEL_DATASET* psBaseData;
LEVEL_DATASET* psCurrLevel;

// dummy level data for single WRF loads
LEVEL_DATASET sSingleWRF;

// initialise the level system
BOOL levInitialise(void)
{
  psLevels = nullptr;
  psBaseData = nullptr;
  psCurrLevel = nullptr;

  return TRUE;
}


// shutdown the level system
void levShutDown(void)
{
  LEVEL_DATASET* psNext;
  SDWORD i;

  while (psLevels)
  {
    delete[] psLevels->pName;
    psLevels->pName = nullptr;
    for (i = 0; i < LEVEL_MAXFILES; i++) { if (psLevels->apDataFiles[i] != nullptr) { delete[] psLevels->apDataFiles[i]; } }
    psNext = psLevels->psNext;
    delete[] psLevels;
    psLevels = psNext;
  }
}

// find the level dataset
BOOL levFindDataSet(STRING* pName, LEVEL_DATASET** ppsDataSet)
{
  LEVEL_DATASET* psNewLevel;

  for (psNewLevel = psLevels; psNewLevel; psNewLevel = psNewLevel->psNext)
  {
    if (psNewLevel->pName != nullptr)
    {
      if (strcmp(psNewLevel->pName, pName) == 0)
      {
        *ppsDataSet = psNewLevel;
        return TRUE;
      }
    }
  }

  return FALSE;
}

// free the data for the current mission
BOOL levReleaseMissionData(void)
{
  SDWORD i;

  // release old data if any was loaded
  if (psCurrLevel != nullptr)
  {
    if (!stageThreeShutDown())
      return FALSE;

    // free up the old data
    for (i = LEVEL_MAXFILES - 1; i >= 0; i--)
    {
      if (i == psCurrLevel->game)
      {
        if (psCurrLevel->psBaseData == nullptr)
        {
          if (!stageTwoShutDown())
            return FALSE;
        }
      }
      else // if (psCurrLevel->apDataFiles[i])
        resReleaseBlockData(i + CURRENT_DATAID);
    }
  }

  return TRUE;
}

// free the currently loaded dataset
BOOL levReleaseAll(void)
{
  SDWORD i;

  // release old data if any was loaded
  if (psCurrLevel != nullptr)
  {
    if (!levReleaseMissionData())
      return FALSE;

    // release the game data
    if (psCurrLevel->psBaseData != nullptr)
    {
      if (!stageTwoShutDown())
        return FALSE;
    }

    if (psCurrLevel->psBaseData)
    {
      for (i = LEVEL_MAXFILES - 1; i >= 0; i--)
      {
        if (psCurrLevel->psBaseData->apDataFiles[i])
          resReleaseBlockData(i);
      }
    }

    if (!stageOneShutDown())
      return FALSE;
  }

  psCurrLevel = nullptr;

  return TRUE;
}

// load up a single manifest unit as a level
BOOL levLoadSingleUnit(STRING* pName)
{
  if (!ManifestHasUnit(pName))
  {
    Neuron::Fatal("levLoadSingleUnit: no dataset or unit named {}", pName);
    return FALSE;
  }

  // free the old data
  levReleaseAll();

  // create the dummy level data
  memset(&sSingleWRF, 0, sizeof(LEVEL_DATASET));
  sSingleWRF.pName = pName;

  if (!stageOneInitialise())
    return FALSE;
  // load the data
  Neuron::DebugTrace("Loading {} ...\n", pName);
  if (!ManifestLoadUnit(pName, 0, DisplayBuffer, displayBufferSize))
    return FALSE;

  if (!stageThreeInitialise())
    return FALSE;

  psCurrLevel = &sSingleWRF;

  return TRUE;
}

BOOL levLoadBaseData(STRING* pName)
{
  LEVEL_DATASET *psNewLevel, *psBaseData;
  SDWORD i;

  Neuron::DebugTrace("Loading base data for level {}\n", pName);

  // find the level dataset
  if (!levFindDataSet(pName, &psNewLevel))
  {
    Neuron::Fatal("levLoadBaseData: couldn't find level data");
    return FALSE;
  }

  if (psNewLevel->type != LDS_CAMSTART && psNewLevel->type != LDS_MKEEP
    && psNewLevel->type != LDS_EXPAND && psNewLevel->type != LDS_MCLEAR && psNewLevel->type != LDS_EXPAND_LIMBO && psNewLevel->type !=
    LDS_MKEEP_LIMBO
  )
  {
    Neuron::Fatal("levLoadBaseData: incorect level type");
    return FALSE;
  }

  // clear all the old data
  levReleaseAll();

  // initialise
  if (!stageOneInitialise())
    return FALSE;

  // load up the base dataset
  psBaseData = psNewLevel->psBaseData;
  for (i = 0; i < LEVEL_MAXFILES; i++)
  {
    if (psBaseData->apDataFiles[i])
    {
      // load the data
      Neuron::DebugTrace("Loading {} ...\n", psBaseData->apDataFiles[i]);
      if (!ManifestLoadUnit(psBaseData->apDataFiles[i], i, DisplayBuffer, displayBufferSize))
        return FALSE;
    }
  }

  psCurrLevel = psNewLevel;

  return TRUE;
}

UBYTE* getLevelName(void) { return (currentLevelName); }

// load up the data for a level
BOOL levLoadData(STRING* pName)
{
  LEVEL_DATASET *psNewLevel, *psBaseData, *psChangeLevel;
  SDWORD i;

  Neuron::DebugTrace("Loading level {}\n", pName);

  // find the level dataset
  if (!levFindDataSet(pName, &psNewLevel))
  {
    Neuron::DebugTrace("levLoadData: dataset {} not found - trying to load as a unit", pName);
    return levLoadSingleUnit(pName);
  }

  /* Keep a copy of the present level name */
  strcpy((char*)currentLevelName, pName);

  // select the change dataset if there is one
  psChangeLevel = nullptr;
  if ((psNewLevel->psChange != nullptr) && (psCurrLevel != nullptr))
  {
    //store the level name
    Neuron::DebugTrace("levLoadData: Found CAMCHANGE dataset\n");
    psChangeLevel = psNewLevel;
    psNewLevel = psNewLevel->psChange;
  }

  // ensure the correct dataset is loaded
  if (psNewLevel->type == LDS_CAMPAIGN)
  {
    Neuron::Fatal("levLoadData: Cannot load a campaign dataset ({})", psNewLevel->pName);
    return FALSE;
  }
  if (psCurrLevel != nullptr)
  {
    if ((psCurrLevel->psBaseData != psNewLevel->psBaseData) || (psCurrLevel->type < LDS_NONE && psNewLevel->type >= LDS_NONE) || (
      psCurrLevel->type >= LDS_NONE && psNewLevel->type < LDS_NONE))
    {
      // there is a dataset loaded but it isn't the correct one
      Neuron::DebugTrace("levLoadData: Incorrect base dataset loaded - levReleaseAll()\n");
      levReleaseAll(); // this sets psCurrLevel to NULL
    }
  }

  // setup the correct dataset to load if necessary
  if (psCurrLevel == nullptr)
  {
#ifdef DEBUG_GROUP0
    if (psNewLevel->psBaseData != nullptr)
      Neuron::DebugTrace("levLoadData: Setting base dataset to load: {}\n", psNewLevel->psBaseData->pName);
#endif
    psBaseData = psNewLevel->psBaseData;
  }
  else
  {
    Neuron::DebugTrace("levLoadData: No base dataset to load\n");
    psBaseData = nullptr;
  }

  // reset the old mission data if necessary
  if (psCurrLevel != nullptr)
  {
    Neuron::DebugTrace("levLoadData: reseting old mission data\n");
    if (!gameReset())
      return FALSE;
    if (!levReleaseMissionData())
      return FALSE;
  }

  Neuron::DebugTrace("levLoadData: Setting game heap\n");

  // initialise if necessary
  if (psNewLevel->type == LDS_COMPLETE || //psNewLevel->type >= MULTI_TYPE_START ||
    psBaseData != nullptr)
  {
    Neuron::DebugTrace("levLoadData: reset game heap\n");
    if (!stageOneInitialise())
      return FALSE;
  }

  // load up a base dataset if necessary
  if (psBaseData != nullptr)
  {
    Neuron::DebugTrace("levLoadData: loading base dataset {}\n", psBaseData->pName);
    for (i = 0; i < LEVEL_MAXFILES; i++)
    {
      if (psBaseData->apDataFiles[i])
      {
        // load the data
        Neuron::DebugTrace("Loading {} ...\n", psBaseData->apDataFiles[i]);
        if (!ManifestLoadUnit(psBaseData->apDataFiles[i], i, DisplayBuffer, displayBufferSize))
          return FALSE;
      }
    }
  }
  if (psNewLevel->type == LDS_CAMCHANGE)
  {
    if (!campaignReset())
      return FALSE;
  }
  if (psNewLevel->game == -1) //no .gam file to load - BETWEEN missions (for Editor games only)
  {
    DEBUG_ASSERT_TEXT(psNewLevel->type == LDS_BETWEEN, "levLoadData: only BETWEEN missions do not need a .gam file");
    Neuron::DebugTrace("levLoadData: no .gam file for level: BETWEEN mission\n");
    Neuron::DebugTrace("levLoadData: start mission - no .gam\n");
    if (!startMission(psNewLevel->type, nullptr))
      return FALSE;

    Neuron::DebugTrace("levLoadData: setting mission heap\n");
  }

  // load the new data
  Neuron::DebugTrace("levLoadData: loading mission dataset: {}\n", psNewLevel->pName);
  for (i = 0; i < LEVEL_MAXFILES; i++)
  {
    if (psNewLevel->game == i)
    {
      // do some more initialising if necessary
      if (psNewLevel->type == LDS_COMPLETE || psNewLevel->type >= MULTI_TYPE_START || psBaseData != nullptr)
      {
        Neuron::Reset(FALSE); //unload font, to avoid crash on 8th load... ajl 15/sep/99
        if (!stageTwoInitialise())
          return FALSE;

        Neuron::DebugTrace("levLoadData: setting map heap\n");
      }

      if (psNewLevel->type == LDS_MKEEP
        || psNewLevel->type == LDS_MCLEAR || psNewLevel->type == LDS_MKEEP_LIMBO
      )
      {
        Neuron::DebugTrace("levLoadData: setting mission heap\n");
      }

      {
        // load the game
        Neuron::DebugTrace("Loading scenario file {} ...", psNewLevel->apDataFiles[i]);
        switch (psNewLevel->type)
        {
        case LDS_COMPLETE:
        case LDS_CAMSTART: Neuron::DebugTrace("COMPLETE / CAMSTART\n");
          //if (!startMission(MISSION_CAMPSTART, psNewLevel->apDataFiles[i]))
          if (!startMission(LDS_CAMSTART, psNewLevel->apDataFiles[i]))
            return FALSE;
          break;
        case LDS_BETWEEN: Neuron::DebugTrace("BETWEEN\n");
          if (!startMission(LDS_BETWEEN, psNewLevel->apDataFiles[i]))
            return FALSE;
          break;

        case LDS_MKEEP: Neuron::DebugTrace("MKEEP\n");
          //if (!startMission(MISSION_OFFKEEP, psNewLevel->apDataFiles[i]))
          if (!startMission(LDS_MKEEP, psNewLevel->apDataFiles[i]))
            return FALSE;
          break;
        case LDS_CAMCHANGE: Neuron::DebugTrace("CAMCHANGE\n");
          //if (!startMission(MISSION_CAMPSTART, psNewLevel->apDataFiles[i]))
          if (!startMission(LDS_CAMCHANGE, psNewLevel->apDataFiles[i]))
            return FALSE;
          break;

        case LDS_EXPAND: Neuron::DebugTrace("EXPAND\n");
          //if (!startMission(MISSION_CAMPEXPAND, psNewLevel->apDataFiles[i]))
          if (!startMission(LDS_EXPAND, psNewLevel->apDataFiles[i]))
            return FALSE;
          break;
        case LDS_EXPAND_LIMBO: Neuron::DebugTrace("EXPAND_LIMBO\n");
          //if (!startMission(MISSION_CAMPEXPAND, psNewLevel->apDataFiles[i]))
          if (!startMission(LDS_EXPAND_LIMBO, psNewLevel->apDataFiles[i]))
            return FALSE;
          break;

        case LDS_MCLEAR: Neuron::DebugTrace("MCLEAR\n");
          //if (!startMission(MISSION_OFFCLEAR, psNewLevel->apDataFiles[i]))
          if (!startMission(LDS_MCLEAR, psNewLevel->apDataFiles[i]))
            return FALSE;
          break;
        case LDS_MKEEP_LIMBO: Neuron::DebugTrace("MKEEP_LIMBO\n");
          //if (!startMission(MISSION_OFFKEEP, psNewLevel->apDataFiles[i]))
          if (!startMission(LDS_MKEEP_LIMBO, psNewLevel->apDataFiles[i]))
            return FALSE;
          break;
        default: DEBUG_ASSERT_TEXT(psNewLevel->type >= MULTI_TYPE_START, "levLoadData: Unexpected mission type");
          Neuron::DebugTrace("MULTIPLAYER\n");
          //if (!startMission(MISSION_CAMPSTART, psNewLevel->apDataFiles[i]))
          if (!startMission(LDS_CAMSTART, psNewLevel->apDataFiles[i]))
            return FALSE;
          break;
        }
      }

      // set the view position if necessary
      if ((psNewLevel->type != LDS_BETWEEN)
        && (psNewLevel->type != LDS_EXPAND) && (psNewLevel->type != LDS_EXPAND_LIMBO)
      )
      {
        if (!newMapInitialise())
          return FALSE;
      }
    }
    else if (psNewLevel->apDataFiles[i])
    {
      // load the data
      Neuron::DebugTrace("Loading {} ...\n", psNewLevel->apDataFiles[i]);
      if (!ManifestLoadUnit(psNewLevel->apDataFiles[i], i + CURRENT_DATAID, DisplayBuffer, displayBufferSize))
        return FALSE;
    }
  }

  if (!stageThreeInitialise())
    return FALSE;

  //want to test with release build too
  //this enables us to to start cam2/cam3 without going via a save game and get the extra droids
  //in from the script-controlled Transporters
  if (psNewLevel->type == LDS_CAMSTART)
    eventFireCallbackTrigger(CALL_NO_REINFORCEMENTS_LEFT);

  //restore the level name for comparisons on next mission load up
  if (psChangeLevel == nullptr)
    psCurrLevel = psNewLevel;
  else
    psCurrLevel = psChangeLevel;

  return TRUE;
}
