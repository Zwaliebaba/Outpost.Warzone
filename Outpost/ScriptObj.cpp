#include "pch.h"
/*
 * ScriptObj.c
 *
 * Object access functions for the script library
 *
 */

#include "Frame.h"
#include "Objects.h"

#include "Script.h"
#include "ScriptTabs.h"
#include "ScriptObj.h"
#include "Group.h"
#include "GTime.h"
#include "Cluster.h"
#include "MessageDef.h"
#include "Message.h"
#include "ResearchDef.h"
#include "AudioSystem.h"
#include "MultiPlay.h"
#include "Text.h"
#include "Levels.h"
#include "ScriptVals.h"
#include "Research.h"

// Get values from a base object
BOOL scrBaseObjGet(UDWORD index)
{
  INTERP_TYPE type;
  BASE_OBJECT* psObj;
  SDWORD val;
  DROID* psDroid;
  STRUCTURE* psStruct;
  FEATURE* psFeature;

  if (!stackPopParams({{ST_BASEOBJECT, &psObj}}))
    return FALSE;

  // Check this is a valid pointer
  if (psObj == nullptr)
  {
    DEBUG_ASSERT_TEXT(FALSE, "scrBaseObjGet: was passed an invalid pointer");
    return FALSE;
  }
  // Check this is a valid pointer
  if (psObj->type != OBJ_DROID && psObj->type != OBJ_STRUCTURE && psObj->type != OBJ_FEATURE)
  {
    DEBUG_ASSERT_TEXT(FALSE, "scrBaseObjGet: invalid object");
    return FALSE;
  }

  // set the type and return value
  switch (index)
  {
  case OBJID_POSX:
    type = VAL_INT;
    val = static_cast<SDWORD>(psObj->x);
    break;
  case OBJID_POSY:
    type = VAL_INT;
    val = static_cast<SDWORD>(psObj->y);
    break;
  case OBJID_POSZ:
    type = VAL_INT;
    val = static_cast<SDWORD>(psObj->z);
    break;
  case OBJID_ID:
    type = VAL_INT;
    val = static_cast<SDWORD>(psObj->id);
    break;
  case OBJID_PLAYER:
    type = VAL_INT;
    val = static_cast<SDWORD>(psObj->player);
    break;
  case OBJID_TYPE:
    type = VAL_INT;
    val = static_cast<SDWORD>(psObj->type);
    break;
  case OBJID_ORDER:
    if (psObj->type != OBJ_DROID)
    {
      DEBUG_ASSERT_TEXT(FALSE, "scrBaseObjGet: order only valid for a droid");
      return FALSE;
    }
    type = VAL_INT;
    val = ((DROID*)psObj)->order;
    if ((val == DORDER_GUARD) && (((DROID*)psObj)->psTarget == nullptr))
      val = DORDER_NONE;
    break;
  case OBJID_ORDERX:
    if (psObj->type != OBJ_DROID)
    {
      DEBUG_ASSERT_TEXT(FALSE, "scrBaseObjGet: order only valid for a droid");
      return FALSE;
    }
    type = VAL_INT;
    val = static_cast<SDWORD>(((DROID*)psObj)->orderX);
    break;
  case OBJID_ORDERY:
    if (psObj->type != OBJ_DROID)
    {
      DEBUG_ASSERT_TEXT(FALSE, "scrBaseObjGet: order only valid for a droid");
      return FALSE;
    }
    type = VAL_INT;
    val = static_cast<SDWORD>(((DROID*)psObj)->orderY);
    break;
  case OBJID_DROIDTYPE:
    if (psObj->type != OBJ_DROID)
    {
      DEBUG_ASSERT_TEXT(FALSE, "scrBaseObjGet: droidType only valid for a droid");
      return FALSE;
    }
    type = VAL_INT;
    val = static_cast<SDWORD>(((DROID*)psObj)->droidType);
    break;
  case OBJID_CLUSTERID:
    if (psObj->type == OBJ_FEATURE)
    {
      DEBUG_ASSERT_TEXT(FALSE, "scrBaseObjGet: clusterID not valid for features");
      return FALSE;
    }
    type = VAL_INT;
    val = clustGetClusterID(psObj);
    break;
  case OBJID_HEALTH:
    switch (psObj->type)
    {
    case OBJ_DROID:
      psDroid = (DROID*)psObj;
      type = VAL_INT;
      val = psDroid->body * 100 / psDroid->originalBody;
      break;
    case OBJ_FEATURE:
      psFeature = (FEATURE*)psObj;
      type = VAL_INT;
      if (psFeature->psStats->damageable)
        val = psFeature->body * 100 / psFeature->psStats->body;
      else
        val = 100;
      break;
    case OBJ_STRUCTURE:
      psStruct = (STRUCTURE*)psObj;
      type = VAL_INT;
      val = psStruct->body * 100 / structureBody(psStruct);
      break;
    }
    break;
  case OBJID_BODY:
    if (psObj->type != OBJ_DROID)
    {
      DEBUG_ASSERT_TEXT(FALSE, "scrBaseObjGet: body only valid for a droid");
      return FALSE;
    }
    type = ST_BODY;
    val = ((DROID*)psObj)->asBits[COMP_BODY].nStat;
    break;
  case OBJID_PROPULSION:
    if (psObj->type != OBJ_DROID)
    {
      DEBUG_ASSERT_TEXT(FALSE, "scrBaseObjGet: propulsion only valid for a droid");
      return FALSE;
    }
    type = ST_PROPULSION;
    val = ((DROID*)psObj)->asBits[COMP_PROPULSION].nStat;
    break;
  case OBJID_WEAPON:
    if (psObj->type != OBJ_DROID)
    {
      DEBUG_ASSERT_TEXT(FALSE, "scrBaseObjGet: weapon only valid for a droid");
      return FALSE;
    }
    type = ST_WEAPON;
    //if (((DROID *)psObj)->numWeaps == 0)
    if (((DROID*)psObj)->asWeaps[0].nStat == 0)
      val = 0;
    else
      val = ((DROID*)psObj)->asWeaps[0].nStat;
    break;
  case OBJID_STRUCTSTAT:
    if (psObj->type != OBJ_STRUCTURE)
    {
      DEBUG_ASSERT_TEXT(FALSE, "scrBaseObjGet: stat only valid for a structure");
      return FALSE;
    }
    type = ST_STRUCTURESTAT;
    val = ((STRUCTURE*)psObj)->pStructureType - asStructureStats;
    break;
  default: DEBUG_ASSERT_TEXT(FALSE, "scrBaseObjGet: unknown variable index");
    return FALSE;
    break;
  }

  // Return the value
  if (!stackPushResult(type, val))
    return FALSE;

  return TRUE;
}

// Set values from a base object
BOOL scrBaseObjSet(UDWORD index)
{
  index = index;

  return TRUE;
}

// convert a base object to a droid if it is the right type
BOOL scrObjToDroid(void)
{
  BASE_OBJECT* psObj;

  if (!stackPopParams({{ST_BASEOBJECT, &psObj}}))
    return FALSE;

  // return NULL if not a droid
  if (psObj->type != OBJ_DROID)
    psObj = nullptr;

  if (!stackPushResult(ST_DROID, psObj))
    return FALSE;

  return TRUE;
}

// convert a base object to a structure if it is the right type
BOOL scrObjToStructure(void)
{
  BASE_OBJECT* psObj;

  if (!stackPopParams({{ST_BASEOBJECT, &psObj}}))
    return FALSE;

  // return NULL if not a droid
  if (psObj->type != OBJ_STRUCTURE)
    psObj = nullptr;

  if (!stackPushResult(ST_STRUCTURE, psObj))
    return FALSE;

  return TRUE;
}

// convert a base object to a feature if it is the right type
BOOL scrObjToFeature(void)
{
  BASE_OBJECT* psObj;

  if (!stackPopParams({{ST_BASEOBJECT, &psObj}}))
    return FALSE;

  // return NULL if not a droid
  if (psObj->type != OBJ_FEATURE)
    psObj = nullptr;

  if (!stackPushResult(ST_FEATURE, psObj))
    return FALSE;

  return TRUE;
}

// cache all the possible values for the last group to try
// to speed up access
static DROID_GROUP* psScrLastGroup;
static SDWORD lgX, lgY, lgMembers, lgHealth;
static UDWORD lgGameTime;

// Get values from a group
BOOL scrGroupObjGet(UDWORD index)
{
  INTERP_TYPE type;
  DROID_GROUP* psGroup;
  SDWORD val;
  DROID* psCurr;

  if (!stackPopParams({{ST_GROUP, &psGroup}}))
    return FALSE;

  // recalculate the values if necessary
  if (lgGameTime != gameTime || psScrLastGroup != psGroup)
  {
    lgGameTime = gameTime;
    psScrLastGroup = psGroup;
    lgMembers = 0;
    lgHealth = 0;
    lgX = lgY = 0;
    for (psCurr = psGroup->psList; psCurr; psCurr = psCurr->psGrpNext)
    {
      lgMembers += 1;
      lgX += static_cast<SDWORD>(psCurr->x);
      lgY += static_cast<SDWORD>(psCurr->y);
      lgHealth += static_cast<SDWORD>((100 * psCurr->body) / psCurr->originalBody);
    }
    if (lgMembers > 0)
    {
      lgX = lgX / lgMembers;
      lgY = lgY / lgMembers;
      lgHealth = lgHealth / lgMembers;
    }
  }

  // set the type and return value
  switch (index)
  {
  case GROUPID_POSX:
    type = VAL_INT;
    val = lgX;
    break;
  case GROUPID_POSY:
    type = VAL_INT;
    val = lgY;
    break;
  case GROUPID_MEMBERS:
    type = VAL_INT;
    val = lgMembers;
    break;
  case GROUPID_HEALTH:
    type = VAL_INT;
    val = lgHealth;
    break;
  default: DEBUG_ASSERT_TEXT(FALSE, "scrGroupObjGet: unknown variable index");
    return FALSE;
    break;
  }

  // Return the value
  if (!stackPushResult(type, val))
    return FALSE;

  return TRUE;
}

// get the name from a stat pointer
STRING* scrGetStatName(INTERP_TYPE type, UDWORD data)
{
  STRING* pName = nullptr;

  switch (type)
  {
  case ST_STRUCTURESTAT:
    if (data < numStructureStats)
      pName = asStructureStats[data].pName;
    break;
  case ST_FEATURESTAT:
    if (data < numFeatureStats)
      pName = asFeatureStats[data].pName;
    break;
  case ST_BODY:
    if (data < numBodyStats)
      pName = asBodyStats[data].pName;
    break;
  case ST_PROPULSION:
    if (data < numPropulsionStats)
      pName = asPropulsionStats[data].pName;
    break;
  case ST_ECM:
    if (data < numECMStats)
      pName = asECMStats[data].pName;
    break;
  case ST_SENSOR:
    if (data < numSensorStats)
      pName = asSensorStats[data].pName;
    break;
  case ST_CONSTRUCT:
    if (data < numConstructStats)
      pName = asConstructStats[data].pName;
    break;
  case ST_WEAPON:
    if (data < numWeaponStats)
      pName = asWeaponStats[data].pName;
    break;
  case ST_REPAIR:
    if (data < numRepairStats)
      pName = asRepairStats[data].pName;
    break;
  case ST_BRAIN:
    if (data < numBrainStats)
      pName = asBrainStats[data].pName;
    break;
  case ST_BASESTATS:
  case ST_COMPONENT:
    // should never have variables of this type
    break;
  }

  if (pName == nullptr)
    Neuron::Fatal("scrGetStatName: cannot get name for a base stat");

  return pName;
}
