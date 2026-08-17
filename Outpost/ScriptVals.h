/*
 * ScriptVals.h
 *
 * Common functions for the scriptvals loader
 */
#ifndef _scriptvals_h
#define _scriptvals_h

#include "Base.h"

// Whether the script is run immediately or stored for later use
using SCRV_TYPE = enum _scrv_type
{
  SCRV_EXEC,
  SCRV_NOEXEC,
};

// Add a new context to the list
extern BOOL scrvAddContext(STRING* pID, SCRIPT_CONTEXT* psContext, SCRV_TYPE type);

// Get a context from the list
extern BOOL scrvGetContext(STRING* pID, SCRIPT_CONTEXT** ppsContext);

// Add a new base pointer variable
extern BOOL scrvAddBasePointer(INTERP_VAL* psVal);

// Check all the base pointers to see if they have died
extern void scrvUpdateBasePointers(void);

// remove a base pointer from the list
extern void scrvReleaseBasePointer(INTERP_VAL* psVal);

// create a group structure for a ST_GROUP variable
extern BOOL scrvNewGroup(INTERP_VAL* psVal);

// release a ST_GROUP variable
extern void scrvReleaseGroup(INTERP_VAL* psVal);

// Initialise the script value module
extern BOOL scrvInitialise(void);

// Shut down the script value module
extern void scrvShutDown(void);

// reset the script value module
extern void scrvReset(void);

// Load a script value file
extern BOOL scrvLoad(UBYTE* pData, UDWORD size);

// Link any object types to the actual pointer values

// Find a base object from it's id
extern BOOL scrvGetBaseObj(UDWORD id, BASE_OBJECT** ppsObj);

// Find a string from it's (string)id
extern BOOL scrvGetString(STRING* pStringID, STRING** ppString);
#endif
