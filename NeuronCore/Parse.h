/*
 * Parse.h
 *
 * The interface between the game and the script compiler: the symbol
 * tables ScriptTabs.cpp fills in statically and hands over through the
 * scriptSet*Tab calls in Script.h.  The compiler itself lives in
 * ScriptComp.cpp with its own internal symbol types.
 */
#ifndef _parse_h
#define _parse_h

/* The possible access types for a type */
using ACCESS_TYPE = enum _access_type
{
  AT_SIMPLE,
  // The type represents a simple data value
  AT_OBJECT,
  // The type represents an object
};

/* Type for a user type symbol */
using TYPE_SYMBOL = struct _type_symbol
{
  SWORD typeID; // The type id to use in the type field of values
  SWORD accessType; // Whether the type is an object or a simple value
  const STRING* pIdent; // Type identifier
};

/* Type for a variable symbol in the game's extern and object-variable
   tables.  The fields are exactly the seven the static initialisers set. */
using VAR_SYMBOL = struct _var_symbol
{
  const STRING* pIdent; // variable's identifier
  INTERP_TYPE type; // variable type
  STORAGE_TYPE storage; // Where the variable is stored
  INTERP_TYPE objType; // The object type if this is an object variable
  UDWORD index; // Index of the variable in its data space
  SCRIPT_VARFUNC get, set; // Access functions
};

/* Type for a constant symbol */
using CONST_SYMBOL = struct _const_symbol
{
  const STRING* pIdent; // variable's identifier
  INTERP_TYPE type; // variable type

  /* The actual value of the constant.
   * Only one of these will be valid depending on type.
   * A union is not used as a union cannot be statically initialised
   */
  BOOL bval;
  SDWORD ival;
  void* oval;
};

/* The maximum number of parameters for an instinct function */
#define INST_MAXPARAMS		20

/* Type for a function symbol */
using FUNC_SYMBOL = struct _func_symbol
{
  const STRING* pIdent; // function's identifier
  SCRIPT_FUNC pFunc; // Pointer to the instinct function
  INTERP_TYPE type; // function type
  UDWORD numParams; // Number of parameters to the function
  INTERP_TYPE aParams[INST_MAXPARAMS];
  // List of parameter types
};

/* The type for a callback trigger symbol */
using CALLBACK_SYMBOL = struct _callback_symbol
{
  const STRING* pIdent; // Callback identifier
  TRIGGER_TYPE type; // user defined callback id >= TR_CALLBACKSTART
  SCRIPT_FUNC pFunc; // Pointer to the instinct function
  UDWORD numParams; // Number of parameters to the function
  INTERP_TYPE aParams[INST_MAXPARAMS];
  // List of parameter types
};

/* The table of user types */
extern TYPE_SYMBOL* asScrTypeTab;

/* The table of external variables and their access functions */
extern VAR_SYMBOL* asScrExternalTab;

/* The table of object variable access routines */
extern VAR_SYMBOL* asScrObjectVarTab;

/* The table of instinct function type definitions */
extern FUNC_SYMBOL* asScrInstinctTab;

/* The table of constant variables */
extern CONST_SYMBOL* asScrConstantTab;

/* The table of callback triggers */
extern CALLBACK_SYMBOL* asScrCallbackTab;

/* Look up a type symbol */
extern BOOL scriptLookUpType(const STRING* pIdent, INTERP_TYPE* pType);

#endif
