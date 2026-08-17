#include "pch.h"

/* Control the execution trace printf's */
#define DEBUG_GROUP0
#include "Frame.h"
#include "Interp.h"
#include "Stack.h"
#include "CodePrint.h"
#include "Script.h"

#include <vector>

// the maximum number of instructions to execute before assuming
// an infinite loop
#define INTERP_MAXINSTRUCTIONS		100000

/* The type equivalence table */
static TYPE_EQUIV* asInterpTypeEquiv;

// whether the interpreter is running
static BOOL bInterpRunning = FALSE;

/* Whether to output trace information */
BOOL interpTrace;

/* Print out trace info if tracing is turned on */
#define TRCPRINTF(x) \
	if (interpTrace) \
		Neuron::DebugTrace x

#define TRCPRINTVAL(x) \
	if (interpTrace) \
		cpPrintVal(x)

#define TRCPRINTMATHSOP(x) \
	if (interpTrace) \
		cpPrintMathsOp(x)

#define TRCPRINTSTACKTOP() \
	if (interpTrace) \
		stackPrintTop()

// TRUE if the interpreter is currently running
BOOL interpProcessorActive(void) { return bInterpRunning; }

/* Find the value store for a context variable slot */
__inline INTERP_VAL* interpGetVarData(SCRIPT_CONTEXT* psContext, UDWORD index)
{
  return &psContext->aValues[index];
}

// get the array data for an array operation
static BOOL interpGetArrayVarData(const ScriptInstr& sInstr, SCRIPT_CONTEXT* psContext, SCRIPT_CODE* psProg, INTERP_VAL** ppsVal)
{
  SDWORD i, val;
  UDWORD size, index;
  const UDWORD base = static_cast<UDWORD>(sInstr.data);
  const SDWORD dimensions = sInstr.type;

  if (base >= psProg->numArrays)
  {
    DEBUG_ASSERT_TEXT(FALSE, "interpGetArrayVarData: array base index out of range");
    return FALSE;
  }
  if (dimensions != psProg->psArrayInfo[base].dimensions)
  {
    DEBUG_ASSERT_TEXT(FALSE, "interpGetArrayVarData: dimensions do not match");
    return FALSE;
  }

  const UBYTE* elements = psProg->psArrayInfo[base].elements;

  /* Pop the indices off the stack - the last dimension's index was pushed
     last - and address row-major with dimension 0 outermost */
  size = 1;
  index = 0;
  for (i = dimensions - 1; i >= 0; i -= 1)
  {
    if (!stackPopParams({{VAL_INT, &val}}))
      return FALSE;

    if ((val < 0) || (val >= elements[i]))
    {
      DEBUG_ASSERT_TEXT(FALSE, "interpGetArrayVarData: Array index for dimension {} out of range", i);
      return FALSE;
    }

    index += static_cast<UDWORD>(val) * size;
    size *= elements[i];
  }

  if (index >= size)
  {
    DEBUG_ASSERT_TEXT(FALSE, "interpGetArrayVarData: Array indexes out of variable space");
    return FALSE;
  }

  *ppsVal = interpGetVarData(psContext, psProg->psArrayInfo[base].base + index);

  return TRUE;
}

// Initialise the interpreter
BOOL interpInitialise(void)
{
  asInterpTypeEquiv = nullptr;

  return TRUE;
}

/* One entry of the script-function call stack */
using InterpFrame = struct _interp_frame
{
  UDWORD returnIp;
  UDWORD frameStart;
  UDWORD frameEnd;
  UDWORD funcIndex;
};

/* Resolve an OP_CALL callee to its C function, for execution and tracing */
static SCRIPT_FUNC interpResolveCall(UDWORD packed)
{
  const UDWORD slot = packed & INSTR_SLOTMASK;

  if (packed & INSTR_CALLBACKFLAG)
    return asScrCallbackTab[slot].pFunc;
  return asScrInstinctTab[slot].pFunc;
}

/* Resolve an OP_VARCALL callee */
static SCRIPT_VARFUNC interpResolveVarCall(UDWORD packed)
{
  const UDWORD slot = packed & INSTR_SLOTMASK;
  const VAR_SYMBOL* psVar = (packed & INSTR_OBJVARFLAG) ? &asScrObjectVarTab[slot] : &asScrExternalTab[slot];

  return (packed & INSTR_VARSETFLAG) ? psVar->set : psVar->get;
}

/* Run a compiled script */
BOOL interpRunScript(SCRIPT_CONTEXT* psContext, INTERP_RUNTYPE runType, UDWORD index, UDWORD offset)
{
  UDWORD ip;
  INTERP_VAL sVal, *psVar;
  UDWORD numSlots;
  UDWORD codeStart, codeEnd, codeBase;
  SCRIPT_FUNC scriptFunc;
  SCRIPT_VARFUNC scriptVarFunc;
  SCRIPT_CODE* psProg;
  SDWORD instructionCount = 0;
  std::vector<InterpFrame> aFrames;

  psProg = psContext->psCode;

  if (bInterpRunning)
  {
    DEBUG_ASSERT_TEXT(FALSE,
      "interpRunScript: interpreter already running" "                 - callback being called from within a script function?");
    return FALSE;
  }

  // note that the interpreter is running to stop recursive script calls
  bInterpRunning = TRUE;

  // Reset the stack in case another script messed up
  stackReset();

  // Turn off tracing initially
  interpTrace = FALSE;

  /* Get the number of context value slots */
  numSlots = psProg->ValueSlots();

  // Find the code range
  switch (runType)
  {
  case IRT_TRIGGER:
    if (index > psProg->numTriggers)
    {
      DEBUG_ASSERT_TEXT(FALSE, "interpRunScript: trigger index out of range");
      bInterpRunning = FALSE;
      return FALSE;
    }
    codeBase = psProg->pTriggerTab[index];
    codeStart = codeBase;
    codeEnd = psProg->pTriggerTab[index + 1];
    break;
  case IRT_EVENT:
    if (index > psProg->numEvents)
    {
      DEBUG_ASSERT_TEXT(FALSE, "interpRunScript: event index out of range");
      bInterpRunning = FALSE;
      return FALSE;
    }
    codeBase = psProg->pEventTab[index];
    codeStart = codeBase + offset;
    codeEnd = psProg->pEventTab[index + 1];
    break;
  default: DEBUG_ASSERT_TEXT(FALSE, "interpRunScript: unknown run type");
    bInterpRunning = FALSE;
    return FALSE;
  }

  UDWORD frameStart = codeStart;
  UDWORD frameEnd = codeEnd;

  // Run the code
  ip = codeStart;
  while (ip < frameEnd)
  {
    if (instructionCount > INTERP_MAXINSTRUCTIONS)
    {
      DEBUG_ASSERT_TEXT(FALSE, "interpRunScript: max instruction count exceeded - infinite loop ?");
      goto exit_with_error;
    }
    instructionCount += 1;

    TRCPRINTF(("%-6d  ", ip));
    {
      const ScriptInstr& sInstr = psProg->aCode[ip];
      switch (sInstr.op)
      {
      case OP_PUSH:
        sVal.type = sInstr.type;
        if (sInstr.type == VAL_BOOL || sInstr.type == VAL_INT || sInstr.type == VAL_TRIGGER || sInstr.type == VAL_EVENT)
          sVal.v.ival = sInstr.arg.ival;
        else
          sVal.v.oval = sInstr.arg.oval;
        TRCPRINTF(("PUSH        "));
        TRCPRINTVAL(&sVal);
        TRCPRINTF(("\n"));
        if (!stackPush(&sVal))
          goto exit_with_error;
        ip += 1;
        break;
      case OP_PUSHREF:
        // The type of the variable is stored in with the opcode
        sVal.type = sInstr.type;
        // store the pointer
        psVar = interpGetVarData(psContext, static_cast<UDWORD>(sInstr.data));
        sVal.v.oval = &(psVar->v.ival);
        TRCPRINTF(("PUSHREF     "));
        TRCPRINTVAL(&sVal);
        TRCPRINTF(("\n"));
        if (!stackPush(&sVal))
          goto exit_with_error;
        ip += 1;
        break;
      case OP_POP: TRCPRINTF(("POP\n"));
        if (!stackPop(&sVal))
          goto exit_with_error;
        ip += 1;
        break;
      case OP_BINARYOP: TRCPRINTMATHSOP(static_cast<UDWORD>(sInstr.data));
        if (!stackBinaryOp(static_cast<OPCODE>(sInstr.data)))
          goto exit_with_error;
        TRCPRINTSTACKTOP();
        TRCPRINTF(("\n"));
        ip += 1;
        break;
      case OP_UNARYOP: TRCPRINTMATHSOP(static_cast<UDWORD>(sInstr.data));
        if (!stackUnaryOp(static_cast<OPCODE>(sInstr.data)))
          goto exit_with_error;
        TRCPRINTSTACKTOP();
        TRCPRINTF(("\n"));
        ip += 1;
        break;
      case OP_PUSHGLOBAL: TRCPRINTF(("PUSHGLOBAL  %d\n", sInstr.data));
        if (static_cast<UDWORD>(sInstr.data) >= numSlots)
        {
          DEBUG_ASSERT_TEXT(FALSE, "interpRunScript: variable index out of range");
          goto exit_with_error;
        }
        if (!stackPush(interpGetVarData(psContext, static_cast<UDWORD>(sInstr.data))))
          goto exit_with_error;
        ip += 1;
        break;
      case OP_POPGLOBAL: TRCPRINTF(("POPGLOBAL   %d ", sInstr.data));
        TRCPRINTSTACKTOP();
        TRCPRINTF(("\n"));
        if (static_cast<UDWORD>(sInstr.data) >= numSlots)
        {
          DEBUG_ASSERT_TEXT(FALSE, "interpRunScript: variable index out of range");
          goto exit_with_error;
        }
        if (!stackPopType(interpGetVarData(psContext, static_cast<UDWORD>(sInstr.data))))
          goto exit_with_error;
        ip += 1;
        break;
      case OP_PUSHARRAYGLOBAL:
        TRCPRINTF(("PUSHARRAYGLOBAL  "));
        if (!interpGetArrayVarData(sInstr, psContext, psProg, &psVar))
          goto exit_with_error;
        TRCPRINTF(("\n"));
        if (!stackPush(psVar))
          goto exit_with_error;
        ip += 1;
        break;
      case OP_POPARRAYGLOBAL:
        TRCPRINTF(("POPARRAYGLOBAL   "));
        if (!interpGetArrayVarData(sInstr, psContext, psProg, &psVar))
          goto exit_with_error;
        TRCPRINTSTACKTOP();
        TRCPRINTF(("\n"));
        if (!stackPopType(psVar))
          goto exit_with_error;
        ip += 1;
        break;
      case OP_JUMPFALSE: TRCPRINTF(("JUMPFALSE   %d (%d)", sInstr.data, static_cast<SDWORD>(ip) + sInstr.data));
        if (!stackPop(&sVal))
          goto exit_with_error;
        if (!sVal.v.bval)
        {
          // Do the jump
          TRCPRINTF((" - done -\n"));
          ip = static_cast<UDWORD>(static_cast<SDWORD>(ip) + sInstr.data);
          if (ip < frameStart || ip > frameEnd)
          {
            DEBUG_ASSERT_TEXT(FALSE, "interpRunScript: jump out of range");
            goto exit_with_error;
          }
        }
        else
        {
          TRCPRINTF(("\n"));
          ip += 1;
        }
        break;
      case OP_JUMPTRUE: TRCPRINTF(("JUMPTRUE    %d (%d)", sInstr.data, static_cast<SDWORD>(ip) + sInstr.data));
        if (!stackPop(&sVal))
          goto exit_with_error;
        if (sVal.v.bval)
        {
          TRCPRINTF((" - done -\n"));
          ip = static_cast<UDWORD>(static_cast<SDWORD>(ip) + sInstr.data);
          if (ip < frameStart || ip > frameEnd)
          {
            DEBUG_ASSERT_TEXT(FALSE, "interpRunScript: jump out of range");
            goto exit_with_error;
          }
        }
        else
        {
          TRCPRINTF(("\n"));
          ip += 1;
        }
        break;
      case OP_JUMP: TRCPRINTF(("JUMP        %d (%d)\n", sInstr.data, static_cast<SDWORD>(ip) + sInstr.data));
        // Do the jump
        ip = static_cast<UDWORD>(static_cast<SDWORD>(ip) + sInstr.data);
        if (ip < frameStart || ip > frameEnd)
        {
          DEBUG_ASSERT_TEXT(FALSE, "interpRunScript: jump out of range");
          goto exit_with_error;
        }
        break;
      case OP_CALL: TRCPRINTF(("CALL        "));
        if (interpTrace)
          cpPrintCallee(sInstr.arg.func);
        TRCPRINTF(("\n"));
        scriptFunc = interpResolveCall(sInstr.arg.func);
        if (!scriptFunc())
          goto exit_with_error;
        ip += 1;
        break;
      case OP_VARCALL: TRCPRINTF(("VARCALL     "));
        if (interpTrace)
          cpPrintVarCallee(sInstr.arg.func);
        TRCPRINTF(("(%d)\n", sInstr.data));
        scriptVarFunc = interpResolveVarCall(sInstr.arg.func);
        if (scriptVarFunc == nullptr)
        {
          DEBUG_ASSERT_TEXT(FALSE, "interpRunScript: no variable access function");
          goto exit_with_error;
        }
        if (!scriptVarFunc(static_cast<UDWORD>(sInstr.data)))
          goto exit_with_error;
        ip += 1;
        break;
      case OP_SCRIPTCALL:
        {
          const UDWORD func = sInstr.arg.func;
          TRCPRINTF(("SCRIPTCALL  %d\n", func));
          if (func >= psProg->numFuncs)
          {
            DEBUG_ASSERT_TEXT(FALSE, "interpRunScript: script function index out of range");
            goto exit_with_error;
          }
          for (const InterpFrame& sFrame : aFrames)
          {
            if (sFrame.funcIndex == func)
            {
              DEBUG_ASSERT_TEXT(FALSE, "interpRunScript: recursive script function call");
              goto exit_with_error;
            }
          }
          /* Pop the arguments into the parameter slots; the last
             parameter was pushed last */
          const SCRIPT_FUNC_DATA& sFunc = psProg->psFuncData[func];
          for (SDWORD param = sFunc.numParams - 1; param >= 0; param -= 1)
          {
            psVar = interpGetVarData(psContext, sFunc.firstParamSlot + static_cast<UDWORD>(param));
            if (!stackPopType(psVar))
              goto exit_with_error;
          }
          InterpFrame sNew;
          sNew.returnIp = ip + 1;
          sNew.frameStart = frameStart;
          sNew.frameEnd = frameEnd;
          sNew.funcIndex = func;
          aFrames.push_back(sNew);
          frameStart = psProg->pFuncTab[func];
          frameEnd = psProg->pFuncTab[func + 1];
          ip = frameStart;
        }
        break;
      case OP_SCRIPTRET:
        TRCPRINTF(("SCRIPTRET\n"));
        if (aFrames.empty())
        {
          DEBUG_ASSERT_TEXT(FALSE, "interpRunScript: return outside a script function");
          goto exit_with_error;
        }
        ip = aFrames.back().returnIp;
        frameStart = aFrames.back().frameStart;
        frameEnd = aFrames.back().frameEnd;
        aFrames.pop_back();
        break;
      case OP_EXIT:
        // jump out of the code
        ip = frameEnd;
        break;
      case OP_PAUSE: TRCPRINTF(("PAUSE       %d\n", sInstr.data));
        DEBUG_ASSERT_TEXT(stackEmpty(), "interpRunScript: OP_PAUSE without empty stack");
        ip += 1;
        // tell the event system to reschedule this event
        if (!eventAddPauseTrigger(psContext, index, ip - codeBase, static_cast<UDWORD>(sInstr.data)))
          goto exit_with_error;
        // now jump out of the event
        ip = frameEnd;
        break;
      default: DEBUG_ASSERT_TEXT(FALSE, "interpRunScript: unknown opcode");
        goto exit_with_error;
        break;
      }
    }

    /* Falling off the end of a script function's body cannot happen for a
       well-formed program - the compiler ends every function in a return -
       so hitting frameEnd with frames still stacked is corruption */
    if (ip >= frameEnd && !aFrames.empty())
    {
      DEBUG_ASSERT_TEXT(FALSE, "interpRunScript: script function ran off its end");
      goto exit_with_error;
    }
  }
  TRCPRINTF(("%-6d  EXIT\n", ip));

  bInterpRunning = FALSE;
  return TRUE;

exit_with_error:
  // Deal with the script crashing or running out of memory
  TRCPRINTF(("*** ERROR EXIT ***\n"));
  bInterpRunning = FALSE;
  return FALSE;
}

/* Set the type equivalence table */
void scriptSetTypeEquiv(TYPE_EQUIV* psTypeTab)
{
#ifdef DEBUG
  SDWORD i;

  for (i = 0; psTypeTab[i].base != 0; i++)
  {
    DEBUG_ASSERT_TEXT(psTypeTab[i].base >= VAL_USERTYPESTART, "scriptSetTypeEquiv: can only set type equivalence for user types");
  }
#endif

  asInterpTypeEquiv = psTypeTab;
}

/* Check if two types are equivalent */
BOOL interpCheckEquiv(INTERP_TYPE to, INTERP_TYPE from)
{
  SDWORD i, j;
  BOOL toRef = FALSE, fromRef = FALSE;

  // check for the VAL_REF flag
  if (to & VAL_REF)
  {
    toRef = TRUE;
    to = to & ~VAL_REF;
  }
  if (from & VAL_REF)
  {
    fromRef = TRUE;
    from = from & ~VAL_REF;
  }
  if (toRef != fromRef)
    return FALSE;

  if (to == from)
    return TRUE;
  if (asInterpTypeEquiv)
  {
    for (i = 0; asInterpTypeEquiv[i].base != 0; i++)
    {
      if (asInterpTypeEquiv[i].base == to)
      {
        for (j = 0; j < asInterpTypeEquiv[i].numEquiv; j++)
        {
          if (asInterpTypeEquiv[i].aEquivTypes[j] == from)
            return TRUE;
        }
      }
    }
  }

  return FALSE;
}

/* Instinct function to turn on tracing */
BOOL interpTraceOn(void)
{
  interpTrace = TRUE;

  return TRUE;
}

/* Instinct function to turn off tracing */
BOOL interpTraceOff(void)
{
  interpTrace = FALSE;

  return TRUE;
}
