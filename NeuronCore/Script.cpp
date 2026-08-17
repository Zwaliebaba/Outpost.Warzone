#include "pch.h"

#include "Frame.h"
#include "Script.h"

// Initialise the script library
BOOL scriptInitialise(EVENT_INIT* psInit)
{
  if (!stackInitialise())
    return FALSE;
  if (!interpInitialise())
    return FALSE;
  if (!eventInitialise(psInit))
    return FALSE;

  return TRUE;
}

// Shutdown the script library
void scriptShutDown(void)
{
  eventShutDown();
  stackShutDown();
}

/* Free a SCRIPT_CODE structure */
void scriptFreeCode(SCRIPT_CODE* psCode)
{
  UDWORD i;

  delete[] psCode->pCode;
  psCode->pCode = nullptr;
  if (psCode->pTriggerTab) { delete[] psCode->pTriggerTab; }
  if (psCode->psTriggerData) { delete[] psCode->psTriggerData; }
  delete[] psCode->pEventTab;
  psCode->pEventTab = nullptr;
  delete[] psCode->pEventLinks;
  psCode->pEventLinks = nullptr;
  if (psCode->pGlobals != nullptr) { delete[] psCode->pGlobals; }
  if (psCode->psArrayInfo != nullptr) { delete[] psCode->psArrayInfo; }
  if (psCode->psDebug)
  {
    for (i = 0; i < psCode->debugEntries; i++) { if (psCode->psDebug[i].pLabel) { delete[] psCode->psDebug[i].pLabel; } }
    delete[] psCode->psDebug;
    psCode->psDebug = nullptr;
  }
  if (psCode->psVarDebug)
  {
    for (i = 0; i < psCode->numGlobals; i++) { if (psCode->psVarDebug[i].pIdent) { delete[] psCode->psVarDebug[i].pIdent; } }
    delete[] psCode->psVarDebug;
    psCode->psVarDebug = nullptr;
  }
  if (psCode->psArrayDebug)
  {
    for (i = 0; i < psCode->numArrays; i++) { if (psCode->psArrayDebug[i].pIdent) { delete[] psCode->psArrayDebug[i].pIdent; } }
    delete[] psCode->psArrayDebug;
    psCode->psArrayDebug = nullptr;
  }
  delete[] psCode;
  psCode = nullptr;
}
