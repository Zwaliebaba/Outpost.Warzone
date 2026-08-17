/*
 * Interp.h
 *
 * Script interpreter definitions
 */
#ifndef _interp_h
#define _interp_h

#include <string>
#include <vector>

/* The possible value types for scripts.

   This id space is deliberately open ended: VAL_USERTYPESTART is an extension
   point, and the game adds its own ids beyond it (see SCR_USER_TYPES in
   ScriptTabs.h).  INTERP_TYPE is therefore an integer typedef rather than an
   enum type, so base and extended ids share one type -- in C++ two enums are
   distinct types and cannot be mixed.  The constants stay in an anonymous
   enum, which keeps them usable as case labels. */
enum
{
  // Basic types
  VAL_BOOL,
  VAL_INT,
  VAL_STRING,

  // events and triggers
  VAL_TRIGGER,
  VAL_EVENT,

  /* Type used by the compiler for functions that do not return a value */
  VAL_VOID,
  VAL_USERTYPESTART,
  // user defined types should start with this id
};

using INTERP_TYPE = SDWORD;

// flag to specify a variable reference rather than simple value
#define VAL_REF		0x00100000

/* A value consists of its type and value */
using INTERP_VAL = struct _interp_val
{
  INTERP_TYPE type;

  union
  {
    BOOL bval; // VAL_BOOL
    SDWORD ival; // VAL_INT
    STRING* sval; // VAL_STRING
    void* oval; // VAL_OBJECT
    void* pVoid; // VAL_VOIDPTR
  } v;
};

// maximum number of equivalent types for a type
#define INTERP_MAXEQUIV		10

// type equivalences
using TYPE_EQUIV = struct _interp_typeequiv
{
  INTERP_TYPE base; // the type that the others are equivalent to
  SDWORD numEquiv; // number of equivalent types
  INTERP_TYPE aEquivTypes[INTERP_MAXEQUIV];
  // the equivalent types
};

/* Opcodes for the script interpreter */
enum
{
  OP_PUSH,
  // Push value onto stack
  OP_PUSHREF,
  // Push a pointer to a variable onto the stack
  OP_POP,
  // Pop value from stack

  OP_PUSHGLOBAL,
  // Push the value of a global variable onto the stack
  OP_POPGLOBAL,
  // Pop a value from the stack into a global variable

  OP_PUSHARRAYGLOBAL,
  // Push the value of a global array variable onto the stack
  OP_POPARRAYGLOBAL,
  // Pop a value from the stack into a global array variable

  OP_CALL,
  // Call a C function: an instinct or callback table slot
  OP_VARCALL,
  // Call a variable access C function: an extern or object-variable slot

  OP_SCRIPTCALL,
  // Call a script-defined function by index
  OP_SCRIPTRET,
  // Return from a script-defined function

  OP_JUMP,
  // Jump to a different location in the script
  OP_JUMPTRUE,
  // Jump if the top stack value is true (reserved; never emitted)
  OP_JUMPFALSE,
  // Jump if the top stack value is false

  OP_BINARYOP,
  // Call a binary maths/boolean operator
  OP_UNARYOP,
  // Call a unary maths/boolean operator

  OP_EXIT,
  // End the program
  OP_PAUSE,
  // temporarily pause the current event

  // The following operations are secondary data to OP_BINARYOP and OP_UNARYOP

  // Maths operators
  OP_ADD,
  OP_SUB,
  OP_MUL,
  OP_DIV,
  OP_NEG,

  // Boolean operators
  OP_AND,
  OP_OR,
  OP_NOT,

  // Comparison operators
  OP_EQUAL,
  OP_NOTEQUAL,
  OP_GREATEREQUAL,
  OP_LESSEQUAL,
  OP_GREATER,
  OP_LESS,
};

using OPCODE = SDWORD;

// maximum sizes for arrays
#define VAR_MAX_DIMENSIONS		4
#define VAR_MAX_ELEMENTS		UBYTE_MAX

/* The type of function called by an OP_CALL */
using SCRIPT_FUNC = BOOL(*)(void);

/* The type of function called to access an object or in-game variable */
using SCRIPT_VARFUNC = BOOL(*)(UDWORD index);

/* One decoded instruction.

   The old form packed opcode and operand into one UDWORD and appended raw
   C pointers as extra 32-bit words, which cannot survive x64.  This form
   stores callees as table indices and keeps every operand in a slot of its
   own width.  Jump deltas count instructions. */
using ScriptInstr = struct _script_instr
{
  OPCODE op;
  INTERP_TYPE type; // operand type for PUSH/PUSHREF; dimensions for array ops
  SDWORD data; // small operand: variable index, jump delta, secondary op,
  // pause time, array base, VARCALL index argument, SCRIPTCALL arity

  union
  {
    SDWORD ival; // PUSH immediate for VAL_BOOL/VAL_INT/VAL_TRIGGER/VAL_EVENT
    void* oval; // PUSH object constant
    UDWORD func; // packed callee, see the INSTR_* helpers below
  } arg;
};

/* OP_CALL packs its callee as: bit 31 = table (0 instinct, 1 callback),
   low bits the slot.  OP_VARCALL packs: bit 31 = table (0 extern,
   1 object-variable), bit 30 = set-not-get, low bits the slot.
   OP_SCRIPTCALL's func is the plain script-function index. */
#define INSTR_CALLBACKFLAG		0x80000000u
#define INSTR_VARSETFLAG		0x40000000u
#define INSTR_OBJVARFLAG		0x80000000u
#define INSTR_SLOTMASK			0x3fffffffu

/* The possible storage types for a variable */
using enum_STORAGE_TYPE = enum _storage_type
{
  ST_PUBLIC,
  // Public variable
  ST_PRIVATE,
  // Private variable
  ST_OBJECT,
  // A value stored in an objects data space.
  ST_EXTERN,
  // An external value accessed by function call
};

using STORAGE_TYPE = UBYTE;

/* Variable debugging info for a script */
using VAR_DEBUG = struct _var_debug
{
  std::string pIdent;
  STORAGE_TYPE storage;
};

/* Array info for a script */
using ARRAY_DATA = struct _array_data
{
  UDWORD base; // the base index of the array values
  UBYTE type; // the array data type
  UBYTE dimensions;
  UBYTE elements[VAR_MAX_DIMENSIONS];
};

/* Array debug info for a script */
using ARRAY_DEBUG = struct _array_debug
{
  std::string pIdent;
  UBYTE storage;
};

/* Line debugging information for a script */
using SCRIPT_DEBUG = struct _script_debug
{
  UDWORD offset; // Offset in the compiled script that corresponds to
  UDWORD line; // this line in the original script.
  std::string pLabel; // the trigger/event that starts at this line
};

/* Different types of triggers.  Open ended in the same way as INTERP_TYPE
   above: the game's callback triggers continue from TR_CALLBACKSTART, so the
   type is an integer typedef and the constants live in an anonymous enum. */
enum
{
  TR_INIT,
  // Trigger fires when the script is first run
  TR_CODE,
  // Trigger uses script code
  TR_WAIT,
  // Trigger after a time pause
  TR_EVERY,
  // Trigger at repeated intervals
  TR_PAUSE,
  // Event has paused for an interval and will restart in the middle of it's code

  TR_CALLBACKSTART,
  // The user defined callback triggers should start with this id
};

using TRIGGER_TYPE = SDWORD;

/* Description of a trigger for the SCRIPT_CODE */
using TRIGGER_DATA = struct _trigger_data
{
  UWORD type; // Type of trigger
  UWORD code; // BOOL - is there code with this trigger
  UDWORD time; // How often to check the trigger
};

/* Data for one script-defined function */
using SCRIPT_FUNC_DATA = struct _script_func_data
{
  UWORD firstParamSlot; // context slot of the first parameter
  UWORD numParams;
  INTERP_TYPE returnType;
};

/* A compiled script and its associated data.

   Body i of a trigger/event/function spans instruction offsets
   [tab[i], tab[i+1]), so each table carries one extra entry.  Context value
   slots are laid out as: declared globals [0, numGlobals), array elements
   [numGlobals, numGlobals + arraySize), then function parameter slots
   (typed by aParamTypes).  Parameter slots are call-scoped temporaries and
   are excluded from the event system's create/release value hooks. */
using SCRIPT_CODE = struct _script_code
{
  std::vector<ScriptInstr> aCode;

  UWORD numTriggers = 0; // The number of triggers
  UWORD numEvents = 0; // The number of events
  std::vector<UDWORD> pTriggerTab; // The table of trigger offsets
  std::vector<TRIGGER_DATA> psTriggerData; // The extra info for each trigger
  std::vector<UDWORD> pEventTab; // The table of event offsets
  std::vector<SWORD> pEventLinks; // The original trigger/event linkage
  // -1 for no link

  UWORD numFuncs = 0; // The number of script-defined functions
  std::vector<UDWORD> pFuncTab; // The table of function offsets
  std::vector<SCRIPT_FUNC_DATA> psFuncData; // Arity and slots per function
  std::vector<INTERP_TYPE> aParamTypes; // Types of all function param slots

  UWORD numGlobals = 0; // The number of global variables
  UWORD numArrays = 0; // the number of arrays in the program
  UDWORD arraySize = 0; // the number of elements in all the defined arrays
  std::vector<INTERP_TYPE> pGlobals; // Types of the global variables
  std::vector<ARRAY_DATA> psArrayInfo; // The sizes of the program arrays

  std::vector<VAR_DEBUG> psVarDebug; // The names and storage types of variables
  std::vector<ARRAY_DEBUG> psArrayDebug; // Debug info for the arrays
  std::vector<SCRIPT_DEBUG> psDebug; // Debugging info for the script

  // The number of context value slots a context for this code needs
  UDWORD ValueSlots() const { return numGlobals + arraySize + static_cast<UDWORD>(aParamTypes.size()); }
};

/* What type of code should be run by the interpreter */
using INTERP_RUNTYPE = enum _interp_runtype
{
  IRT_TRIGGER,
  // Run trigger code
  IRT_EVENT,
  // Run event code
};

/* Check if two types are equivalent */
extern BOOL interpCheckEquiv(INTERP_TYPE to, INTERP_TYPE from);

// Initialise the interpreter
extern BOOL interpInitialise(void);

// TRUE if the interpreter is currently running
extern BOOL interpProcessorActive(void);

#endif
