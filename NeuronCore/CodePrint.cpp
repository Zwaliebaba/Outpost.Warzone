#include "pch.h"

#include <cstdint>

#include "Frame.h"
#include "Interp.h"
#include "Parse.h"
#include "CodePrint.h"
#include "Script.h"

/* Display a value type */
void cpPrintType(INTERP_TYPE type)
{
  UDWORD i;
  BOOL ref = FALSE;

  if (type & VAL_REF)
  {
    ref = TRUE;
    type = type & ~VAL_REF;
  }

  switch (type)
  {
  case VAL_BOOL: Neuron::DebugTrace("BOOL");
    break;
  case VAL_INT: Neuron::DebugTrace("INT");
    break;
  case VAL_STRING: Neuron::DebugTrace("STRING");
    break;
  case VAL_TRIGGER: Neuron::DebugTrace("TRIGGER");
    break;
  case VAL_EVENT: Neuron::DebugTrace("EVENT");
    break;
  case VAL_VOID: Neuron::DebugTrace("VOID");
    break;
  default:
    // See if it is a user defined type
    if (asScrTypeTab)
    {
      for (i = 0; asScrTypeTab[i].typeID != 0; i++)
      {
        if (asScrTypeTab[i].typeID == type)
        {
          Neuron::DebugTrace("{}", asScrTypeTab[i].pIdent);
          return;
        }
      }
    }
    DEBUG_ASSERT_TEXT(FALSE, "cpPrintType: Unknown type");
    break;
  }

  if (ref)
    Neuron::DebugTrace(" REF");
}

/* Display a value  */
void cpPrintVal(INTERP_VAL* psVal)
{
  UDWORD i;

  if (psVal->type & VAL_REF)
  {
    Neuron::DebugTrace("type: ");
    cpPrintType(psVal->type);
    Neuron::DebugTrace(" value: {:x}", psVal->v.ival);
    return;
  }

  switch (psVal->type)
  {
  case VAL_BOOL: Neuron::DebugTrace("type: BOOL    value: {}", psVal->v.bval ? "true" : "false");
    break;
  case VAL_INT: Neuron::DebugTrace("type: INT     value: {}", psVal->v.ival);
    break;
  case VAL_STRING: Neuron::DebugTrace("type: STRING  value: {}", psVal->v.sval);
    break;
  case VAL_TRIGGER: Neuron::DebugTrace("type: TRIGGER value: {}", psVal->v.ival);
    break;
  case VAL_EVENT: Neuron::DebugTrace("type: EVENT   value: {}", psVal->v.ival);
    break;
  default:
    // See if it is a user defined type
    if (asScrTypeTab)
    {
      for (i = 0; asScrTypeTab[i].typeID != 0; i++)
      {
        if (asScrTypeTab[i].typeID == psVal->type)
        {
          Neuron::DebugTrace("type: {} value: {:x}", asScrTypeTab[i].pIdent, psVal->v.ival);
          return;
        }
      }
    }
    DEBUG_ASSERT_TEXT(FALSE, "cpPrintVal: Unknown value type");
    break;
  }
}

/* Print the name of an OP_CALL / OP_SCRIPTCALL callee from its packed form */
void cpPrintCallee(UDWORD packed)
{
  const UDWORD slot = packed & INSTR_SLOTMASK;

  if (packed & INSTR_CALLBACKFLAG)
  {
    if (asScrCallbackTab)
      Neuron::DebugTrace("{}", asScrCallbackTab[slot].pIdent);
    else
      Neuron::DebugTrace("callback {}", slot);
  }
  else
  {
    if (asScrInstinctTab)
      Neuron::DebugTrace("{}", asScrInstinctTab[slot].pIdent);
    else
      Neuron::DebugTrace("instinct {}", slot);
  }
}

/* Print the name of an OP_VARCALL callee from its packed form */
void cpPrintVarCallee(UDWORD packed)
{
  const UDWORD slot = packed & INSTR_SLOTMASK;
  const char* pDir = (packed & INSTR_VARSETFLAG) ? "set" : "get";

  if (packed & INSTR_OBJVARFLAG)
  {
    if (asScrObjectVarTab)
      Neuron::DebugTrace("{} {}", pDir, asScrObjectVarTab[slot].pIdent);
    else
      Neuron::DebugTrace("{} objvar {}", pDir, slot);
  }
  else
  {
    if (asScrExternalTab)
      Neuron::DebugTrace("{} {}", pDir, asScrExternalTab[slot].pIdent);
    else
      Neuron::DebugTrace("{} extern {}", pDir, slot);
  }
}

/* Display a maths operator */
void cpPrintMathsOp(UDWORD opcode)
{
  switch (opcode)
  {
  case OP_ADD: Neuron::DebugTrace("ADD         ");
    break;
  case OP_SUB: Neuron::DebugTrace("SUB         ");
    break;
  case OP_MUL: Neuron::DebugTrace("MUL         ");
    break;
  case OP_DIV: Neuron::DebugTrace("DIV         ");
    break;
  case OP_NEG: Neuron::DebugTrace("NEG         ");
    break;
  case OP_AND: Neuron::DebugTrace("AND         ");
    break;
  case OP_OR: Neuron::DebugTrace("OR          ");
    break;
  case OP_NOT: Neuron::DebugTrace("NOT         ");
    break;
  case OP_EQUAL: Neuron::DebugTrace("EQUAL       ");
    break;
  case OP_NOTEQUAL: Neuron::DebugTrace("NOTEQUAL    ");
    break;
  case OP_GREATEREQUAL: Neuron::DebugTrace("GREATEREQUAL");
    break;
  case OP_LESSEQUAL: Neuron::DebugTrace("LESSEQUAL   ");
    break;
  case OP_GREATER: Neuron::DebugTrace("GREATER     ");
    break;
  case OP_LESS: Neuron::DebugTrace("LESS        ");
    break;
  default: DEBUG_ASSERT_TEXT(FALSE, "cpPrintMathsOp: unknown operator");
    break;
  }
}

/* Print a label for an instruction offset, if debug info carries one */
static void cpPrintLabel(SCRIPT_CODE* psProg, UDWORD offset)
{
  for (const SCRIPT_DEBUG& sEntry : psProg->psDebug)
  {
    if (sEntry.offset == offset && !sEntry.pLabel.empty())
      Neuron::DebugTrace("{}:\n", sEntry.pLabel);
  }
}

/* Display the contents of a program in readable form */
void cpPrintProgram(SCRIPT_CODE* psProg)
{
  DEBUG_ASSERT_TEXT(psProg != NULL, "cpPrintProgram: Invalid program pointer");

  Neuron::DebugTrace("triggers: {}  events: {}  functions: {}  globals: {}  arrays: {} ({} values)\n",
                     psProg->numTriggers, psProg->numEvents, psProg->numFuncs,
                     psProg->numGlobals, psProg->numArrays, psProg->arraySize);

  for (UDWORD ip = 0; ip < psProg->aCode.size(); ip++)
  {
    const ScriptInstr& sInstr = psProg->aCode[ip];

    cpPrintLabel(psProg, ip);
    Neuron::DebugTrace("{:6}  ", ip);
    switch (sInstr.op)
    {
    case OP_PUSH:
      Neuron::DebugTrace("PUSH        ");
      cpPrintType(sInstr.type);
      if (sInstr.type == VAL_BOOL || sInstr.type == VAL_INT || sInstr.type == VAL_TRIGGER || sInstr.type == VAL_EVENT)
        Neuron::DebugTrace(" {}", sInstr.arg.ival);
      else
        Neuron::DebugTrace(" {:x}", reinterpret_cast<std::uintptr_t>(sInstr.arg.oval));
      break;
    case OP_PUSHREF:
      Neuron::DebugTrace("PUSHREF     ");
      cpPrintType(sInstr.type);
      Neuron::DebugTrace(" var {}", sInstr.data);
      break;
    case OP_POP: Neuron::DebugTrace("POP         ");
      break;
    case OP_PUSHGLOBAL: Neuron::DebugTrace("PUSHGLOBAL  {}", sInstr.data);
      break;
    case OP_POPGLOBAL: Neuron::DebugTrace("POPGLOBAL   {}", sInstr.data);
      break;
    case OP_PUSHARRAYGLOBAL: Neuron::DebugTrace("PUSHARRAY   base {} dims {}", sInstr.data, sInstr.type);
      break;
    case OP_POPARRAYGLOBAL: Neuron::DebugTrace("POPARRAY    base {} dims {}", sInstr.data, sInstr.type);
      break;
    case OP_CALL:
      Neuron::DebugTrace("CALL        ");
      cpPrintCallee(sInstr.arg.func);
      break;
    case OP_VARCALL:
      Neuron::DebugTrace("VARCALL     ");
      cpPrintVarCallee(sInstr.arg.func);
      Neuron::DebugTrace("({})", sInstr.data);
      break;
    case OP_SCRIPTCALL:
      Neuron::DebugTrace("SCRIPTCALL  {} ({} params)", sInstr.arg.func, sInstr.data);
      break;
    case OP_SCRIPTRET: Neuron::DebugTrace("SCRIPTRET   ");
      break;
    case OP_JUMP: Neuron::DebugTrace("JUMP        {} ({})", sInstr.data, static_cast<SDWORD>(ip) + sInstr.data);
      break;
    case OP_JUMPTRUE: Neuron::DebugTrace("JUMPTRUE    {} ({})", sInstr.data, static_cast<SDWORD>(ip) + sInstr.data);
      break;
    case OP_JUMPFALSE: Neuron::DebugTrace("JUMPFALSE   {} ({})", sInstr.data, static_cast<SDWORD>(ip) + sInstr.data);
      break;
    case OP_BINARYOP: cpPrintMathsOp(static_cast<UDWORD>(sInstr.data));
      break;
    case OP_UNARYOP: cpPrintMathsOp(static_cast<UDWORD>(sInstr.data));
      break;
    case OP_EXIT: Neuron::DebugTrace("EXIT        ");
      break;
    case OP_PAUSE: Neuron::DebugTrace("PAUSE       {}", sInstr.data);
      break;
    default: DEBUG_ASSERT_TEXT(FALSE, "cpPrintProgram: unknown opcode");
      break;
    }
    Neuron::DebugTrace("\n");
  }
}
