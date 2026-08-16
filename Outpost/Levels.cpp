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
#include "LevelInt.h"
#include "Game.h"
#include "Lighting.h"
#include "PieState.h"
#include "Data.h"
#include "MultiWDG.h"

#include "Script.h"
#include "ScriptTabs.h"
#include "PieMode.h"

// semi hack to get the playstation to load resources from the WDG

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

// return values from the lexer
STRING* pLevToken;
SDWORD levVal;

// modes for the parser
enum
{
  LP_START,
  // no input received
  LP_LEVEL,
  // level token received
  LP_LEVELDONE,
  // defined a level waiting for players/type/data
  LP_PLAYERS,
  // players token received
  LP_TYPE,
  // type token received
  LP_DATASET,
  // dataset token received
  LP_WAITDATA,
  // defining level data, waiting for data token
  LP_DATA,
  // data token received
  LP_GAME,
  // game token received
};

/*// the current data file to parse
static UBYTE	*pDataFile;
static SDWORD	dataFileSize;

// the current position in the data file
static UBYTE	*pDataPtr;
static SDWORD	levLine;

// the token buffer
#define TOKEN_MAX	255
static STRING	aTokenBuff[TOKEN_MAX];
*/

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

// error report function for the level parser
void levError(STRING* pError)
{
  char* pText;
  int line;

  levGetErrorData(&line, &pText);

#ifdef DEBUG
  DEBUG_ASSERT_TEXT(FALSE, "Level File parse error:\n{} at line {} text {}\n", pError, line, pText);
#else
  Neuron::Fatal("Level File parse error:\n{} at line {} text {}\n", pError, line, pText);
#endif
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

// parse a level description data file
BOOL levParse(UBYTE* pBuffer, SDWORD size)
{
  SDWORD token, state, currData = 0;
  LEVEL_DATASET* psDataSet = nullptr;
  LEVEL_DATASET* psFoundData;

  levSetInputBuffer(pBuffer, size);

  state = LP_START;
  token = lev_lex();
  while (token != 0)
  {
    switch (token)
    {
    case LTK_LEVEL:
    case LTK_CAMPAIGN:
    case LTK_CAMSTART:
    case LTK_CAMCHANGE:
    case LTK_EXPAND:
    case LTK_BETWEEN:
    case LTK_MKEEP:
    case LTK_MCLEAR:
    case LTK_EXPAND_LIMBO:
    case LTK_MKEEP_LIMBO:
      if (state == LP_START || state == LP_WAITDATA)
      {
        // start a new level data set
        psDataSet = new (std::nothrow) LEVEL_DATASET[1];
        if (!psDataSet)
        {
          levError("Out of memory");
          return FALSE;
        }
        memset(psDataSet, 0, sizeof(LEVEL_DATASET));
        psDataSet->players = 1;
        psDataSet->game = -1;
        LIST_ADDEND(psLevels, psDataSet, LEVEL_DATASET);
        currData = 0;

        // set the dataset type
        switch (token)
        {
        case LTK_LEVEL:
          psDataSet->type = LDS_COMPLETE;
          break;
        case LTK_CAMPAIGN:
          psDataSet->type = LDS_CAMPAIGN;
          break;
        case LTK_CAMSTART:
          psDataSet->type = LDS_CAMSTART;
          break;
        case LTK_BETWEEN:
          psDataSet->type = LDS_BETWEEN;
          break;
        case LTK_MKEEP:
          psDataSet->type = LDS_MKEEP;
          break;
        case LTK_CAMCHANGE:
          psDataSet->type = LDS_CAMCHANGE;
          break;
        case LTK_EXPAND:
          psDataSet->type = LDS_EXPAND;
          break;
        case LTK_MCLEAR:
          psDataSet->type = LDS_MCLEAR;
          break;
        case LTK_EXPAND_LIMBO:
          psDataSet->type = LDS_EXPAND_LIMBO;
          break;
        case LTK_MKEEP_LIMBO:
          psDataSet->type = LDS_MKEEP_LIMBO;
          break;
        default: DEBUG_ASSERT_TEXT(FALSE, "eh?");
          break;
        }
      }
      else
      {
        levError("Syntax Error");
        return FALSE;
      }
      state = LP_LEVEL;
      break;
    case LTK_PLAYERS:
      if (state == LP_LEVELDONE && (psDataSet->type == LDS_COMPLETE || psDataSet->type >= MULTI_TYPE_START))
        state = LP_PLAYERS;
      else
      {
        levError("Syntax Error");
        return FALSE;
      }
      break;
    case LTK_TYPE:
      if (state == LP_LEVELDONE && psDataSet->type == LDS_COMPLETE)
        state = LP_TYPE;
      else
      {
        levError("Syntax Error");
        return FALSE;
      }
      break;
    case LTK_INTEGER:
      if (state == LP_PLAYERS)
        psDataSet->players = static_cast<SWORD>(levVal);
      else if (state == LP_TYPE)
      {
        if (levVal < MULTI_TYPE_START)
        {
          levError("invalid type number");
          return FALSE;
        }

        psDataSet->type = static_cast<SWORD>(levVal);
      }
      else
      {
        levError("Syntax Error");
        return FALSE;
      }
      state = LP_LEVELDONE;
      break;
    case LTK_DATASET:
      if (state == LP_LEVELDONE && psDataSet->type != LDS_COMPLETE)
        state = LP_DATASET;
      else
      {
        levError("Syntax Error");
        return FALSE;
      }
      break;
    case LTK_DATA:
      if (state == LP_WAITDATA)
        state = LP_DATA;
      else if (state == LP_LEVELDONE)
      {
        if (psDataSet->type == LDS_CAMSTART || psDataSet->type == LDS_MKEEP
          || psDataSet->type == LDS_CAMCHANGE || psDataSet->type == LDS_EXPAND || psDataSet->type == LDS_MCLEAR || psDataSet->type ==
          LDS_EXPAND_LIMBO || psDataSet->type == LDS_MKEEP_LIMBO
        )
        {
          levError("Missing dataset command");
          return FALSE;
        }
        state = LP_DATA;
      }
      else
      {
        levError("Syntax Error");
        return FALSE;
      }
      break;
    case LTK_GAME:
      if ((state == LP_WAITDATA || state == LP_LEVELDONE) && psDataSet->game == -1 && psDataSet->type != LDS_CAMPAIGN)
        state = LP_GAME;
      else
      {
        levError("Syntax Error");
        return FALSE;
      }
      break;
    case LTK_IDENT:
      if (state == LP_LEVEL)
      {
        if (psDataSet->type == LDS_CAMCHANGE)
        {
          // campaign change dataset - need to find the full data set
          if (!levFindDataSet(pLevToken, &psFoundData))
          {
            levError("Cannot find full data set for camchange");
            return FALSE;
          }

          if (psFoundData->type != LDS_CAMSTART)
          {
            levError("Invalid data set name for cam change");
            return FALSE;
          }
          psFoundData->psChange = psDataSet;
        }
        // store the level name
        psDataSet->pName = new (std::nothrow) STRING[strlen(pLevToken) + 1];
        if (!psDataSet->pName)
        {
          levError("Out of memory");
          return FALSE;
        }
        strcpy(psDataSet->pName, pLevToken);
        state = LP_LEVELDONE;
      }
      else if (state == LP_DATASET)
      {
        // find the dataset
        if (!levFindDataSet(pLevToken, &psDataSet->psBaseData))
        {
          levError("Unknown dataset");
          return FALSE;
        }
        state = LP_WAITDATA;
      }
      else
      {
        levError("Syntax Error");
        return FALSE;
      }
      break;
    case LTK_STRING:
      if (state == LP_DATA || state == LP_GAME)
      {
        if (currData >= LEVEL_MAXFILES)
        {
          levError("Too many data files");
          return FALSE;
        }

        // note the game index if necessary
        if (state == LP_GAME)
          psDataSet->game = static_cast<SWORD>(currData);

        // store the data name
        psDataSet->apDataFiles[currData] = new (std::nothrow) STRING[strlen(pLevToken) + 1];
        if (!psDataSet->apDataFiles[currData])
        {
          levError("Out of memory");
          return FALSE;
        }
        resToLower(pLevToken);
        strcpy(psDataSet->apDataFiles[currData], pLevToken);

        currData += 1;
        state = LP_WAITDATA;
      }
      else
      {
        levError("Syntax Error");
        return FALSE;
      }
      break;
    default:
      levError("Unexpected token");
      break;
    }

    // get the next token
    token = lev_lex();
  }

  if (state != LP_WAITDATA || currData == 0)
  {
    levError("Unexpected end of file");
    return FALSE;
  }

  return TRUE;
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

// load up a single wrf file
BOOL levLoadSingleWRF(STRING* pName)
{
  // free the old data
  levReleaseAll();

  // create the dummy level data
  memset(&sSingleWRF, 0, sizeof(LEVEL_DATASET));
  sSingleWRF.pName = pName;

  // load up the WRF
  if (!stageOneInitialise())
    return FALSE;
  // load the data
  Neuron::DebugTrace("Loading {} ...\n", pName);
  if (!resLoad(pName, 0, DisplayBuffer, displayBufferSize))
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
      if (!resLoad(psBaseData->apDataFiles[i], i, DisplayBuffer, displayBufferSize))
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
    Neuron::DebugTrace("levLoadData: dataset {} not found - trying to load as WRF", pName);
    return levLoadSingleWRF(pName);
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

  // if this is a single player level - disable the multiple WDG
  if (psNewLevel->type < LDS_NONE)
    wdgDisableAddonWDG();

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
        if (!resLoad(psBaseData->apDataFiles[i], i, DisplayBuffer, displayBufferSize))
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
      if (!resLoad(psNewLevel->apDataFiles[i], i + CURRENT_DATAID, DisplayBuffer, displayBufferSize))
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
