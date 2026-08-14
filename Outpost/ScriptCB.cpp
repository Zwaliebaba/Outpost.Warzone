#include "pch.h"
/*
 * ScriptCB.c
 *
 * functions to deal with parameterised script callback triggers.
 *
 */

#include "Frame.h"
#include "Objects.h"
#include "Script.h"
#include "ScriptTabs.h"
#include "ScriptCB.h"
#include "Projectile.h"
#include "HCI.h"
#include "Group.h"
#include "Transporter.h"
#include "Mission.h"
#include "Research.h"

// unit taken over..
DROID* psScrCBDroidTaken;

// The pointer to the droid that was just built for a CALL_NEWDROID
DROID* psScrCBNewDroid;
STRUCTURE* psScrCBNewDroidFact; // id of factory that built it.

// the attacker and target for a CALL_ATTACKED
BASE_OBJECT *psScrCBAttacker, *psScrCBTarget;

// alliance details
UDWORD CBallFrom, CBallTo;

BOOL scrCBDroidTaken(void)
{
  DROID** ppsDroid;
  BOOL triggered = FALSE;

  if (!stackPopParams(1, VAL_REF | ST_DROID, &ppsDroid))
    return FALSE;

  if (psScrCBDroidTaken == nullptr)
  {
    triggered = FALSE;
    *ppsDroid = nullptr;
  }
  else
  {
    triggered = TRUE;
    *ppsDroid = psScrCBDroidTaken;
  }

  psScrCBDroidTaken = nullptr;

  if (!stackPushResult(VAL_BOOL, triggered))
    return FALSE;

  return TRUE;
}

// Deal with a CALL_NEWDROID
BOOL scrCBNewDroid(void)
{
  SDWORD player;
  DROID** ppsDroid;
  STRUCTURE** ppsStructure;
  BOOL triggered = FALSE;

  if (!stackPopParams(3, VAL_INT, &player, VAL_REF | ST_DROID, &ppsDroid, VAL_REF | ST_STRUCTURE, &ppsStructure))
    return FALSE;

  if (psScrCBNewDroid == nullptr)
  {
    // eh? got called without setting the new droid
    DEBUG_ASSERT_TEXT(FALSE, "scrCBNewUnit: no unit has been set");
    triggered = FALSE;
    *ppsDroid = nullptr;
    *ppsStructure = nullptr;
  }
  else if (psScrCBNewDroid->player == static_cast<UDWORD>(player))
  {
    triggered = TRUE;
    *ppsDroid = psScrCBNewDroid;
    *ppsStructure = psScrCBNewDroidFact;
  }

  if (!stackPushResult(VAL_BOOL, triggered))
    return FALSE;

  return TRUE;
}

// Deal with a CALL_STRUCT_ATTACKED
BOOL scrCBStructAttacked(void)
{
  SDWORD player;
  STRUCTURE** ppsTarget;
  BASE_OBJECT** ppsAttacker; //, **ppsTarget;
  BOOL triggered = FALSE;

  if (!stackPopParams(3, VAL_INT, &player, VAL_REF | ST_STRUCTURE, &ppsTarget, VAL_REF | ST_BASEOBJECT, &ppsAttacker))
    return FALSE;

  if (psLastStructHit == nullptr)
  {
    DEBUG_ASSERT_TEXT(FALSE, "scrCBStructAttacked: no target has been set");
    triggered = FALSE;
    *ppsAttacker = nullptr;
    *ppsTarget = nullptr;
  }
  else if (psLastStructHit->player == static_cast<UDWORD>(player))
  {
    triggered = TRUE;
    *ppsAttacker = g_pProjLastAttacker;
    *ppsTarget = psLastStructHit;
  }
  else
  {
    triggered = FALSE;
    *ppsAttacker = nullptr;
    *ppsTarget = nullptr;
  }

  if (!stackPushResult(VAL_BOOL, triggered))
    return FALSE;

  return TRUE;
}

// Deal with a CALL_DROID_ATTACKED
BOOL scrCBDroidAttacked(void)
{
  SDWORD player;
  DROID** ppsTarget;
  BASE_OBJECT** ppsAttacker; //, **ppsTarget;
  BOOL triggered = FALSE;

  if (!stackPopParams(3, VAL_INT, &player, VAL_REF | ST_DROID, &ppsTarget, VAL_REF | ST_BASEOBJECT, &ppsAttacker))
    return FALSE;

  if (psLastDroidHit == nullptr)
  {
    DEBUG_ASSERT_TEXT(FALSE, "scrCBUnitAttacked: no target has been set");
    triggered = FALSE;
    *ppsAttacker = nullptr;
    *ppsTarget = nullptr;
  }
  else if (psLastDroidHit->player == static_cast<UDWORD>(player))
  {
    triggered = TRUE;
    *ppsAttacker = g_pProjLastAttacker;
    *ppsTarget = psLastDroidHit;
  }
  else
  {
    triggered = FALSE;
    *ppsAttacker = nullptr;
    *ppsTarget = nullptr;
  }

  if (!stackPushResult(VAL_BOOL, triggered))
    return FALSE;

  return TRUE;
}

// Deal with a CALL_ATTACKED
BOOL scrCBAttacked(void)
{
  SDWORD player;
  BASE_OBJECT** ppsTarget;
  BASE_OBJECT** ppsAttacker; //, **ppsTarget;
  BOOL triggered = FALSE;

  if (!stackPopParams(3, VAL_INT, &player, VAL_REF | ST_BASEOBJECT, &ppsTarget, VAL_REF | ST_BASEOBJECT, &ppsAttacker))
    return FALSE;

  if (psScrCBTarget == nullptr)
  {
    DEBUG_ASSERT_TEXT(FALSE, "scrCBAttacked: no target has been set");
    triggered = FALSE;
    *ppsAttacker = nullptr;
    *ppsTarget = nullptr;
  }
  else if (psScrCBTarget->player == static_cast<UDWORD>(player))
  {
    triggered = TRUE;
    *ppsAttacker = g_pProjLastAttacker;
    *ppsTarget = psScrCBTarget;
  }
  else
  {
    triggered = FALSE;
    *ppsAttacker = nullptr;
    *ppsTarget = nullptr;
  }

  if (!stackPushResult(VAL_BOOL, triggered))
    return FALSE;

  return TRUE;
}

// The button id

// deal with CALL_BUTTON_PRESSED
BOOL scrCBButtonPressed(void)
{
  UDWORD button;
  BOOL triggered = FALSE;

  if (!stackPopParams(1, VAL_INT, &button))
    return FALSE;

  if (button == intLastWidget)
    triggered = TRUE;

  if (!stackPushResult(VAL_BOOL, triggered))
    return FALSE;

  return TRUE;
}

// the Droid that was selected for a CALL_DROID_SELECTED
DROID* psCBSelectedDroid;

// deal with CALL_DROID_SELECTED
BOOL scrCBDroidSelected(void)
{
  DROID** ppsDroid;

  if (!stackPopParams(1, VAL_REF | ST_DROID, &ppsDroid))
    return FALSE;

  *ppsDroid = psCBSelectedDroid;

  if (!stackPushResult(VAL_BOOL, TRUE))
    return FALSE;

  return TRUE;
}

// the object that was last killed for a CALL_OBJ_DESTROYED
BASE_OBJECT* psCBObjDestroyed;

// deal with a CALL_OBJ_DESTROYED
BOOL scrCBObjDestroyed(void)
{
  SDWORD player;
  BASE_OBJECT** ppsObj;
  BOOL retval;

  if (!stackPopParams(2, VAL_INT, &player, VAL_REF | ST_BASEOBJECT, &ppsObj))
    return FALSE;

  if ((psCBObjDestroyed != nullptr) && (psCBObjDestroyed->player == static_cast<UDWORD>(player)) && (psCBObjDestroyed->type != OBJ_FEATURE))
  {
    retval = TRUE;
    *ppsObj = psCBObjDestroyed;
  }
  else
  {
    retval = FALSE;
    *ppsObj = nullptr;
  }

  if (!stackPushResult(VAL_BOOL, retval))
    return FALSE;

  return TRUE;
}

// deal with a CALL_STRUCT_DESTROYED
BOOL scrCBStructDestroyed(void)
{
  SDWORD player;
  BASE_OBJECT** ppsObj;
  BOOL retval;

  if (!stackPopParams(2, VAL_INT, &player, VAL_REF | ST_STRUCTURE, &ppsObj))
    return FALSE;

  if ((psCBObjDestroyed != nullptr) && (psCBObjDestroyed->player == static_cast<UDWORD>(player)) && (psCBObjDestroyed->type ==
    OBJ_STRUCTURE))
  {
    retval = TRUE;
    *ppsObj = psCBObjDestroyed;
  }
  else
  {
    retval = FALSE;
    *ppsObj = nullptr;
  }

  if (!stackPushResult(VAL_BOOL, retval))
    return FALSE;

  return TRUE;
}

// deal with a CALL_DROID_DESTROYED
BOOL scrCBDroidDestroyed(void)
{
  SDWORD player;
  BASE_OBJECT** ppsObj;
  BOOL retval;

  if (!stackPopParams(2, VAL_INT, &player, VAL_REF | ST_DROID, &ppsObj))
    return FALSE;

  if ((psCBObjDestroyed != nullptr) && (psCBObjDestroyed->player == static_cast<UDWORD>(player)) && (psCBObjDestroyed->type == OBJ_DROID))
  {
    retval = TRUE;
    *ppsObj = psCBObjDestroyed;
  }
  else
  {
    retval = FALSE;
    *ppsObj = nullptr;
  }

  if (!stackPushResult(VAL_BOOL, retval))
    return FALSE;

  return TRUE;
}

// deal with a CALL_FEATURE_DESTROYED
BOOL scrCBFeatureDestroyed(void)
{
  BASE_OBJECT** ppsObj;
  BOOL retval;

  if (!stackPopParams(1, VAL_REF | ST_FEATURE, &ppsObj))
    return FALSE;

  if (psCBObjDestroyed != nullptr)
  {
    retval = TRUE;
    *ppsObj = psCBObjDestroyed;
  }
  else
  {
    retval = FALSE;
    *ppsObj = nullptr;
  }

  if (!stackPushResult(VAL_BOOL, retval))
    return FALSE;

  return TRUE;
}

// the last object to be seen for a CALL_OBJ_SEEN
BASE_OBJECT* psScrCBObjSeen;
// the object that saw psScrCBObjSeen for a CALL_OBJ_SEEN
BASE_OBJECT* psScrCBObjViewer;

// deal with all the object seen functions
BOOL scrCBObjectSeen(SDWORD callback)
{
  BASE_OBJECT** ppsObj;
  BASE_OBJECT** ppsViewer;
  SDWORD player;
  BOOL retval;

  if (!stackPopParams(3, VAL_INT, &player, VAL_REF | ST_BASEOBJECT, &ppsObj, VAL_REF | ST_BASEOBJECT, &ppsViewer))
    return FALSE;

  if (psScrCBObjSeen == nullptr)
  {
    DEBUG_ASSERT_TEXT(FALSE, "scrCBObjectSeen: no object set");
    return FALSE;
  }

  *ppsObj = nullptr;
  if (((psScrCBObjViewer != nullptr) && (psScrCBObjViewer->player != player)) || !psScrCBObjSeen->visible[player])
    retval = FALSE;
  else if ((callback == CALL_DROID_SEEN) && (psScrCBObjSeen->type != OBJ_DROID))
    retval = FALSE;
  else if ((callback == CALL_STRUCT_SEEN) && (psScrCBObjSeen->type != OBJ_STRUCTURE))
    retval = FALSE;
  else if ((callback == CALL_FEATURE_SEEN) && (psScrCBObjSeen->type != OBJ_FEATURE))
    retval = FALSE;
  else
  {
    retval = TRUE;
    *ppsObj = psScrCBObjSeen;
    *ppsViewer = psScrCBObjViewer;
  }

  if (!stackPushResult(VAL_BOOL, retval))
    return FALSE;

  return TRUE;
}

// deal with a CALL_OBJ_SEEN
BOOL scrCBObjSeen(void) { return scrCBObjectSeen(CALL_OBJ_SEEN); }

// deal with a CALL_DROID_SEEN
BOOL scrCBDroidSeen(void) { return scrCBObjectSeen(CALL_DROID_SEEN); }

// deal with a CALL_STRUCT_SEEN
BOOL scrCBStructSeen(void) { return scrCBObjectSeen(CALL_STRUCT_SEEN); }

// deal with a CALL_FEATURE_SEEN
BOOL scrCBFeatureSeen(void) { return scrCBObjectSeen(CALL_FEATURE_SEEN); }

BOOL scrCBTransporterOffMap(void)
{
  SDWORD player;
  BOOL retval;
  DROID* psTransporter;

  if (!stackPopParams(1, VAL_INT, &player))
    return FALSE;

  psTransporter = transporterGetScriptCurrent();

  if ((psTransporter != nullptr) && (psTransporter->player == static_cast<UDWORD>(player)))
    retval = TRUE;
  else
    retval = FALSE;

  if (!stackPushResult(VAL_BOOL, retval))
    return FALSE;

  return TRUE;
}

BOOL scrCBTransporterLanded(void)
{
  SDWORD player;
  DROID_GROUP* psGroup;
  DROID *psTransporter, *psDroid, *psNext;
  BOOL retval;

  if (!stackPopParams(2, ST_GROUP, &psGroup, VAL_INT, &player))
    return FALSE;

  psTransporter = transporterGetScriptCurrent();

  if ((psTransporter == nullptr) || (psTransporter->player != static_cast<UDWORD>(player)))
    retval = FALSE;
  else
  {
    /* if not selectedPlayer unload droids */
    if (static_cast<UDWORD>(player) != selectedPlayer)
    {
      /* transfer droids from transporter group to current group */
      for (psDroid = psTransporter->psGroup->psList; psDroid; psDroid = psNext)
      {
        psNext = psDroid->psGrpNext;
        if (psDroid != psTransporter)
        {
          grpLeave(psTransporter->psGroup, psDroid);
          grpJoin(psGroup, psDroid);
        }
      }
    }

    retval = TRUE;
  }

  if (!stackPushResult(VAL_BOOL, retval))
    return FALSE;

  return TRUE;
}

// tell the scripts when a cluster is no longer valid
SDWORD scrCBEmptyClusterID;

BOOL scrCBClusterEmpty(void)
{
  SDWORD* pClusterID;

  if (!stackPopParams(1, VAL_REF | VAL_INT, &pClusterID))
    return FALSE;

  *pClusterID = scrCBEmptyClusterID;

  if (!stackPushResult(VAL_BOOL, TRUE))
    return FALSE;

  return TRUE;
}

// note when a vtol has finished returning to base - used to vanish
// vtols when they are attacking from off map
DROID* psScrCBVtolOffMap;

BOOL scrCBVtolOffMap(void)
{
  SDWORD player;
  DROID** ppsVtol;
  BOOL retval;

  if (!stackPopParams(2, VAL_INT, &player, VAL_REF | ST_DROID, &ppsVtol))
    return FALSE;

  if (psScrCBVtolOffMap == nullptr)
  {
    DEBUG_ASSERT_TEXT(FALSE, "scrCBVtolAtBase: NULL vtol pointer");
    return FALSE;
  }

  retval = FALSE;
  if (psScrCBVtolOffMap->player == player)
  {
    retval = TRUE;
    *ppsVtol = psScrCBVtolOffMap;
  }
  psScrCBVtolOffMap = nullptr;

  if (!stackPushResult(VAL_BOOL, retval))
    return FALSE;

  return TRUE;
}

/*called when selectedPlayer completes some research*/
BOOL scrCBResCompleted(void)
{
  RESEARCH** ppsResearch;
  BOOL retVal;

  if (!stackPopParams(1, VAL_REF | ST_RESEARCH, &ppsResearch))
    return FALSE;

  if (psCBLastResearch == nullptr)
  {
    DEBUG_ASSERT_TEXT(FALSE, "scrCBResCompleted: no research has been set");
    retVal = FALSE;
    *ppsResearch = nullptr;
  }
  else
  {
    retVal = TRUE;
    *ppsResearch = psCBLastResearch;
  }

  if (!stackPushResult(VAL_BOOL, retVal))
    return FALSE;

  return TRUE;
}

/* when a humna player leaves a game*/
BOOL scrCBPlayerLeft(void)
{
  SDWORD player;
  if (!stackPopParams(1, VAL_INT, &player))
    return FALSE;

  if (!stackPushResult(VAL_BOOL, TRUE))
    return FALSE;

  return TRUE;
}

// alliance has been offered.
BOOL scrCBAllianceOffer(void)
{
  SDWORD *from, *to;

  if (!stackPopParams(2, VAL_REF | VAL_INT, &from, VAL_REF | VAL_INT, &to))
    return FALSE;

  *from = CBallFrom;
  *to = CBallTo;

  if (!stackPushResult(VAL_BOOL, TRUE))
    return FALSE;

  return TRUE;
}
