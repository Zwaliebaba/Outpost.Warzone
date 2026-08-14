/*
 * ScriptTabs.c
 *
 * All the tables for the script compiler
 *
 */

#include "Frame.h"
#include "Widget.h"
#include "Objects.h"
#include "Script.h"
#include "ScriptTabs.h"

// Get all the function prototypes
#include "ScriptFuncs.h"
#include "ScriptExtern.h"
#include "ScriptObj.h"
#include "ScriptVals.h"
#include "ScriptCB.h"
#include "ScriptAI.h"

#include "Droid.h"
#include "HCI.h"
#include "Message.h"
#include "Levels.h"
#include "Order.h"
#include "GTime.h"
#include "Mission.h"

#include "Design.h"			// for the iddes_...
#include "Display.h"		// for the MT_...  
#include "MultiPlay.h"
#include "IntFac.h"

/* The table of user defined types
 * The format is :
 *         <type id no.>,  "Type name",   <access type>
 *
 * The type id no. should start at VAL_USERTYPESTART and increase by one per type.
 * The access type controls whether the type is a simple data type or an object type.
 */
TYPE_SYMBOL asTypeTable[] = {
  {ST_INTMESSAGE, AT_SIMPLE, "INTMESSAGE", scrValDefSave, scrValDefLoad},
  {ST_BASEOBJECT, AT_OBJECT, "BASEOBJ", scrValDefSave, scrValDefLoad}, {ST_DROID, AT_OBJECT, "DROID", scrValDefSave, scrValDefLoad},
  {ST_STRUCTURE, AT_OBJECT, "STRUCTURE", scrValDefSave, scrValDefLoad}, {ST_FEATURE, AT_OBJECT, "FEATURE", scrValDefSave, scrValDefLoad},
  {ST_BASESTATS, AT_SIMPLE, "BASESTATS", scrValDefSave, scrValDefLoad},

  // Component types
  {ST_COMPONENT, AT_SIMPLE, "COMPONENT", scrValDefSave, scrValDefLoad}, {ST_BODY, AT_SIMPLE, "BODY", scrValDefSave, scrValDefLoad},
  {ST_PROPULSION, AT_SIMPLE, "PROPULSION", scrValDefSave, scrValDefLoad}, {ST_ECM, AT_SIMPLE, "ECM", scrValDefSave, scrValDefLoad},
  {ST_SENSOR, AT_SIMPLE, "SENSOR", scrValDefSave, scrValDefLoad}, {ST_CONSTRUCT, AT_SIMPLE, "CONSTRUCT", scrValDefSave, scrValDefLoad},
  {ST_WEAPON, AT_SIMPLE, "WEAPON", scrValDefSave, scrValDefLoad}, {ST_REPAIR, AT_SIMPLE, "REPAIR", scrValDefSave, scrValDefLoad},
  {ST_BRAIN, AT_SIMPLE, "BRAIN", scrValDefSave, scrValDefLoad}, {ST_TEMPLATE, AT_SIMPLE, "TEMPLATE", scrValDefSave, scrValDefLoad},
  {ST_STRUCTUREID, AT_SIMPLE, "STRUCTUREID", scrValDefSave, scrValDefLoad},
  {ST_STRUCTURESTAT, AT_SIMPLE, "STRUCTURESTAT", scrValDefSave, scrValDefLoad},
  {ST_FEATURESTAT, AT_SIMPLE, "FEATURESTAT", scrValDefSave, scrValDefLoad},
  {ST_DROIDID, AT_SIMPLE, "DROIDID", scrValDefSave, scrValDefLoad}, {ST_TEXTSTRING, AT_SIMPLE, "TEXTSTRING", scrValDefSave, scrValDefLoad},
  {ST_SOUND, AT_SIMPLE, "SOUND", scrValDefSave, scrValDefLoad}, {ST_LEVEL, AT_SIMPLE, "LEVEL", scrValDefSave, scrValDefLoad},
  {ST_GROUP, AT_OBJECT, "GROUP", scrValDefSave, scrValDefLoad}, {ST_RESEARCH, AT_SIMPLE, "RESEARCHSTAT", scrValDefSave, scrValDefLoad},

  //private types for code - NOT used in the scripts - hence the ""
  {ST_POINTER_O, AT_OBJECT, ""}, {ST_POINTER_T, AT_SIMPLE, ""}, {ST_POINTER_S, AT_SIMPLE, ""},

  /* This final entry marks the end of the type list */
  {0, AT_SIMPLE, "END OF TYPE LIST"},
};

/* The table of script callable C functions
 * This is laid out :
 *     "ScriptName",   function_Pointer,  <function return type>
 *      <number of parameters>,  <parameter types>,  FALSE
 */
FUNC_SYMBOL asFuncTable[] = {
  // These functions are part of the script library
  {"traceOn", interpTraceOn, VAL_VOID, 0, {VAL_VOID}}, {"traceOff", interpTraceOff, VAL_VOID, 0, {VAL_VOID}},
  {"setEventTrigger", eventSetTrigger, VAL_VOID, 2, {VAL_EVENT, VAL_TRIGGER}},
  {"eventTraceLevel", eventSetTraceLevel, VAL_VOID, 1, {VAL_INT}},

  // Trigger functions
  {"objectInRange", scrObjectInRange, VAL_BOOL, 4, {VAL_INT, VAL_INT, VAL_INT, VAL_INT}},
  {"droidInRange", scrDroidInRange, VAL_BOOL, 4, {VAL_INT, VAL_INT, VAL_INT, VAL_INT}},
  {"structInRange", scrStructInRange, VAL_BOOL, 4, {VAL_INT, VAL_INT, VAL_INT, VAL_INT}},
  {"playerPower", scrPlayerPower, VAL_INT, 1, {VAL_INT}},
  {"objectInArea", scrObjectInArea, VAL_BOOL, 5, {VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT}},
  {"droidInArea", scrDroidInArea, VAL_BOOL, 5, {VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT}},
  {"structInArea", scrStructInArea, VAL_BOOL, 5, {VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT}},
  {"seenStructInArea", scrSeenStructInArea, VAL_BOOL, 7, {VAL_INT, VAL_INT, VAL_BOOL, VAL_INT, VAL_INT, VAL_INT, VAL_INT}},
  {"structButNoWallsInArea", scrStructButNoWallsInArea, VAL_BOOL, 5, {VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT}},
  {"numObjectsInArea", scrNumObjectsInArea, VAL_INT, 5, {VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT}},
  {"numDroidsInArea", scrNumDroidsInArea, VAL_INT, 5, {VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT}},
  {"numStructsInArea", scrNumStructsInArea, VAL_INT, 5, {VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT}},
  {"numStructsButNotWallsInArea", scrNumStructsButNotWallsInArea, VAL_INT, 5, {VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT}},
  {"numStructsByTypeInArea", scrNumStructsByTypeInArea, VAL_INT, 6, {VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT}},
  {"droidHasSeen", scrDroidHasSeen, VAL_BOOL, 2, {ST_BASEOBJECT, VAL_INT}},
  {"buildingDestroyed", scrBuildingDestroyed, VAL_BOOL, 2, {ST_STRUCTUREID, VAL_INT}},
  {"structureIdle", scrStructureIdle, VAL_BOOL, 1, {ST_STRUCTURE}},
  {"initEnumStruct", scrInitEnumStruct, VAL_VOID, 4, {VAL_BOOL, ST_STRUCTURESTAT, VAL_INT, VAL_INT}},
  {"enumStruct", scrEnumStruct, ST_STRUCTURE, 0, {VAL_VOID}},
  {"structureBeingBuilt", scrStructureBeingBuilt, VAL_BOOL, 2, {ST_STRUCTURESTAT, VAL_INT}}, {
    "structureComplete", scrStructureComplete, VAL_BOOL, // pc multiplayer only
    1, {ST_STRUCTURE}
  },
  {"structureBuilt", scrStructureBuilt, VAL_BOOL, 2, {ST_STRUCTURESTAT, VAL_INT}},
  {"anyDroidsLeft", scrAnyDroidsLeft, VAL_BOOL, 1, {VAL_INT}}, {"anyStructButWallsLeft", scrAnyStructButWallsLeft, VAL_BOOL, 1, {VAL_INT}},
  {"anyFactoriesLeft", scrAnyFactoriesLeft, VAL_BOOL, 1, {VAL_INT}},

  // event functions
  {"enableComponent", scrEnableComponent, VAL_VOID, 2, {ST_COMPONENT, VAL_INT}},
  {"makeComponentAvailable", scrMakeComponentAvailable, VAL_VOID, 2, {ST_COMPONENT, VAL_INT}},
  {"enableStructure", scrEnableStructure, VAL_VOID, 2, {ST_STRUCTURESTAT, VAL_INT}}, {
    "isStructureAvailable", scrIsStructureAvailable, VAL_BOOL, // pc multiplay only
    2, {ST_STRUCTURESTAT, VAL_INT}
  },
  {"addDroid", scrAddDroid, ST_DROID, 4, {ST_TEMPLATE, VAL_INT, VAL_INT, VAL_INT}},
  {"addDroidToMissionList", scrAddDroidToMissionList, ST_DROID, 2, {ST_TEMPLATE, VAL_INT}},
  {"buildDroid", scrBuildDroid, VAL_VOID, 4, {ST_TEMPLATE, ST_STRUCTURE, VAL_INT, VAL_INT}},
  {"addTemplate", scrAddTemplate, VAL_BOOL, 2, {ST_TEMPLATE, VAL_INT}}, {"addReticuleButton", scrAddReticuleButton, VAL_VOID, 1, {VAL_INT}},
  {"removeReticuleButton", scrRemoveReticuleButton, VAL_VOID, 2, {VAL_INT, VAL_BOOL}},
  {"addMessage", scrAddMessage, VAL_VOID, 4, {ST_INTMESSAGE, VAL_INT, VAL_INT, VAL_BOOL}},
  {"removeMessage", scrRemoveMessage, VAL_VOID, 3, {ST_INTMESSAGE, VAL_INT, VAL_INT}},

  /*	{ "addTutorialMessage",		scrAddTutorialMessage,	VAL_VOID,
      2, { ST_INTMESSAGE, VAL_INT } },*/

  {"selectDroidByID", scrSelectDroidByID, VAL_BOOL, 2, {ST_DROIDID, VAL_INT}},
  {"setAssemblyPoint", scrSetAssemblyPoint, VAL_VOID, 3, {ST_STRUCTURE, VAL_INT, VAL_INT}},
  {"attackLocation", scrAttackLocation, VAL_VOID, 3, {VAL_INT, VAL_INT, VAL_INT}},
  {"initGetFeature", scrInitGetFeature, VAL_VOID, 3, {ST_FEATURESTAT, VAL_INT, VAL_INT}},
  {"getFeature", scrGetFeature, ST_FEATURE, 1, {VAL_INT}}, {"addFeature", scrAddFeature, ST_FEATURE, 3, {ST_FEATURESTAT, VAL_INT, VAL_INT}},
  {"destroyFeature", scrDestroyFeature, VAL_VOID, 1, {ST_FEATURE}},
  {"addStructure", scrAddStructure, ST_STRUCTURE, 4, {ST_STRUCTURESTAT, VAL_INT, VAL_INT, VAL_INT}},
  {"destroyStructure", scrDestroyStructure, VAL_VOID, 1, {ST_STRUCTURE}}, {"centreView", scrCentreView, VAL_VOID, 1, {ST_BASEOBJECT}},
  {"centreViewPos", scrCentreViewPos, VAL_VOID, 2, {VAL_INT, VAL_INT}},
  {"getStructure", scrGetStructure, ST_STRUCTURE, 2, {ST_STRUCTURESTAT, VAL_INT}},
  {"getTemplate", scrGetTemplate, ST_TEMPLATE, 2, {ST_COMPONENT, VAL_INT}}, {"getDroid", scrGetDroid, ST_DROID, 2, {ST_COMPONENT, VAL_INT}},
  {"setScrollParams", scrSetScrollParams, VAL_VOID, 4, {VAL_INT, VAL_INT, VAL_INT, VAL_INT}},
  {"setScrollMinX", scrSetScrollMinX, VAL_VOID, 1, {VAL_INT}}, {"setScrollMinY", scrSetScrollMinY, VAL_VOID, 1, {VAL_INT}},
  {"setScrollMaxX", scrSetScrollMaxX, VAL_VOID, 1, {VAL_INT}}, {"setScrollMaxY", scrSetScrollMaxY, VAL_VOID, 1, {VAL_INT}},
  {"setDefaultSensor", scrSetDefaultSensor, VAL_VOID, 2, {ST_SENSOR, VAL_INT}},
  {"setDefaultECM", scrSetDefaultECM, VAL_VOID, 2, {ST_ECM, VAL_INT}},
  {"setDefaultRepair", scrSetDefaultRepair, VAL_VOID, 2, {ST_REPAIR, VAL_INT}},
  {"setStructureLimits", scrSetStructureLimits, VAL_VOID, 3, {ST_STRUCTURESTAT, VAL_INT, VAL_INT}},
  {"setAllStructureLimits", scrSetAllStructureLimits, VAL_VOID, 2, {VAL_INT, VAL_INT}},
  {"applyLimitSet", scrApplyLimitSet, VAL_VOID, 0, {VAL_VOID}}, {"playSound", scrPlaySound, VAL_VOID, 2, {ST_SOUND, VAL_INT}},
  {"playSoundPos", scrPlaySoundPos, VAL_VOID, 5, {ST_SOUND, VAL_INT, VAL_INT, VAL_INT, VAL_INT}},
  {"addConsoleText", scrAddConsoleText, VAL_VOID, 2, {ST_TEXTSTRING, VAL_INT}},
  {"showConsoleText", scrShowConsoleText, VAL_VOID, 2, {ST_TEXTSTRING, VAL_INT}},
  {"tagConsoleText", scrTagConsoleText, VAL_VOID, 2, {ST_TEXTSTRING, VAL_INT}}, {"turnPowerOn", scrTurnPowerOn, VAL_VOID, 0, {VAL_VOID}},
  {"turnPowerOff", scrTurnPowerOff, VAL_VOID, 0, {VAL_VOID}}, {"tutorialEnd", scrTutorialEnd, VAL_VOID, 0, {VAL_VOID}},
  {"clearConsole", scrClearConsole, VAL_VOID, 0, {VAL_VOID}}, {"playVideo", scrPlayVideo, VAL_VOID, 2, {ST_TEXTSTRING, ST_TEXTSTRING}},
  {"gameOverMessage", scrGameOverMessage, VAL_VOID, 4, {ST_INTMESSAGE, VAL_INT, VAL_INT, VAL_BOOL}},
  {"gameOver", scrGameOver, VAL_VOID, 1, {VAL_BOOL}},
  {"playBackgroundAudio", scrPlayBackgroundAudio, VAL_VOID, 2, {ST_TEXTSTRING, VAL_INT}},
  {"playCDAudio", scrPlayCDAudio, VAL_VOID, 1, {VAL_INT}}, {"stopCDAudio", scrStopCDAudio, VAL_VOID, 0, {VAL_VOID}},
  {"pauseCDAudio", scrPauseCDAudio, VAL_VOID, 0, {VAL_VOID}}, {"resumeCDAudio", scrResumeCDAudio, VAL_VOID, 0, {VAL_VOID}},
  {"setRetreatPoint", scrSetRetreatPoint, VAL_VOID, 3, {VAL_INT, VAL_INT, VAL_INT}},
  {"setRetreatForce", scrSetRetreatForce, VAL_VOID, 2, {VAL_INT, VAL_INT}},
  {"setRetreatLeadership", scrSetRetreatLeadership, VAL_VOID, 2, {VAL_INT, VAL_INT}},
  {"setGroupRetreatForce", scrSetGroupRetreatForce, VAL_VOID, 2, {ST_GROUP, VAL_INT}},
  {"setGroupRetreatLeadership", scrSetGroupRetreatLeadership, VAL_VOID, 2, {ST_GROUP, VAL_INT}},
  {"setGroupRetreatPoint", scrSetGroupRetreatPoint, VAL_VOID, 3, {ST_GROUP, VAL_INT, VAL_INT}},
  {"setRetreatHealth", scrSetRetreatHealth, VAL_VOID, 2, {VAL_INT, VAL_INT}},
  {"setGroupRetreatHealth", scrSetGroupRetreatHealth, VAL_VOID, 2, {ST_GROUP, VAL_INT}},
  {"startMission", scrStartMission, VAL_VOID, 2, {VAL_INT, ST_LEVEL}}, {"setSnow", scrSetSnow, VAL_VOID, 1, {VAL_BOOL}},
  {"setRain", scrSetRain, VAL_VOID, 1, {VAL_BOOL}}, {"setBackgroundFog", scrSetBackgroundFog, VAL_VOID, 1, {VAL_BOOL}},
  {"setDepthFog", scrSetDepthFog, VAL_VOID, 1, {VAL_BOOL}}, {"setFogColour", scrSetFogColour, VAL_VOID, 3, {VAL_INT, VAL_INT, VAL_INT}},
  {"setTransporterExit", scrSetTransporterExit, VAL_VOID, 3, {VAL_INT, VAL_INT, VAL_INT}},
  {"flyTransporterIn", scrFlyTransporterIn, VAL_VOID, 4, {VAL_INT, VAL_INT, VAL_INT, VAL_BOOL}},
  {"addDroidToTransporter", scrAddDroidToTransporter, VAL_VOID, 2, {ST_DROID, ST_DROID}},

  /*{ "endMission",			scrEndMission,				VAL_VOID,
    1, { VAL_BOOL } },*/

  {"structureBuiltInRange", scrStructureBuiltInRange, ST_STRUCTURE, 5, {ST_STRUCTURESTAT, VAL_INT, VAL_INT, VAL_INT, VAL_INT}},
  {"random", scrRandom, VAL_INT, 1, {VAL_INT}}, {"randomiseSeed", scrRandomiseSeed, VAL_VOID, 0, {VAL_VOID}},
  {"enableResearch", scrEnableResearch, VAL_VOID, 2, {ST_RESEARCH, VAL_INT}},
  {"completeResearch", scrCompleteResearch, VAL_VOID, 2, {ST_RESEARCH, VAL_INT}}, {"flashOn", scrFlashOn, VAL_VOID, 1, {VAL_INT}},
  {"flashOff", scrFlashOff, VAL_VOID, 1, {VAL_INT}}, {"setPowerLevel", scrSetPowerLevel, VAL_VOID, 2, {VAL_INT, VAL_INT}},
  {"addPower", scrAddPower, VAL_VOID, 2, {VAL_INT, VAL_INT}},
  {"setLandingZone", scrSetLandingZone, VAL_VOID, 4, {VAL_INT, VAL_INT, VAL_INT, VAL_INT}},
  {"setLimboLanding", scrSetLimboLanding, VAL_VOID, 4, {VAL_INT, VAL_INT, VAL_INT, VAL_INT}},
  {"setNoGoArea", scrSetNoGoArea, VAL_VOID, 5, {VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT}},
  {"initAllNoGoAreas", scrInitAllNoGoAreas, VAL_VOID, 0, {VAL_VOID}}, {"setRadarZoom", scrSetRadarZoom, VAL_VOID, 1, {VAL_INT}},
  {"setReinforcementTime", scrSetReinforcementTime, VAL_VOID, 1, {VAL_INT}}, {"setMissionTime", scrSetMissionTime, VAL_VOID, 1, {VAL_INT}},
  {"missionTimeRemaining", scrMissionTimeRemaining, VAL_INT, 0, {VAL_VOID}},
  {"flushConsoleMessages", scrFlushConsoleMessages, VAL_VOID, 0, {VAL_VOID}},
  {"pickStructLocation", scrPickStructLocation, VAL_BOOL, 4, {ST_STRUCTURESTAT, VAL_REF | VAL_INT, VAL_REF | VAL_INT, VAL_INT}},

  // AI functions
  {"groupAddDroid", scrGroupAddDroid, VAL_VOID, 2, {ST_GROUP, ST_DROID}},
  {"groupAddArea", scrGroupAddArea, VAL_VOID, 6, {ST_GROUP, VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT}},
  {"groupAddAreaNoGroup", scrGroupAddArea, VAL_VOID, 6, {ST_GROUP, VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT}},
  {"groupAddGroup", scrGroupAddGroup, VAL_VOID, 2, {ST_GROUP, ST_GROUP}},
  {"groupMember", scrGroupMember, VAL_BOOL, 2, {ST_GROUP, ST_DROID}}, {"idleGroup", scrIdleGroup, VAL_INT, 1, {ST_GROUP}},
  {"initIterateGroup", scrInitIterateGroup, VAL_VOID, 1, {ST_GROUP}}, {"iterateGroup", scrIterateGroup, ST_DROID, 1, {ST_GROUP}},
  {"droidLeaveGroup", scrDroidLeaveGroup, VAL_VOID, 1, {ST_DROID}}, {"orderDroid", scrOrderDroid, VAL_VOID, 2, {ST_DROID, VAL_INT}},
  {"orderDroidLoc", scrOrderDroidLoc, VAL_VOID, 4, {ST_DROID, VAL_INT, VAL_INT, VAL_INT}},
  {"orderDroidObj", scrOrderDroidObj, VAL_VOID, 3, {ST_DROID, VAL_INT, ST_BASEOBJECT}},
  {"orderDroidStatsLoc", scrOrderDroidStatsLoc, VAL_VOID, 5, {ST_DROID, VAL_INT, ST_BASESTATS, VAL_INT, VAL_INT}},
  {"orderGroup", scrOrderGroup, VAL_VOID, 2, {ST_GROUP, VAL_INT}},
  {"orderGroupLoc", scrOrderGroupLoc, VAL_VOID, 4, {ST_GROUP, VAL_INT, VAL_INT, VAL_INT}},
  {"orderGroupObj", scrOrderGroupObj, VAL_VOID, 3, {ST_GROUP, VAL_INT, ST_BASEOBJECT}},
  {"setDroidSecondary", scrSetDroidSecondary, VAL_VOID, 3, {ST_DROID, VAL_INT, VAL_INT}},
  {"setGroupSecondary", scrSetGroupSecondary, VAL_VOID, 3, {ST_GROUP, VAL_INT, VAL_INT}},
  {"initIterateCluster", scrInitIterateCluster, VAL_VOID, 1, {VAL_INT}},
  {"iterateCluster", scrIterateCluster, ST_BASEOBJECT, 0, {VAL_VOID}},
  {"cmdDroidAddDroid", scrCmdDroidAddDroid, VAL_VOID, 2, {ST_DROID, ST_DROID}},

  // a couple of example functions
  {"numMessageBox", scrNumMB, VAL_VOID, 1, {VAL_INT}}, {"debugBox", scrNumMB, VAL_VOID, 1, {VAL_INT}},
  {"approxRoot", scrApproxRoot, VAL_INT, 2, {VAL_INT, VAL_INT}}, {"refTest", scrRefTest, VAL_VOID, 1, {VAL_INT}},

  // geo funcs
  {"distBetweenTwoPoints", scrDistanceTwoPts, VAL_INT, 4, {VAL_INT, VAL_INT, VAL_INT, VAL_INT}},
  {"losTwoObjects", scrLOSTwoBaseObjects, VAL_BOOL, 3, {ST_BASEOBJECT, ST_BASEOBJECT, VAL_BOOL}},
  {
    "killStructsInArea", scrDestroyStructuresInArea, VAL_VOID, 8, {VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_BOOL, VAL_BOOL}
  },
  {
    "getThreatInArea", scrThreatInArea, VAL_INT, 10,
    {VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_BOOL}
  },
  {"getNearestGateway", scrGetNearestGateway, VAL_BOOL, 4, {VAL_INT, VAL_INT, VAL_REF | VAL_INT, VAL_REF | VAL_INT}},
  {"setWaterTile", scrSetWaterTile, VAL_VOID, 1, {VAL_INT}}, {"setRubbleTile", scrSetRubbleTile, VAL_VOID, 1, {VAL_INT}},
  {"setCampaignNumber", scrSetCampaignNumber, VAL_VOID, 1, {VAL_INT}},
  {"testStructureModule", scrTestStructureModule, VAL_BOOL, 3, {VAL_INT, ST_STRUCTURE, VAL_INT}},
  {"killDroidsInArea", scrDestroyUnitsInArea, VAL_INT, 5, {VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT}},
  {"vanishUnit", scrRemoveDroid, VAL_VOID, 1, {ST_DROID}}, {"forceDamageObject", scrForceDamage, VAL_VOID, 2, {ST_BASEOBJECT, VAL_INT}},

  //multiplayer stuff.
  {"isHumanPlayer", scrIsHumanPlayer, VAL_BOOL, 1, {VAL_INT}}, {"offerAlliance", scrOfferAlliance, VAL_VOID, 2, {VAL_INT, VAL_INT}},
  {"createAlliance", scrCreateAlliance, VAL_VOID, 2, {VAL_INT, VAL_INT}},
  {"breakAlliance", scrBreakAlliance, VAL_VOID, 2, {VAL_INT, VAL_INT}}, {"allianceExists", scrAllianceExists, VAL_BOOL, 0, {VAL_VOID}},
  {"allianceExistsBetween", scrAllianceExistsBetween, VAL_BOOL, 2, {VAL_INT, VAL_INT}},
  {"playerInAlliance", scrPlayerInAlliance, VAL_BOOL, 1, {VAL_INT}}, {"dominatingAlliance", scrDominatingAlliance, VAL_BOOL, 0, {VAL_VOID}},
  {"myResponsibility", scrMyResponsibility, VAL_BOOL, 1, {VAL_INT}},

  // object conversion routines
  {"objToDroid", scrObjToDroid, ST_DROID, 1, {ST_BASEOBJECT}}, {"objToStructure", scrObjToStructure, ST_STRUCTURE, 1, {ST_BASEOBJECT}},
  {"objToFeature", scrObjToFeature, ST_FEATURE, 1, {ST_BASEOBJECT}}, {"getGameStatus", scrGetGameStatus, VAL_BOOL, 1, {VAL_INT}},

  //player colour access functions
  {"getPlayerColour", scrGetPlayerColour, VAL_INT, 1, {VAL_INT}}, {"setPlayerColour", scrSetPlayerColour, VAL_VOID, 2, {VAL_INT, VAL_INT}},
  {"takeOverDroidsInArea", scrTakeOverDroidsInArea, VAL_INT, 6, {VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT}},
  {
    "takeOverDroidsInAreaExp", scrTakeOverDroidsInAreaExp, VAL_INT, 8,
    {VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT}
  },
  {"takeOverStructsInArea", scrTakeOverStructsInArea, VAL_INT, 6, {VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT}},
  {"takeOverSingleDroid", scrTakeOverSingleDroid, ST_DROID, 2, {ST_DROID, VAL_INT}},
  {"takeOverSingleStructure", scrTakeOverSingleStructure, ST_STRUCTURE, 2, {ST_STRUCTURE, VAL_INT}},
  {"resetStructTargets", scrResetStructTargets, VAL_VOID, 0, {VAL_VOID},},
  {"resetDroidTargets", scrResetDroidTargets, VAL_VOID, 0, {VAL_VOID},}, {"setStructTarPref", scrSetStructTarPref, VAL_VOID, 1, {VAL_INT},},
  {"setStructTarIgnore", scrSetStructTarIgnore, VAL_VOID, 1, {VAL_INT},}, {"setDroidTarPref", scrSetDroidTarPref, VAL_VOID, 1, {VAL_INT},},
  {"setDroidTarIgnore", scrSetDroidTarIgnore, VAL_VOID, 1, {VAL_INT},},
  {"structTargetInArea", scrStructTargetInArea, ST_STRUCTURE, 6, {VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT,},},
  {"structTargetOnMap", scrStructTargetOnMap, ST_STRUCTURE, 2, {VAL_INT, VAL_INT},},
  {"droidTargetInArea", scrDroidTargetInArea, ST_DROID, 6, {VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT, VAL_INT,},},
  {"droidTargetOnMap", scrDroidTargetOnMap, ST_DROID, 2, {VAL_INT, VAL_INT},},
  {"targetInCluster", scrTargetInCluster, ST_DROID, 2, {VAL_INT, VAL_INT},},
  {"setDroidsToSafetyFlag", scrSetDroidsToSafetyFlag, VAL_VOID, 1, {VAL_BOOL}},
  {"setPlayCountDown", scrSetPlayCountDown, VAL_VOID, 1, {VAL_BOOL}}, {"getDroidCount", scrGetDroidCount, VAL_INT, 1, {VAL_INT}},
  {"fireWeaponAtObj", scrFireWeaponAtObj, VAL_VOID, 2, {ST_WEAPON, ST_BASEOBJECT}},
  {"fireWeaponAtLoc", scrFireWeaponAtLoc, VAL_VOID, 3, {ST_WEAPON, VAL_INT, VAL_INT}},
  {"setDroidKills", scrSetDroidKills, VAL_VOID, 2, {ST_DROID, VAL_INT}},
  {"resetPlayerVisibility", scrResetPlayerVisibility, VAL_VOID, 1, {VAL_INT}},
  {"setVTOLReturnPos", scrSetVTOLReturnPos, VAL_VOID, 3, {VAL_INT, VAL_INT, VAL_INT}}, {"isVtol", scrIsVtol, VAL_BOOL, 1, {ST_DROID}},
  {"tutorialTemplates", scrTutorialTemplates, VAL_VOID, 0, {VAL_VOID}},
  {"resetLimboMission", scrResetLimboMission, VAL_VOID, 0, {VAL_VOID}},

  //ajl skirmish funcs
  {"skDoResearch", scrSkDoResearch, VAL_VOID, 3, {ST_STRUCTURE, VAL_INT, VAL_INT}},
  {"skLocateEnemy", scrSkLocateEnemy, ST_BASEOBJECT, 1, {VAL_INT}},
  {"skCanBuildTemplate", scrSkCanBuildTemplate, VAL_BOOL, 3, {VAL_INT, ST_STRUCTURE, ST_TEMPLATE}},
  {"skVtolEnableCheck", scrSkVtolEnableCheck, VAL_BOOL, 1, {VAL_INT}},
  {"skGetFactoryCapacity", scrSkGetFactoryCapacity, VAL_INT, 1, {ST_STRUCTURE}},
  {"skDifficultyModifier", scrSkDifficultyModifier, VAL_VOID, 1, {VAL_INT}},
  {
    "skDefenseLocation", scrSkDefenseLocation, VAL_BOOL, 6,
    {VAL_REF | VAL_INT, VAL_REF | VAL_INT, ST_STRUCTURESTAT, ST_STRUCTURESTAT, ST_DROID, VAL_INT}
  },
  {"skFireLassat", scrSkFireLassat, VAL_VOID, 2, {VAL_INT, ST_BASEOBJECT}},

  //	{ "skOrderDroidLineBuild",	scrSkOrderDroidLineBuild,	VAL_VOID,
  //	6, { ST_DROID, ST_STRUCTURESTAT, VAL_INT, VAL_INT, VAL_INT, VAL_INT } },

  /* This final entry marks the end of the function list */
  {"FUNCTION LIST END", nullptr, VAL_VOID, 0, {VAL_VOID}}
};

/*
 * The table of external variables and their access functions.
 * The table is laid out as follows :
 *
 *   "variable_identifier", <variable type>, ST_EXTERN, 0, <index>,
 *		<get_function>, <set_function>
 *
 * The Storage type for an external variable is always ST_EXTERN.
 * The index is not used by the compiler but is passed to the access function
 * to allow one function to deal with a number of variables
 */
VAR_SYMBOL asExternTable[] = {
  {"isPSX", VAL_BOOL, ST_EXTERN, 0, EXTID_ISPSX, scrGenExternGet, nullptr},
  {"trackTransporter", VAL_BOOL, ST_EXTERN, 0, EXTID_TRACKTRANSPORTER, scrGenExternGet, nullptr},
  {"mapWidth", VAL_INT, ST_EXTERN, 0, EXTID_MAPWIDTH, scrGenExternGet, nullptr},
  {"mapHeight", VAL_INT, ST_EXTERN, 0, EXTID_MAPHEIGHT, scrGenExternGet, nullptr},
  {"gameInitialised", VAL_BOOL, ST_EXTERN, 0, EXTID_GAMEINIT, scrGenExternGet, nullptr},
  {"selectedPlayer", VAL_INT, ST_EXTERN, 0, EXTID_SELECTEDPLAYER, scrGenExternGet, nullptr},
  {"gameTime", VAL_INT, ST_EXTERN, 0, EXTID_GAMETIME, scrGenExternGet, nullptr},
  {"gameLevel", VAL_INT, ST_EXTERN, 0, EXTID_GAMELEVEL, scrGenExternGet, scrGenExternSet},
  {"inTutorial", VAL_BOOL, ST_EXTERN, 0, EXTID_TUTORIAL, scrGenExternGet, scrGenExternSet},
  {"cursorType", VAL_INT, ST_EXTERN, 0, EXTID_CURSOR, scrGenExternGet, nullptr},
  {"intMode", VAL_INT, ST_EXTERN, 0, EXTID_INTMODE, scrGenExternGet, nullptr},
  {"targetedObjectType", VAL_INT, ST_EXTERN, 0, EXTID_TARGETTYPE, scrGenExternGet, nullptr},
  {"extraVictoryFlag", VAL_BOOL, ST_EXTERN, 0, EXTID_EXTRAVICTORYFLAG, scrGenExternGet, scrGenExternSet},
  {"extraFailFlag", VAL_BOOL, ST_EXTERN, 0, EXTID_EXTRAFAILFLAG, scrGenExternGet, scrGenExternSet},
  {"multiPlayerGameType", VAL_INT, ST_EXTERN, 0, EXTID_MULTIGAMETYPE, scrGenExternGet, nullptr},
  {"multiPlayerMaxPlayers", VAL_INT, ST_EXTERN, 0, EXTID_MULTIGAMEHUMANMAX, scrGenExternGet, nullptr},
  {"multiPlayerBaseType", VAL_INT, ST_EXTERN, 0, EXTID_MULTIGAMEBASETYPE, scrGenExternGet, nullptr},

  /* This entry marks the end of the variable list */
  {nullptr, VAL_VOID, ST_EXTERN, 0, 0, nullptr, nullptr}
};

/*
 * The table of object variables and their access functions.
 * The table is laid out as follows :
 *
 *   "variable_identifier", <variable type>, ST_OBJECT, <object type>, <index>,
 *		<get_function>, <set_function>
 *
 * The Storage type for an object variable is always ST_OBJECT.
 * The object type is the type of the object this is a member of.
 * The index is not used by the compiler but is passed to the access function
 * to allow one function to deal with a number of variables
 */
VAR_SYMBOL asObjTable[] = {
  // base object variables
  {"x", VAL_INT, ST_OBJECT, ST_BASEOBJECT, OBJID_POSX, scrBaseObjGet, nullptr},
  {"y", VAL_INT, ST_OBJECT, ST_BASEOBJECT, OBJID_POSY, scrBaseObjGet, nullptr},
  {"z", VAL_INT, ST_OBJECT, ST_BASEOBJECT, OBJID_POSZ, scrBaseObjGet, nullptr},
  {"id", VAL_INT, ST_OBJECT, ST_BASEOBJECT, OBJID_ID, scrBaseObjGet, nullptr},
  {"player", VAL_INT, ST_OBJECT, ST_BASEOBJECT, OBJID_PLAYER, scrBaseObjGet, nullptr},
  {"type", VAL_INT, ST_OBJECT, ST_BASEOBJECT, OBJID_TYPE, scrBaseObjGet, nullptr},
  {"clusterID", VAL_INT, ST_OBJECT, ST_BASEOBJECT, OBJID_CLUSTERID, scrBaseObjGet, nullptr},
  {"health", VAL_INT, ST_OBJECT, ST_BASEOBJECT, OBJID_HEALTH, scrBaseObjGet, nullptr},

  // droid variables
  {"order", VAL_INT, ST_OBJECT, ST_DROID, OBJID_ORDER, scrBaseObjGet, nullptr},
  {"droidType", VAL_INT, ST_OBJECT, ST_DROID, OBJID_DROIDTYPE, scrBaseObjGet, nullptr},
  {"body", ST_BODY, ST_OBJECT, ST_DROID, OBJID_BODY, scrBaseObjGet, nullptr},
  {"propulsion", ST_PROPULSION, ST_OBJECT, ST_DROID, OBJID_PROPULSION, scrBaseObjGet, nullptr},
  {"weapon", ST_WEAPON, ST_OBJECT, ST_DROID, OBJID_WEAPON, scrBaseObjGet, nullptr},
  {"orderx", VAL_INT, ST_OBJECT, ST_DROID, OBJID_ORDERX, scrBaseObjGet, nullptr},
  {"ordery", VAL_INT, ST_OBJECT, ST_DROID, OBJID_ORDERY, scrBaseObjGet, nullptr},

  // structure variables
  {"stat", ST_STRUCTURESTAT, ST_OBJECT, ST_STRUCTURE, OBJID_STRUCTSTAT, scrBaseObjGet, nullptr},

  // group variables
  {"x", VAL_INT, ST_OBJECT, ST_GROUP, GROUPID_POSX, scrGroupObjGet, nullptr},
  {"y", VAL_INT, ST_OBJECT, ST_GROUP, GROUPID_POSY, scrGroupObjGet, nullptr},
  {"members", VAL_INT, ST_OBJECT, ST_GROUP, GROUPID_MEMBERS, scrGroupObjGet, nullptr},
  {"health", VAL_INT, ST_OBJECT, ST_GROUP, GROUPID_HEALTH, scrGroupObjGet, nullptr},

  /* This entry marks the end of the variable list */
  {nullptr, VAL_VOID, ST_OBJECT, VAL_VOID, 0, nullptr, nullptr}
};

#ifndef NOSCRIPT
/* The table of constant variables
 * The format is :
 *
 *	"variable name", <variable type>, <bool value>, <int value>,
 *						<object pointer value>
 * 
 * Only the value corresponding to the type should be set, all other values
 * should be 0.
 *
 * Any user-type constants should use the object pointer value.
 */
CONST_SYMBOL asConstantTable[] = {
  /*	{ "TEST_BOOL_CONST",	VAL_BOOL, TRUE,		0,		0 },
    { "TEST_INT_CONST",		VAL_INT,	0,		10,		0 },*/

  //reticule button IDs	- for scrFlashOn & Off
  // original annette styley
  {"OPTIONS", VAL_INT, 0, IDRET_OPTIONS, nullptr}, {"CANCEL", VAL_INT, 0, IDRET_CANCEL, nullptr},
  {"BUILD", VAL_INT, 0, IDRET_BUILD, nullptr}, {"MANUFACTURE", VAL_INT, 0, IDRET_MANUFACTURE, nullptr},
  {"RESEARCH", VAL_INT, 0, IDRET_RESEARCH, nullptr}, {"INTELMAP", VAL_INT, 0, IDRET_INTEL_MAP, nullptr},
  {"DESIGN", VAL_INT, 0, IDRET_DESIGN, nullptr}, {"COMMAND", VAL_INT, 0, IDRET_COMMAND, nullptr},

  // new styley that supports many other buttons
  {"IDRET_OPTIONS", VAL_INT, 0, IDRET_OPTIONS, nullptr}, {"IDRET_CANCEL", VAL_INT, 0, IDRET_CANCEL, nullptr},
  {"IDRET_BUILD", VAL_INT, 0, IDRET_BUILD, nullptr}, {"IDRET_MANUFACTURE", VAL_INT, 0, IDRET_MANUFACTURE, nullptr},
  {"IDRET_RESEARCH", VAL_INT, 0, IDRET_RESEARCH, nullptr}, {"IDRET_INTELMAP", VAL_INT, 0, IDRET_INTEL_MAP, nullptr},
  {"IDRET_DESIGN", VAL_INT, 0, IDRET_DESIGN, nullptr}, {"IDRET_COMMAND", VAL_INT, 0, IDRET_COMMAND, nullptr},
  {"IDRET_ORDER", VAL_INT, 0, IDRET_ORDER, nullptr}, {"IDRET_TRANSPORTER", VAL_INT, 0, IDRET_TRANSPORTER, nullptr},

  // Design screen buttons
  {"IDDES_TEMPLSTART", VAL_INT, 0, IDDES_TEMPLSTART, nullptr}, {"IDDES_SYSTEMBUTTON", VAL_INT, 0, IDDES_SYSTEMBUTTON, nullptr},
  {"IDDES_BODYBUTTON", VAL_INT, 0, IDDES_BODYBUTTON, nullptr}, {"IDDES_PROPBUTTON", VAL_INT, 0, IDDES_PROPBUTTON, nullptr},
  {"IDOBJ_STATSTART", VAL_INT, 0, IDOBJ_STATSTART, nullptr},

  // one below obj_statstart
  {"IDOBJ_OBJSTART", VAL_INT, 0, IDOBJ_OBJSTART, nullptr}, {"IDSTAT_START", VAL_INT, 0, IDSTAT_START, nullptr},

  //message Types
  {"RES_MSG", VAL_INT, 0, MSG_RESEARCH, nullptr}, {"CAMP_MSG", VAL_INT, 0, MSG_CAMPAIGN, nullptr},
  {"MISS_MSG", VAL_INT, 0, MSG_MISSION, nullptr}, {"PROX_MSG", VAL_INT, 0, MSG_PROXIMITY, nullptr},
  //{ "TUT_MSG",	VAL_INT,	0,		MSG_TUTORIAL,		0}, NOT NEEDED

  //used for null pointers
  {"NULLTEMPLATE", ST_POINTER_T, 0, 0, nullptr}, {"NULLOBJECT", ST_POINTER_O, 0, 0, nullptr}, {"NULLSTAT", ST_POINTER_S, 0, 0, nullptr},
  {"NULLSTRING", ST_TEXTSTRING, 0, 0, nullptr},

  //barbarian player ids
  {"BARBARIAN1", VAL_INT, 0, BARB1, nullptr}, {"BARBARIAN2", VAL_INT, 0, BARB2, nullptr},

  //#define used to set the reinforcement timer with
  {"LZ_COMPROMISED_TIME", VAL_INT, 0, SCR_LZ_COMPROMISED_TIME, nullptr},

  // BASEOBJ types
  {"OBJ_DROID", VAL_INT, 0, OBJ_DROID, nullptr}, {"OBJ_STRUCTURE", VAL_INT, 0, OBJ_STRUCTURE, nullptr},
  {"OBJ_FEATURE", VAL_INT, 0, OBJ_FEATURE, nullptr},
  //mission Types
  {"CAMP_START", VAL_INT, 0, LDS_CAMSTART, nullptr},
#ifndef COVERMOUNT
  {"CAMP_EXPAND", VAL_INT, 0, LDS_EXPAND, nullptr},
#endif
  {"OFF_KEEP", VAL_INT, 0, LDS_MKEEP, nullptr},
#ifndef COVERMOUNT
  {"OFF_CLEAR", VAL_INT, 0, LDS_MCLEAR, nullptr},
#endif
  {"BETWEEN", VAL_INT, 0, LDS_BETWEEN, nullptr},
  // droid types
  {"DROID_WEAPON", VAL_INT, 0, DROID_WEAPON, nullptr}, {"DROID_SENSOR", VAL_INT, 0, DROID_SENSOR, nullptr},
  {"DROID_ECM", VAL_INT, 0, DROID_ECM, nullptr}, {"DROID_CONSTRUCT", VAL_INT, 0, DROID_CONSTRUCT, nullptr},
  {"DROID_PERSON", VAL_INT, 0, DROID_PERSON, nullptr}, {"DROID_CYBORG", VAL_INT, 0, DROID_CYBORG, nullptr},
  {"DROID_TRANSPORTER", VAL_INT, 0, DROID_TRANSPORTER, nullptr}, {"DROID_COMMAND", VAL_INT, 0, DROID_COMMAND, nullptr},
  {"DROID_REPAIR", VAL_INT, 0, DROID_REPAIR, nullptr},

  // structure types
  {"REF_HQ", VAL_INT, 0, REF_HQ, nullptr}, {"REF_FACTORY", VAL_INT, 0, REF_FACTORY, nullptr},
  {"REF_FACTORY_MODULE", VAL_INT, 0, REF_FACTORY_MODULE, nullptr}, {"REF_POWER_GEN", VAL_INT, 0, REF_POWER_GEN, nullptr},
  {"REF_POWER_MODULE", VAL_INT, 0, REF_POWER_MODULE, nullptr}, {"REF_RESOURCE_EXTRACTOR", VAL_INT, 0, REF_RESOURCE_EXTRACTOR, nullptr},
  {"REF_DEFENSE", VAL_INT, 0, REF_DEFENSE, nullptr}, {"REF_WALL", VAL_INT, 0, REF_WALL, nullptr},
  {"REF_WALLCORNER", VAL_INT, 0, REF_WALLCORNER, nullptr}, {"REF_RESEARCH", VAL_INT, 0, REF_RESEARCH, nullptr},
  {"REF_RESEARCH_MODULE", VAL_INT, 0, REF_RESEARCH_MODULE, nullptr}, {"REF_REPAIR_FACILITY", VAL_INT, 0, REF_REPAIR_FACILITY, nullptr},
  {"REF_COMMAND_CONTROL", VAL_INT, 0, REF_COMMAND_CONTROL, nullptr}, {"REF_CYBORG_FACTORY", VAL_INT, 0, REF_CYBORG_FACTORY, nullptr},
  {"REF_VTOL_FACTORY", VAL_INT, 0, REF_VTOL_FACTORY, nullptr}, {"REF_REARM_PAD", VAL_INT, 0, REF_REARM_PAD, nullptr},
  {"REF_MISSILE_SILO", VAL_INT, 0, REF_MISSILE_SILO, nullptr},

  // primary orders
  {"DORDER_NONE", VAL_INT, 0, DORDER_NONE, nullptr}, {"DORDER_STOP", VAL_INT, 0, DORDER_STOP, nullptr},
  {"DORDER_MOVE", VAL_INT, 0, DORDER_MOVE, nullptr}, {"DORDER_ATTACK", VAL_INT, 0, DORDER_ATTACK, nullptr},
  {"DORDER_BUILD", VAL_INT, 0, DORDER_BUILD, nullptr}, {"DORDER_HELPBUILD", VAL_INT, 0, DORDER_HELPBUILD, nullptr},
  {"DORDER_LINEBUILD", VAL_INT, 0, DORDER_LINEBUILD, nullptr}, {"DORDER_DEMOLISH", VAL_INT, 0, DORDER_DEMOLISH, nullptr},
  {"DORDER_REPAIR", VAL_INT, 0, DORDER_REPAIR, nullptr}, {"DORDER_OBSERVE", VAL_INT, 0, DORDER_OBSERVE, nullptr},
  {"DORDER_FIRESUPPORT", VAL_INT, 0, DORDER_FIRESUPPORT, nullptr}, {"DORDER_RETREAT", VAL_INT, 0, DORDER_RETREAT, nullptr},
  {"DORDER_DESTRUCT", VAL_INT, 0, DORDER_DESTRUCT, nullptr}, {"DORDER_RTB", VAL_INT, 0, DORDER_RTB, nullptr},
  {"DORDER_RTR", VAL_INT, 0, DORDER_RTR, nullptr}, {"DORDER_RUN", VAL_INT, 0, DORDER_RUN, nullptr},
  {"DORDER_EMBARK", VAL_INT, 0, DORDER_EMBARK, nullptr}, {"DORDER_DISEMBARK", VAL_INT, 0, DORDER_DISEMBARK, nullptr},
  {"DORDER_SCOUT", VAL_INT, 0, DORDER_SCOUT, nullptr},

  // secondary orders
  {"DSO_ATTACK_RANGE", VAL_INT, 0, DSO_ATTACK_RANGE, nullptr}, {"DSO_REPAIR_LEVEL", VAL_INT, 0, DSO_REPAIR_LEVEL, nullptr},
  {"DSO_ATTACK_LEVEL", VAL_INT, 0, DSO_ATTACK_LEVEL, nullptr}, {"DSO_RECYCLE", VAL_INT, 0, DSO_RECYCLE, nullptr},
  {"DSO_PATROL", VAL_INT, 0, DSO_PATROL, nullptr}, {"DSO_HALTTYPE", VAL_INT, 0, DSO_HALTTYPE, nullptr},
  {"DSO_RETURN_TO_LOC", VAL_INT, 0, DSO_RETURN_TO_LOC, nullptr},

  // secondary order stats
  {"DSS_ARANGE_SHORT", VAL_INT, 0, DSS_ARANGE_SHORT, nullptr}, {"DSS_ARANGE_LONG", VAL_INT, 0, DSS_ARANGE_LONG, nullptr},
  {"DSS_ARANGE_DEFAULT", VAL_INT, 0, DSS_ARANGE_DEFAULT, nullptr}, {"DSS_REPLEV_LOW", VAL_INT, 0, DSS_REPLEV_LOW, nullptr},
  {"DSS_REPLEV_HIGH", VAL_INT, 0, DSS_REPLEV_HIGH, nullptr}, {"DSS_REPLEV_NEVER", VAL_INT, 0, DSS_REPLEV_NEVER, nullptr},
  {"DSS_ALEV_ALWAYS", VAL_INT, 0, DSS_ALEV_ALWAYS, nullptr}, {"DSS_ALEV_ATTACKED", VAL_INT, 0, DSS_ALEV_ATTACKED, nullptr},
  {"DSS_ALEV_NEVER", VAL_INT, 0, DSS_ALEV_NEVER, nullptr}, {"DSS_HALT_HOLD", VAL_INT, 0, DSS_HALT_HOLD, nullptr},
  {"DSS_HALT_GUARD", VAL_INT, 0, DSS_HALT_GUARD, nullptr}, {"DSS_HALT_PERSUE", VAL_INT, 0, DSS_HALT_PERSUE, nullptr},
  {"DSS_RECYCLE_SET", VAL_INT, 0, DSS_RECYCLE_SET, nullptr}, {"DSS_ASSPROD_START", VAL_INT, 0, DSS_ASSPROD_START, nullptr},
  {"DSS_ASSPROD_END ", VAL_INT, 0, DSS_ASSPROD_END, nullptr}, {"DSS_RTL_REPAIR", VAL_INT, 0, DSS_RTL_REPAIR, nullptr},
  {"DSS_RTL_BASE", VAL_INT, 0, DSS_RTL_BASE, nullptr}, {"DSS_RTL_TRANSPORT", VAL_INT, 0, DSS_RTL_TRANSPORT, nullptr},
  {"DSS_PATROL_SET", VAL_INT, 0, DSS_PATROL_SET, nullptr},

  // button id's
  {"IDRET_OPTIONS", VAL_INT, 0, IDRET_OPTIONS, nullptr}, {"IDRET_BUILD", VAL_INT, 0, IDRET_BUILD, nullptr},
  {"IDRET_MANUFACTURE", VAL_INT, 0, IDRET_MANUFACTURE, nullptr}, {"IDRET_RESEARCH", VAL_INT, 0, IDRET_RESEARCH, nullptr},
  {"IDRET_INTELMAP", VAL_INT, 0, IDRET_INTEL_MAP, nullptr}, {"IDRET_DESIGN", VAL_INT, 0, IDRET_DESIGN, nullptr},
  {"IDRET_CANCEL", VAL_INT, 0, IDRET_CANCEL, nullptr}, {"IDRET_COMMAND", VAL_INT, 0, IDRET_COMMAND, nullptr},

  // Cursor types
  {"IMAGE_CURSOR_SELECT", VAL_INT, 0, IMAGE_CURSOR_SELECT, nullptr}, {"IMAGE_CURSOR_ATTACK", VAL_INT, 0, IMAGE_CURSOR_ATTACK, nullptr},
  {"IMAGE_CURSOR_MOVE", VAL_INT, 0, IMAGE_CURSOR_MOVE, nullptr}, {"IMAGE_CURSOR_ECM", VAL_INT, 0, IMAGE_CURSOR_ECM, nullptr},
  {"IMAGE_CURSOR_REPAIR", VAL_INT, 0, IMAGE_CURSOR_REPAIR, nullptr}, {"IMAGE_CURSOR_PICKUP", VAL_INT, 0, IMAGE_CURSOR_PICKUP, nullptr},
  {"IMAGE_CURSOR_DEFAULT", VAL_INT, 0, IMAGE_CURSOR_DEFAULT, nullptr}, {"IMAGE_CURSOR_BUILD", VAL_INT, 0, IMAGE_CURSOR_BUILD, nullptr},
  {"IMAGE_CURSOR_GUARD", VAL_INT, 0, IMAGE_CURSOR_GUARD, nullptr}, {"IMAGE_CURSOR_BRIDGE", VAL_INT, 0, IMAGE_CURSOR_BRIDGE, nullptr},
  {"IMAGE_CURSOR_ATTACH", VAL_INT, 0, IMAGE_CURSOR_ATTACH, nullptr}, {"IMAGE_CURSOR_LOCKON", VAL_INT, 0, IMAGE_CURSOR_LOCKON, nullptr},
  {"IMAGE_CURSOR_FIX", VAL_INT, 0, IMAGE_CURSOR_FIX, nullptr}, {"IMAGE_CURSOR_EMBARK", VAL_INT, 0, IMAGE_CURSOR_EMBARK, nullptr},
  {"INT_NORMAL", VAL_INT, 0, INT_NORMAL, nullptr}, // Standard mode (just the reticule)
  {"INT_OPTION", VAL_INT, 0, INT_OPTION, nullptr}, // Option screen
  {"INT_EDITSTAT", VAL_INT, 0, INT_EDITSTAT, nullptr}, // Stat screen up for placing objects
  {"INT_EDIT", VAL_INT, 0, INT_EDIT, nullptr}, // Edit mode
  {"INT_OBJECT", VAL_INT, 0, INT_OBJECT, nullptr}, // Object screen
  {"INT_STAT", VAL_INT, 0, INT_STAT, nullptr}, // Object screen with stat screen
  {"INT_CMDORDER", VAL_INT, 0, INT_CMDORDER, nullptr}, // Object screen with command droids and orders screen
  {"INT_DESIGN", VAL_INT, 0, INT_DESIGN, nullptr}, // Design screen
  {"INT_INTELMAP", VAL_INT, 0, INT_INTELMAP, nullptr}, // Intelligence Map
  {"INT_ORDER", VAL_INT, 0, INT_ORDER, nullptr}, {"INT_INGAMEOP", VAL_INT, 0, INT_INGAMEOP, nullptr}, // in game options.
  {"INT_TRANSPORTER", VAL_INT, 0, INT_TRANSPORTER, nullptr}, //Loading/unloading a Transporter
  {"INT_MISSIONRES", VAL_INT, 0, INT_MISSIONRES, nullptr}, // Results of a mission display.
  {"INT_MULTIMENU", VAL_INT, 0, INT_MULTIMENU, nullptr}, // multiplayer only, player stats etc...

  // parameters for getGameStatus
  {"STATUS_ReticuleIsOpen", VAL_INT, 0, STATUS_ReticuleIsOpen, nullptr}, // is the reticule current being displayed (TRUE=yes)
  {"STATUS_BattleMapViewEnabled", VAL_INT, 0, STATUS_BattleMapViewEnabled, nullptr},
  // Are we currently in the battlemap view (tactical display) true=yes
  {"STATUS_DeliveryReposInProgress", VAL_INT, 0, STATUS_DeliveryReposInProgress, nullptr}, // Are we currently in the delivery repos mode

  //possible values for externed 	targetedObjectType
  {"MT_TERRAIN", VAL_INT, 0, MT_TERRAIN, nullptr}, {"MT_RESOURCE", VAL_INT, 0, MT_RESOURCE, nullptr},
  {"MT_BLOCKING", VAL_INT, 0, MT_BLOCKING, nullptr}, {"MT_RIVER", VAL_INT, 0, MT_RIVER, nullptr},
  {"MT_TRENCH", VAL_INT, 0, MT_TRENCH, nullptr}, {"MT_OWNSTRDAM", VAL_INT, 0, MT_OWNSTRDAM, nullptr},
  {"MT_OWNSTROK", VAL_INT, 0, MT_OWNSTROK, nullptr}, {"MT_OWNSTRINCOMP", VAL_INT, 0, MT_OWNSTRINCOMP, nullptr},
  {"MT_REPAIR", VAL_INT, 0, MT_REPAIR, nullptr}, {"MT_REPAIRDAM", VAL_INT, 0, MT_REPAIRDAM, nullptr},
  {"MT_ENEMYSTR", VAL_INT, 0, MT_ENEMYSTR, nullptr}, {"MT_TRANDROID", VAL_INT, 0, MT_TRANDROID, nullptr},
  {"MT_OWNDROID", VAL_INT, 0, MT_OWNDROID, nullptr}, {"MT_OWNDROIDDAM", VAL_INT, 0, MT_OWNDROIDDAM, nullptr},
  {"MT_ENEMYDROID", VAL_INT, 0, MT_ENEMYDROID, nullptr}, {"MT_COMMAND", VAL_INT, 0, MT_COMMAND, nullptr},
  {"MT_ARTIFACT", VAL_INT, 0, MT_ARTIFACT, nullptr}, {"MT_DAMFEATURE", VAL_INT, 0, MT_DAMFEATURE, nullptr},
  {"MT_SENSOR", VAL_INT, 0, MT_SENSOR, nullptr},

  // structure target types
  {"ST_HQ", VAL_INT, 0, SCR_ST_HQ, nullptr}, {"ST_FACTORY", VAL_INT, 0, SCR_ST_FACTORY, nullptr},
  {"ST_POWER_GEN", VAL_INT, 0, SCR_ST_POWER_GEN, nullptr}, {"ST_RESOURCE_EXTRACTOR", VAL_INT, 0, SCR_ST_RESOURCE_EXTRACTOR, nullptr},
  {"ST_WALL", VAL_INT, 0, SCR_ST_WALL, nullptr}, {"ST_RESEARCH", VAL_INT, 0, SCR_ST_RESEARCH, nullptr},
  {"ST_REPAIR_FACILITY", VAL_INT, 0, SCR_ST_REPAIR_FACILITY, nullptr}, {"ST_COMMAND_CONTROL", VAL_INT, 0, SCR_ST_COMMAND_CONTROL, nullptr},
  {"ST_CYBORG_FACTORY", VAL_INT, 0, SCR_ST_CYBORG_FACTORY, nullptr}, {"ST_VTOL_FACTORY", VAL_INT, 0, SCR_ST_VTOL_FACTORY, nullptr},
  {"ST_REARM_PAD", VAL_INT, 0, SCR_ST_REARM_PAD, nullptr}, {"ST_SENSOR", VAL_INT, 0, SCR_ST_SENSOR, nullptr},
  {"ST_DEF_GROUND", VAL_INT, 0, SCR_ST_DEF_GROUND, nullptr}, {"ST_DEF_AIR", VAL_INT, 0, SCR_ST_DEF_AIR, nullptr},
  {"ST_DEF_IDF", VAL_INT, 0, SCR_ST_DEF_IDF, nullptr}, {"ST_DEF_ALL", VAL_INT, 0, SCR_ST_DEF_ALL, nullptr},

  // droid target types
  {"DT_COMMAND", VAL_INT, 0, SCR_DT_COMMAND, nullptr}, {"DT_SENSOR", VAL_INT, 0, SCR_DT_SENSOR, nullptr},
  {"DT_CONSTRUCT", VAL_INT, 0, SCR_DT_CONSTRUCT, nullptr}, {"DT_REPAIR", VAL_INT, 0, SCR_DT_REPAIR, nullptr},
  {"DT_WEAP_GROUND", VAL_INT, 0, SCR_DT_WEAP_GROUND, nullptr}, {"DT_WEAP_AIR", VAL_INT, 0, SCR_DT_WEAP_AIR, nullptr},
  {"DT_WEAP_IDF", VAL_INT, 0, SCR_DT_WEAP_IDF, nullptr}, {"DT_WEAP_ALL", VAL_INT, 0, SCR_DT_WEAP_ALL, nullptr},
  {"DT_LIGHT", VAL_INT, 0, SCR_DT_LIGHT, nullptr}, {"DT_MEDIUM", VAL_INT, 0, SCR_DT_MEDIUM, nullptr},
  {"DT_HEAVY", VAL_INT, 0, SCR_DT_HEAVY, nullptr}, {"DT_SUPER_HEAVY", VAL_INT, 0, SCR_DT_SUPER_HEAVY, nullptr},
  {"DT_TRACK", VAL_INT, 0, SCR_DT_TRACK, nullptr}, {"DT_HTRACK", VAL_INT, 0, SCR_DT_HTRACK, nullptr},
  {"DT_WHEEL", VAL_INT, 0, SCR_DT_WHEEL, nullptr}, {"DT_LEGS", VAL_INT, 0, SCR_DT_LEGS, nullptr},
  {"DT_GROUND", VAL_INT, 0, SCR_DT_GROUND, nullptr}, {"DT_VTOL", VAL_INT, 0, SCR_DT_VTOL, nullptr},
  {"DT_HOVER", VAL_INT, 0, SCR_DT_HOVER, nullptr},

  // multiplayer
  //	{ "DMATCH",				VAL_INT,	0,		DMATCH,					0 },
  {"CAMPAIGN", VAL_INT, 0, CAMPAIGN, nullptr}, {"TEAMPLAY", VAL_INT, 0, TEAMPLAY, nullptr}, {"SKIRMISH", VAL_INT, 0, SKIRMISH, nullptr},
  {"CAMP_CLEAN", VAL_INT, 0, CAMP_CLEAN, nullptr}, {"CAMP_BASE", VAL_INT, 0, CAMP_BASE, nullptr},
  {"CAMP_WALLS", VAL_INT, 0, CAMP_WALLS, nullptr},

  /* This entry marks the end of the constant list */
  {"CONSTANT LIST END", VAL_VOID}
};
#endif

/* The Table of callback triggers
 * The format is :
 *
 * "callback name", <callback id>
 *
 * The callback id should be a unique id number starting at TR_CALLBACKSTART
 * and increasing sequentially by 1
 */
CALLBACK_SYMBOL asCallbackTable[] = {
  {"CALL_GAMEINIT", CALL_GAMEINIT, nullptr, 0}, {"CALL_DELIVPOINTMOVED", CALL_DELIVPOINTMOVED, nullptr, 0},
  {"CALL_DROIDDESIGNED", CALL_DROIDDESIGNED, nullptr, 0},
  //{ "CALL_RESEARCHCOMPLETED",		CALL_RESEARCHCOMPLETED,		NULL, 0 }, - callback function added
  {"CALL_DROIDBUILT", CALL_DROIDBUILT, nullptr, 0}, {"CALL_POWERGEN_BUILT", CALL_POWERGEN_BUILT, nullptr, 0},
  {"CALL_RESEX_BUILT", CALL_RESEX_BUILT, nullptr, 0}, {"CALL_RESEARCH_BUILT", CALL_RESEARCH_BUILT, nullptr, 0},
  {"CALL_FACTORY_BUILT", CALL_FACTORY_BUILT, nullptr, 0}, {"CALL_MISSION_START", CALL_MISSION_START, nullptr, 0},
  {"CALL_MISSION_END", CALL_MISSION_END, nullptr, 0}, {"CALL_VIDEO_QUIT", CALL_VIDEO_QUIT, nullptr, 0},
  {"CALL_LAUNCH_TRANSPORTER", CALL_LAUNCH_TRANSPORTER, nullptr, 0}, {"CALL_START_NEXT_LEVEL", CALL_START_NEXT_LEVEL, nullptr, 0},
  {"CALL_TRANSPORTER_REINFORCE", CALL_TRANSPORTER_REINFORCE, nullptr, 0}, {"CALL_MISSION_TIME", CALL_MISSION_TIME, nullptr, 0},
  {"CALL_ELECTRONIC_TAKEOVER", CALL_ELECTRONIC_TAKEOVER, nullptr, 0},

  // tutorial only callbacks
  {"CALL_BUILDLIST", CALL_BUILDLIST, nullptr, 0}, {"CALL_BUILDGRID", CALL_BUILDGRID, nullptr, 0},
  {"CALL_RESEARCHLIST", CALL_RESEARCHLIST, nullptr, 0}, {"CALL_MANURUN", CALL_MANURUN, nullptr, 0},
  {"CALL_MANULIST", CALL_MANULIST, nullptr, 0}, {"CALL_BUTTON_PRESSED", CALL_BUTTON_PRESSED, scrCBButtonPressed, 1, {VAL_INT}},
  {"CALL_DROID_SELECTED", CALL_DROID_SELECTED, scrCBDroidSelected, 1, {VAL_REF | ST_DROID}},
  {"CALL_DESIGN_QUIT", CALL_DESIGN_QUIT, nullptr, 0}, {"CALL_DESIGN_WEAPON", CALL_DESIGN_WEAPON, nullptr, 0},
  {"CALL_DESIGN_SYSTEM", CALL_DESIGN_SYSTEM, nullptr, 0}, {"CALL_DESIGN_COMMAND", CALL_DESIGN_COMMAND, nullptr, 0},
  {"CALL_DESIGN_BODY", CALL_DESIGN_BODY, nullptr, 0}, {"CALL_DESIGN_PROPULSION", CALL_DESIGN_PROPULSION, nullptr, 0},

  // callback triggers with parameters
  {"CALL_RESEARCHCOMPLETED", CALL_RESEARCHCOMPLETED, scrCBResCompleted, 1, {VAL_REF | ST_RESEARCH}},
  {"CALL_NEWDROID", CALL_NEWDROID, scrCBNewDroid, 3, {VAL_INT, VAL_REF | ST_DROID,VAL_REF | ST_STRUCTURE}},
  {"CALL_STRUCT_ATTACKED", CALL_STRUCT_ATTACKED, scrCBStructAttacked, 3, {VAL_INT, VAL_REF | ST_STRUCTURE, VAL_REF | ST_BASEOBJECT}},
  {"CALL_DROID_ATTACKED", CALL_DROID_ATTACKED, scrCBDroidAttacked, 3, {VAL_INT, VAL_REF | ST_DROID, VAL_REF | ST_BASEOBJECT}},
  {"CALL_ATTACKED", CALL_ATTACKED, scrCBAttacked, 3, {VAL_INT, VAL_REF | ST_BASEOBJECT, VAL_REF | ST_BASEOBJECT}},
  {"CALL_STRUCT_SEEN", CALL_STRUCT_SEEN, scrCBStructSeen, 3, {VAL_INT, VAL_REF | ST_STRUCTURE, VAL_REF | ST_BASEOBJECT}},
  {"CALL_DROID_SEEN", CALL_DROID_SEEN, scrCBDroidSeen, 3, {VAL_INT, VAL_REF | ST_DROID, VAL_REF | ST_BASEOBJECT}},
  {"CALL_FEATURE_SEEN", CALL_FEATURE_SEEN, scrCBFeatureSeen, 3, {VAL_INT, VAL_REF | ST_FEATURE, VAL_REF | ST_BASEOBJECT}},
  {"CALL_OBJ_SEEN", CALL_OBJ_SEEN, scrCBObjSeen, 3, {VAL_INT, VAL_REF | ST_BASEOBJECT, VAL_REF | ST_BASEOBJECT}},
  {"CALL_OBJ_DESTROYED", CALL_OBJ_DESTROYED, scrCBObjDestroyed, 2, {VAL_INT, VAL_REF | ST_BASEOBJECT}},
  {"CALL_STRUCT_DESTROYED", CALL_STRUCT_DESTROYED, scrCBStructDestroyed, 2, {VAL_INT, VAL_REF | ST_STRUCTURE}},
  {"CALL_DROID_DESTROYED", CALL_DROID_DESTROYED, scrCBDroidDestroyed, 2, {VAL_INT, VAL_REF | ST_DROID}},
  {"CALL_FEATURE_DESTROYED", CALL_FEATURE_DESTROYED, scrCBFeatureDestroyed, 1, {VAL_REF | ST_FEATURE}},
  {"CALL_OBJECTOPEN", CALL_OBJECTOPEN, nullptr, 0}, {"CALL_OBJECTCLOSE", CALL_OBJECTCLOSE, nullptr, 0},
  {"CALL_TRANSPORTER_OFFMAP", CALL_TRANSPORTER_OFFMAP, scrCBTransporterOffMap, 1, {VAL_INT}},
  {"CALL_TRANSPORTER_LANDED", CALL_TRANSPORTER_LANDED, scrCBTransporterLanded, 2, {ST_GROUP, VAL_INT}},
  {"CALL_ALL_ONSCREEN_DROIDS_SELECTED", CALL_ALL_ONSCREEN_DROIDS_SELECTED, nullptr, 0},
  {"CALL_NO_REINFORCEMENTS_LEFT", CALL_NO_REINFORCEMENTS_LEFT, nullptr, 0},
  {"CALL_CLUSTER_EMPTY", CALL_CLUSTER_EMPTY, scrCBClusterEmpty, 1, {VAL_REF | VAL_INT}},
  {"CALL_VTOL_OFF_MAP", CALL_VTOL_OFF_MAP, scrCBVtolOffMap, 2, {VAL_INT, VAL_REF | ST_DROID}},
  {"CALL_UNITTAKEOVER", CALL_UNITTAKEOVER, scrCBDroidTaken, 1, {VAL_REF | ST_DROID}},
  {"CALL_PLAYERLEFT", CALL_PLAYERLEFT, scrCBPlayerLeft, 1, {VAL_INT}},
  {"CALL_ALLIANCEOFFER", CALL_ALLIANCEOFFER, scrCBAllianceOffer, 2, {VAL_REF | VAL_INT,VAL_REF | VAL_INT}},

  /* This entry marks the end of the callback list */
  {"CALLBACK LIST END", 0}
};

/* The table of type equivalence
 * The format is :
 *
 *       <base type>  <num equivalents>  <eqivalent types>
 *
 */
TYPE_EQUIV asEquivTable[] = {
  {ST_BASEOBJECT, 3, {ST_DROID, ST_STRUCTURE, ST_FEATURE,}},
  {ST_COMPONENT, 8, {ST_BODY, ST_PROPULSION, ST_ECM, ST_SENSOR, ST_CONSTRUCT, ST_WEAPON, ST_REPAIR, ST_BRAIN}},
  {ST_BASESTATS, 2, {ST_STRUCTURESTAT, ST_FEATURESTAT}}, {ST_DROID, 1, {ST_POINTER_O,}}, {ST_STRUCTURE, 1, {ST_POINTER_O,}},
  {ST_FEATURE, 1, {ST_POINTER_O,}}, {ST_BASEOBJECT, 1, {ST_POINTER_O,}}, {ST_TEMPLATE, 1, {ST_POINTER_T,}}, {ST_BODY, 1, {ST_POINTER_S,}},
  {ST_PROPULSION, 1, {ST_POINTER_S,}}, {ST_WEAPON, 1, {ST_POINTER_S,}}, {ST_ECM, 1, {ST_POINTER_S,}}, {ST_SENSOR, 1, {ST_POINTER_S,}},
  {ST_CONSTRUCT, 1, {ST_POINTER_S,}}, {ST_REPAIR, 1, {ST_POINTER_S,}}, {ST_BRAIN, 1, {ST_POINTER_S,}},

  /* This marks the end of the equivalence list */
  {0, 0}
};

// Initialise the script system
BOOL scrTabInitialise(void)
{
  EVENT_INIT sInit;

  sInit.valInit = 50;
  sInit.valExt = 5;
  sInit.trigInit = 35; // was 20 ... not enough
  sInit.trigExt = 5;
  sInit.contInit = 50;
  sInit.contExt = 5;
  if (!scriptInitialise(&sInit))
    return FALSE;

  if (!eventInitValueFuncs(ST_MAXTYPE))
    return FALSE;

  scrvInitialise();
#ifndef NOSCRIPT

  // Set the constant table
  scriptSetConstTab(asConstantTable);
#endif
  // Set the function table
  scriptSetFuncTab(asFuncTable);

  // Set the type table
  scriptSetTypeTab(asTypeTable);

  // Set the external variable table
  scriptSetExternalTab(asExternTable);

  // Set the object variable table
  scriptSetObjectTab(asObjTable);

  // Set the callback table
  scriptSetCallbackTab(asCallbackTable);

  // Set the type equivalence table
  scriptSetTypeEquiv(asEquivTable);

  // Set the create and release functions
  if (!eventAddValueCreate(ST_BASEOBJECT, scrvAddBasePointer))
    return FALSE;
  if (!eventAddValueRelease(ST_BASEOBJECT, scrvReleaseBasePointer))
    return FALSE;

  if (!eventAddValueCreate(ST_DROID, scrvAddBasePointer))
    return FALSE;
  if (!eventAddValueRelease(ST_DROID, scrvReleaseBasePointer))
    return FALSE;

  if (!eventAddValueCreate(ST_STRUCTURE, scrvAddBasePointer))
    return FALSE;
  if (!eventAddValueRelease(ST_STRUCTURE, scrvReleaseBasePointer))
    return FALSE;

  if (!eventAddValueCreate(ST_FEATURE, scrvAddBasePointer))
    return FALSE;
  if (!eventAddValueRelease(ST_FEATURE, scrvReleaseBasePointer))
    return FALSE;

  if (!eventAddValueCreate(ST_GROUP, scrvNewGroup))
    return FALSE;
  if (!eventAddValueRelease(ST_GROUP, scrvReleaseGroup))
    return FALSE;

  // initialise various variables
  scrGameLevel = 0;
  bInTutorial = FALSE;

  return TRUE;
}

// Shut down the script system
void scrShutDown(void)
{
  scrvShutDown();
  scriptShutDown();
}
