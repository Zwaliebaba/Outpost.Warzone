/*
 * ScriptVals.h
 *
 * Common functions for the scriptvals loader
 */
#ifndef _scriptvals_h
#define _scriptvals_h

#include "Base.h"

// The possible types of initialisation values
using INIT_TYPE = enum _init_type
{
  IT_BOOL,
  IT_INDEX,
  IT_STRING,
};

// All the possible values that may be used to initialise a variable
using VAR_INIT = struct _var_init
{
  INIT_TYPE type;
  SDWORD index;
  STRING* pString;
};

// store array access data
using ARRAY_INDEXES = struct _array_indexes
{
  SDWORD dimensions;
  SDWORD elements[VAR_MAX_DIMENSIONS];
};

/* Set the current input buffer for the lexer */
extern void scrvSetInputBuffer(UBYTE* pBuffer, UDWORD size);

extern void scrvGetErrorData(int* pLine, char** ppText);

/* A simple error reporting routine */
extern void scrv_error(char* pMessage, ...);

// parse a value file
extern int scrv_parse(void);

// Lookup a type
extern BOOL scrvLookUpType(STRING* pIdent, INTERP_TYPE* pType);

// Lookup a variable identifier
extern BOOL scrvLookUpVar(STRING* pIdent, UDWORD* pIndex);

// Lookup an array identifier
extern BOOL scrvLookUpArray(STRING* pIdent, UDWORD* pIndex);

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
