/*
 * Parse.h
 *
 * Definitions for the script parser
 */
#ifndef _parse_h
#define _parse_h

/* Maximum number of TEXT items in any one Yacc rule */
#define TEXT_BUFFERS 10

/* Definition for the chunks of code that are used within the compiler */
using CODE_BLOCK = struct _code_block
{
  UDWORD size; // size of the code block
  UDWORD* pCode; // pointer to the code data
  UDWORD debugEntries;
  SCRIPT_DEBUG* psDebug; // Debugging info for the script.
  INTERP_TYPE type; // The type of the code block
};

/* The chunk of code returned from parsing a parameter list. */
using PARAM_BLOCK = struct _param_block
{
  UDWORD numParams;
  INTERP_TYPE* aParams; // List of parameter types
  UDWORD size;
  UDWORD* pCode; // The code that puts the parameters onto the stack
};

/* The types of a functions parameters, returned from parsing a parameter declaration */
using PARAM_DECL = struct _param_decl
{
  UDWORD numParams;
  INTERP_TYPE* aParams;
};

/* The chunk of code used while parsing a conditional statement */
using COND_BLOCK = struct _cond_block
{
  UDWORD numOffsets;
  UDWORD* aOffsets; // Positions in the code that have to be
  // replaced with the offset to the end of the
  // conditional statment (for the jumps).
  UDWORD size;
  UDWORD* pCode;
  UDWORD debugEntries; // Number of debugging entries in psDebug.
  SCRIPT_DEBUG* psDebug; // Debugging info for the script.
};

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
  STRING* pIdent; // Type identifier
};

/* Type for a variable identifier declaration */
using VAR_IDENT_DECL = struct _var_ident_decl
{
  STRING* pIdent; // variable identifier
  SDWORD dimensions; // number of dimensions of an array - 0 for normal var
  SDWORD elements[VAR_MAX_DIMENSIONS]; // number of elements in an array 
};

/* Type for a variable symbol */
using VAR_SYMBOL = struct _var_symbol
{
  STRING* pIdent; // variable's identifier
  INTERP_TYPE type; // variable type
  STORAGE_TYPE storage; // Where the variable is stored
  INTERP_TYPE objType; // The object type if this is an object variable
  UDWORD index; // Index of the variable in its data space
  SCRIPT_VARFUNC get, set; // Access functions if the variable is stored in an object/in-game
  SDWORD dimensions; // number of dimensions of an array - 0 for normal var
  SDWORD elements[VAR_MAX_DIMENSIONS]; // number of elements in an array 

  struct _var_symbol* psNext;
};

/* Type for an array access block */
using ARRAY_BLOCK = struct _array_block
{
  VAR_SYMBOL* psArrayVar;
  SDWORD dimensions;

  UDWORD size;
  UDWORD* pCode;
  UDWORD debugEntries; // Number of debugging entries in psDebug.
  SCRIPT_DEBUG* psDebug; // Debugging info for the script.
};

/* Type for a constant symbol */
using CONST_SYMBOL = struct _const_symbol
{
  STRING* pIdent; // variable's identifier
  INTERP_TYPE type; // variable type

  /* The actual value of the constant. 
   * Only one of these will be valid depending on type.
   * A union is not used as a union cannot be statically initialised
   */
  BOOL bval;
  SDWORD ival;
  void* oval;
};

/* The chunk of code used to reference an object variable */
using OBJVAR_BLOCK = struct _objvar_block
{
  VAR_SYMBOL* psObjVar; // The object variables symbol

  UDWORD size;
  UDWORD* pCode; // The code to get the object value on the stack
};

/* The maximum number of parameters for an instinct function */
#define INST_MAXPARAMS		20

/* Type for a function symbol */
using FUNC_SYMBOL = struct _func_symbol
{
#ifndef NOSCRIPT
  STRING* pIdent; // function's identifier
#endif
  SCRIPT_FUNC pFunc; // Pointer to the instinct function
#ifndef NOSCRIPT
  INTERP_TYPE type; // function type
  UDWORD numParams; // Number of parameters to the function
  INTERP_TYPE aParams[INST_MAXPARAMS];
  // List of parameter types
  BOOL script; // Whether the function is defined in the script
  // or a C instinct function
  UDWORD size; // The size of script code
  UDWORD* pCode; // The code for a function if it is defined in the script
  UDWORD location; // The position of the function in the final code block
  UDWORD debugEntries; // Number of debugging entries in psDebug.
  SCRIPT_DEBUG* psDebug; // Debugging info for the script.

  struct _func_symbol* psNext;
#endif
};

/* The type for a variable declaration */
using VAR_DECL = struct _var_decl
{
  INTERP_TYPE type;
  STORAGE_TYPE storage;
};

/* The type for a trigger sub declaration */
using TRIGGER_DECL = struct _trigger_decl
{
  TRIGGER_TYPE type;
  UDWORD size;
  UDWORD* pCode;
  UDWORD time;
};

/* Type for a trigger symbol */
using TRIGGER_SYMBOL = struct _trigger_symbol
{
  STRING* pIdent; // Trigger's identifier
  UDWORD index; // The triggers index number
  TRIGGER_TYPE type; // Trigger type
  UDWORD size; // Code size for the trigger
  UDWORD* pCode; // The trigger code
  UDWORD time; // How often to check the trigger

  UDWORD debugEntries;
  SCRIPT_DEBUG* psDebug;

  struct _trigger_symbol* psNext;
};

/* The type for a callback trigger symbol */
using CALLBACK_SYMBOL = struct _callback_symbol
{
  STRING* pIdent; // Callback identifier
  TRIGGER_TYPE type; // user defined callback id >= TR_CALLBACKSTART
  SCRIPT_FUNC pFunc; // Pointer to the instinct function
  UDWORD numParams; // Number of parameters to the function
  INTERP_TYPE aParams[INST_MAXPARAMS];
  // List of parameter types
};

/* Type for an event symbol */
using EVENT_SYMBOL = struct _event_symbol
{
  STRING* pIdent; // Event's identifier
  UDWORD index; // the events index number
  UDWORD size; // Code size for the event
  UDWORD* pCode; // Event code
  SDWORD trigger; // Index of the event's trigger

  UDWORD debugEntries;
  SCRIPT_DEBUG* psDebug;

  struct _event_symbol* psNext;
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

/* Set the current input buffer for the lexer */
extern void scriptSetInputBuffer(UBYTE* pBuffer, UDWORD size);

/* Initialise the parser ready for a new script */
extern BOOL scriptInitParser(void);

/* Set off the scenario file parser */
extern int scr_parse(void);

/* Give an error message */
void scr_error(char* pMessage, ...);

extern void scriptGetErrorData(int* pLine, char** ppText);

/* Look up a type symbol */
extern BOOL scriptLookUpType(STRING* pIdent, INTERP_TYPE* pType);

/* Add a new variable symbol */
extern BOOL scriptAddVariable(VAR_DECL* psStorage, VAR_IDENT_DECL* psVarIdent);

/* Add a new trigger symbol */
extern BOOL scriptAddTrigger(STRING* pIdent, TRIGGER_DECL* psDecl, UDWORD line);

/* Add a new event symbol */
extern BOOL scriptDeclareEvent(STRING* pIdent, EVENT_SYMBOL** ppsEvent);

// Add the code to a defined event
extern BOOL scriptDefineEvent(EVENT_SYMBOL* psEvent, CODE_BLOCK* psCode, SDWORD trigger);

/* Look up a variable symbol */
extern BOOL scriptLookUpVariable(STRING* pIdent, VAR_SYMBOL** ppsSym);

/* Look up a constant variable symbol */
extern BOOL scriptLookUpConstant(STRING* pIdent, CONST_SYMBOL** ppsSym);

/* Lookup a trigger symbol */
extern BOOL scriptLookUpTrigger(STRING* pIdent, TRIGGER_SYMBOL** ppsTrigger);

/* Lookup a callback trigger symbol */
extern BOOL scriptLookUpCallback(STRING* pIdent, CALLBACK_SYMBOL** ppsCallback);

/* Lookup an event symbol */
extern BOOL scriptLookUpEvent(STRING* pIdent, EVENT_SYMBOL** ppsEvent);

/* Reset the local variable symbol table */
extern void scriptClearLocalVariables(void);

/* Add a new function symbol */
extern BOOL scriptStartFunctionDef(STRING* pIdent, // Functions name
                                   INTERP_TYPE type); // return type

/* Store the parameter types for the current script function definition  */
extern BOOL scriptSetParameters(UDWORD numParams, // number of parameters
                                INTERP_TYPE* pParams); // parameter types

/* Store the code for a script function definition.
 * Clean up the local variable list for this function definition.
 */
extern BOOL scriptSetCode(CODE_BLOCK* psBlock); // The code block

/* Look up a function symbol */
extern BOOL scriptLookUpFunction(STRING* pIdent, FUNC_SYMBOL** ppsSym);

#endif
