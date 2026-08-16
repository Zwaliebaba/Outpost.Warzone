/*
 * game.h	  
 *
 * load and save game routines.
 * Very likely to ALL change! All the headers are separately defined at the 
 * moment - they probably don't need to be - if no difference make into one. 
 * Also the struct defintions throughout the game could be re-ordered to contain 
 * the variables required for saving so that don't need to create a load more here!
 */
#ifndef _game_h
#define _game_h							  

/***************************************************************************/
/*
 *	Global Definitions
 */
/***************************************************************************/
//the version number used in save games
#define VERSION_7				7				//string ID names saved for comps an objects
#define VERSION_8				8				//research status saved out for user saved games
#define VERSION_9				9				//power and experience saved out for user saved games
#define VERSION_10				10				//includes gateways and zones in .map file.
#define VERSION_11				11				//newstyle save game with extending structure checked in 13 Nov.
#define VERSION_12				12				//mission and order stuff checked in 20 Nov.
#define VERSION_13				13				//odds and ends to 24 Nov. and hashed scripts
#define VERSION_14				14				//
#define VERSION_15				15				//
#define VERSION_16				16				//beta save game
#define VERSION_17				17				//objId and new struct stats included
#define VERSION_18				18				//droid name savegame validity stamps
#define VERSION_19				19				//alliances, colours, radar zoom
#define VERSION_20				20				//MAX_NAME_ SIZE replaced by MAX_SAVE_NAME_SIZE
#define VERSION_21              21              //timeStartHold saved out for research facilities
#define VERSION_22              22              //asRundData
#define VERSION_23              23              //limbo droids and camstart droids saved properly, no cluster save
#define VERSION_24              24              //reinforceTime, droid move, droid resistance
#define VERSION_25              25              //limbo droid, research module hold fixed, cleaned by Alex
#define VERSION_26              26              //reArm pads
#define VERSION_27              27              //unit not the "d" word, experience and repair facility currentPtsAdded
#define VERSION_28              28              //rearm pads currentPtsAdded saved
#define VERSION_29              29              //mission scroll limits saved
#define VERSION_30              30              //script external variables saved
#define VERSION_31              31              //mission cheat time saved
#define VERSION_32              32              //factory secondary order saved
#define VERSION_33              33              //skirmish save 

#ifdef SAVE_TEST
#define	CURRENT_VERSION_NUM		VERSION_33
#else
#define	CURRENT_VERSION_NUM		VERSION_33
#endif

//used in the loadGame
#define KEEPOBJECTS				TRUE
#define FREEMEM					TRUE


enum
{
  GTYPE_SCENARIO_START,
  // Initial scenario state.
  GTYPE_SCENARIO_EXPAND,
  // Scenario scroll area expansion.
  GTYPE_MISSION,
  // Stand alone mission.
  GTYPE_SAVE_START,
  // User saved game - at the start of a level.
  GTYPE_SAVE_MIDMISSION,
  // User saved game - in the middle of a level
};

using VIS_SAVEHEADER = struct _vis_save_header
{
  STRING aFileType[4];
  UDWORD version;
};

using FX_SAVEHEADER = struct _fx_save_header
{
  STRING aFileType[4];
  UDWORD version;
  UDWORD entries;
};

using SCORE_SAVEHEADER = struct _score_save_header
{
  STRING aFileType[4];
  UDWORD version;
  UDWORD entries; // should always be one for this?
};

/***************************************************************************/
/*
 *	Global ProtoTypes
 */
/***************************************************************************/

extern BOOL loadGame(STRING* pGameToLoad, BOOL keepObjects, BOOL freeMem);
/*This just loads up the .gam file to determine which level data to set up*/
extern BOOL loadGameInit(STRING* pGameToLoad);

//direct access for forceloader
extern BOOL gameLoad(UBYTE* pFileData, UDWORD filesize);

UDWORD RemapPlayerNumber(UDWORD OldNumber);

#endif
