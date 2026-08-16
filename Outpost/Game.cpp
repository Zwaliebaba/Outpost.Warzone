#include "pch.h"
/*
	ALL PSX, HASH_NAMES AND WIN32 excluded stuff removed - Alex M.
*/

/* Standard library headers */
#include <stdio.h>
#include <direct.h>
#include <assert.h>

/* Warzone src and library headers */
#include "Frame.h"
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
#include "LoadSave.h"
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

using DROID_SAVEHEADER = struct _droid_save_header
{
  STRING aFileType[4];
  UDWORD version;
  UDWORD quantity;
};

using STRUCT_SAVEHEADER = struct _struct_save_header
{
  STRING aFileType[4];
  UDWORD version;
  UDWORD quantity;
};

using TEMPLATE_SAVEHEADER = struct _template_save_header
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
using COMPLIST_SAVEHEADER = struct _compList_save_header
{
  STRING aFileType[4];
  UDWORD version;
  UDWORD quantity;
};

/* Structure definitions for loading and saving map data */
using STRUCTLIST_SAVEHEADER = struct _structList_save_header
{
  STRING aFileType[4];
  UDWORD version;
  UDWORD quantity;
};

using RESEARCH_SAVEHEADER = struct _research_save_header
{
  STRING aFileType[4];
  UDWORD version;
  UDWORD quantity;
};

using MESSAGE_SAVEHEADER = struct _message_save_header
{
  STRING aFileType[4];
  UDWORD version;
  UDWORD quantity;
};

using PROXIMITY_SAVEHEADER = struct _proximity_save_header
{
  STRING aFileType[4];
  UDWORD version;
  UDWORD quantity;
};

using FLAG_SAVEHEADER = struct _flag_save_header
{
  STRING aFileType[4];
  UDWORD version;
  UDWORD quantity;
};

using PRODUCTION_SAVEHEADER = struct _production_save_header
{
  STRING aFileType[4];
  UDWORD version;
};

using STRUCTLIMITS_SAVEHEADER = struct _structLimits_save_header
{
  STRING aFileType[4];
  UDWORD version;
  UDWORD quantity;
};

using COMMAND_SAVEHEADER = struct _command_save_header
{
  STRING aFileType[4];
  UDWORD version;
  UDWORD quantity;
};

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

using SAVE_COMPONENT_V19 = struct _save_component_v19
{
  STRING name[MAX_SAVE_NAME_SIZE_V19];
};

using SAVE_COMPONENT = struct _save_component
{
  STRING name[MAX_SAVE_NAME_SIZE];
};

using SAVE_WEAPON_V19 = struct _save_weapon_v19
{
  STRING name[MAX_SAVE_NAME_SIZE_V19];
  UDWORD hitPoints; //- remove at some point
  UDWORD ammo;
  UDWORD lastFired;
};

using SAVE_WEAPON = struct _save_weapon
{
  STRING name[MAX_SAVE_NAME_SIZE];
  UDWORD hitPoints; //- remove at some point
  UDWORD ammo;
  UDWORD lastFired;
};

using SAVE_POWER = struct _savePower
{
  UDWORD currentPower;
  UDWORD extractedPower;
};

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

#define GAME_SAVE_V10	\
	GAME_SAVE_V7;		\
	SAVE_POWER	power[MAX_PLAYERS]

using SAVE_GAME_V10 = struct save_game_v10
{
  GAME_SAVE_V10;
};

#define GAME_SAVE_V11	\
	GAME_SAVE_V10;		\
	iView currentPlayerPos

using SAVE_GAME_V11 = struct save_game_v11
{
  GAME_SAVE_V11;
};

#define GAME_SAVE_V12	\
	GAME_SAVE_V11;		\
	UDWORD	missionTime;\
	UDWORD	saveKey

using SAVE_GAME_V12 = struct save_game_v12
{
  GAME_SAVE_V12;
};

#define GAME_SAVE_V14			\
	GAME_SAVE_V12;				\
	SDWORD	missionOffTime;		\
	SDWORD	missionETA;			\
    UWORD   missionHomeLZ_X;	\
    UWORD   missionHomeLZ_Y;	\
	SDWORD	missionPlayerX;		\
	SDWORD	missionPlayerY;		\
	UWORD	iTranspEntryTileX[MAX_PLAYERS];	\
	UWORD	iTranspEntryTileY[MAX_PLAYERS];	\
	UWORD	iTranspExitTileX[MAX_PLAYERS];	\
	UWORD	iTranspExitTileY[MAX_PLAYERS];	\
	UDWORD 	aDefaultSensor[MAX_PLAYERS];	\
	UDWORD	aDefaultECM[MAX_PLAYERS];		\
	UDWORD	aDefaultRepair[MAX_PLAYERS]

using SAVE_GAME_V14 = struct save_game_v14
{
  GAME_SAVE_V14;
};

#define GAME_SAVE_V15			\
	GAME_SAVE_V14;				\
	BOOL	offWorldKeepLists;\
	UBYTE	aDroidExperience[MAX_PLAYERS][MAX_RECYCLED_DROIDS];\
	UDWORD	RubbleTile;\
	UDWORD	WaterTile;\
	UDWORD	fogColour;\
	UDWORD	fogState

using SAVE_GAME_V15 = struct save_game_v15
{
  GAME_SAVE_V15;
};

#define GAME_SAVE_V16			\
	GAME_SAVE_V15;				\
	LANDING_ZONE   sLandingZone[MAX_NOGO_AREAS]

using SAVE_GAME_V16 = struct save_game_v16
{
  GAME_SAVE_V16;
};

#define GAME_SAVE_V17			\
	GAME_SAVE_V16;				\
	UDWORD   objId

using SAVE_GAME_V17 = struct save_game_v17
{
  GAME_SAVE_V17;
};

#define GAME_SAVE_V18			\
	GAME_SAVE_V17;				\
	char		buildDate[MAX_STR_LENGTH];		\
	UDWORD		oldestVersion;	\
	UDWORD		validityKey

using SAVE_GAME_V18 = struct save_game_v18
{
  GAME_SAVE_V18;
};

#define GAME_SAVE_V19			\
	GAME_SAVE_V18;				\
	UBYTE alliances[MAX_PLAYERS][MAX_PLAYERS];\
	UBYTE playerColour[MAX_PLAYERS];\
	UBYTE radarZoom

using SAVE_GAME_V19 = struct save_game_v19
{
  GAME_SAVE_V19;
};

#define GAME_SAVE_V20			\
	GAME_SAVE_V19;				\
	UBYTE bDroidsToSafetyFlag;	\
	POINT	asVTOLReturnPos[MAX_PLAYERS]

using SAVE_GAME_V20 = struct save_game_v20
{
  GAME_SAVE_V20;
};

#define GAME_SAVE_V22			\
	GAME_SAVE_V20;				\
	RUN_DATA	asRunData[MAX_PLAYERS]

using SAVE_GAME_V22 = struct save_game_v22
{
  GAME_SAVE_V22;
};

#define GAME_SAVE_V24			\
	GAME_SAVE_V22;				\
	UDWORD reinforceTime;		\
	UBYTE bPlayCountDown;	\
	UBYTE bPlayerHasWon;	\
	UBYTE bPlayerHasLost;	\
	UBYTE dummy3

using SAVE_GAME_V24 = struct save_game_v24
{
  GAME_SAVE_V24;
};

/*
#define GAME_SAVE_V27		\
	UDWORD	gameTime;		\
	UDWORD	GameType;		\
	SDWORD	ScrollMinX;		\
	SDWORD	ScrollMinY;		\
	UDWORD	ScrollMaxX;		\
	UDWORD	ScrollMaxY;		\
	STRING	levelName[MAX_LEVEL_SIZE];	\
	SAVE_POWER	power[MAX_PLAYERS];		\
	iView	currentPlayerPos;	\
	UDWORD	missionTime;	\
	UDWORD	saveKey;		\
	SDWORD	missionOffTime;	\
	SDWORD	missionETA;		\
    UWORD   missionHomeLZ_X;\
    UWORD   missionHomeLZ_Y;\
	SDWORD	missionPlayerX;	\
	SDWORD	missionPlayerY;	\
	UWORD	iTranspEntryTileX[MAX_PLAYERS];	\
	UWORD	iTranspEntryTileY[MAX_PLAYERS];	\
	UWORD	iTranspExitTileX[MAX_PLAYERS];	\
	UWORD	iTranspExitTileY[MAX_PLAYERS];	\
	UDWORD 	aDefaultSensor[MAX_PLAYERS];	\
	UDWORD	aDefaultECM[MAX_PLAYERS];		\
	UDWORD	aDefaultRepair[MAX_PLAYERS];	\
	BOOL	offWorldKeepLists;\
	UWORD	aDroidExperience[MAX_PLAYERS][MAX_RECYCLED_DROIDS];\
	UDWORD	RubbleTile;		\
	UDWORD	WaterTile;		\
	UDWORD	fogColour;		\
	UDWORD	fogState;		\
	LANDING_ZONE   sLandingZone[MAX_NOGO_AREAS];\
	UDWORD  objId;			\
	char	buildDate[MAX_STR_LENGTH];		\
	UDWORD	oldestVersion;	\
	UDWORD	validityKey;\
	UBYTE	alliances[MAX_PLAYERS][MAX_PLAYERS];\
	UBYTE	playerColour[MAX_PLAYERS];\
	UBYTE	radarZoom;		\
	UBYTE	bDroidsToSafetyFlag;	\
	POINT	asVTOLReturnPos[MAX_PLAYERS];\
	RUN_DATA asRunData[MAX_PLAYERS];\
	UDWORD	reinforceTime;	\
	UBYTE	bPlayCountDown;	\
	UBYTE	bPlayerHasWon;	\
	UBYTE	bPlayerHasLost;	\
	UBYTE	dummy3

typedef struct save_game_v27
{
	GAME_SAVE_V27;
} SAVE_GAME_V27;
*/
#define GAME_SAVE_V27			\
	GAME_SAVE_V24;				\
	UWORD	awDroidExperience[MAX_PLAYERS][MAX_RECYCLED_DROIDS]

using SAVE_GAME_V27 = struct save_game_v27
{
  GAME_SAVE_V27;
};

#define GAME_SAVE_V29			\
	GAME_SAVE_V27;				\
	UWORD	missionScrollMinX;  \
	UWORD	missionScrollMinY;  \
	UWORD	missionScrollMaxX;  \
	UWORD	missionScrollMaxY

using SAVE_GAME_V29 = struct save_game_v29
{
  GAME_SAVE_V29;
};

#define GAME_SAVE_V30			\
	GAME_SAVE_V29;				\
    SDWORD  scrGameLevel;       \
    UBYTE   bExtraVictoryFlag;  \
    UBYTE   bExtraFailFlag;     \
    UBYTE   bTrackTransporter

using SAVE_GAME_V30 = struct save_game_v30
{
  GAME_SAVE_V30;
};

//extra code for the patch - saves out whether cheated with the mission timer
#define GAME_SAVE_V31           \
    GAME_SAVE_V30;				\
    SDWORD	missionCheatTime

using SAVE_GAME_V31 = struct save_game_v31
{
  GAME_SAVE_V31;
};

// alexl. skirmish saves
#define GAME_SAVE_V33           \
    GAME_SAVE_V31;				\
	MULTIPLAYERGAME sGame;		\
	NETPLAY			sNetPlay;	\
	UDWORD			savePlayer;	\
	STRING			sPName[32];	\
	BOOL			multiPlayer;\
	NETPLAYERID			sPlayer2dpid[MAX_PLAYERS]

using SAVE_GAME_V33 = struct save_game_v33
{
  GAME_SAVE_V33;
};

using SAVE_GAME = struct save_game
{
  GAME_SAVE_V33;
};

#define TEMP_DROID_MAXPROGS	3
#define	SAVE_COMP_PROGRAM	8
#define SAVE_COMP_WEAPON	9

using SAVE_MOVE_CONTROL = struct _save_move_control
{
  UBYTE Status; // Inactive, Navigating or moving point to point status
  UBYTE Mask; // Mask used for the creation of this path	
  //	SBYTE	Direction;					// Direction object should be moving (0-7) 0=Up,1=Up-Right
  //	SDWORD	Speed;						// Speed at which object moves along the movement list
  UBYTE Position; // Position in asPath
  UBYTE numPoints; // number of points in asPath
  PATH_POINT asPath[TRAVELSIZE]; // Pointer to list of block X,Y coordinates.
  // Values prefixed by 0x8000 are pixel coordinates instead of
  // block coordinates
  SDWORD DestinationX; // DestinationX,Y should match objects current X,Y
  SDWORD DestinationY; //		location for this movement to be complete.
  //   	UDWORD	Direction3D;				// *** not necessary
  //	UDWORD	TargetDir;					// *** not necessary Direction the object should be facing
  //	SDWORD	Gradient;					// Gradient of line
  //	SDWORD	DeltaX;						// Distance from start to end position of current movement X
  //	SDWORD	DeltaY;						// Distance from start to end position of current movement Y
  //	SDWORD	XStep;						// Adjustment to the characters X position each movement
  //	SDWORD	YStep;						// Adjustment to the characters Y position each movement
  //	SDWORD	DestPixelX;					// Pixel coordinate destination for pixel movement (NOT the final X)
  //	SDWORD	DestPixelY;					// Pixel coordiante destination for pixel movement (NOT the final Y)
  SDWORD srcX, srcY, targetX, targetY;

  /* Stuff for John's movement update */
  float fx, fy; // droid location as a fract
  //	FRACT	dx,dy;						// x and y change for current direction
  // NOTE: this is supposed to replace Speed
  float speed; // Speed of motion
  SWORD boundX, boundY; // Vector for the end of path boundary
  SWORD dir; // direction of motion (not the direction the droid is facing)

  SWORD bumpDir; // direction at last bump
  UDWORD bumpTime; // time of first bump with something
  UWORD lastBump; // time of last bump with a droid - relative to bumpTime
  UWORD pauseTime; // when MOVEPAUSE started - relative to bumpTime
  UWORD bumpX, bumpY; // position of last bump

  UDWORD shuffleStart; // when a shuffle started

  struct _formation* psFormation; // formation the droid is currently a member of

  /* vtol movement - GJ */
  SWORD iVertSpeed;
  UWORD iAttackRuns;

  /* Only needed for Alex's movement update ? */
};

#define DROID_SAVE_V9		\
	OBJECT_SAVE_V19;			\
	SAVE_COMPONENT_V19	asBits[DROID_MAXCOMP]; \
	UDWORD		body;		\
	UBYTE		droidType;	\
	UDWORD		saveType;	\
	UDWORD		numWeaps;	\
	SAVE_WEAPON_V19	asWeaps[TEMP_DROID_MAXPROGS];	\
	UDWORD		numKills

using SAVE_DROID_V9 = struct _save_droid_v9
{
  DROID_SAVE_V9;
};

/*save DROID SAVE 11 */
#define DROID_SAVE_V11		\
	OBJECT_SAVE_V19;			\
	SAVE_COMPONENT_V19	asBits[DROID_MAXCOMP]; \
	UDWORD		body;		\
	UBYTE		droidType;	\
	UBYTE		saveType;	\
	UDWORD		numWeaps;	\
	SAVE_WEAPON_V19	asWeaps[TEMP_DROID_MAXPROGS];	\
	UDWORD		numKills;	\
	UWORD	turretRotation;	\
	UWORD	turretPitch

using SAVE_DROID_V11 = struct _save_droid_v11
{
  DROID_SAVE_V11;
};

#define DROID_SAVE_V12		\
	DROID_SAVE_V9;			\
	UWORD	turretRotation;	\
	UWORD	turretPitch;	\
	SDWORD	order;			\
	UWORD	orderX,orderY;	\
	UWORD	orderX2,orderY2;\
	UDWORD	timeLastHit;	\
	UDWORD	targetID;		\
	UDWORD	secondaryOrder;	\
	SDWORD	action;			\
	UDWORD	actionX,actionY;\
	UDWORD	actionTargetID;	\
	UDWORD	actionStarted;	\
	UDWORD	actionPoints;	\
	UWORD	actionHeight

using SAVE_DROID_V12 = struct _save_droid_v12
{
  DROID_SAVE_V12;
};

#define DROID_SAVE_V14		\
	DROID_SAVE_V12;			\
	CHAR	tarStatName[MAX_STR_SIZE];\
    UDWORD	baseStructID;	\
	UBYTE	group;			\
	UBYTE	selected;		\
	UBYTE	cluster_unused;		\
	UBYTE	visible[MAX_PLAYERS];\
	UDWORD	died;			\
	UDWORD	lastEmission

using SAVE_DROID_V14 = struct _save_droid_v14
{
  DROID_SAVE_V14;
};

//DROID_SAVE_18 replaces DROID_SAVE_14
#define DROID_SAVE_V18		\
	DROID_SAVE_V12;			\
	CHAR	tarStatName[MAX_SAVE_NAME_SIZE_V19];\
    UDWORD	baseStructID;	\
	UBYTE	group;			\
	UBYTE	selected;		\
	UBYTE	cluster_unused;		\
	UBYTE	visible[MAX_PLAYERS];\
	UDWORD	died;			\
	UDWORD	lastEmission

using SAVE_DROID_V18 = struct _save_droid_v18
{
  DROID_SAVE_V18;
};

//DROID_SAVE_20 replaces all previous saves uses 60 character names
#define DROID_SAVE_V20		\
	OBJECT_SAVE_V20;			\
	SAVE_COMPONENT	asBits[DROID_MAXCOMP]; \
	UDWORD		body;		\
	UBYTE		droidType;	\
	UDWORD		saveType;	\
	UDWORD		numWeaps;	\
	SAVE_WEAPON	asWeaps[TEMP_DROID_MAXPROGS];	\
	UDWORD		numKills;	\
	UWORD	turretRotation;	\
	UWORD	turretPitch;	\
	SDWORD	order;			\
	UWORD	orderX,orderY;	\
	UWORD	orderX2,orderY2;\
	UDWORD	timeLastHit;	\
	UDWORD	targetID;		\
	UDWORD	secondaryOrder;	\
	SDWORD	action;			\
	UDWORD	actionX,actionY;\
	UDWORD	actionTargetID;	\
	UDWORD	actionStarted;	\
	UDWORD	actionPoints;	\
	UWORD	actionHeight;	\
	CHAR	tarStatName[MAX_SAVE_NAME_SIZE];\
    UDWORD	baseStructID;	\
	UBYTE	group;			\
	UBYTE	selected;		\
	UBYTE	cluster_unused;		\
	UBYTE	visible[MAX_PLAYERS];\
	UDWORD	died;			\
	UDWORD	lastEmission

using SAVE_DROID_V20 = struct _save_droid_v20
{
  DROID_SAVE_V20;
};

#define DROID_SAVE_V21		\
	DROID_SAVE_V20;			\
	UDWORD	commandId

using SAVE_DROID_V21 = struct _save_droid_v21
{
  DROID_SAVE_V21;
};

#define DROID_SAVE_V24		\
	DROID_SAVE_V21;			\
	SDWORD	resistance;		\
	SAVE_MOVE_CONTROL	sMove;	\
	SWORD		formationDir;	\
	SDWORD		formationX;	\
	SDWORD		formationY

using SAVE_DROID_V24 = struct _save_droid_v24
{
  DROID_SAVE_V24;
};

using SAVE_DROID = struct _save_droid
{
  DROID_SAVE_V24;
};

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

using SAVE_STRUCTURE_V12 = struct _save_structure_v12
{
  STRUCTURE_SAVE_V12;
};

#define STRUCTURE_SAVE_V14 \
	STRUCTURE_SAVE_V12; \
	UBYTE	visible[MAX_PLAYERS]

using SAVE_STRUCTURE_V14 = struct _save_structure_v14
{
  STRUCTURE_SAVE_V14;
};

#define STRUCTURE_SAVE_V15 \
	STRUCTURE_SAVE_V14; \
	char	researchName[MAX_SAVE_NAME_SIZE_V19]

using SAVE_STRUCTURE_V15 = struct _save_structure_v15
{
  STRUCTURE_SAVE_V15;
};

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

using SAVE_STRUCTURE_V20 = struct _save_structure_v20
{
  STRUCTURE_SAVE_V20;
};

#define STRUCTURE_SAVE_V21 \
	STRUCTURE_SAVE_V20; \
	UDWORD				commandId

using SAVE_STRUCTURE_V21 = struct _save_structure_v21
{
  STRUCTURE_SAVE_V21;
};

using SAVE_STRUCTURE = struct _save_structure
{
  STRUCTURE_SAVE_V21;
};

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

using SAVE_TEMPLATE_V2 = struct _save_template_v2
{
  TEMPLATE_SAVE_V2;
};

using SAVE_TEMPLATE_V14 = struct _save_template_v14
{
  TEMPLATE_SAVE_V14;
};

using SAVE_TEMPLATE_V20 = struct _save_template_v20
{
  TEMPLATE_SAVE_V20;
};

using SAVE_TEMPLATE = struct _save_template
{
  TEMPLATE_SAVE_V20;
};

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

using SAVE_FEATURE_V20 = struct _save_feature_v20
{
  FEATURE_SAVE_V20;
};

using SAVE_FEATURE = struct _save_feature
{
  FEATURE_SAVE_V20;
};

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

using SAVE_COMPLIST_V6 = struct _save_compList_v6
{
  COMPLIST_SAVE_V6;
};

using SAVE_COMPLIST_V20 = struct _save_compList_v20
{
  COMPLIST_SAVE_V20;
};

using SAVE_COMPLIST = struct _save_compList
{
  COMPLIST_SAVE_V20;
};

#define STRUCTLIST_SAVE_V6 \
	CHAR				name[MAX_SAVE_NAME_SIZE_V19]; \
	UBYTE				state; \
	UBYTE				player

#define STRUCTLIST_SAVE_V20 \
	CHAR				name[MAX_SAVE_NAME_SIZE]; \
	UBYTE				state; \
	UBYTE				player

using SAVE_STRUCTLIST_V6 = struct _save_structList_v6
{
  STRUCTLIST_SAVE_V6;
};

using SAVE_STRUCTLIST_V20 = struct _save_structList_v20
{
  STRUCTLIST_SAVE_V20;
};

using SAVE_STRUCTLIST = struct _save_structList
{
  STRUCTLIST_SAVE_V20;
};

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

using SAVE_RESEARCH_V8 = struct _save_research_v8
{
  RESEARCH_SAVE_V8;
};

using SAVE_RESEARCH_V20 = struct _save_research_v20
{
  RESEARCH_SAVE_V20;
};

using SAVE_RESEARCH = struct _save_research
{
  RESEARCH_SAVE_V20;
};

using SAVE_MESSAGE = struct _save_message
{
  MESSAGE_TYPE type; //The type of message 
  BOOL bObj;
  CHAR name[MAX_STR_SIZE];
  UDWORD objId; //Id for Proximity messages!
  BOOL read; //flag to indicate whether message has been read
  UDWORD player; //which player this message belongs to
};

using SAVE_PROXIMITY = struct _save_proximity
{
  POSITION_TYPE type; /*the type of position obj - FlagPos or ProxDisp*/
  UDWORD frameNumber; /*when the Position was last drawn*/
  UDWORD screenX; /*screen coords and radius of Position imd */
  UDWORD screenY;
  UDWORD screenR;
  UDWORD player; /*which player the Position belongs to*/
  BOOL selected; /*flag to indicate whether the Position */
  UDWORD objId; //Id for Proximity messages!
  UDWORD radarX; //Used to store the radar coords - if to be drawn
  UDWORD radarY;
  UDWORD timeLastDrawn; //stores the time the 'button' was last drawn for animation
  UDWORD strobe; //id of image last used
  UDWORD buttonID; //id of the button for the interface
};

using SAVE_FLAG_V18 = struct _save_flag_v18
{
  POSITION_TYPE type; /*the type of position obj - FlagPos or ProxDisp*/
  UDWORD frameNumber; /*when the Position was last drawn*/
  UDWORD screenX; /*screen coords and radius of Position imd */
  UDWORD screenY;
  UDWORD screenR;
  UDWORD player; /*which player the Position belongs to*/
  BOOL selected; /*flag to indicate whether the Position */
  iVector coords; //the world coords of the Position
  UBYTE factoryInc; //indicates whether the first, second etc factory
  UBYTE factoryType; //indicates whether standard, cyborg or vtol factory
  UBYTE dummyNOTUSED; //sub value. needed to order production points.
  UBYTE dummyNOTUSED2;
};

using SAVE_FLAG = struct _save_flag
{
  POSITION_TYPE type; /*the type of position obj - FlagPos or ProxDisp*/
  UDWORD frameNumber; /*when the Position was last drawn*/
  UDWORD screenX; /*screen coords and radius of Position imd */
  UDWORD screenY;
  UDWORD screenR;
  UDWORD player; /*which player the Position belongs to*/
  BOOL selected; /*flag to indicate whether the Position */
  iVector coords; //the world coords of the Position
  UBYTE factoryInc; //indicates whether the first, second etc factory
  UBYTE factoryType; //indicates whether standard, cyborg or vtol factory
  UBYTE dummyNOTUSED; //sub value. needed to order production points.
  UBYTE dummyNOTUSED2;
  UDWORD repairId;
};

using SAVE_PRODUCTION = struct _save_production
{
  UBYTE quantity; //number to build
  UBYTE built; //number built on current run
  UDWORD multiPlayerID; //template to build
};

#define STRUCTLIMITS_SAVE_V2 \
	CHAR				name[MAX_SAVE_NAME_SIZE_V19]; \
	UBYTE				limit; \
	UBYTE				player

using SAVE_STRUCTLIMITS_V2 = struct _save_structLimits_v2
{
  STRUCTLIMITS_SAVE_V2;
};

#define STRUCTLIMITS_SAVE_V20 \
	CHAR				name[MAX_SAVE_NAME_SIZE]; \
	UBYTE				limit; \
	UBYTE				player

using SAVE_STRUCTLIMITS_V20 = struct _save_structLimits_v20
{
  STRUCTLIMITS_SAVE_V20;
};

using SAVE_STRUCTLIMITS = struct _save_structLimits
{
  STRUCTLIMITS_SAVE_V20;
};

#define COMMAND_SAVE_V20 \
	UDWORD				droidID

using SAVE_COMMAND_V20 = struct _save_command_v20
{
  COMMAND_SAVE_V20;
};

using SAVE_COMMAND = struct _save_command
{
  COMMAND_SAVE_V20;
};

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
static UDWORD oldestSaveGameVersion = CURRENT_VERSION_NUM;

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
static BOOL LoadGameFromWDG;
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

char* getSaveStructNameV(SAVE_STRUCTURE* psSaveStructure) { return (psSaveStructure->name); }

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
  /* Load in the chosen file data */
  /*#ifdef WIN32
    pFileData = DisplayBuffer;
    if (!loadFileToBuffer(aFileName, pFileData, displayBufferSize, &fileSize))
    {
      DBPRINTF(("loadgame: Fail2\n"));
      goto error;
    }
  #else
    if (loadFileFromWDG(aFileName,&pFileData,&fileSize,WDG_USESUPPLIED)!=WDG_OK)
    {
      DBPRINTF(("loadgame: Fail3\n"));
      goto error;
    }
  #endif
    if (!gameLoad(pFileData, fileSize))
    {
      DBPRINTF(("loadgame: Fail4\n"));
      goto error;
    }
  */
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
  setDesiredPitch(INITIAL_DESIRED_PITCH);

#ifndef COVERMOUNT
#endif

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
          psDroid->direction = static_cast<UWORD>(pDroidInit->direction);
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
DROID* buildDroidFromSaveDroidV11(SAVE_DROID_V11* psSaveDroid)
{
  DROID_TEMPLATE *psTemplate, sTemplate;
  DROID* psDroid;
  BOOL found;
  UDWORD i;
  SDWORD compInc;
  UDWORD burnTime;

  psTemplate = &sTemplate;

  //set up the template
  //copy the values across

  strncpy(psTemplate->aName, psSaveDroid->name, DROID_MAXNAME);
  psTemplate->aName[DROID_MAXNAME - 1] = 0;
  //ignore the first comp - COMP_UNKNOWN
  found = TRUE;
  for (i = 1; i < DROID_MAXCOMP; i++)
  {
    compInc = getCompFromName(i, psSaveDroid->asBits[i].name);
    if (compInc < 0)
    {
      Neuron::Fatal("This component no longer exists - {}, the droid will be deleted", psSaveDroid->asBits[i].name);
      found = FALSE;
      break; //continue;
    }
    psTemplate->asParts[i] = static_cast<UDWORD>(compInc);
  }
  if (!found)
  {
    //ignore this record
    return nullptr;
  }
  psTemplate->numWeaps = psSaveDroid->numWeaps;
  found = TRUE;
  for (i = 0; i < psSaveDroid->numWeaps; i++)
  {
    psTemplate->asWeaps[i] = getCompFromName(COMP_WEAPON, psSaveDroid->asWeaps[i].name);

    if (psTemplate->asWeaps[i] < 0)
    {
      Neuron::Fatal("This component no longer exists - {}, the droid will be deleted", psSaveDroid->asWeaps[i].name);
      found = FALSE;
      break;
    }
  }
  if (!found)
  {
    //ignore this record
    return nullptr;
  }

  psTemplate->buildPoints = calcTemplateBuild(psTemplate);
  psTemplate->powerPoints = calcTemplatePower(psTemplate);
  psTemplate->droidType = static_cast<DROID_TYPE>(psSaveDroid->droidType);

  /*create the Droid */

  // ignore brains for now
  psTemplate->asParts[COMP_BRAIN] = 0;

  psDroid = buildDroid(psTemplate, psSaveDroid->x, psSaveDroid->y, psSaveDroid->player, FALSE);

  //copy the droid's weapon stats
  if (psDroid->asWeaps[0].nStat > 0)
  {
    //only one weapon now
    i = 0;
    psDroid->asWeaps[i].hitPoints = psSaveDroid->asWeaps[i].hitPoints;
    psDroid->asWeaps[i].ammo = psSaveDroid->asWeaps[i].ammo;
    psDroid->asWeaps[i].lastFired = psSaveDroid->asWeaps[i].lastFired;
  }
  //copy the values across
  psDroid->id = psSaveDroid->id;
  //are these going to ever change from the values set up with?
  //			psDroid->z = psSaveDroid->z;		// use the correct map height value

  psDroid->direction = static_cast<UWORD>(psSaveDroid->direction);
  psDroid->body = psSaveDroid->body;
  if (psDroid->body > psDroid->originalBody)
    psDroid->body = psDroid->originalBody;

  psDroid->inFire = psSaveDroid->inFire;
  psDroid->burnDamage = psSaveDroid->burnDamage;
  burnTime = psSaveDroid->burnStart;
  psDroid->burnStart = burnTime;

  psDroid->numKills = static_cast<UWORD>(psSaveDroid->numKills);
  //version 11
  psDroid->turretRotation = psSaveDroid->turretRotation;
  psDroid->turretPitch = psSaveDroid->turretPitch;

  psDroid->psGroup = nullptr;
  psDroid->psGrpNext = nullptr;

  return psDroid;
}

// -----------------------------------------------------------------------------------------
DROID* buildDroidFromSaveDroidV19(SAVE_DROID_V18* psSaveDroid, UDWORD version)
{
  DROID_TEMPLATE *psTemplate, sTemplate;
  DROID* psDroid;
  SAVE_DROID_V14* psSaveDroidV14;
  BOOL found;
  UDWORD i, id;
  SDWORD compInc;
  UDWORD burnTime;

  psTemplate = &sTemplate;

  psTemplate->pName = nullptr;

  //set up the template
  //copy the values across

  strncpy(psTemplate->aName, psSaveDroid->name, DROID_MAXNAME);
  psTemplate->aName[DROID_MAXNAME - 1] = 0;

  //ignore the first comp - COMP_UNKNOWN
  found = TRUE;
  for (i = 1; i < DROID_MAXCOMP; i++)
  {
    compInc = getCompFromName(i, psSaveDroid->asBits[i].name);

    if (compInc < 0)
    {
      Neuron::Fatal("This component no longer exists - {}, the droid will be deleted", psSaveDroid->asBits[i].name);

      found = FALSE;
      break; //continue;
    }
    psTemplate->asParts[i] = static_cast<UDWORD>(compInc);
  }
  if (!found)
  {
    //ignore this record
    DEBUG_ASSERT_TEXT(found, "buildUnitFromSavedUnit; failed to find weapon");
    return nullptr;
  }
  psTemplate->numWeaps = psSaveDroid->numWeaps;
  found = TRUE;
  if (psSaveDroid->numWeaps > 0)
  {
    psTemplate->asWeaps[0] = getCompFromName(COMP_WEAPON, psSaveDroid->asWeaps[0].name);

    if (psTemplate->asWeaps[0] < 0)
    {
      Neuron::Fatal("This component no longer exists - {}, the droid will be deleted", psSaveDroid->asWeaps[0].name);
      found = FALSE;
    }
  }
  if (!found)
  {
    //ignore this record
    DEBUG_ASSERT_TEXT(found, "buildUnitFromSavedUnit; failed to find weapon");
    return nullptr;
  }

  psTemplate->buildPoints = calcTemplateBuild(psTemplate);
  psTemplate->powerPoints = calcTemplatePower(psTemplate);
  psTemplate->droidType = static_cast<DROID_TYPE>(psSaveDroid->droidType);

  /*create the Droid */

  // ignore brains for now
  // not any *$£&!!! more - JOHN

  if (psSaveDroid->x == INVALID_XY) { psDroid = buildDroid(psTemplate, psSaveDroid->x, psSaveDroid->y, psSaveDroid->player, TRUE); }
  else if (psSaveDroid->saveType == DROID_ON_TRANSPORT) { psDroid = buildDroid(psTemplate, 0, 0, psSaveDroid->player, TRUE); }
  else { psDroid = buildDroid(psTemplate, psSaveDroid->x, psSaveDroid->y, psSaveDroid->player, FALSE); }

  if (psDroid == nullptr)
  {
    DEBUG_ASSERT_TEXT(FALSE, "buildUnitFromSavedUnit; failed to build unit");
    return nullptr;
  }

  //copy the droid's weapon stats
  if (psDroid->asWeaps[0].nStat > 0)
  {
    psDroid->asWeaps[0].hitPoints = psSaveDroid->asWeaps[0].hitPoints;
    psDroid->asWeaps[0].ammo = psSaveDroid->asWeaps[0].ammo;
    psDroid->asWeaps[0].lastFired = psSaveDroid->asWeaps[0].lastFired;
  }
  //copy the values across
  psDroid->id = psSaveDroid->id;
  //are these going to ever change from the values set up with?
  //			psDroid->z = psSaveDroid->z;		// use the correct map height value

  psDroid->direction = static_cast<UWORD>(psSaveDroid->direction);
  psDroid->body = psSaveDroid->body;
  if (psDroid->body > psDroid->originalBody)
    psDroid->body = psDroid->originalBody;

  psDroid->inFire = psSaveDroid->inFire;
  psDroid->burnDamage = psSaveDroid->burnDamage;
  burnTime = psSaveDroid->burnStart;
  psDroid->burnStart = burnTime;

  psDroid->numKills = static_cast<UWORD>(psSaveDroid->numKills);
  //version 14
  psDroid->resistance = droidResistance(psDroid);

  if (version >= VERSION_11) //version 11
  {
    psDroid->turretRotation = psSaveDroid->turretRotation;
    psDroid->turretPitch = psSaveDroid->turretPitch;
  }
  if (version >= VERSION_12) //version 12
  {
    psDroid->order = psSaveDroid->order;
    psDroid->orderX = psSaveDroid->orderX;
    psDroid->orderY = psSaveDroid->orderY;
    psDroid->orderX2 = psSaveDroid->orderX2;
    psDroid->orderY2 = psSaveDroid->orderY2;
    psDroid->timeLastHit = psSaveDroid->timeLastHit;
    //rebuild the object pointer from the ID
    psDroid->psTarget = (BASE_OBJECT*)psSaveDroid->targetID;
    psDroid->secondaryOrder = psSaveDroid->secondaryOrder;
    psDroid->action = psSaveDroid->action;
    psDroid->actionX = psSaveDroid->actionX;
    psDroid->actionY = psSaveDroid->actionY;
    //rebuild the object pointer from the ID
    psDroid->psActionTarget = (BASE_OBJECT*)psSaveDroid->actionTargetID;
    psDroid->actionStarted = psSaveDroid->actionStarted;
    psDroid->actionPoints = psSaveDroid->actionPoints;
    //actionHeight has been renamed to powerAccrued - AB 7/1/99
    psDroid->powerAccrued = psSaveDroid->actionHeight;
    //added for V14

    psDroid->psGroup = nullptr;
    psDroid->psGrpNext = nullptr;
  }
  if ((version >= VERSION_14) && (version < VERSION_18)) //version 14
  {
    //warning V14 - v17 only		
    //current Save Droid V18+ uses larger tarStatName
    //subsequent structure elements are not aligned between the two
    psSaveDroidV14 = (SAVE_DROID_V14*)psSaveDroid;
    if (psSaveDroidV14->tarStatName[0] == 0)
      psDroid->psTarStats = nullptr;
    else
    {
      id = getStructStatFromName(psSaveDroidV14->tarStatName);
      if (id != -1)
        psDroid->psTarStats = (BASE_STATS*)&asStructureStats[id];
      else
      {
        DEBUG_ASSERT_TEXT(FALSE, "loadUnit TargetStat not found");
        psDroid->psTarStats = nullptr;
        orderDroid(psDroid, DORDER_STOP);
      }
    }
    //warning V14 - v17 only		
    //rebuild the object pointer from the ID
    psDroid->psBaseStruct = (struct _structure*)psSaveDroidV14->baseStructID;
    psDroid->group = psSaveDroidV14->group;
    psDroid->selected = psSaveDroidV14->selected;
    psDroid->died = psSaveDroidV14->died;
    psDroid->lastEmission = psSaveDroidV14->lastEmission;
    //warning V14 - v17 only		
    for (i = 0; i < MAX_PLAYERS; i++)
      psDroid->visible[i] = psSaveDroidV14->visible[i];
    //end warning V14 - v17 only		
  }
  else if (version >= VERSION_18) //version 18
  {
    if (psSaveDroid->tarStatName[0] == 0)
      psDroid->psTarStats = nullptr;
    else
    {
      id = getStructStatFromName(psSaveDroid->tarStatName);
      if (id != -1)
        psDroid->psTarStats = (BASE_STATS*)&asStructureStats[id];
      else
      {
        DEBUG_ASSERT_TEXT(FALSE, "loadUnit TargetStat not found");
        psDroid->psTarStats = nullptr;
      }
    }
    //rebuild the object pointer from the ID
    psDroid->psBaseStruct = (struct _structure*)psSaveDroid->baseStructID;
    psDroid->group = psSaveDroid->group;
    psDroid->selected = psSaveDroid->selected;
    psDroid->died = psSaveDroid->died;
    psDroid->lastEmission = psSaveDroid->lastEmission;
    for (i = 0; i < MAX_PLAYERS; i++)
      psDroid->visible[i] = psSaveDroid->visible[i];
  }

  return psDroid;
}

// -----------------------------------------------------------------------------------------
//version 20 + after names change
DROID* buildDroidFromSaveDroid(SAVE_DROID* psSaveDroid, UDWORD version)
{
  DROID_TEMPLATE *psTemplate, sTemplate;
  DROID* psDroid;
  BOOL found;
  UDWORD i, id;
  SDWORD compInc;
  UDWORD burnTime;

  version;

  psTemplate = &sTemplate;

  psTemplate->pName = nullptr;

  //set up the template
  //copy the values across

  strncpy(psTemplate->aName, psSaveDroid->name, DROID_MAXNAME);
  psTemplate->aName[DROID_MAXNAME - 1] = 0;
  //ignore the first comp - COMP_UNKNOWN
  found = TRUE;
  for (i = 1; i < DROID_MAXCOMP; i++)
  {
    compInc = getCompFromName(i, psSaveDroid->asBits[i].name);

    //HACK to get the game to load when ECMs, Sensors or RepairUnits have been deleted
    if (compInc < 0 AND (i == COMP_ECM OR i == COMP_SENSOR OR i == COMP_REPAIRUNIT))
    {
      //set the ECM to be the defaultECM ...
      if (i == COMP_ECM)
        compInc = aDefaultECM[psSaveDroid->player];
      else if (i == COMP_SENSOR)
        compInc = aDefaultSensor[psSaveDroid->player];
      else if (i == COMP_REPAIRUNIT)
        compInc = aDefaultRepair[psSaveDroid->player];
    }
    else if (compInc < 0)
    {
      Neuron::Fatal("This component no longer exists - {}, the droid will be deleted", psSaveDroid->asBits[i].name);

      found = FALSE;
      break; //continue;
    }
    psTemplate->asParts[i] = static_cast<UDWORD>(compInc);
  }
  if (!found)
  {
    //ignore this record
    DEBUG_ASSERT_TEXT(found, "buildUnitFromSavedUnit; failed to find weapon");
    return nullptr;
  }
  psTemplate->numWeaps = psSaveDroid->numWeaps;
  found = TRUE;
  if (psSaveDroid->numWeaps > 0)
  {
    psTemplate->asWeaps[0] = getCompFromName(COMP_WEAPON, psSaveDroid->asWeaps[0].name);

    if (psTemplate->asWeaps[0] < 0)
    {
      Neuron::Fatal("This component no longer exists - {}, the droid will be deleted", psSaveDroid->asWeaps[0].name);
      found = FALSE;
    }
  }
  if (!found)
  {
    //ignore this record
    DEBUG_ASSERT_TEXT(found, "buildUnitFromSavedUnit; failed to find weapon");
    return nullptr;
  }

  psTemplate->buildPoints = calcTemplateBuild(psTemplate);
  psTemplate->powerPoints = calcTemplatePower(psTemplate);
  psTemplate->droidType = static_cast<DROID_TYPE>(psSaveDroid->droidType);

  /*create the Droid */

  // ignore brains for now
  // not any *$£&!!! more - JOHN

  turnOffMultiMsg(TRUE);

  if (psSaveDroid->x == INVALID_XY) { psDroid = buildDroid(psTemplate, psSaveDroid->x, psSaveDroid->y, psSaveDroid->player, TRUE); }
  else if (psSaveDroid->saveType == DROID_ON_TRANSPORT) { psDroid = buildDroid(psTemplate, 0, 0, psSaveDroid->player, TRUE); }
  else { psDroid = buildDroid(psTemplate, psSaveDroid->x, psSaveDroid->y, psSaveDroid->player, FALSE); }

  if (psDroid == nullptr)
  {
    DEBUG_ASSERT_TEXT(FALSE, "buildUnitFromSavedUnit; failed to build unit");
    return nullptr;
  }

  turnOffMultiMsg(FALSE);

  //copy the droid's weapon stats
  if (psDroid->asWeaps[0].nStat > 0)
  {
    psDroid->asWeaps[0].hitPoints = psSaveDroid->asWeaps[0].hitPoints;
    psDroid->asWeaps[0].ammo = psSaveDroid->asWeaps[0].ammo;
    psDroid->asWeaps[0].lastFired = psSaveDroid->asWeaps[0].lastFired;
  }
  //copy the values across
  psDroid->id = psSaveDroid->id;
  //are these going to ever change from the values set up with?
  //			psDroid->z = psSaveDroid->z;		// use the correct map height value

  psDroid->direction = static_cast<UWORD>(psSaveDroid->direction);
  psDroid->body = psSaveDroid->body;
  if (psDroid->body > psDroid->originalBody)
    psDroid->body = psDroid->originalBody;

  psDroid->inFire = psSaveDroid->inFire;
  psDroid->burnDamage = psSaveDroid->burnDamage;
  burnTime = psSaveDroid->burnStart;
  psDroid->burnStart = burnTime;

  psDroid->numKills = static_cast<UWORD>(psSaveDroid->numKills);
  //version 14
  psDroid->resistance = droidResistance(psDroid);

  //version 11
  psDroid->turretRotation = psSaveDroid->turretRotation;
  psDroid->turretPitch = psSaveDroid->turretPitch;

  //version 12
  psDroid->order = psSaveDroid->order;
  psDroid->orderX = psSaveDroid->orderX;
  psDroid->orderY = psSaveDroid->orderY;
  psDroid->orderX2 = psSaveDroid->orderX2;
  psDroid->orderY2 = psSaveDroid->orderY2;
  psDroid->timeLastHit = psSaveDroid->timeLastHit;
  //rebuild the object pointer from the ID
  psDroid->psTarget = (BASE_OBJECT*)psSaveDroid->targetID;
  psDroid->secondaryOrder = psSaveDroid->secondaryOrder;
  psDroid->action = psSaveDroid->action;
  psDroid->actionX = psSaveDroid->actionX;
  psDroid->actionY = psSaveDroid->actionY;
  //rebuild the object pointer from the ID
  psDroid->psActionTarget = (BASE_OBJECT*)psSaveDroid->actionTargetID;
  psDroid->actionStarted = psSaveDroid->actionStarted;
  psDroid->actionPoints = psSaveDroid->actionPoints;
  //actionHeight has been renamed to powerAccrued - AB 7/1/99
  psDroid->powerAccrued = psSaveDroid->actionHeight;
  //added for V14

  //version 18
  if (psSaveDroid->tarStatName[0] == 0)
    psDroid->psTarStats = nullptr;
  else
  {
    id = getStructStatFromName(psSaveDroid->tarStatName);
    if (id != -1)
      psDroid->psTarStats = (BASE_STATS*)&asStructureStats[id];
    else
    {
      DEBUG_ASSERT_TEXT(FALSE, "loadUnit TargetStat not found");
      psDroid->psTarStats = nullptr;
    }
  }
  //rebuild the object pointer from the ID
  psDroid->psBaseStruct = (struct _structure*)psSaveDroid->baseStructID;
  psDroid->group = psSaveDroid->group;
  psDroid->selected = psSaveDroid->selected;
  psDroid->died = psSaveDroid->died;
  psDroid->lastEmission = psSaveDroid->lastEmission;
  for (i = 0; i < MAX_PLAYERS; i++)
    psDroid->visible[i] = psSaveDroid->visible[i];

  if (version >= VERSION_21) //version 21
  {
    if ((psDroid->droidType != DROID_TRANSPORTER) && (psDroid->droidType != DROID_COMMAND))
    {
      //rebuild group from command id in loadDroidSetPointers
      psDroid->psGroup = (struct _droid_group*)psSaveDroid->commandId;
      psDroid->psGrpNext = (DROID*)UDWORD_MAX;
    }
  }
  else
  {
    if ((psDroid->droidType != DROID_TRANSPORTER) && (psDroid->droidType != DROID_COMMAND))
    {
      //dont rebuild group from command id in loadDroidSetPointers
      psDroid->psGroup = nullptr;
      psDroid->psGrpNext = nullptr;
    }
  }

  if (version >= VERSION_24) //version 24
  {
    psDroid->resistance = static_cast<SWORD>(psSaveDroid->resistance);
    memcpy(&psDroid->sMove, &psSaveDroid->sMove, sizeof(SAVE_MOVE_CONTROL));
    psDroid->sMove.fz = static_cast<float>(psDroid->z);
    if (psDroid->sMove.psFormation != nullptr)
    {
      psDroid->sMove.psFormation = nullptr;
      psSaveDroid->formationDir;
      psSaveDroid->formationX;
      psSaveDroid->formationY;
      // join a formation if it exists at the destination
      if (formationFind(&psDroid->sMove.psFormation, psSaveDroid->formationX, psSaveDroid->formationY))
        formationJoin(psDroid->sMove.psFormation, (BASE_OBJECT*)psDroid);
      else
      {
        // no formation so create a new one
        if (formationNew(&psDroid->sMove.psFormation, FT_LINE, psSaveDroid->formationX, psSaveDroid->formationY, psSaveDroid->formationDir))
          formationJoin(psDroid->sMove.psFormation, (BASE_OBJECT*)psDroid);
      }
    }
  }
  return psDroid;
}

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
      psStructure->direction = static_cast<UWORD>(psSaveStructure->direction);
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
    pFeature->direction = static_cast<UWORD>(psSaveFeature->direction);
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
  SAVE_STRUCTURE sSave; // close eyes now.
  SAVE_STRUCTURE* psSaveStructure = &sSave; // assumes save_struct is larger than all previous ones...
  auto psSaveStructure2 = (SAVE_STRUCTURE_V2*)&sSave;
  auto psSaveStructure12 = (SAVE_STRUCTURE_V12*)&sSave;
  auto psSaveStructure14 = (SAVE_STRUCTURE_V14*)&sSave;
  auto psSaveStructure15 = (SAVE_STRUCTURE_V15*)&sSave;
  auto psSaveStructure17 = (SAVE_STRUCTURE_V17*)&sSave;
  auto psSaveStructure20 = (SAVE_STRUCTURE_V20*)&sSave;
  // ok you can open them again..

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

  if (psHeader->version < VERSION_12)
    sizeOfSaveStruture = sizeof(SAVE_STRUCTURE_V2);
  else if (psHeader->version < VERSION_14)
    sizeOfSaveStruture = sizeof(SAVE_STRUCTURE_V12);
  else if (psHeader->version <= VERSION_14)
    sizeOfSaveStruture = sizeof(SAVE_STRUCTURE_V14);
  else if (psHeader->version <= VERSION_16)
    sizeOfSaveStruture = sizeof(SAVE_STRUCTURE_V15);
  else if (psHeader->version <= VERSION_19)
    sizeOfSaveStruture = sizeof(SAVE_STRUCTURE_V17);
  else if (psHeader->version <= VERSION_20)
    sizeOfSaveStruture = sizeof(SAVE_STRUCTURE_V20);
  else
    sizeOfSaveStruture = sizeof(SAVE_STRUCTURE);

  /* Load in the structure data */
  for (count = 0; count < psHeader->quantity; count++, pFileData += sizeOfSaveStruture)
  {
    if (psHeader->version < VERSION_12)
    {
      memcpy(psSaveStructure2, pFileData, sizeOfSaveStruture);
      xx = (psSaveStructure2->x >> TILE_SHIFT);
      yy = (psSaveStructure2->y >> TILE_SHIFT);
    }
    else if (psHeader->version < VERSION_14)
    {
      memcpy(psSaveStructure12, pFileData, sizeOfSaveStruture);
      xx = (psSaveStructure12->x >> TILE_SHIFT);
      yy = (psSaveStructure12->y >> TILE_SHIFT);
    }
    else if (psHeader->version <= VERSION_14)
    {
      memcpy(psSaveStructure14, pFileData, sizeOfSaveStruture);
      xx = (psSaveStructure14->x >> TILE_SHIFT);
      yy = (psSaveStructure14->y >> TILE_SHIFT);
    }
    else if (psHeader->version <= VERSION_16)
    {
      memcpy(psSaveStructure15, pFileData, sizeOfSaveStruture);
      xx = (psSaveStructure15->x >> TILE_SHIFT);
      yy = (psSaveStructure15->y >> TILE_SHIFT);
    }
    else if (psHeader->version <= VERSION_19)
    {
      memcpy(psSaveStructure17, pFileData, sizeOfSaveStruture);
      xx = (psSaveStructure17->x >> TILE_SHIFT);
      yy = (psSaveStructure17->y >> TILE_SHIFT);
    }
    else if (psHeader->version <= VERSION_20)
    {
      memcpy(psSaveStructure20, pFileData, sizeOfSaveStruture);
      xx = (psSaveStructure20->x >> TILE_SHIFT);
      yy = (psSaveStructure20->y >> TILE_SHIFT);
    }
    else
    {
      memcpy(psSaveStructure, pFileData, sizeOfSaveStruture);
      xx = (psSaveStructure->x >> TILE_SHIFT);
      yy = (psSaveStructure->y >> TILE_SHIFT);
    }

    for (x = (xx * scale); x < (xx * scale) + scale; x++)
    {
      for (y = (yy * scale); y < (yy * scale) + scale; y++)
        backDropSprite->bmp[((offY + y) * BACKDROP_WIDTH) + x + offX] = COL_RED;
    }
  }
  return TRUE;
}
