#include "pch.h"

#include "Frame.h"
#include "Script.h"

// Initialise the script library
BOOL scriptInitialise(void)
{
  if (!stackInitialise())
    return FALSE;
  if (!interpInitialise())
    return FALSE;
  if (!eventInitialise())
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
