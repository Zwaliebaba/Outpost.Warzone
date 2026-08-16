#include "pch.h"
#include <directxmath.h>
/*
	ALL PSX, HASH_NAMES AND WIN32 excluded stuff removed - Alex M.
*/

/* Standard library headers */
#include <stdio.h>
#include <direct.h>
#include <assert.h>

/* Warzone src and library headers */
#include "Frame.h"
#include "StrRes.h"
#include "FrameInt.h"
#include "Ivis02.h"
#include "Script.h"
#include "GTime.h"
#include "Map.h"
#include "Edit2D.h"
#include "Droid.h"
#include "Action.h"
#include "Game.h"
#include "Research.h"
#include "Power.h"
#include "Player.h"
#include "Projectile.h"
#include "Text.h"
#include "Message.h"
#include "HCI.h"
#include "Display.h"
#include "Display3D.h"
#include "Map.h"
#include "Effects.h"
#include "Init.h"
#include "Mission.h"
#include "PieState.h"
#include "Scores.h"
#include "AudioID.h"
#include "AnimID.h"
#include "Design.h"
#include "Lighting.h"
#include "Component.h"
#include "Radar.h"
#include "CmdDroid.h"
#include "Formation.h"
#include "FormationDef.h"
#include "WarzoneConfig.h"
#include "MultiPlay.h"
#include "NetPlay.h"
#include "FrontEnd.h"
#include "Levels.h"
#include "Mission.h"
#include "Geometry.h"
#include "AudioSystem.h"
#include "Gateway.h"
#include "ScriptTabs.h"
#include "ScriptExtern.h"
#include "MultiStat.h"
#include "Wrappers.h"
#include "Palette.h"

#define MAX_SAVE_NAME_SIZE_V19	40
#define MAX_SAVE_NAME_SIZE	60

#if (MAX_NAME_SIZE > MAX_SAVE_NAME_SIZE)
#error warning the current MAX_NAME_SIZE is to big for the save game
#endif

#define MAX_BODY			SWORD_MAX
#define SAVEKEY_ONMISSION	0x100

UDWORD RemapPlayerNumber(UDWORD OldNumber);

using GAME_SAVEHEADER = struct _game_save_header
{
  STRING aFileType[4];
  UDWORD version;
};


using STRUCT_SAVEHEADER = struct _struct_save_header
{
  STRING aFileType[4];
  UDWORD version;
  UDWORD quantity;
};


using FEATURE_SAVEHEADER = struct _feature_save_header
{
  STRING aFileType[4];
  UDWORD version;
  UDWORD quantity;
};

/* Structure definitions for loading and saving map data */
using TILETYPE_SAVEHEADER = struct
{
  STRING aFileType[4];
  UDWORD version;
  UDWORD quantity;
};

/* Structure definitions for loading and saving map data */

/* Structure definitions for loading and saving map data */








/* Sanity check definitions for the save struct file sizes */
#define GAME_HEADER_SIZE			8
#define DROID_HEADER_SIZE			12
#define DROIDINIT_HEADER_SIZE		12
#define STRUCT_HEADER_SIZE			12
#define TEMPLATE_HEADER_SIZE		12
#define FEATURE_HEADER_SIZE			12
#define TILETYPE_HEADER_SIZE		12
#define COMPLIST_HEADER_SIZE		12
#define STRUCTLIST_HEADER_SIZE		12
#define RESEARCH_HEADER_SIZE		12
#define MESSAGE_HEADER_SIZE			12
#define PROXIMITY_HEADER_SIZE		12
#define FLAG_HEADER_SIZE			12
#define PRODUCTION_HEADER_SIZE		8
#define STRUCTLIMITS_HEADER_SIZE	12
#define COMMAND_HEADER_SIZE			12

// general save definitions
#define MAX_LEVEL_SIZE 20

#define OBJECT_SAVE_V19 \
	STRING				name[MAX_SAVE_NAME_SIZE_V19]; \
	UDWORD				id; \
	UDWORD				x,y,z; \
	UDWORD				direction; \
	UDWORD				player; \
	BOOL				inFire; \
	UDWORD				burnStart; \
	UDWORD				burnDamage

#define OBJECT_SAVE_V20 \
	STRING				name[MAX_SAVE_NAME_SIZE]; \
	UDWORD				id; \
	UDWORD				x,y,z; \
	UDWORD				direction; \
	UDWORD				player; \
	BOOL				inFire; \
	UDWORD				burnStart; \
	UDWORD				burnDamage






#define GAME_SAVE_V7	\
	UDWORD	gameTime;	\
	UDWORD	GameType;		/* Type of game , one of the GTYPE_... enums. */ \
	SDWORD	ScrollMinX;		/* Scroll Limits */ \
	SDWORD	ScrollMinY; \
	UDWORD	ScrollMaxX; \
	UDWORD	ScrollMaxY; \
	STRING	levelName[MAX_LEVEL_SIZE]	//name of the level to load up when mid game

using SAVE_GAME_V7 = struct save_game_v7
{
  GAME_SAVE_V7;
};





#define TEMP_DROID_MAXPROGS	3
#define	SAVE_COMP_PROGRAM	8
#define SAVE_COMP_WEAPON	9




/*save DROID SAVE 11 */






//DROID_SAVE_18 replaces DROID_SAVE_14


//DROID_SAVE_20 replaces all previous saves uses 60 character names







using DROIDINIT_SAVEHEADER = struct _droidinit_save_header
{
  STRING aFileType[4];
  UDWORD version;
  UDWORD quantity;
};

using SAVE_DROIDINIT = struct _save_droidinit
{
  OBJECT_SAVE_V19;
};

/*
 *	STRUCTURE Definitions
 */

#define STRUCTURE_SAVE_V2 \
	OBJECT_SAVE_V19; \
	UBYTE				status; \
	SDWORD				currentBuildPts; \
	UDWORD				body; \
	UDWORD				armour; \
	UDWORD				resistance; \
	UDWORD				dummy1; \
	UDWORD				subjectInc;  /*research inc or factory prod id*/\
	UDWORD				timeStarted; \
	UDWORD				output; \
	UDWORD				capacity; \
	UDWORD				quantity

using SAVE_STRUCTURE_V2 = struct _save_structure_v2
{
  STRUCTURE_SAVE_V2;
};

#define STRUCTURE_SAVE_V12 \
	STRUCTURE_SAVE_V2; \
	UDWORD				factoryInc;			\
	UBYTE				loopsPerformed;		\
	UDWORD				powerAccrued;		\
	UDWORD				dummy2;			\
	UDWORD				droidTimeStarted;	\
	UDWORD				timeToBuild;		\
	UDWORD				timeStartHold


#define STRUCTURE_SAVE_V14 \
	STRUCTURE_SAVE_V12; \
	UBYTE	visible[MAX_PLAYERS]


#define STRUCTURE_SAVE_V15 \
	STRUCTURE_SAVE_V14; \
	char	researchName[MAX_SAVE_NAME_SIZE_V19]


#define STRUCTURE_SAVE_V17 \
	STRUCTURE_SAVE_V15;\
	SWORD				currentPowerAccrued

using SAVE_STRUCTURE_V17 = struct _save_structure_v17
{
  STRUCTURE_SAVE_V17;
};

#define STRUCTURE_SAVE_V20 \
	OBJECT_SAVE_V20; \
	UBYTE				status; \
	SDWORD				currentBuildPts; \
	UDWORD				body; \
	UDWORD				armour; \
	UDWORD				resistance; \
	UDWORD				dummy1; \
	UDWORD				subjectInc;  /*research inc or factory prod id*/\
	UDWORD				timeStarted; \
	UDWORD				output; \
	UDWORD				capacity; \
	UDWORD				quantity; \
	UDWORD				factoryInc;			\
	UBYTE				loopsPerformed;		\
	UDWORD				powerAccrued;		\
	UDWORD				dummy2;			\
	UDWORD				droidTimeStarted;	\
	UDWORD				timeToBuild;		\
	UDWORD				timeStartHold; \
	UBYTE				visible[MAX_PLAYERS]; \
	char				researchName[MAX_SAVE_NAME_SIZE]; \
	SWORD				currentPowerAccrued


#define STRUCTURE_SAVE_V21 \
	STRUCTURE_SAVE_V20; \
	UDWORD				commandId



//PROGRAMS NEED TO BE REMOVED FROM DROIDS - 7/8/98
// multiPlayerID for templates needs to be saved - 29/10/98
#define TEMPLATE_SAVE_V2 \
	CHAR				name[MAX_SAVE_NAME_SIZE_V19]; \
	UDWORD				ref; \
	UDWORD				player; \
	UBYTE				droidType; \
	STRING				asParts[DROID_MAXCOMP][MAX_SAVE_NAME_SIZE_V19]; \
	UDWORD				numWeaps; \
	STRING				asWeaps[TEMP_DROID_MAXPROGS][MAX_SAVE_NAME_SIZE_V19]; \
	UDWORD				numProgs; \
	STRING				asProgs[TEMP_DROID_MAXPROGS][MAX_SAVE_NAME_SIZE_V19]

// multiPlayerID for templates needs to be saved - 29/10/98
#define TEMPLATE_SAVE_V14 \
	CHAR				name[MAX_SAVE_NAME_SIZE_V19]; \
	UDWORD				ref; \
	UDWORD				player; \
	UBYTE				droidType; \
	STRING				asParts[DROID_MAXCOMP][MAX_SAVE_NAME_SIZE_V19]; \
	UDWORD				numWeaps; \
	STRING				asWeaps[TEMP_DROID_MAXPROGS][MAX_SAVE_NAME_SIZE_V19]; \
	UDWORD				multiPlayerID

#define TEMPLATE_SAVE_V20 \
	CHAR				name[MAX_SAVE_NAME_SIZE]; \
	UDWORD				ref; \
	UDWORD				player; \
	UBYTE				droidType; \
	STRING				asParts[DROID_MAXCOMP][MAX_SAVE_NAME_SIZE]; \
	UDWORD				numWeaps; \
	STRING				asWeaps[TEMP_DROID_MAXPROGS][MAX_SAVE_NAME_SIZE]; \
	UDWORD				multiPlayerID





#define FEATURE_SAVE_V2 \
	OBJECT_SAVE_V19

using SAVE_FEATURE_V2 = struct _save_feature_v2
{
  FEATURE_SAVE_V2;
};

#define FEATURE_SAVE_V14 \
	FEATURE_SAVE_V2; \
	UBYTE	visible[MAX_PLAYERS]

using SAVE_FEATURE_V14 = struct _save_feature_v14
{
  FEATURE_SAVE_V14;
};

#define FEATURE_SAVE_V20 \
	OBJECT_SAVE_V20; \
	UBYTE	visible[MAX_PLAYERS]



#define COMPLIST_SAVE_V6 \
	CHAR				name[MAX_SAVE_NAME_SIZE_V19]; \
	UBYTE				type; \
	UBYTE				state; \
	UBYTE				player

#define COMPLIST_SAVE_V20 \
	CHAR				name[MAX_SAVE_NAME_SIZE]; \
	UBYTE				type; \
	UBYTE				state; \
	UBYTE				player




#define STRUCTLIST_SAVE_V6 \
	CHAR				name[MAX_SAVE_NAME_SIZE_V19]; \
	UBYTE				state; \
	UBYTE				player

#define STRUCTLIST_SAVE_V20 \
	CHAR				name[MAX_SAVE_NAME_SIZE]; \
	UBYTE				state; \
	UBYTE				player




#define RESEARCH_SAVE_V8 \
	CHAR				name[MAX_SAVE_NAME_SIZE_V19]; \
	UBYTE				possible[MAX_PLAYERS]; \
	UBYTE				researched[MAX_PLAYERS]; \
	UDWORD				currentPoints[MAX_PLAYERS]

#define RESEARCH_SAVE_V20 \
	CHAR				name[MAX_SAVE_NAME_SIZE]; \
	UBYTE				possible[MAX_PLAYERS]; \
	UBYTE				researched[MAX_PLAYERS]; \
	UDWORD				currentPoints[MAX_PLAYERS]









#define STRUCTLIMITS_SAVE_V2 \
	CHAR				name[MAX_SAVE_NAME_SIZE_V19]; \
	UBYTE				limit; \
	UBYTE				player


#define STRUCTLIMITS_SAVE_V20 \
	CHAR				name[MAX_SAVE_NAME_SIZE]; \
	UBYTE				limit; \
	UBYTE				player



#define COMMAND_SAVE_V20 \
	UDWORD				droidID



/* The different types of droid */
using DROID_SAVE_TYPE = enum _droid_save_type
{
  DROID_NORMAL,
  // Weapon droid
  DROID_ON_TRANSPORT,
};

/***************************************************************************/
/*
 *	Local Variables
 */
/***************************************************************************/
extern UDWORD objID; // unique ID creation thing..

static UDWORD saveGameVersion = 0;

static UDWORD savedGameTime;
static UDWORD savedObjId;

static UDWORD HashedName;
static STRUCTURE* psStructList;
static FEATURE* psFeatureList;
static FLAG_POSITION** ppsCurrentFlagPosLists;
static SDWORD startX, startY;
static UDWORD width, height;
static UDWORD gameType;
static BOOL IsScenario;
/***************************************************************************/
/*
 *	Local ProtoTypes
 */
/***************************************************************************/
BOOL gameLoad(UBYTE* pFileData, UDWORD filesize);
static BOOL gameLoadV7(UBYTE* pFileData, UDWORD filesize);
static BOOL gameLoadV(UBYTE* pFileData, UDWORD filesize, UDWORD version);
static BOOL writeGameFile(STRING* pFileName, SDWORD saveType);
static BOOL writeMapFile(STRING* pFileName);

static BOOL loadSaveDroidInitV2(UBYTE* pFileData, UDWORD filesize, UDWORD quantity);

static BOOL loadSaveDroidInit(UBYTE* pFileData, UDWORD filesize);
static DROID_TEMPLATE* FindDroidTemplate(STRING* name, UDWORD player);

static BOOL loadSaveDroid(UBYTE* pFileData, UDWORD filesize, DROID** ppsCurrentDroidLists);
static BOOL loadSaveDroidV(UBYTE* pFileData, UDWORD filesize, UDWORD numDroids, UDWORD version, DROID** ppsCurrentDroidLists);
static BOOL loadDroidSetPointers(void);
static BOOL writeDroidFile(STRING* pFileName, DROID** ppsCurrentDroidLists);

static BOOL loadSaveStructure(UBYTE* pFileData, UDWORD filesize);
static BOOL loadSaveStructureV7(UBYTE* pFileData, UDWORD filesize, UDWORD numStructures);
static BOOL loadStructSetPointers(void);
static BOOL writeStructFile(STRING* pFileName);

static BOOL loadSaveTemplate(UBYTE* pFileData, UDWORD filesize);
static BOOL loadSaveTemplateV(UBYTE* pFileData, UDWORD filesize, UDWORD numTemplates);
static BOOL writeTemplateFile(STRING* pFileName);

static BOOL loadSaveFeature(UBYTE* pFileData, UDWORD filesize);
static BOOL loadSaveFeatureV14(UBYTE* pFileData, UDWORD filesize, UDWORD numFeatures, UDWORD version);
static BOOL writeFeatureFile(STRING* pFileName);

BOOL loadTerrainTypeMap(UBYTE* pFileData, UDWORD filesize); // now used in gamepsx.c aswell

static BOOL loadSaveCompList(UBYTE* pFileData, UDWORD filesize);
static BOOL loadSaveCompListV(UBYTE* pFileData, UDWORD filesize, UDWORD numRecords, UDWORD version);

static BOOL loadSaveStructTypeList(UBYTE* pFileData, UDWORD filesize);
static BOOL loadSaveStructTypeListV(UBYTE* pFileData, UDWORD filesize, UDWORD numRecords);

static BOOL loadSaveResearch(UBYTE* pFileData, UDWORD filesize);
static BOOL loadSaveResearchV(UBYTE* pFileData, UDWORD filesize, UDWORD numRecords);

static BOOL loadSaveMessage(UBYTE* pFileData, UDWORD filesize, SWORD levelType);
static BOOL loadSaveMessageV(UBYTE* pFileData, UDWORD filesize, UDWORD numMessages, UDWORD version, SWORD levelType);


static BOOL getNameFromComp(UDWORD compType, STRING* pDest, UDWORD compIndex);

//adjust the name depending on type of save game and whether resourceNames are used
static BOOL getSaveObjectName(STRING* pName);

/* set the global scroll values to use for the save game */
static void setMapScroll();

char* getSaveStructNameV19(SAVE_STRUCTURE_V17* psSaveStructure) { return (psSaveStructure->name); }

/*This just loads up the .gam file to determine which level data to set up - split up
so can be called in levLoadData when starting a game from a load save game*/

//
// GameIsLevelStart is always TRUE on both PC & PSX versions !!!
//

// -----------------------------------------------------------------------------------------
BOOL loadGameInit(STRING* pGameToLoad)
{
  UBYTE* pFileData = nullptr;
  UDWORD fileSize;

  /* Load in the chosen file data */

  pFileData = DisplayBuffer;
  if (!loadFileToBuffer(pGameToLoad, pFileData, displayBufferSize, &fileSize))
  {
    Neuron::DebugTrace("loadgame: Fail2\n");
    goto error;
  }

  if (!gameLoad(pFileData, fileSize))
  {
    Neuron::DebugTrace("loadgame: Fail4\n");
    goto error;
  }
  return TRUE;

error: Neuron::DebugTrace("loadgame: ERROR\n");

  /* Start the game clock */
  gameTimeStart();

  //		bMultiPlayer = TRUE;				// reenable multi player messages.
  return FALSE;
}

// -----------------------------------------------------------------------------------------
BOOL loadGame(STRING* pGameToLoad, BOOL keepObjects, BOOL freeMem)
{
  STRING aFileName[256];
  UDWORD fileExten, fileSize;
  UBYTE* pFileData = nullptr;
  UDWORD player;

  Neuron::DebugTrace("loadGame\n");

  /* Stop the game clock */
  gameTimeStop();


  /* Clear all the objects off the map and free up the map memory */
  proj_FreeAllProjectiles(); //always clear this
  if (freeMem)
  {
    //clear out the audio
    AudioSystem::StopAll();

    freeAllDroids();
    freeAllStructs();
    freeAllFeatures();

    if (psMapTiles) {}
    if (aMapLinePoints) { delete[] aMapLinePoints; }
    //clear all the messages?
    releaseAllProxDisp();
  }

  if (!keepObjects)
  {
    //initialise the lists
    for (player = 0; player < MAX_PLAYERS; player++)
    {
      apsDroidLists[player] = nullptr;
      apsStructLists[player] = nullptr;
      apsFeatureLists[player] = nullptr;
      apsFlagPosLists[player] = nullptr;
      //clear all the messages?
      apsProxDisp[player] = nullptr;
    }
    initFactoryNumFlag();
  }


  //Stuff added after level load to avoid being reset or initialised during load

  //clear the player Power structs
  if (!keepObjects)
    clearPlayerPower();

  //initialise the scroll values

  //before loading the data - turn power off so don't get any power low warnings
  powerCalculated = FALSE;

  strcpy(aFileName, pGameToLoad);
  fileExten = strlen(aFileName) - 3; // hack - !
  aFileName[fileExten - 1] = '\0';
  strcat(aFileName, "\\");

  //if (freeMem) - this now works for Cam Start and Cam Change
  if (gameType != GTYPE_SCENARIO_EXPAND)
  {
    LOADBARCALLBACK(); //		loadingScreenCallback();
    //load in the terrain type map
    aFileName[fileExten] = '\0';
    strcat(aFileName, "TTypes.ttp");
    /* Load in the chosen file data */
    pFileData = DisplayBuffer;
    if (!loadFileToBuffer(aFileName, pFileData, displayBufferSize, &fileSize))
    {
      Neuron::DebugTrace("loadgame: Fail23\n");
      goto error;
    }

    //load the terrain type data
    if (pFileData)
    {
      if (!loadTerrainTypeMap(pFileData, fileSize))
      {
        Neuron::DebugTrace("loadgame: Fail25\n");
        goto error;
      }
    }
  }

  //load up the Droid Templates BEFORE any structures are loaded
  LOADBARCALLBACK(); //	loadingScreenCallback();


  //if Campaign Expand then don't load in another map
  if (gameType != GTYPE_SCENARIO_EXPAND)
  {
    LOADBARCALLBACK(); //		loadingScreenCallback();
    psMapTiles = nullptr;
    aMapLinePoints = nullptr;
    //load in the map file 
    aFileName[fileExten] = '\0';
    strcat(aFileName, "game.map");
    /* Load in the chosen file data */
    pFileData = DisplayBuffer;
    if (!loadFileToBuffer(aFileName, pFileData, displayBufferSize, &fileSize))
    {
      Neuron::DebugTrace("loadgame: Fail5\n");
      goto error;
    }

    // on the PSX we check for a fail ... in case we run out of mem (likely!)
    // well if it is good enough for the PSX it's good enough for the PC - John.
    if (!mapLoad(pFileData, fileSize))
    {
      Neuron::DebugTrace("loadgame: Fail7\n");
      return (FALSE);
    }

#ifdef JOHN
    // load in the gateway map
    /*		aFileName[fileExten] = '\0';
        strcat(aFileName, "gates.txt");
        // Load in the chosen file data
    #ifdef WIN32
        pFileData = DisplayBuffer;
        if (!loadFileToBuffer(aFileName, pFileData, displayBufferSize, &fileSize))
        {
          DBPRINTF(("loadgame: Failed to load gates.txt\n"));
          goto error;
        }
    #else
        if (!LoadGameLoad(aFileName,&pFileData,&fileSize,UserSaveGame)) goto error;
    #endif
    
        if (!gwLoadGateways(pFileData, fileSize))
        {
          DBPRINTF(("loadgame: failed to parse gates.txt"));
          return FALSE;
        }*/
#endif
  }

  //save game stuff added after map load

  LOADBARCALLBACK(); //	loadingScreenCallback();

  //adjust the scroll range for the new map or the expanded map
  setMapScroll();

  //initialise the Templates' build and power points before loading in any droids
  initTemplatePoints();

  //if user save game then load up the research BEFORE any droids or structures are loaded

    LOADBARCALLBACK(); //		loadingScreenCallback();
    //load in the droid initialisation file
    aFileName[fileExten] = '\0';
    strcat(aFileName, "dinit.bjo");
    /* Load in the chosen file data */
    pFileData = DisplayBuffer;
    if (!loadFileToBuffer(aFileName, pFileData, displayBufferSize, &fileSize))
    {
      Neuron::DebugTrace("loadgame: Fail8\n");
      goto error;
    }

    if (!loadSaveDroidInit(pFileData, fileSize))
    {
      Neuron::DebugTrace("loadgame: Fail10\n");
      goto error;
    }

  LOADBARCALLBACK(); //	loadingScreenCallback();
  //21feb	if (saveGameOnMission && UserSaveGame)

  LOADBARCALLBACK(); //	loadingScreenCallback();
  //load in the features -do before the structures
  aFileName[fileExten] = '\0';
  strcat(aFileName, "feat.bjo");
  /* Load in the chosen file data */
  pFileData = DisplayBuffer;
  if (!loadFileToBuffer(aFileName, pFileData, displayBufferSize, &fileSize))
  {
    Neuron::DebugTrace("loadgame: Fail14\n");
    goto error;
  }

  //load the data into apsFeatureLists
  if (!loadSaveFeature(pFileData, fileSize))
  {
    Neuron::DebugTrace("loadgame: Fail16\n");
    goto error;
  }

  //load droid templates moved from here to BEFORE any structures loaded in

  //load in the structures
  LOADBARCALLBACK(); //	loadingScreenCallback();
  aFileName[fileExten] = '\0';
  strcat(aFileName, "struct.bjo");
  /* Load in the chosen file data */
  pFileData = DisplayBuffer;
  if (!loadFileToBuffer(aFileName, pFileData, displayBufferSize, &fileSize))
  {
    Neuron::DebugTrace("loadgame: Fail17\n");
    goto error;
  }
  //load the data into apsStructLists
  if (!loadSaveStructure(pFileData, fileSize))
  {
    Neuron::DebugTrace("loadgame: Fail19\n");
    goto error;
  }

  LOADBARCALLBACK(); //	loadingScreenCallback();

  //if user save game then load up the current level for structs and components

  LOADBARCALLBACK(); //	loadingScreenCallback();


  LOADBARCALLBACK(); //	loadingScreenCallback();

  LOADBARCALLBACK(); //	loadingScreenCallback();


  LOADBARCALLBACK(); //	loadingScreenCallback();


  LOADBARCALLBACK(); //	loadingScreenCallback();


  LOADBARCALLBACK(); //	loadingScreenCallback();


  LOADBARCALLBACK(); //	loadingScreenCallback();

    //load in the structure limits
    //load the data into structLimits DONE IN SCRIPTS NOW so just init
    initStructLimits();

    //set up the structure Limits
    setCurrentStructQuantity(TRUE);

  LOADBARCALLBACK(); //	loadingScreenCallback();

  //check that delivery points haven't been put down in invalid location
  checkDeliveryPoints(saveGameVersion);


  /* Reset the player AI */
  playerReset();

  //turn power on for rest of game
  powerCalculated = TRUE;

  LOADBARCALLBACK(); //	loadingScreenCallback();


  LOADBARCALLBACK(); //	loadingScreenCallback();

  //don't need to do this anymore - AB 22/04/98
  //set up the power levels for each player if not

  //set all players to have some power at start - will be scripted!

  //set these values to suitable for first map - will be scripted!

  //if user save game then reset the time - THIS SETS BOTH TIMERS - BEWARE IF YOU USE IT

  //check the research button isn't flashing unnecessarily
  //cancel first
  stopReticuleButtonFlash(IDRET_RESEARCH);
  //then see if needs to be set
  intCheckResearchButton();

  //set up the mission countdown flag
  setMissionCountDown();

  /* Start the game clock */
  gameTimeStart();

  //after the clock has been reset need to check if any res_extractors are active
  checkResExtractorsActive();

  //		bMultiPlayer = TRUE;				// reenable multi player messages.
  setViewAngle(INITIAL_STARTING_PITCH);
  setDesiredPitch(DirectX::XMConvertToRadians(INITIAL_DESIRED_PITCH));


  //need to clear before setting up
  clearMissionWidgets();
  //put any widgets back on for the missions
  resetMissionWidgets();

  Neuron::DebugTrace("loadGame: done\n");

  return TRUE;

error: Neuron::DebugTrace("loadgame: ERROR\n");

  /* Clear all the objects off the map and free up the map memory */
  freeAllDroids();
  freeAllStructs();
  freeAllFeatures();
  droidTemplateShutDown();
  if (psMapTiles) {}
  if (aMapLinePoints) { delete[] aMapLinePoints; }
  psMapTiles = nullptr;
  aMapLinePoints = nullptr;

  /*if (!loadFile("blank.map", &pFileData, &fileSize))
  {
    return FALSE;
  }

  if (!mapLoad(pFileData, fileSize))
  {
    return FALSE;
  }

  FREE(pFileData);*/

  /* Start the game clock */
  gameTimeStart();
  //		bMultiPlayer = TRUE;				// reenable multi player messages.

  return FALSE;
}


// -----------------------------------------------------------------------------------------
BOOL gameLoad(UBYTE* pFileData, UDWORD filesize)
{
  GAME_SAVEHEADER* psHeader;

  /* Check the file type */
  psHeader = (GAME_SAVEHEADER*)pFileData;
  if (psHeader->aFileType[0] != 'g' || psHeader->aFileType[1] != 'a' || psHeader->aFileType[2] != 'm' || psHeader->aFileType[3] != 'e')
  {
    Neuron::Fatal("gameLoad: Incorrect file type");
    return FALSE;
  }

  //increment to the start of the data
  pFileData += GAME_HEADER_SIZE;

  Neuron::DebugTrace("gl .gam file is version {}\n",psHeader->version);
  //set main version Id from game file
  saveGameVersion = psHeader->version;

  /* Check the file version */
  if (psHeader->version < VERSION_7)
  {
    Neuron::Fatal("gameLoad: unsupported save format version {}",psHeader->version);
    return FALSE;
  }
  if (psHeader->version < VERSION_9)
  {
    if (!gameLoadV7(pFileData, filesize))
      return FALSE;
  }
  else
  {
    /* Versions 9-33 were the user save formats. Saves are gone; every
     * shipped .gam is version 8 or below. */
    Neuron::Fatal("gameLoad: unsupported save format version {}",psHeader->version);
    return FALSE;
  }

  return TRUE;
}

// -----------------------------------------------------------------------------------------
/* code specific to version 7 of a save game */
BOOL gameLoadV7(UBYTE* pFileData, UDWORD filesize)
{
  SAVE_GAME_V7* psSaveGame;
  LEVEL_DATASET* psNewLevel;

  psSaveGame = (SAVE_GAME_V7*)pFileData;

  if ((sizeof(SAVE_GAME_V7) + GAME_HEADER_SIZE) > filesize)
  {
    Neuron::Fatal("gameLoad: unexpected end of file");
    return FALSE;
  }

  savedGameTime = psSaveGame->gameTime;

  //set the scroll varaibles
  startX = psSaveGame->ScrollMinX;
  startY = psSaveGame->ScrollMinY;
  width = psSaveGame->ScrollMaxX - psSaveGame->ScrollMinX;
  height = psSaveGame->ScrollMaxY - psSaveGame->ScrollMinY;
  gameType = psSaveGame->GameType;
  /* Shipped .gam files carry scenario types only; the GTYPE_SAVE_START branch
   * that re-entered levLoadData here was ancient (v7-8) user-save loading. */
  IsScenario = TRUE;

  return TRUE;
}

// -----------------------------------------------------------------------------------------
// Process the droid initialisation file (dinit.bjo). Creates droids for
// the scenario being loaded. This is *NEVER* called for a user save game
//
BOOL loadSaveDroidInit(UBYTE* pFileData, UDWORD filesize)
{
  DROIDINIT_SAVEHEADER* psHeader;

  /* Check the file type */
  psHeader = (DROIDINIT_SAVEHEADER*)pFileData;
  if (psHeader->aFileType[0] != 'd' || psHeader->aFileType[1] != 'i' || psHeader->aFileType[2] != 'n' || psHeader->aFileType[3] != 't')
  {
    Neuron::Fatal("loadSaveUnitInit: Incorrect file type");
    return FALSE;
  }

  //increment to the start of the data
  pFileData += DROIDINIT_HEADER_SIZE;

  /* Check the file version */
  if (psHeader->version < VERSION_7)
  {
    Neuron::Fatal("UnitInit; unsupported save format version {}",psHeader->version);
    return FALSE;
  }
  if (psHeader->version <= CURRENT_VERSION_NUM)
  {
    if (!loadSaveDroidInitV2(pFileData, filesize, psHeader->quantity))
      return FALSE;
  }
  else
  {
    Neuron::Fatal("UnitInit: undefined save format version {}",psHeader->version);
    return FALSE;
  }

  return TRUE;
}

// -----------------------------------------------------------------------------------------
// Used for all droids
BOOL loadSaveDroidInitV2(UBYTE* pFileData, UDWORD filesize, UDWORD quantity)
{
  SAVE_DROIDINIT* pDroidInit;
  DROID_TEMPLATE* psTemplate;
  DROID* psDroid;
  UDWORD i;
  UDWORD NumberOfSkippedDroids = 0;

  UNUSEDPARAMETER(filesize);

  pDroidInit = (SAVE_DROIDINIT*)pFileData;

  for (i = 0; i < quantity; i++)
  {
    pDroidInit->player = RemapPlayerNumber(pDroidInit->player);

    if (pDroidInit->player >= MAX_PLAYERS)
    {
      pDroidInit->player = MAX_PLAYERS - 1; // now don't lose any droids ... force them to be the last player
      NumberOfSkippedDroids++;
    }

    psTemplate = FindDroidTemplate(pDroidInit->name, pDroidInit->player);

    if (psTemplate == nullptr)
    {
      Neuron::DebugTrace("loadSaveUnitInitV2:\nUnable to find template for {} player {}", pDroidInit->name,pDroidInit->player);

#ifdef DEBUG
#endif
    }
    else
    {
      // Need to set apCompList[pDroidInit->player][componenttype][compid] = AVAILABLE for each droid.

      {
        psDroid = buildDroid(psTemplate, (pDroidInit->x & (~TILE_MASK)) + TILE_UNITS / 2, (pDroidInit->y & (~TILE_MASK)) + TILE_UNITS / 2,
                             pDroidInit->player, FALSE);

        if (psDroid)
        {
          psDroid->id = pDroidInit->id;
          // on-disk degrees in [0, 360), wrapped into the engine's (-pi, pi]
          psDroid->direction = DirectX::XMScalarModAngle(DirectX::XMConvertToRadians(static_cast<float>(pDroidInit->direction)));
          addDroid(psDroid, apsDroidLists);
        }
        else
          Neuron::Fatal("This droid cannot be built - {}", pDroidInit->name);
      }
    }
    pDroidInit++;
  }

  if (NumberOfSkippedDroids)
  {
    Neuron::Fatal("unitLoad: Bad Player number in {} unit(s)... assigned to the last player!\n", NumberOfSkippedDroids);
  }
  return TRUE;
}

// -----------------------------------------------------------------------------------------
DROID_TEMPLATE* FindDroidTemplate(STRING* name, UDWORD player)
{
  UDWORD TempPlayer;
  DROID_TEMPLATE* Template;
  UDWORD id;

  UNUSEDPARAMETER(player);

  /*#ifdef RESOURCE_NAMES
  
    //get the name from the resource associated with it 
    if (!strresGetIDNum(psStringRes, name, &id))
    {
      DBERROR(("Cannot find resource for template - %s", name));
      return NULL;
    }
    //get the string from the id
    name = strresGetString(psStringRes, id);
  
  #else
  
    UNUSEDPARAMETER(id);
  
  #endif*/

  //get the name from the resource associated with it 
  if (!strresGetIDNum(psStringRes, name, &id))
  {
    Neuron::Fatal("Cannot find resource for template - {}", name);
    return nullptr;
  }
  //get the string from the id
  name = strresGetString(psStringRes, id);

  for (TempPlayer = 0; TempPlayer < MAX_PLAYERS; TempPlayer++)
  {
    Template = apsDroidTemplates[TempPlayer];

    while (Template)
    {
      if (strcmp(name, Template->aName) == 0)
        return Template;
      Template = Template->psNext;
    }
  }

  return nullptr;
}

// -----------------------------------------------------------------------------------------
UDWORD RemapPlayerNumber(UDWORD OldNumber) { return (OldNumber); }

// -----------------------------------------------------------------------------------------
BOOL loadSaveStructure(UBYTE* pFileData, UDWORD filesize)
{
  STRUCT_SAVEHEADER* psHeader;

  /* Check the file type */
  psHeader = (STRUCT_SAVEHEADER*)pFileData;
  if (psHeader->aFileType[0] != 's' || psHeader->aFileType[1] != 't' || psHeader->aFileType[2] != 'r' || psHeader->aFileType[3] != 'u')
  {
    Neuron::Fatal("loadSaveStructure: Incorrect file type");
    return FALSE;
  }

  //increment to the start of the data
  pFileData += STRUCT_HEADER_SIZE;

  /* Check the file version */
  if (psHeader->version < VERSION_7)
  {
    Neuron::Fatal("StructLoad: unsupported save format version {}",psHeader->version);
    return FALSE;
  }
  if (psHeader->version < VERSION_9)
  {
    if (!loadSaveStructureV7(pFileData, filesize, psHeader->quantity))
      return FALSE;
  }
  else
  {
    /* Versions 9-33 were the user save formats; saves are gone and shipped
     * struct.bjo files are version 8. */
    Neuron::Fatal("StructLoad: unsupported save format version {}",psHeader->version);
    return FALSE;
  }

  return TRUE;
}

// -----------------------------------------------------------------------------------------
/* code specific to version 7 of a save structure */
BOOL loadSaveStructureV7(UBYTE* pFileData, UDWORD filesize, UDWORD numStructures)
{
  SAVE_STRUCTURE_V2 *psSaveStructure, sSaveStructure;
  STRUCTURE* psStructure;
  REPAIR_FACILITY* psRepair;
  STRUCTURE_STATS* psStats = nullptr;
  UDWORD count, statInc;
  BOOL found;
  UDWORD NumberOfSkippedStructures = 0;
  UDWORD burnTime;

  psSaveStructure = &sSaveStructure;

  if ((sizeof(SAVE_STRUCTURE_V2) * numStructures + STRUCT_HEADER_SIZE) > filesize)
  {
    Neuron::Fatal("structureLoad: unexpected end of file");
    return FALSE;
  }

  /* Load in the structure data */
  for (count = 0; count < numStructures; count++, pFileData += sizeof(SAVE_STRUCTURE_V2))
  {
    memcpy(psSaveStructure, pFileData, sizeof(SAVE_STRUCTURE_V2));

    psSaveStructure->player = RemapPlayerNumber(psSaveStructure->player);

    if (psSaveStructure->player >= MAX_PLAYERS)
    {
      psSaveStructure->player = MAX_PLAYERS - 1;
      NumberOfSkippedStructures++;
    }
    //get the stats for this structure
    found = FALSE;

    if (!getSaveObjectName(psSaveStructure->name))
      continue;

    for (statInc = 0; statInc < numStructureStats; statInc++)
    {
      psStats = asStructureStats + statInc;
      //loop until find the same name

      if (!strcmp(psStats->pName, psSaveStructure->name))
      {
        found = TRUE;
        break;
      }
    }
    //if haven't found the structure - ignore this record!
    if (!found)
    {
      Neuron::Fatal("This structure no longer exists - {}",getSaveStructNameV19((SAVE_STRUCTURE_V17*)psSaveStructure));
      continue;
    }
    /*create the Structure */
    //psStructure = buildStructure((asStructureStats + psSaveStructure->
    //	structureInc), psSaveStructure->x, psSaveStructure->y, 

    //for modules - need to check the base structure exists
    if (IsStatExpansionModule(psStats))
    {
      psStructure = getTileStructure(psSaveStructure->x >> TILE_SHIFT, psSaveStructure->y >> TILE_SHIFT);
      if (psStructure == nullptr)
      {
        Neuron::Fatal("No owning structure for module - {} for player - {}",
          getSaveStructNameV19((SAVE_STRUCTURE_V17*)psSaveStructure), psSaveStructure->player);
        //ignore this module
        continue;
      }
    }

    //check not too near the edge

    //check not trying to build too near the edge
    if (((psSaveStructure->x >> TILE_SHIFT) < TOO_NEAR_EDGE) || ((psSaveStructure->x >> TILE_SHIFT) > static_cast<SDWORD>(mapWidth -
      TOO_NEAR_EDGE)))
    {
      Neuron::Fatal("Structure {}, x coord too near the edge of the map. id - {}",
        getSaveStructNameV19((SAVE_STRUCTURE_V17*)psSaveStructure), psSaveStructure->id);
      continue;
    }
    if (((psSaveStructure->y >> TILE_SHIFT) < TOO_NEAR_EDGE) || ((psSaveStructure->y >> TILE_SHIFT) > static_cast<SDWORD>(mapHeight -
      TOO_NEAR_EDGE)))
    {
      Neuron::Fatal("Structure {}, y coord too near the edge of the map. id - {}",
        getSaveStructNameV19((SAVE_STRUCTURE_V17*)psSaveStructure), psSaveStructure->id);
      continue;
    }

    psStructure = buildStructure(psStats, psSaveStructure->x, psSaveStructure->y, psSaveStructure->player,TRUE);
    if (!psStructure)
    {
      DEBUG_ASSERT_TEXT(FALSE, "loadSaveStructure:Unable to create structure");
      return FALSE;
    }

    /*The original code here didn't work and so the scriptwriters worked 
    round it by using the module ID - so making it work now will screw up 
    the scripts -so in ALL CASES overwrite the ID!*/
    //don't copy the module's id etc 
    //if (IsStatExpansionModule(psStats)==FALSE)
    {
      //copy the values across
      psStructure->id = psSaveStructure->id;
      //are these going to ever change from the values set up with?
      // on-disk degrees in [0, 360), wrapped into the engine's (-pi, pi]
      psStructure->direction = DirectX::XMScalarModAngle(DirectX::XMConvertToRadians(static_cast<float>(psSaveStructure->direction)));
    }

    psStructure->inFire = psSaveStructure->inFire;
    psStructure->burnDamage = psSaveStructure->burnDamage;
    burnTime = psSaveStructure->burnStart;
    psStructure->burnStart = burnTime;

    psStructure->status = psSaveStructure->status;
    if (psStructure->status == SS_BUILT)
      buildingComplete(psStructure);

    //if not a save game, don't want to overwrite any of the stats so continue
    if (gameType != GTYPE_SAVE_START)
      continue;

    psStructure->currentBuildPts = static_cast<SWORD>(psSaveStructure->currentBuildPts);
    switch (psStructure->pStructureType->type)
    {
    case REF_FACTORY:
      //NOT DONE AT PRESENT
      ((FACTORY*)psStructure->pFunctionality)->capacity = static_cast<UBYTE>(psSaveStructure->capacity);
      //((FACTORY *)psStructure->pFunctionality)->productionOutput = psSaveStructure->
      //((FACTORY *)psStructure->pFunctionality)->quantity = psSaveStructure->
      //((FACTORY *)psStructure->pFunctionality)->timeStarted = gameTime -
      //((FACTORY*)psStructure->pFunctionality)->timeToBuild = ((DROID_TEMPLATE *)
      //	psSaveStructure->subjectInc)->buildPoints / ((FACTORY *)psStructure->pFunctionality)->

      //adjust the module structures IMD
      if (psSaveStructure->capacity) { psStructure->sDisplay.imd = factoryModuleIMDs[psSaveStructure->capacity - 1][0]; }
      break;
    case REF_RESEARCH:
      ((RESEARCH_FACILITY*)psStructure->pFunctionality)->capacity = psSaveStructure->capacity;
      ((RESEARCH_FACILITY*)psStructure->pFunctionality)->researchPoints = psSaveStructure->output;
      ((RESEARCH_FACILITY*)psStructure->pFunctionality)->timeStarted = (psSaveStructure->timeStarted);
      if (psSaveStructure->subjectInc != -1)
      {
        ((RESEARCH_FACILITY*)psStructure->pFunctionality)->psSubject = (BASE_STATS*)(asResearch + psSaveStructure->subjectInc);
        ((RESEARCH_FACILITY*)psStructure->pFunctionality)->timeToResearch = (asResearch + psSaveStructure->subjectInc)->researchPoints / ((
          RESEARCH_FACILITY*)psStructure->pFunctionality)->researchPoints;
      }
      //adjust the module structures IMD
      if (psSaveStructure->capacity) { psStructure->sDisplay.imd = researchModuleIMDs[psSaveStructure->capacity - 1]; }
      break;
    case REF_REPAIR_FACILITY: //CODE THIS SOMETIME
      psRepair = ((REPAIR_FACILITY*)psStructure->pFunctionality);
      psRepair->psDeliveryPoint = nullptr;
      psRepair->psObj = nullptr;
      psRepair->currentPtsAdded = 0;
      break;
    }
  }

  if (NumberOfSkippedStructures > 0)
    Neuron::Fatal("structureLoad: invalid player number in {} structures ... assigned to the last player!\n\n",NumberOfSkippedStructures);

  return TRUE;
}

// -----------------------------------------------------------------------------------------
//return id of a research topic based on the name
UDWORD getResearchIdFromName(STRING* pName)
{
  UDWORD inc;

  for (inc = 0; inc < numResearch; inc++)
  {
    if (!strcmp(asResearch[inc].pName, pName))
      return inc;
  }

  Neuron::Fatal("Unknown research - {}", pName);
  return UDWORD_MAX;
}


// -----------------------------------------------------------------------------------------
BOOL loadSaveFeature(UBYTE* pFileData, UDWORD filesize)
{
  FEATURE_SAVEHEADER* psHeader;

  /* Check the file type */
  psHeader = (FEATURE_SAVEHEADER*)pFileData;
  if (psHeader->aFileType[0] != 'f' || psHeader->aFileType[1] != 'e' || psHeader->aFileType[2] != 'a' || psHeader->aFileType[3] != 't')
  {
    Neuron::Fatal("loadSaveFeature: Incorrect file type");
    return FALSE;
  }

  //increment to the start of the data
  pFileData += FEATURE_HEADER_SIZE;

  /* Check the file version */
  if (psHeader->version < VERSION_7)
  {
    Neuron::Fatal("FeatLoad: unsupported save format version {}",psHeader->version);
    return FALSE;
  }
  if (psHeader->version < VERSION_9)
  {
    if (!loadSaveFeatureV14(pFileData, filesize, psHeader->quantity, psHeader->version))
      return FALSE;
  }
  else
  {
    /* Versions 9-33 were the user save formats; saves are gone and shipped
     * feat.bjo files are version 8. */
    Neuron::Fatal("FeatLoad: unsupported save format version {}",psHeader->version);
    return FALSE;
  }

  return TRUE;
}


// -----------------------------------------------------------------------------------------
/* code for all version 8 - 14 save features */
BOOL loadSaveFeatureV14(UBYTE* pFileData, UDWORD filesize, UDWORD numFeatures, UDWORD version)
{
  SAVE_FEATURE_V14* psSaveFeature;
  FEATURE* pFeature;
  UDWORD count, i, statInc;
  FEATURE_STATS* psStats = nullptr;
  BOOL found;
  UDWORD sizeOfSaveFeature;

  if (version < VERSION_14)
    sizeOfSaveFeature = sizeof(SAVE_FEATURE_V2);
  else
    sizeOfSaveFeature = sizeof(SAVE_FEATURE_V14);

  if ((sizeOfSaveFeature * numFeatures + FEATURE_HEADER_SIZE) > filesize)
  {
    Neuron::Fatal("featureLoad: unexpected end of file");
    return FALSE;
  }

  /* Load in the feature data */
  for (count = 0; count < numFeatures; count++, pFileData += sizeOfSaveFeature)
  {
    psSaveFeature = (SAVE_FEATURE_V14*)pFileData;

    //get the stats for this feature
    found = FALSE;

    if (!getSaveObjectName(psSaveFeature->name))
      continue;

    for (statInc = 0; statInc < numFeatureStats; statInc++)
    {
      psStats = asFeatureStats + statInc;
      //loop until find the same name

      if (!strcmp(psStats->pName, psSaveFeature->name))
      {
        found = TRUE;
        break;
      }
    }
    //if haven't found the feature - ignore this record!
    if (!found)
    {
      Neuron::Fatal("This feature no longer exists - {}",psSaveFeature->name);

      continue;
    }
    //create the Feature
    //buildFeature(asFeatureStats + psSaveFeature->featureInc, 
    pFeature = buildFeature(psStats, psSaveFeature->x, psSaveFeature->y,TRUE);
    //will be added to the top of the linked list
    if (!pFeature)
    {
      DEBUG_ASSERT_TEXT(FALSE, "loadSaveFeature:Unable to create feature");
      return FALSE;
    }
    //restore values
    pFeature->id = psSaveFeature->id;
    // on-disk degrees in [0, 360), wrapped into the engine's (-pi, pi]
    pFeature->direction = DirectX::XMScalarModAngle(DirectX::XMConvertToRadians(static_cast<float>(psSaveFeature->direction)));
    pFeature->inFire = psSaveFeature->inFire;
    pFeature->burnDamage = psSaveFeature->burnDamage;
    if (version >= VERSION_14)
    {
      for (i = 0; i < MAX_PLAYERS; i++)
      {
        pFeature->visible[i] = psSaveFeature->visible[i];
        //set the Tile flag if visible for the selectedPlayer
        if ((i == selectedPlayer) AND (pFeature->visible[i] == UBYTE_MAX))
          setFeatTileDraw(pFeature);
      }
    }
  }

  return TRUE;
}

// -----------------------------------------------------------------------------------------

// load up a terrain tile type map file
BOOL loadTerrainTypeMap(UBYTE* pFileData, UDWORD filesize)
{
  TILETYPE_SAVEHEADER* psHeader;
  UDWORD i;
  UWORD* pType;

  if (filesize < TILETYPE_HEADER_SIZE)
  {
    Neuron::Fatal("loadTerrainTypeMap: file too small");
    return FALSE;
  }

  // Check the header
  psHeader = (TILETYPE_SAVEHEADER*)pFileData;
  if (psHeader->aFileType[0] != 't' || psHeader->aFileType[1] != 't' || psHeader->aFileType[2] != 'y' || psHeader->aFileType[3] != 'p')
  {
    Neuron::Fatal("loadTerrainTypeMap: Incorrect file type");
    return FALSE;
  }
  /*	Version doesn't matter for now
    if (psHeader->version != VERSION_2)
    {
      DBERROR(("loadTerrainTypeMap: Incorrect file version"));
      return FALSE;
    }*/

  // reset the terrain table
  memset(terrainTypes, 0, sizeof(terrainTypes));

  // Load the terrain type mapping
  pType = (UWORD*)(pFileData + TILETYPE_HEADER_SIZE);
  for (i = 0; i < psHeader->quantity; i++)
  {
    if (i >= MAX_TILE_TEXTURES)
    {
      Neuron::Fatal("loadTerrainTypeMap: too many types");
      return FALSE;
    }
    if (*pType > TER_MAX)
    {
      Neuron::Fatal("loadTerrainTypeMap: terrain type out of range");
      return FALSE;
    }

    terrainTypes[i] = static_cast<UBYTE>(*pType);
    pType += 1;
  }

  return TRUE;
}



// -----------------------------------------------------------------------------------------
/* set the global scroll values to use for the save game */
void setMapScroll()
{
  //if loading in a pre version5 then scroll values will not have been set up so set to max poss
  if (width == 0 AND height == 0)
  {
    scrollMinX = 0;
    scrollMaxX = mapWidth;
    scrollMinY = 0;
    scrollMaxY = mapHeight;
    return;
  }
  scrollMinX = startX;
  scrollMinY = startY;
  scrollMaxX = startX + width;
  scrollMaxY = startY + height;
  //check not going beyond width/height of map
  if (scrollMaxX > static_cast<SDWORD>(mapWidth))
  {
    scrollMaxX = mapWidth;
    Neuron::DebugTrace("scrollMaxX was too big It has been set to map width");
  }
  if (scrollMaxY > static_cast<SDWORD>(mapHeight))
  {
    scrollMaxY = mapHeight;
    Neuron::DebugTrace("scrollMaxY was too big It has been set to map height");
  }
}

// -----------------------------------------------------------------------------------------
BOOL getSaveObjectName(STRING* pName)
{
#ifdef RESOURCE_NAMES

  UDWORD id;

  //check not a user save game
  if (IsScenario)
  {
    //see if the name has a resource associated with it by trying to get the ID for the string
    if (!strresGetIDNum(psStringRes, pName, &id))
    {
      Neuron::Fatal("Cannot find string resource {}", pName);
      return FALSE;
    }

    //get the string from the id if one exists
    strcpy(pName, strresGetString(psStringRes, id));
  }
#else

  //don't do anything with the name
  UNUSEDPARAMETER(pName);

#endif

  return TRUE;
}

// -----------------------------------------------------------------------------------------
/*returns the current type of save game being loaded*/

// -----------------------------------------------------------------------------------------
SDWORD getCompFromNamePreV7(UDWORD compType, STRING* pName)
{
#ifndef RESOURCE_NAMES

  BASE_STATS* psStats = nullptr;
  UDWORD numStats = 0, count, statSize = 0, id;
  STRING* pTranslatedName;

  switch (compType)
  {
  case COMP_BODY:
    psStats = (BASE_STATS*)asBodyStats;
    numStats = numBodyStats;
    statSize = sizeof(BODY_STATS);
    break;
  case COMP_BRAIN:
    psStats = (BASE_STATS*)asBrainStats;
    numStats = numBrainStats;
    statSize = sizeof(BRAIN_STATS);
    break;
  case COMP_PROPULSION:
    psStats = (BASE_STATS*)asPropulsionStats;
    numStats = numPropulsionStats;
    statSize = sizeof(PROPULSION_STATS);
    break;
  case COMP_REPAIRUNIT:
    psStats = (BASE_STATS*)asRepairStats;
    numStats = numRepairStats;
    statSize = sizeof(REPAIR_STATS);
    break;
  case COMP_ECM:
    psStats = (BASE_STATS*)asECMStats;
    numStats = numECMStats;
    statSize = sizeof(ECM_STATS);
    break;
  case COMP_SENSOR:
    psStats = (BASE_STATS*)asSensorStats;
    numStats = numSensorStats;
    statSize = sizeof(SENSOR_STATS);
    break;
  case COMP_CONSTRUCT:
    psStats = (BASE_STATS*)asConstructStats;
    numStats = numConstructStats;
    statSize = sizeof(CONSTRUCT_STATS);
    break;
  /*case COMP_PROGRAM:
    psStats = (BASE_STATS*)asProgramStats;
    numStats = numProgramStats;
    statSize = sizeof(PROGRAM_STATS);
    break;*/
  case COMP_WEAPON:
    psStats = (BASE_STATS*)asWeaponStats;
    numStats = numWeaponStats;
    statSize = sizeof(WEAPON_STATS);
    break;
  default:
    //COMP_UNKNOWN should be an error
    Neuron::Fatal("Invalid component type - game.c");
  }

  //find the stat with the same name
  for (count = 0; count < numStats; count++)
  {
    //get the translated name from the stat
    if (!strresGetIDNum(psStringRes, psStats->pName, &id))
    {
      Neuron::Fatal("Unable to find string resource for {}", getStatName(psStats) );
      return -1;
    }
    //get the string from the id
    pTranslatedName = strresGetString(psStringRes, id);

    if (!strcmp(pTranslatedName, pName))
      return count;
    psStats = (BASE_STATS*)((UDWORD)psStats + statSize);
  }

  //return -1 if record not found or an invalid component type is passed in
  return -1;

#else

  UNUSEDPARAMETER(compType); UNUSEDPARAMETER(pName); return getCompFromName(compType, pName);

#endif
}

// -----------------------------------------------------------------------------------------
SDWORD getStatFromNamePreV7(BOOL isFeature, STRING* pName)
{
#ifndef RESOURCE_NAMES

  BASE_STATS* psStats;
  UDWORD numStats = 0, count, statSize, id;
  STRING* pTranslatedName;

  if (isFeature)
  {
    psStats = (BASE_STATS*)asFeatureStats;
    numStats = numFeatureStats;
    statSize = sizeof(FEATURE_STATS);
  }
  else
  {
    psStats = (BASE_STATS*)asStructureStats;
    numStats = numStructureStats;
    statSize = sizeof(STRUCTURE_STATS);
  }

  //find the stat with the same name
  for (count = 0; count < numStats; count++)
  {
    //get the translated name from the stat
    if (!strresGetIDNum(psStringRes, psStats->pName, &id))
    {
      Neuron::Fatal("Unable to find string resource for {}", getStatName(psStats) );
      return -1;
    }

    //get the string from the id
    pTranslatedName = strresGetString(psStringRes, id);

    if (!strcmp(pTranslatedName, pName))
      return count;
    psStats = (BASE_STATS*)((UDWORD)psStats + statSize);
  }

  //return -1 if record not found or an invalid component type is passed in
  return -1;

#else

  UNUSEDPARAMETER(compType); UNUSEDPARAMETER(pName); return getCompFromName(compType, pName);

#endif
}

// -----------------------------------------------------------------------------------------
//copies a Stat name into a destination string for a given stat type and index
static BOOL getNameFromComp(UDWORD compType, STRING* pDest, UDWORD compIndex)
{
  BASE_STATS* psStats;

  //allocate the stats pointer
  switch (compType)
  {
  case COMP_BODY:
    psStats = (BASE_STATS*)(asBodyStats + compIndex);
    break;
  case COMP_BRAIN:
    psStats = (BASE_STATS*)(asBrainStats + compIndex);
    break;
  case COMP_PROPULSION:
    psStats = (BASE_STATS*)(asPropulsionStats + compIndex);
    break;
  case COMP_REPAIRUNIT:
    psStats = (BASE_STATS*)(asRepairStats + compIndex);
    break;
  case COMP_ECM:
    psStats = (BASE_STATS*)(asECMStats + compIndex);
    break;
  case COMP_SENSOR:
    psStats = (BASE_STATS*)(asSensorStats + compIndex);
    break;
  case COMP_CONSTRUCT:
    psStats = (BASE_STATS*)(asConstructStats + compIndex);
    break;
  /*case COMP_PROGRAM:
    psStats = (BASE_STATS*)(asProgramStats + compIndex);
    break;*/
  case COMP_WEAPON:
    psStats = (BASE_STATS*)(asWeaponStats + compIndex);
    break;
  default: Neuron::Fatal("Invalid component type - game.c");
    return FALSE;
  }

  //copy the name into the destination string
  strcpy(pDest, psStats->pName);
  return TRUE;
}

// -----------------------------------------------------------------------------------------
// END

// draws the structures onto a completed map preview sprite.
BOOL plotStructurePreview(iSprite* backDropSprite, UBYTE scale, UDWORD offX, UDWORD offY)
{
  /* Shipped struct.bjo files are version 8, so only the V2 record layout
   * can arrive here; the reader for the save-era layouts went with saves. */
  SAVE_STRUCTURE_V2 sSave;
  SAVE_STRUCTURE_V2* psSaveStructure2 = &sSave;

  STRUCT_SAVEHEADER* psHeader;
  STRING aFileName[256];
  UDWORD xx, yy, x, y, count, fileSize, sizeOfSaveStruture;
  UBYTE* pFileData = nullptr;
  LEVEL_DATASET* psLevel;

  levFindDataSet(game.map, &psLevel);
  strcpy(aFileName, psLevel->apDataFiles[0]);
  aFileName[strlen(aFileName) - 4] = '\0';
  strcat(aFileName, "\\struct.bjo");

  pFileData = DisplayBuffer;
  if (!loadFileToBuffer(aFileName, pFileData, displayBufferSize, &fileSize))
    Neuron::DebugTrace("plotStructurePreview: Fail1\n");

  /* Check the file type */
  psHeader = (STRUCT_SAVEHEADER*)pFileData;
  if (psHeader->aFileType[0] != 's' || psHeader->aFileType[1] != 't' || psHeader->aFileType[2] != 'r' || psHeader->aFileType[3] != 'u')
  {
    Neuron::Fatal("plotStructurePreview: Incorrect file type");
    return FALSE;
  }

  //increment to the start of the data
  pFileData += STRUCT_HEADER_SIZE;

  if (psHeader->version >= VERSION_12)
  {
    Neuron::Fatal("plotStructurePreview: unsupported save format version {}", psHeader->version);
    return FALSE;
  }
  sizeOfSaveStruture = sizeof(SAVE_STRUCTURE_V2);

  /* Load in the structure data */
  for (count = 0; count < psHeader->quantity; count++, pFileData += sizeOfSaveStruture)
  {
    {
      memcpy(psSaveStructure2, pFileData, sizeOfSaveStruture);
      xx = (psSaveStructure2->x >> TILE_SHIFT);
      yy = (psSaveStructure2->y >> TILE_SHIFT);
    }

    for (x = (xx * scale); x < (xx * scale) + scale; x++)
    {
      for (y = (yy * scale); y < (yy * scale) + scale; y++)
        backDropSprite->bmp[((offY + y) * BACKDROP_WIDTH) + x + offX] = COL_RED;
    }
  }
  return TRUE;
}
