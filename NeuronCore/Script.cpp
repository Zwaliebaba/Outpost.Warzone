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
  delete psCode;
}
