#include "pch.h"
/*
 * ScriptVals.c
 *
 * Script data value functions
 *
 */

#include "Frame.h"
#include "Script.h"
#include "Objects.h"
#include "Base.h"
#include "ScriptTabs.h"
#include "ScriptVals.h"
#include "GTime.h"
#include "Text.h"
#include "Group.h"

// Keep all the loaded script contexts
using SCRV_STORE = struct _scrv_store
{
  SCRV_TYPE type;
  STRING* pIDString;
  SCRIPT_CONTEXT* psContext;

  struct _scrv_store* psNext;
};

// The list of script contexts
static SCRV_STORE* psContextStore = nullptr;

// keep a note of all base object pointers
#define MAX_BASEPOINTER		200
static INTERP_VAL* asBasePointers[MAX_BASEPOINTER];

// Initialise the script value module
BOOL scrvInitialise(void)
{
  psContextStore = nullptr;
  memset(asBasePointers, 0, sizeof(asBasePointers));

  return TRUE;
}

// Shut down the script value module
void scrvShutDown(void)
{
  SCRV_STORE* psCurr;
  while (psContextStore)
  {
    psCurr = psContextStore;
    psContextStore = psContextStore->psNext;

    delete[] psCurr->pIDString;
    psCurr->pIDString = nullptr;
    delete[] psCurr;
    psCurr = nullptr;
  }
}

// reset the script value module
void scrvReset(void)
{
  SCRV_STORE* psCurr;
  while (psContextStore)
  {
    psCurr = psContextStore;
    psContextStore = psContextStore->psNext;

    delete[] psCurr->pIDString;
    psCurr->pIDString = nullptr;
    delete[] psCurr;
    psCurr = nullptr;
  }

  psContextStore = nullptr;
  memset(asBasePointers, 0, sizeof(asBasePointers));
}

// Add a new context to the list
BOOL scrvAddContext(STRING* pID, SCRIPT_CONTEXT* psContext, SCRV_TYPE type)
{
  SCRV_STORE* psNew;

  psNew = new (std::nothrow) SCRV_STORE[1];
  if (!psNew)
  {
    DBERROR(("scrvAddContext: Out of memory"));
    return FALSE;
  }
  psNew->pIDString = new (std::nothrow) STRING[strlen(pID) + 1];
  if (!psNew->pIDString)
  {
    DBERROR(("scrvAddContext: Out of memory"));
    return FALSE;
  }
  strcpy(psNew->pIDString, pID);
  psNew->type = type;
  psNew->psContext = psContext;

  psNew->psNext = psContextStore;
  psContextStore = psNew;

  return TRUE;
}

// Add a new base pointer variable
BOOL scrvAddBasePointer(INTERP_VAL* psVal)
{
  SDWORD i;

  for (i = 0; i < MAX_BASEPOINTER; i++)
  {
    if (asBasePointers[i] == nullptr)
    {
      asBasePointers[i] = psVal;
      return TRUE;
    }
  }

  return FALSE;
}

// remove a base pointer from the list
void scrvReleaseBasePointer(INTERP_VAL* psVal)
{
  SDWORD i;

  for (i = 0; i < MAX_BASEPOINTER; i++)
  {
    if (asBasePointers[i] == psVal)
    {
      asBasePointers[i] = nullptr;
      return;
    }
  }
}

// Check all the base pointers to see if they have died
void scrvUpdateBasePointers(void)
{
  SDWORD i;
  INTERP_VAL* psVal;
  BASE_OBJECT* psObj;

  for (i = 0; i < MAX_BASEPOINTER; i++)
  {
    if (asBasePointers[i] != nullptr)
    {
      psVal = asBasePointers[i];
      psObj = static_cast<BASE_OBJECT*>(psVal->v.oval);

      if (psObj && psObj->died && psObj->died != NOT_CURRENT_LIST)
        psVal->v.oval = nullptr;
    }
  }
}

// create a group structure for a ST_GROUP variable
BOOL scrvNewGroup(INTERP_VAL* psVal)
{
  DROID_GROUP* psGroup;

  if (!grpCreate(&psGroup))
    return FALSE;

  // increment the refcount so the group doesn't get automatically freed when empty
  grpJoin(psGroup, nullptr);

  psVal->v.oval = psGroup;

  return TRUE;
}

// release a ST_GROUP variable
void scrvReleaseGroup(INTERP_VAL* psVal)
{
  DROID_GROUP* psGroup;

  psGroup = static_cast<DROID_GROUP*>(psVal->v.oval);
  grpReset(psGroup);

  ASSERT((psGroup->refCount == 1, "scrvReleaseGroup: ref count is wrong"));

  // do a final grpLeave to free the group
  grpLeave(psGroup, nullptr);
}

// Get a context from the list
BOOL scrvGetContext(STRING* pID, SCRIPT_CONTEXT** ppsContext)
{
  SCRV_STORE* psCurr;

  for (psCurr = psContextStore; psCurr; psCurr = psCurr->psNext)
  {
    if (strcmp(psCurr->pIDString, pID) == 0)
    {
      *ppsContext = psCurr->psContext;
      return TRUE;
    }
  }

  DBERROR(("scrvGetContext: couldn't find context for id: %s", pID));
  return FALSE;
}

// Find a base object from it's id
BOOL scrvGetBaseObj(UDWORD id, BASE_OBJECT** ppsObj)
{
  BASE_OBJECT* psObj;

  psObj = getBaseObjFromId(id);
  *ppsObj = psObj;

  if (psObj == nullptr)
    return FALSE;
  return TRUE;
}

// Find a string from it's (string)id
BOOL scrvGetString(STRING* pStringID, STRING** ppString)
{
  UDWORD id;

  //get the ID for the string
  if (!strresGetIDNum(psStringRes, pStringID, &id))
  {
    DBERROR(("Cannot find the string id %s ", pStringID));
    return FALSE;
  }
  //get the string from the id
  *ppString = strresGetString(psStringRes, id);

  return TRUE;
}

// Link any object types to the actual pointer values
/*BOOL scrvLinkValues(void)
{
	SCRV_STORE	*psCurr;
	UDWORD		index;
	SCRIPT_CODE		*psCode;
	SCRIPT_CONTEXT	*psContext;
	INTERP_VAL		*psVal;
	BASE_OBJECT		*psObj;
	SDWORD			compIndex;
	STRING			*pStr;

	for(psCurr = psContextStore; psCurr; psCurr=psCurr->psNext)
	{
		psContext = psCurr->psContext;
		psCode = psContext->psCode;
		for(index=0; index<psCode->numGlobals; index++)
		{
			switch (psCode->pGlobals[index])
			{
			case ST_DROID:
				if (!eventGetContextVal(psContext, index, &psVal))
				{
					DBERROR(("scrvLinkValues: Droid variable not found"));
					return FALSE;
				}
				if (!scrvGetBaseObj((UDWORD)psVal->v.ival, &psObj))
				{
					DBERROR(("scrvLinkValues: Droid id %d not found", psVal->v.ival));
					return FALSE;
				}
				if (psObj->type != OBJ_DROID)
				{
					DBERROR(("scrvLinkValues: Object id %d is not a droid"));
					return FALSE;
				}
				else
				{
					psVal->v.oval = psObj;
				}
				break;
			case ST_STRUCTURE:
				if (!eventGetContextVal(psContext, index, &psVal))
				{
					DBERROR(("scrvLinkValues: Structure variable not found"));
					return FALSE;
				}
				if (!scrvGetBaseObj((UDWORD)psVal->v.ival, &psObj))
				{
					DBERROR(("scrvLinkValues: Structure id %d not found", psVal->v.ival));
					return FALSE;
				}
				if (psObj->type != OBJ_STRUCTURE)
				{
					DBERROR(("scrvLinkValues: Object id %d is not a structure"));
					return FALSE;
				}
				else
				{
					psVal->v.oval = psObj;
				}
				break;
			case ST_FEATURE:
				if (!eventGetContextVal(psContext, index, &psVal))
				{
					DBERROR(("scrvLinkValues: Feature variable not found"));
					return FALSE;
				}
				if (!scrvGetBaseObj((UDWORD)psVal->v.ival, &psObj))
				{
					DBERROR(("scrvLinkValues: Feature id %d not found", psVal->v.ival));
					return FALSE;
				}
				if (psObj->type != OBJ_FEATURE)
				{
					DBERROR(("scrvLinkValues: Object id %d is not a Feature"));
					return FALSE;
				}
				else
				{
					psVal->v.oval = psObj;
				}
				break;
			case ST_BODY:
				if (!eventGetContextVal(psContext, index, &psVal))
				{
					DBERROR(("scrvLinkValues: body variable not found"));
					return FALSE;
				}
				compIndex = getCompFromName(COMP_BODY, psVal->v.sval);
				if (compIndex == -1)
				{
					DBERROR(("scrvLinkValues: body component %s not found",
						psVal->v.sval));
					return FALSE;
				}
				FREE(psVal->v.sval);
				psVal->v.ival = compIndex;
				break;
			case ST_PROPULSION:
				if (!eventGetContextVal(psContext, index, &psVal))
				{
					DBERROR(("scrvLinkValues: propulsion variable not found"));
					return FALSE;
				}
				compIndex = getCompFromName(COMP_PROPULSION, psVal->v.sval);
				if (compIndex == -1)
				{
					DBERROR(("scrvLinkValues: propulsion component %s not found",
						psVal->v.sval));
					return FALSE;
				}
				FREE(psVal->v.sval);
				psVal->v.ival = compIndex;
				break;
			case ST_ECM:
				if (!eventGetContextVal(psContext, index, &psVal))
				{
					DBERROR(("scrvLinkValues: ECM variable not found"));
					return FALSE;
				}
				compIndex = getCompFromName(COMP_ECM, psVal->v.sval);
				if (compIndex == -1)
				{
					DBERROR(("scrvLinkValues: ECM component %s not found",
						psVal->v.sval));
					return FALSE;
				}
				FREE(psVal->v.sval);
				psVal->v.ival = compIndex;
				break;
			case ST_SENSOR:
				if (!eventGetContextVal(psContext, index, &psVal))
				{
					DBERROR(("scrvLinkValues: sensor variable not found"));
					return FALSE;
				}
				compIndex = getCompFromName(COMP_SENSOR, psVal->v.sval);
				if (compIndex == -1)
				{
					DBERROR(("scrvLinkValues: sensor component %s not found",
						psVal->v.sval));
					return FALSE;
				}
				FREE(psVal->v.sval);
				psVal->v.ival = compIndex;
				break;
			case ST_CONSTRUCT:
				if (!eventGetContextVal(psContext, index, &psVal))
				{
					DBERROR(("scrvLinkValues: construct variable not found"));
					return FALSE;
				}
				compIndex = getCompFromName(COMP_CONSTRUCT, psVal->v.sval);
				if (compIndex == -1)
				{
					DBERROR(("scrvLinkValues: construct component %s not found",
						psVal->v.sval));
					return FALSE;
				}
				FREE(psVal->v.sval);
				psVal->v.ival = compIndex;
				break;
			case ST_WEAPON:
				if (!eventGetContextVal(psContext, index, &psVal))
				{
					DBERROR(("scrvLinkValues: weapon variable not found"));
					return FALSE;
				}
				compIndex = getCompFromName(COMP_WEAPON, psVal->v.sval);
				if (compIndex == -1)
				{
					DBERROR(("scrvLinkValues: weapon component %s not found",
						psVal->v.sval));
					return FALSE;
				}
				FREE(psVal->v.sval);
				psVal->v.ival = compIndex;
				break;
			default:
				// These values do not need to be linked
				break;
			}
		}

		// See if we have to run the script
		if (psCurr->type == SCRV_EXEC)
		{
			eventRunContext(psCurr->psContext, gameTime/SCR_TICKRATE);
		}
	}

	return TRUE;
}*/
