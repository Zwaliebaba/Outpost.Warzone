#include "pch.h"

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
  /*	case VAL_FLOAT:
      DBPRINTF(("FLOAT"));
      break;*/
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
          Neuron::DebugTrace("{}", asScrTypeTab[i].pIdent );
          return;
        }
      }
    }
    ASSERT_TEXT(FALSE, "cpPrintType: Unknown type");
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
  case VAL_INT: Neuron::DebugTrace("type: INT     value: {}", psVal->v.ival );
    break;
  /*	case VAL_FLOAT:
      DBPRINTF(("type: FLOAT   value: %f", psVal->v.fval ));
      break;*/
  case VAL_STRING: Neuron::DebugTrace("type: STRING  value: {}", psVal->v.sval );
    break;
  case VAL_TRIGGER: Neuron::DebugTrace("type: TRIGGER value: {}", psVal->v.ival );
    break;
  case VAL_EVENT: Neuron::DebugTrace("type: EVENT   value: {}", psVal->v.ival );
    break;
  default:
    // See if it is a user defined type
    if (asScrTypeTab)
    {
      for (i = 0; asScrTypeTab[i].typeID != 0; i++)
      {
        if (asScrTypeTab[i].typeID == psVal->type)
        {
          Neuron::DebugTrace("type: {} value: {:x}", asScrTypeTab[i].pIdent, psVal->v.ival );
          return;
        }
      }
    }
    ASSERT_TEXT(FALSE, "cpPrintVal: Unknown value type");
    break;
  }
}

/* Display a value from a program that has been packed with an opcode */
void cpPrintPackedVal(UDWORD* ip)
{
  INTERP_TYPE type = (*ip) & OPCODE_DATAMASK;
  UDWORD i, data = *(ip + 1);

  if (type & VAL_REF)
  {
    Neuron::DebugTrace("type: ");
    cpPrintType(type);
    Neuron::DebugTrace(" value: {:x}", data);
    return;
  }

  switch (type)
  {
  case VAL_BOOL: Neuron::DebugTrace("BOOL   : {}", static_cast<BOOL>(data) ? "true" : "false");
    break;
  case VAL_INT: Neuron::DebugTrace("INT     : {}", static_cast<SDWORD>(data) );
    break;
  /*	case VAL_FLOAT:
      DBPRINTF(("FLOAT   : %f", (float)data ));
      break;*/
  case VAL_STRING: Neuron::DebugTrace("STRING  : {}", (STRING *)data );
    break;
  case VAL_TRIGGER: Neuron::DebugTrace("TRIGGER : {}", static_cast<SDWORD>(data) );
    break;
  case VAL_EVENT: Neuron::DebugTrace("EVENT   : {}", static_cast<SDWORD>(data) );
    break;
  default:
    // See if it is a user defined type
    if (asScrTypeTab)
    {
      for (i = 0; asScrTypeTab[i].typeID != 0; i++)
      {
        if (asScrTypeTab[i].typeID == type)
        {
          Neuron::DebugTrace("type: {} value: {:x}", asScrTypeTab[i].pIdent, data );
          return;
        }
      }
    }
    ASSERT_TEXT(FALSE, "cpPrintVal: Unknown value type");
    break;
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
  case OP_NOTEQUAL: Neuron::DebugTrace("NOT_EQUAL   ");
    break;
  case OP_GREATEREQUAL: Neuron::DebugTrace("GRT_EQUAL   ");
    break;
  case OP_LESSEQUAL: Neuron::DebugTrace("LESS_EQUAL  ");
    break;
  case OP_GREATER: Neuron::DebugTrace("GREATER     ");
    break;
  case OP_LESS: Neuron::DebugTrace("LESS        ");
    break;
  default: ASSERT_TEXT(FALSE, "cpPrintMathsOp: unknown operator");
    break;
  }
}

/* Print a function name */
void cpPrintFunc(SCRIPT_FUNC pFunc)
{
  SDWORD i;

  // Search the instinct functions
  if (asScrInstinctTab)
  {
    for (i = 0; asScrInstinctTab[i].pFunc != nullptr; i++)
    {
      if (asScrInstinctTab[i].pFunc == pFunc)
      {
        Neuron::DebugTrace("{}", asScrInstinctTab[i].pIdent);
        return;
      }
    }
  }

  // Search the callback functions
  if (asScrCallbackTab)
  {
    for (i = 0; asScrCallbackTab[i].type != 0; i++)
    {
      if (asScrCallbackTab[i].pFunc == pFunc)
      {
        Neuron::DebugTrace("{}", asScrCallbackTab[i].pIdent);
        return;
      }
    }
  }
}

/* Print a variable access function name */
void cpPrintVarFunc(SCRIPT_VARFUNC pFunc, UDWORD index)
{
  SDWORD i;

  // Search the external variable functions
  if (asScrExternalTab)
  {
    for (i = 0; asScrExternalTab[i].pIdent != nullptr; i++)
    {
      if (asScrExternalTab[i].set == pFunc && asScrExternalTab[i].index == index)
      {
        Neuron::DebugTrace("{} (set)", asScrExternalTab[i].pIdent);
        return;
      }
      if (asScrExternalTab[i].get == pFunc && asScrExternalTab[i].index == index)
      {
        Neuron::DebugTrace("{} (get)", asScrExternalTab[i].pIdent);
        return;
      }
    }
  }

  // Search the object functions
  if (asScrObjectVarTab)
  {
    for (i = 0; asScrObjectVarTab[i].pIdent != nullptr; i++)
    {
      if (asScrObjectVarTab[i].set == pFunc && asScrObjectVarTab[i].index == index)
      {
        Neuron::DebugTrace("{} (set)", asScrObjectVarTab[i].pIdent);
        return;
      }
      if (asScrObjectVarTab[i].get == pFunc && asScrObjectVarTab[i].index == index)
      {
        Neuron::DebugTrace("{} (get)", asScrObjectVarTab[i].pIdent);
        return;
      }
    }
  }
}

/* Print the array information */
void cpPrintArrayInfo(UDWORD** pip, SCRIPT_CODE* psProg)
{
  SDWORD i, dimensions; //, elements[VAR_MAX_DIMENSIONS];
  UDWORD* ip = *pip;
  UDWORD base;

  // get the base index of the array
  base = (*ip) & ARRAY_BASE_MASK;

  // get the number of dimensions
  dimensions = ((*ip) & ARRAY_DIMENSION_MASK) >> ARRAY_DIMENSION_SHIFT;

  // get the number of elements for each dimension

  Neuron::DebugTrace("{}->", base);
  for (i = 0; i < psProg->psArrayInfo[base].dimensions; i += 1)
    Neuron::DebugTrace("[{}]", psProg->psArrayInfo[base].elements[i]);
  // calculate the number of DWORDs needed to store the number of elements for each dimension of the array

  // update the insrtuction pointer
  *pip += 1; // + elementDWords;
}

/* Display the contents of a program in readable form */
void cpPrintProgram(SCRIPT_CODE* psProg)
{
  UDWORD *ip, *end;
  OPCODE opcode;
  UDWORD data, i, dim;
  SCRIPT_DEBUG* psCurrDebug = nullptr;
  BOOL debugInfo, triggerCode;
  UDWORD jumpOffset;
  VAR_DEBUG* psCurrVar;
  ARRAY_DATA* psCurrArray;
  ARRAY_DEBUG* psCurrArrayDebug;

  debugInfo = psProg->psDebug != nullptr;

  if (debugInfo)
  {
    // Print out the global variables
    if (psProg->numGlobals > 0)
    {
      Neuron::DebugTrace("index  storage  type variable name\n");
      psCurrVar = psProg->psVarDebug;
      for (i = 0; i < psProg->numGlobals; i++)
      {
        Neuron::DebugTrace("{:<6} {}  {:<4} {}\n", psCurrVar - psProg->psVarDebug,
          psCurrVar->storage == ST_PUBLIC ? "Public " : "Private", psProg->pGlobals[i], psCurrVar->pIdent);
        psCurrVar++;
      }
    }

    if (psProg->numArrays > 0)
    {
      Neuron::DebugTrace("\narrays\n");
      psCurrArray = psProg->psArrayInfo;
      psCurrArrayDebug = psProg->psArrayDebug;
      for (i = 0; i < psProg->numArrays; i++)
      {
        Neuron::DebugTrace("{:<6} {}  {:<4} {}", i,
          psCurrArrayDebug->storage == ST_PUBLIC ? "Public " : "Private", psCurrArray->type, psCurrArrayDebug->pIdent);
        for (dim = 0; dim < psCurrArray->dimensions; dim += 1)
          Neuron::DebugTrace("[{}]", psCurrArray->elements[dim]);
        Neuron::DebugTrace("\n");
        psCurrArray++;
        psCurrArrayDebug++;
      }
    }

    Neuron::DebugTrace("\nindex  line   offset\n");
    psCurrDebug = psProg->psDebug;
  }
  else
    Neuron::DebugTrace("index         offset\n");

  // Find the first trigger with code
  for (jumpOffset = 0; jumpOffset < psProg->numTriggers; jumpOffset++)
  {
    if (psProg->psTriggerData[jumpOffset].type == TR_CODE)
      break;
  }

  ip = psProg->pCode;
  triggerCode = psProg->numTriggers > 0 ? TRUE : FALSE;
  end = (UDWORD*)(((UBYTE*)ip) + psProg->size);
  opcode = (*ip) >> OPCODE_SHIFT;
  data = (*ip) & OPCODE_DATAMASK;
  while (ip < end)
  {
    // display a label if there is one
    if (debugInfo)
    {
      if (static_cast<UDWORD>(ip - psProg->pCode) == psCurrDebug->offset && psCurrDebug->pLabel != nullptr)
        Neuron::DebugTrace("{}\n", psCurrDebug->pLabel);
    }

    // Display the trigger/event index
    if (triggerCode)
    {
      if (ip - psProg->pCode == psProg->pTriggerTab[jumpOffset])
      {
        Neuron::DebugTrace("{:<6} ", jumpOffset);
        jumpOffset += 1;
        // Find the next trigger with code
        while (jumpOffset < psProg->numTriggers)
        {
          if (psProg->psTriggerData[jumpOffset].type == TR_CODE)
            break;
          jumpOffset++;
        }
        if (jumpOffset >= psProg->numTriggers)
        {
          // Got to the end of the triggers
          triggerCode = FALSE;
          jumpOffset = 0;
        }
      }
      else
        Neuron::DebugTrace("       ");
    }
    else
    {
      if (ip - psProg->pCode == psProg->pEventTab[jumpOffset])
      {
        Neuron::DebugTrace("{:<6} ", jumpOffset);
        jumpOffset += 1;
      }
      else
        Neuron::DebugTrace("       ");
    }

    // Display the line number
    if (debugInfo)
    {
      if (static_cast<UDWORD>(ip - psProg->pCode) == psCurrDebug->offset)
      {
        Neuron::DebugTrace("{:<6} ", psCurrDebug->line);
        psCurrDebug++;
      }
      else
        Neuron::DebugTrace("       ");
    }

    // Display the code offset
    Neuron::DebugTrace("{:<6}  ", ip - psProg->pCode);
    switch (opcode)
    {
    case OP_PUSH: Neuron::DebugTrace("PUSH        ");
      cpPrintPackedVal(ip);
      Neuron::DebugTrace("\n");
      ip += aOpSize[opcode];
      break;
    case OP_PUSHREF: Neuron::DebugTrace("PUSHREF     ");
      cpPrintPackedVal(ip);
      Neuron::DebugTrace("\n");
      ip += aOpSize[opcode];
      break;
    case OP_POP: Neuron::DebugTrace("POP\n");
      ip += aOpSize[opcode];
      break;
    case OP_PUSHGLOBAL: Neuron::DebugTrace("PUSHGLOBAL  {}\n", data);
      ip += aOpSize[opcode];
      break;
    case OP_POPGLOBAL: Neuron::DebugTrace("POPGLOBAL   {}\n", data);
      ip += aOpSize[opcode];
      break;
    case OP_PUSHARRAYGLOBAL: Neuron::DebugTrace("PUSHARRAYGLOBAL  ");
      cpPrintArrayInfo(&ip, psProg);
      Neuron::DebugTrace("\n");
      break;
    case OP_POPARRAYGLOBAL: Neuron::DebugTrace("POPARRAYGLOBAL   ");
      cpPrintArrayInfo(&ip, psProg);
      Neuron::DebugTrace("\n");
      break;
    case OP_CALL: Neuron::DebugTrace("CALL        ");
      cpPrintFunc((SCRIPT_FUNC)(*(ip + 1)));
      Neuron::DebugTrace("\n");
      ip += aOpSize[opcode];
      break;
    case OP_VARCALL: Neuron::DebugTrace("VARCALL     ");
      cpPrintVarFunc((SCRIPT_VARFUNC)(*(ip + 1)), data);
      Neuron::DebugTrace("({})\n", data);
      ip += aOpSize[opcode];
      break;
    case OP_JUMP: Neuron::DebugTrace("JUMP        {} ({})\n", static_cast<SWORD>(data), ip - psProg->pCode + static_cast<SWORD>(data));
      ip += aOpSize[opcode];
      break;
    case OP_JUMPTRUE: Neuron::DebugTrace("JUMPTRUE    {} ({})\n", static_cast<SWORD>(data), ip - psProg->pCode + static_cast<SWORD>(data));
      ip += aOpSize[opcode];
      break;
    case OP_JUMPFALSE: Neuron::DebugTrace("JUMPFALSE   {} ({})\n", static_cast<SWORD>(data), ip - psProg->pCode + static_cast<SWORD>(data));
      ip += aOpSize[opcode];
      break;
    case OP_BINARYOP:
    case OP_UNARYOP:
      cpPrintMathsOp(data);
      Neuron::DebugTrace("\n");
      ip += aOpSize[opcode];
      break;
    case OP_EXIT: Neuron::DebugTrace("EXIT\n");
      ip += aOpSize[opcode];
      break;
    case OP_PAUSE: Neuron::DebugTrace("PAUSE       {}\n", data);
      ip += aOpSize[opcode];
      break;
    default: ASSERT_TEXT(FALSE,"cpPrintProgram: Unknown opcode: {:x}", *ip);
      break;
    }

    opcode = (*ip) >> OPCODE_SHIFT;
    data = (*ip) & OPCODE_DATAMASK;
  }
}