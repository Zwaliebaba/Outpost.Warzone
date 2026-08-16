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
/* The .gam/.bjo format versions. Shipped level content is version 5-8; the
 * user-save formats (9-33) are gone with save/load itself, and the readers
 * reject anything above 8. CURRENT_VERSION_NUM survives as the map format's
 * version stamp. */
#define VERSION_7				7
#define VERSION_8				8
#define VERSION_9				9
#define VERSION_12				12
#define VERSION_14				14
#define VERSION_19				19

#define	CURRENT_VERSION_NUM		33

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
