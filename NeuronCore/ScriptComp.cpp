/*
 * ScriptComp.cpp
 *
 * The native .slo compiler: recursive-descent parser and code generator
 * emitting ScriptInstr records.  Replaces the generated Script_y.cpp /
 * Script_l.cpp.  The language it accepts is specified in
 * Docs/ScriptLanguage.md; where that document and the shipped scripts
 * disagree, the scripts win.
 */
#include "pch.h"

#include "Frame.h"
#include "Script.h"
#include "ScriptLex.h"

#include <string>
#include <unordered_map>
#include <vector>

/* The game symbol tables, set once at startup by ScriptTabs.cpp */
TYPE_SYMBOL* asScrTypeTab;
FUNC_SYMBOL* asScrInstinctTab;
VAR_SYMBOL* asScrExternalTab;
VAR_SYMBOL* asScrObjectVarTab;
CONST_SYMBOL* asScrConstantTab;
CALLBACK_SYMBOL* asScrCallbackTab;

void scriptSetTypeTab(TYPE_SYMBOL* psTypeTab) { asScrTypeTab = psTypeTab; }
void scriptSetFuncTab(FUNC_SYMBOL* psFuncTab) { asScrInstinctTab = psFuncTab; }
void scriptSetExternalTab(VAR_SYMBOL* psExtTab) { asScrExternalTab = psExtTab; }
void scriptSetObjectTab(VAR_SYMBOL* psObjTab) { asScrObjectVarTab = psObjTab; }
void scriptSetConstTab(CONST_SYMBOL* psConstTab) { asScrConstantTab = psConstTab; }
void scriptSetCallbackTab(CALLBACK_SYMBOL* psCallTab) { asScrCallbackTab = psCallTab; }

/* Look up a type symbol */
BOOL scriptLookUpType(const STRING* pIdent, INTERP_TYPE* pType)
{
  if (asScrTypeTab)
  {
    for (UDWORD i = 0; asScrTypeTab[i].typeID != 0; i++)
    {
      if (strcmp(asScrTypeTab[i].pIdent, pIdent) == 0)
      {
        *pType = asScrTypeTab[i].typeID;
        return TRUE;
      }
    }
  }

  return FALSE;
}

namespace Neuron
{
namespace
{
/* A chunk of generated code with the type of the value it leaves on the
   stack (VAL_VOID for statements) */
struct Block
{
  std::vector<ScriptInstr> aCode;
  INTERP_TYPE type = VAL_VOID;
  std::vector<SCRIPT_DEBUG> aDebug;
};

/* A script-declared variable or array */
struct VarSym
{
  INTERP_TYPE type = VAL_VOID;
  STORAGE_TYPE storage = ST_PUBLIC;
  UDWORD index = 0; // plain: context slot.  array: array number
  SDWORD dims = 0; // 0 for a plain variable
  SDWORD elements[VAR_MAX_DIMENSIONS] = {};
};

/* A script trigger */
struct TriggerSym
{
  std::string name;
  TRIGGER_TYPE type = TR_INIT;
  UDWORD time = 0;
  Block sCode; // empty unless TR_CODE or callback-with-params
  UDWORD line = 0;
};

/* A script event */
struct EventSym
{
  std::string name;
  SDWORD trigger = -1;
  bool defined = false;
  Block sCode;
  UDWORD line = 0;
};

/* A script-defined function */
struct FuncSym
{
  std::string name;
  INTERP_TYPE returnType = VAL_VOID;
  UWORD numParams = 0;
  INTERP_TYPE aParams[INST_MAXPARAMS] = {};
  UDWORD firstParamSlot = 0; // absolute context slot of parameter 0
  Block sCode;
  UDWORD line = 0;
};

/* What broad kind a value type is, for the operator typing rules */
enum class TypeKind
{
  Int,
  Bool,
  Object, // a user type with AT_OBJECT access
  User, // everything else: other user types, triggers, events
};

/* An assignable place, resolved from the left of an '=' */
struct Place
{
  enum class Kind
  {
    Global, // script variable (or function parameter)
    Extern, // ST_EXTERN with a set function
    ObjMember, // object member through a set accessor
    ArrayElem, // element of a script array
  };

  Kind kind = Kind::Global;
  INTERP_TYPE type = VAL_VOID;
  UDWORD index = 0; // Global/Extern: slot / extern index.  ArrayElem: array number
  UDWORD slot = 0; // Extern/ObjMember: table slot of the accessor pair
  bool objVar = false; // ObjMember: true (accessor in the objvar table)
  SDWORD dims = 0; // ArrayElem: dimensions indexed
  Block sPrefix; // ObjMember: object code.  ArrayElem: index code
};

struct Compiler
{
  const std::vector<ScriptToken>* pTokens = nullptr;
  size_t pos = 0;
  bool genDebug = true;

  std::unordered_map<std::string, VarSym> vars;
  std::vector<std::string> aVarNames; // by slot, for debug info
  std::vector<STORAGE_TYPE> aVarStorage;
  std::vector<std::string> aArrayNames;
  std::vector<STORAGE_TYPE> aArrayStorage;
  std::vector<ARRAY_DATA> asArrays;

  std::unordered_map<std::string, UDWORD> triggerIds;
  std::vector<TriggerSym> triggers;
  std::unordered_map<std::string, UDWORD> eventIds;
  std::vector<EventSym> events;
  std::unordered_map<std::string, UDWORD> funcIds;
  std::vector<FuncSym> funcs;
  std::vector<INTERP_TYPE> aParamTypes; // flat, all functions

  std::unordered_map<std::string, UDWORD> instinctMap;
  std::unordered_map<std::string, UDWORD> callbackMap;
  std::unordered_map<std::string, UDWORD> constMap;
  std::unordered_map<std::string, UDWORD> externMap;
  std::unordered_map<std::string, INTERP_TYPE> typeMap;

  UWORD numGlobals = 0;
  UDWORD arraySize = 0; // total elements across arrays; known after var section

  FuncSym* psCurFunc = nullptr; // non-null while compiling a function body
  std::unordered_map<std::string, VarSym> paramVars; // current function's params
};

/* ------------------------------------------------------------------ */
/* Token helpers */

const ScriptToken& Peek(Compiler& _c, size_t _ahead = 0)
{
  const size_t i = _c.pos + _ahead;
  return (*_c.pTokens)[i < _c.pTokens->size() ? i : _c.pTokens->size() - 1];
}

const ScriptToken& Take(Compiler& _c)
{
  const ScriptToken& sTok = Peek(_c);
  if (sTok.type != ScriptTok::End)
    _c.pos += 1;
  return sTok;
}

[[noreturn]] void Err(const ScriptToken& _tok, const std::string& _what)
{
  Neuron::Fatal("script compile error at line {} col {}: {}", _tok.line, _tok.col, _what);
}

const ScriptToken& Expect(Compiler& _c, ScriptTok _type, const char* _context)
{
  const ScriptToken& sTok = Peek(_c);
  if (sTok.type != _type)
    Err(sTok, std::string("expected '") + ScriptTokName(_type) + "' " + _context + ", got '" +
        (sTok.type == ScriptTok::Ident ? sTok.text : ScriptTokName(sTok.type)) + "'");
  return Take(_c);
}

bool Accept(Compiler& _c, ScriptTok _type)
{
  if (Peek(_c).type != _type)
    return false;
  Take(_c);
  return true;
}

/* ------------------------------------------------------------------ */
/* Type helpers */

TypeKind KindOf(INTERP_TYPE _type)
{
  if (_type == VAL_INT)
    return TypeKind::Int;
  if (_type == VAL_BOOL)
    return TypeKind::Bool;
  if (asScrTypeTab)
  {
    for (UDWORD i = 0; asScrTypeTab[i].typeID != 0; i++)
    {
      if (asScrTypeTab[i].typeID == _type)
        return asScrTypeTab[i].accessType == AT_OBJECT ? TypeKind::Object : TypeKind::User;
    }
  }
  return TypeKind::User; // triggers, events, unlisted user ids
}

const char* TypeName(INTERP_TYPE _type)
{
  switch (_type)
  {
  case VAL_BOOL: return "BOOL";
  case VAL_INT: return "INT";
  case VAL_STRING: return "STRING";
  case VAL_TRIGGER: return "TRIGGER";
  case VAL_EVENT: return "EVENT";
  case VAL_VOID: return "VOID";
  default: break;
  }
  if (asScrTypeTab)
  {
    for (UDWORD i = 0; asScrTypeTab[i].typeID != 0; i++)
    {
      if (asScrTypeTab[i].typeID == _type)
        return asScrTypeTab[i].pIdent;
    }
  }
  return "user type";
}

/* ------------------------------------------------------------------ */
/* Code emission helpers */

void Emit(Block& _block, OPCODE _op, INTERP_TYPE _type = 0, SDWORD _data = 0)
{
  ScriptInstr sInstr;
  sInstr.op = _op;
  sInstr.type = _type;
  sInstr.data = _data;
  sInstr.arg.ival = 0;
  _block.aCode.push_back(sInstr);
}

ScriptInstr& LastInstr(Block& _block) { return _block.aCode.back(); }

void Append(Block& _dest, Block& _src)
{
  const UDWORD base = static_cast<UDWORD>(_dest.aCode.size());
  _dest.aCode.insert(_dest.aCode.end(), _src.aCode.begin(), _src.aCode.end());
  for (SCRIPT_DEBUG& sEntry : _src.aDebug)
  {
    sEntry.offset += base;
    _dest.aDebug.push_back(std::move(sEntry));
  }
}

void MarkLine(Compiler& _c, Block& _block, UDWORD _line)
{
  if (!_c.genDebug)
    return;
  SCRIPT_DEBUG sEntry;
  sEntry.offset = static_cast<UDWORD>(_block.aCode.size());
  sEntry.line = _line;
  _block.aDebug.push_back(std::move(sEntry));
}

/* Read of a script variable / function parameter / extern */
void EmitVarGet(const ScriptToken& _at, Block& _block, const VarSym& _var)
{
  if (_var.storage == ST_EXTERN)
  {
    if (asScrExternalTab[_var.index].get == nullptr)
      Err(_at, "no get function for external variable");
    Emit(_block, OP_VARCALL, 0, static_cast<SDWORD>(asScrExternalTab[_var.index].index));
    LastInstr(_block).arg.func = _var.index & INSTR_SLOTMASK;
  }
  else
    Emit(_block, OP_PUSHGLOBAL, 0, static_cast<SDWORD>(_var.index));
  _block.type = _var.type;
}

/* ------------------------------------------------------------------ */
/* Identifier resolution */

enum class IdentKind
{
  Unknown,
  Type,
  Var, // script variable (plain), or current function's parameter
  Array, // script array
  Extern,
  Const,
  Instinct,
  ScriptFunc,
  Trigger,
  Event,
  Callback,
};

/* Mirrors the old lexer's classification order: type, variable, constant,
   function, trigger, event, callback. */
IdentKind Resolve(Compiler& _c, const std::string& _name, UDWORD* _pIndex)
{
  if (const auto it = _c.typeMap.find(_name); it != _c.typeMap.end())
  {
    *_pIndex = static_cast<UDWORD>(it->second);
    return IdentKind::Type;
  }
  if (_c.psCurFunc != nullptr)
  {
    if (const auto it = _c.paramVars.find(_name); it != _c.paramVars.end())
    {
      *_pIndex = it->second.index;
      return IdentKind::Var;
    }
  }
  if (const auto it = _c.externMap.find(_name); it != _c.externMap.end())
  {
    *_pIndex = it->second;
    return IdentKind::Extern;
  }
  if (const auto it = _c.vars.find(_name); it != _c.vars.end())
  {
    *_pIndex = it->second.index;
    return it->second.dims > 0 ? IdentKind::Array : IdentKind::Var;
  }
  if (const auto it = _c.constMap.find(_name); it != _c.constMap.end())
  {
    *_pIndex = it->second;
    return IdentKind::Const;
  }
  if (const auto it = _c.instinctMap.find(_name); it != _c.instinctMap.end())
  {
    *_pIndex = it->second;
    return IdentKind::Instinct;
  }
  if (const auto it = _c.funcIds.find(_name); it != _c.funcIds.end())
  {
    *_pIndex = it->second;
    return IdentKind::ScriptFunc;
  }
  if (const auto it = _c.triggerIds.find(_name); it != _c.triggerIds.end())
  {
    *_pIndex = it->second;
    return IdentKind::Trigger;
  }
  if (const auto it = _c.eventIds.find(_name); it != _c.eventIds.end())
  {
    *_pIndex = it->second;
    return IdentKind::Event;
  }
  if (const auto it = _c.callbackMap.find(_name); it != _c.callbackMap.end())
  {
    *_pIndex = it->second;
    return IdentKind::Callback;
  }
  return IdentKind::Unknown;
}

const VarSym* FindVar(Compiler& _c, const std::string& _name)
{
  if (_c.psCurFunc != nullptr)
  {
    if (const auto it = _c.paramVars.find(_name); it != _c.paramVars.end())
      return &it->second;
  }
  if (const auto it = _c.vars.find(_name); it != _c.vars.end())
    return &it->second;
  return nullptr;
}

/* ------------------------------------------------------------------ */
/* Expressions */

Block ParseExpr(Compiler& _c, int _minPrec);
Block ParseCall(Compiler& _c, const ScriptToken& _name, IdentKind _kind, UDWORD _index, bool _exprContext);

/* Object member lookup, filtered by the object expression's type as the
   old parser's objVarContext did */
SDWORD LookUpObjVar(INTERP_TYPE _objType, const std::string& _name)
{
  if (asScrObjectVarTab == nullptr)
    return -1;
  for (SDWORD i = 0; asScrObjectVarTab[i].pIdent != nullptr; i++)
  {
    if (interpCheckEquiv(asScrObjectVarTab[i].objType, _objType) && _name == asScrObjectVarTab[i].pIdent)
      return i;
  }
  return -1;
}

/* Array indexing: emits the index expressions and checks the dimension
   count; returns the index code */
Block ParseArrayIndex(Compiler& _c, const ScriptToken& _at, const VarSym& _array)
{
  Block sIndex;
  SDWORD dims = 0;
  while (Accept(_c, ScriptTok::LBracket))
  {
    Block sOne = ParseExpr(_c, 0);
    if (sOne.type != VAL_INT)
      Err(_at, "array index must be INT");
    Expect(_c, ScriptTok::RBracket, "after array index");
    Append(sIndex, sOne);
    dims += 1;
  }
  if (dims != _array.dims)
    Err(_at, "invalid number of array dimensions for this variable");
  return sIndex;
}

/* Postfix '.' member accesses on an object expression (reads) */
Block ParseMemberGets(Compiler& _c, Block _base)
{
  while (Peek(_c).type == ScriptTok::Dot)
  {
    const ScriptToken& sDot = Take(_c);
    if (KindOf(_base.type) != TypeKind::Object)
      Err(sDot, "'.' needs an object value on its left");
    const ScriptToken& sMember = Expect(_c, ScriptTok::Ident, "after '.'");
    const SDWORD slot = LookUpObjVar(_base.type, sMember.text);
    if (slot < 0)
      Err(sMember, "unknown member '" + sMember.text + "' for this object type");
    if (asScrObjectVarTab[slot].get == nullptr)
      Err(sMember, "no get function for object variable");
    Emit(_base, OP_VARCALL, 0, static_cast<SDWORD>(asScrObjectVarTab[slot].index));
    LastInstr(_base).arg.func = INSTR_OBJVARFLAG | (static_cast<UDWORD>(slot) & INSTR_SLOTMASK);
    _base.type = asScrObjectVarTab[slot].type;
  }
  return _base;
}

Block ParsePrimary(Compiler& _c)
{
  const ScriptToken sTok = Peek(_c);
  Block sBlock;

  switch (sTok.type)
  {
  case ScriptTok::Integer:
    Take(_c);
    Emit(sBlock, OP_PUSH, VAL_INT);
    LastInstr(sBlock).arg.ival = sTok.ival;
    sBlock.type = VAL_INT;
    return sBlock;

  case ScriptTok::KwTrue:
  case ScriptTok::KwFalse:
    Take(_c);
    Emit(sBlock, OP_PUSH, VAL_BOOL);
    LastInstr(sBlock).arg.ival = (sTok.type == ScriptTok::KwTrue) ? 1 : 0;
    sBlock.type = VAL_BOOL;
    return sBlock;

  case ScriptTok::KwInactive:
    // an "inactive" trigger value
    Take(_c);
    Emit(sBlock, OP_PUSH, VAL_TRIGGER);
    LastInstr(sBlock).arg.ival = -1;
    sBlock.type = VAL_TRIGGER;
    return sBlock;

  case ScriptTok::LParen:
    {
      Take(_c);
      Block sInner = ParseExpr(_c, 0);
      Expect(_c, ScriptTok::RParen, "to close '('");
      return ParseMemberGets(_c, std::move(sInner));
    }

  case ScriptTok::Minus:
    {
      Take(_c);
      Block sOperand = ParsePrimary(_c);
      if (sOperand.type != VAL_INT)
        Err(sTok, "unary '-' needs an INT operand");
      Emit(sOperand, OP_UNARYOP, 0, OP_NEG);
      sOperand.type = VAL_INT;
      return sOperand;
    }

  case ScriptTok::KwNot:
    {
      Take(_c);
      Block sOperand = ParsePrimary(_c);
      if (sOperand.type != VAL_BOOL)
        Err(sTok, "'not' needs a BOOL operand");
      Emit(sOperand, OP_UNARYOP, 0, OP_NOT);
      sOperand.type = VAL_BOOL;
      return sOperand;
    }

  case ScriptTok::Ident:
    {
      Take(_c);
      UDWORD index = 0;
      const IdentKind kind = Resolve(_c, sTok.text, &index);
      switch (kind)
      {
      case IdentKind::Var:
        {
          const VarSym* psVar = FindVar(_c, sTok.text);
          EmitVarGet(sTok, sBlock, *psVar);
          return ParseMemberGets(_c, std::move(sBlock));
        }
      case IdentKind::Extern:
        {
          VarSym sExtern;
          sExtern.type = asScrExternalTab[index].type;
          sExtern.storage = ST_EXTERN;
          sExtern.index = index;
          EmitVarGet(sTok, sBlock, sExtern);
          return ParseMemberGets(_c, std::move(sBlock));
        }
      case IdentKind::Array:
        {
          const VarSym& sArray = _c.vars.find(sTok.text)->second;
          Block sIndex = ParseArrayIndex(_c, sTok, sArray);
          Append(sBlock, sIndex);
          Emit(sBlock, OP_PUSHARRAYGLOBAL, sArray.dims, static_cast<SDWORD>(sArray.index));
          sBlock.type = sArray.type;
          return ParseMemberGets(_c, std::move(sBlock));
        }
      case IdentKind::Const:
        {
          const CONST_SYMBOL& sConst = asScrConstantTab[index];
          Emit(sBlock, OP_PUSH, sConst.type);
          if (sConst.type == VAL_BOOL)
            LastInstr(sBlock).arg.ival = sConst.bval;
          else if (sConst.type == VAL_INT)
            LastInstr(sBlock).arg.ival = sConst.ival;
          else
            LastInstr(sBlock).arg.oval = sConst.oval;
          sBlock.type = sConst.type;
          return ParseMemberGets(_c, std::move(sBlock));
        }
      case IdentKind::Instinct:
      case IdentKind::ScriptFunc:
        return ParseMemberGets(_c, ParseCall(_c, sTok, kind, index, true));
      case IdentKind::Trigger:
        Emit(sBlock, OP_PUSH, VAL_TRIGGER);
        LastInstr(sBlock).arg.ival = static_cast<SDWORD>(index);
        sBlock.type = VAL_TRIGGER;
        return sBlock;
      case IdentKind::Event:
        Emit(sBlock, OP_PUSH, VAL_EVENT);
        LastInstr(sBlock).arg.ival = static_cast<SDWORD>(index);
        sBlock.type = VAL_EVENT;
        return sBlock;
      default:
        Err(sTok, "unknown identifier '" + sTok.text + "'");
      }
    }

  default:
    Err(sTok, std::string("unexpected '") + ScriptTokName(sTok.type) + "' in expression");
  }
}

struct BinOp
{
  ScriptTok tok;
  int prec;
  OPCODE op;
};

constexpr BinOp kBinOps[] = {
  {ScriptTok::KwOr, 1, OP_OR},
  {ScriptTok::KwAnd, 2, OP_AND},
  {ScriptTok::Equal, 3, OP_EQUAL},
  {ScriptTok::NotEqual, 3, OP_NOTEQUAL},
  {ScriptTok::GreaterEqual, 4, OP_GREATEREQUAL},
  {ScriptTok::LessEqual, 4, OP_LESSEQUAL},
  {ScriptTok::Greater, 4, OP_GREATER},
  {ScriptTok::Less, 4, OP_LESS},
  {ScriptTok::Plus, 5, OP_ADD},
  {ScriptTok::Minus, 5, OP_SUB},
  {ScriptTok::Star, 6, OP_MUL},
  {ScriptTok::Slash, 6, OP_DIV},
};

const BinOp* FindBinOp(ScriptTok _tok)
{
  for (const BinOp& sOp : kBinOps)
  {
    if (sOp.tok == _tok)
      return &sOp;
  }
  return nullptr;
}

/* The typing rules per operator group match the old grammar exactly:
   arithmetic and orderings are INT only; and/or are BOOL only; equality
   accepts INT pairs, BOOL pairs, or user/object pairs that pass the
   equivalence table. */
INTERP_TYPE CheckBinOp(const ScriptToken& _at, OPCODE _op, INTERP_TYPE _left, INTERP_TYPE _right)
{
  switch (_op)
  {
  case OP_ADD:
  case OP_SUB:
  case OP_MUL:
  case OP_DIV:
    if (_left != VAL_INT || _right != VAL_INT)
      Err(_at, std::string("arithmetic needs INT operands, got ") + TypeName(_left) + " and " + TypeName(_right));
    return VAL_INT;
  case OP_GREATEREQUAL:
  case OP_LESSEQUAL:
  case OP_GREATER:
  case OP_LESS:
    if (_left != VAL_INT || _right != VAL_INT)
      Err(_at, std::string("comparison needs INT operands, got ") + TypeName(_left) + " and " + TypeName(_right));
    return VAL_BOOL;
  case OP_AND:
  case OP_OR:
    if (_left != VAL_BOOL || _right != VAL_BOOL)
      Err(_at, std::string("boolean operator needs BOOL operands, got ") + TypeName(_left) + " and " + TypeName(_right));
    return VAL_BOOL;
  case OP_EQUAL:
  case OP_NOTEQUAL:
    {
      const TypeKind left = KindOf(_left);
      const TypeKind right = KindOf(_right);
      if (left == TypeKind::Int && right == TypeKind::Int)
        return VAL_BOOL;
      if (left == TypeKind::Bool && right == TypeKind::Bool)
        return VAL_BOOL;
      if ((left == TypeKind::Object || left == TypeKind::User) && left == right && interpCheckEquiv(_left, _right))
        return VAL_BOOL;
      Err(_at, std::string("type mismatch for equality: ") + TypeName(_left) + " and " + TypeName(_right));
    }
  default:
    Err(_at, "unknown operator");
  }
}

Block ParseExpr(Compiler& _c, int _minPrec)
{
  Block sLeft = ParsePrimary(_c);

  while (true)
  {
    const ScriptToken& sTok = Peek(_c);
    const BinOp* psOp = FindBinOp(sTok.type);
    if (psOp == nullptr || psOp->prec < _minPrec)
      return sLeft;
    Take(_c);

    Block sRight = ParseExpr(_c, psOp->prec + 1);
    const INTERP_TYPE result = CheckBinOp(sTok, psOp->op, sLeft.type, sRight.type);

    Block sOut;
    Append(sOut, sLeft);
    Append(sOut, sRight);
    Emit(sOut, OP_BINARYOP, 0, psOp->op);
    sOut.type = result;
    sLeft = std::move(sOut);
  }
}

/* ------------------------------------------------------------------ */
/* Calls */

/* Parses '(' params ')' and emits parameter pushes followed by the call.
   _exprContext false adds the discard POP for a non-void result. */
Block ParseCall(Compiler& _c, const ScriptToken& _name, IdentKind _kind, UDWORD _index, bool _exprContext)
{
  const bool isScript = (_kind == IdentKind::ScriptFunc);
  const FUNC_SYMBOL* psInstinct = isScript ? nullptr : &asScrInstinctTab[_index];
  FuncSym* psScript = isScript ? &_c.funcs[_index] : nullptr;
  const UDWORD numParams = isScript ? psScript->numParams : psInstinct->numParams;
  const INTERP_TYPE* aParamTypes = isScript ? psScript->aParams : psInstinct->aParams;
  const INTERP_TYPE returnType = isScript ? psScript->returnType : psInstinct->type;

  Expect(_c, ScriptTok::LParen, "to open the parameter list");

  Block sBlock;
  UDWORD count = 0;
  if (!Accept(_c, ScriptTok::RParen))
  {
    do
    {
      const ScriptToken& sAt = Peek(_c);
      INTERP_TYPE paramType;
      if (Accept(_c, ScriptTok::KwRef))
      {
        // reference to a script global / parameter
        const ScriptToken& sVarTok = Expect(_c, ScriptTok::Ident, "after 'ref'");
        const VarSym* psVar = FindVar(_c, sVarTok.text);
        if (psVar == nullptr || psVar->dims != 0)
          Err(sVarTok, "'ref' needs a script variable");
        if (psVar->storage == ST_EXTERN)
          Err(sVarTok, "cannot use external variables in this context");
        paramType = psVar->type | VAL_REF;
        Emit(sBlock, OP_PUSHREF, paramType, static_cast<SDWORD>(psVar->index));
      }
      else
      {
        Block sParam = ParseExpr(_c, 0);
        paramType = sParam.type;
        Append(sBlock, sParam);
      }

      if (count < numParams && !interpCheckEquiv(aParamTypes[count], paramType))
        Err(sAt, "type mismatch for parameter " + std::to_string(count) +
            ": expected " + TypeName(aParamTypes[count] & ~VAL_REF) + ", got " + TypeName(paramType & ~VAL_REF));
      count += 1;
    }
    while (Accept(_c, ScriptTok::Comma));
    Expect(_c, ScriptTok::RParen, "to close the parameter list");
  }

  if (count != numParams)
    Err(_name, "expected " + std::to_string(numParams) + " parameters, got " + std::to_string(count));

  if (isScript)
  {
    Emit(sBlock, OP_SCRIPTCALL, 0, static_cast<SDWORD>(numParams));
    LastInstr(sBlock).arg.func = _index;
  }
  else
  {
    Emit(sBlock, OP_CALL);
    LastInstr(sBlock).arg.func = _index & INSTR_SLOTMASK;
  }
  sBlock.type = returnType;

  if (!_exprContext && returnType != VAL_VOID)
  {
    Emit(sBlock, OP_POP);
    sBlock.type = VAL_VOID;
  }

  return sBlock;
}

/* ------------------------------------------------------------------ */
/* Statements */

Block ParseStatementList(Compiler& _c);

/* Parses an assignable place: variable, member chain or array element */
Place ParsePlace(Compiler& _c)
{
  const ScriptToken sTok = Expect(_c, ScriptTok::Ident, "at the start of a statement");
  UDWORD index = 0;
  const IdentKind kind = Resolve(_c, sTok.text, &index);
  Place sPlace;

  switch (kind)
  {
  case IdentKind::Var:
    {
      const VarSym* psVar = FindVar(_c, sTok.text);
      sPlace.kind = Place::Kind::Global;
      sPlace.type = psVar->type;
      sPlace.index = psVar->index;
      break;
    }
  case IdentKind::Extern:
    sPlace.kind = Place::Kind::Extern;
    sPlace.type = asScrExternalTab[index].type;
    sPlace.slot = index;
    sPlace.index = asScrExternalTab[index].index;
    break;
  case IdentKind::Array:
    {
      const VarSym& sArray = _c.vars.find(sTok.text)->second;
      sPlace.kind = Place::Kind::ArrayElem;
      sPlace.type = sArray.type;
      sPlace.index = sArray.index;
      sPlace.dims = sArray.dims;
      sPlace.sPrefix = ParseArrayIndex(_c, sTok, sArray);
      break;
    }
  default:
    Err(sTok, "'" + sTok.text + "' cannot be assigned to");
  }

  /* Member chain: everything up to the last '.' is an object read, the
     final member is the store target */
  while (Peek(_c).type == ScriptTok::Dot)
  {
    // Build the object-read code for the current place
    Block sObj;
    switch (sPlace.kind)
    {
    case Place::Kind::Global:
      Emit(sObj, OP_PUSHGLOBAL, 0, static_cast<SDWORD>(sPlace.index));
      break;
    case Place::Kind::Extern:
      Emit(sObj, OP_VARCALL, 0, static_cast<SDWORD>(sPlace.index));
      LastInstr(sObj).arg.func = sPlace.slot & INSTR_SLOTMASK;
      break;
    case Place::Kind::ArrayElem:
      Append(sObj, sPlace.sPrefix);
      Emit(sObj, OP_PUSHARRAYGLOBAL, sPlace.dims, static_cast<SDWORD>(sPlace.index));
      break;
    case Place::Kind::ObjMember:
      sObj = std::move(sPlace.sPrefix);
      Emit(sObj, OP_VARCALL, 0, static_cast<SDWORD>(asScrObjectVarTab[sPlace.slot].index));
      LastInstr(sObj).arg.func = INSTR_OBJVARFLAG | (sPlace.slot & INSTR_SLOTMASK);
      break;
    }

    const ScriptToken& sDot = Take(_c);
    if (KindOf(sPlace.type) != TypeKind::Object)
      Err(sDot, "'.' needs an object value on its left");
    const ScriptToken& sMember = Expect(_c, ScriptTok::Ident, "after '.'");
    const SDWORD slot = LookUpObjVar(sPlace.type, sMember.text);
    if (slot < 0)
      Err(sMember, "unknown member '" + sMember.text + "' for this object type");

    Place sNext;
    sNext.kind = Place::Kind::ObjMember;
    sNext.type = asScrObjectVarTab[slot].type;
    sNext.slot = static_cast<UDWORD>(slot);
    sNext.objVar = true;
    sNext.sPrefix = std::move(sObj);
    sPlace = std::move(sNext);
  }

  return sPlace;
}

/* value-first, then object/index code, then the store - the operand order
   the accessor functions rely on */
Block EmitStore(Compiler& _c, const ScriptToken& _at, Place& _place, Block& _value)
{
  (void)_c;
  if (!interpCheckEquiv(_place.type, _value.type))
    Err(_at, std::string("type mismatch for assignment: expected ") + TypeName(_place.type) + ", got " + TypeName(_value.type));

  Block sOut;
  Append(sOut, _value);

  switch (_place.kind)
  {
  case Place::Kind::Global:
    Emit(sOut, OP_POPGLOBAL, 0, static_cast<SDWORD>(_place.index));
    break;
  case Place::Kind::Extern:
    if (asScrExternalTab[_place.slot].set == nullptr)
      Err(_at, "no set function for external variable");
    Emit(sOut, OP_VARCALL, 0, static_cast<SDWORD>(_place.index));
    LastInstr(sOut).arg.func = INSTR_VARSETFLAG | (_place.slot & INSTR_SLOTMASK);
    break;
  case Place::Kind::ObjMember:
    if (asScrObjectVarTab[_place.slot].set == nullptr)
      Err(_at, "no set function for object variable");
    Append(sOut, _place.sPrefix);
    Emit(sOut, OP_VARCALL, 0, static_cast<SDWORD>(asScrObjectVarTab[_place.slot].index));
    LastInstr(sOut).arg.func = INSTR_OBJVARFLAG | INSTR_VARSETFLAG | (_place.slot & INSTR_SLOTMASK);
    break;
  case Place::Kind::ArrayElem:
    Append(sOut, _place.sPrefix);
    Emit(sOut, OP_POPARRAYGLOBAL, _place.dims, static_cast<SDWORD>(_place.index));
    break;
  }

  return sOut;
}

Block ParseStatement(Compiler& _c)
{
  const ScriptToken sTok = Peek(_c);
  Block sBlock;

  switch (sTok.type)
  {
  case ScriptTok::KwIf:
    {
      /* Each clause: cond, JUMPFALSE past body and end-jump, body,
         JUMP-to-end.  All end-jumps are patched to the end of the chain. */
      std::vector<UDWORD> aEndJumps;
      bool moreClauses = true;
      while (moreClauses)
      {
        Take(_c); // 'if'
        Expect(_c, ScriptTok::LParen, "after 'if'");
        MarkLine(_c, sBlock, Peek(_c).line);
        Block sCond = ParseExpr(_c, 0);
        if (sCond.type != VAL_BOOL)
          Err(sTok, "'if' condition must be BOOL");
        Expect(_c, ScriptTok::RParen, "to close the 'if' condition");
        Expect(_c, ScriptTok::LBrace, "to open the 'if' body");
        Block sBody = ParseStatementList(_c);
        Expect(_c, ScriptTok::RBrace, "to close the 'if' body");

        Append(sBlock, sCond);
        Emit(sBlock, OP_JUMPFALSE, 0, static_cast<SDWORD>(sBody.aCode.size()) + 2);
        Append(sBlock, sBody);
        aEndJumps.push_back(static_cast<UDWORD>(sBlock.aCode.size()));
        Emit(sBlock, OP_JUMP, 0, 0); // patched below

        moreClauses = false;
        if (Peek(_c).type == ScriptTok::KwElse)
        {
          if (Peek(_c, 1).type == ScriptTok::KwIf)
          {
            Take(_c); // 'else', loop continues with 'if'
            moreClauses = true;
          }
          else
          {
            Take(_c); // 'else'
            Expect(_c, ScriptTok::LBrace, "to open the 'else' body");
            Block sElse = ParseStatementList(_c);
            Expect(_c, ScriptTok::RBrace, "to close the 'else' body");
            Append(sBlock, sElse);
          }
        }
      }

      /* The final clause's end-jump is redundant (it jumps one
         instruction) when there is no else; the old compiler emitted it
         too, so keep the shape identical and patch them all. */
      for (const UDWORD site : aEndJumps)
        sBlock.aCode[site].data = static_cast<SDWORD>(sBlock.aCode.size()) - static_cast<SDWORD>(site);
      return sBlock;
    }

  case ScriptTok::KwWhile:
    {
      Take(_c);
      Expect(_c, ScriptTok::LParen, "after 'while'");
      MarkLine(_c, sBlock, sTok.line);
      Block sCond = ParseExpr(_c, 0);
      if (sCond.type != VAL_BOOL)
        Err(sTok, "'while' condition must be BOOL");
      Expect(_c, ScriptTok::RParen, "to close the 'while' condition");
      Expect(_c, ScriptTok::LBrace, "to open the 'while' body");
      Block sBody = ParseStatementList(_c);
      Expect(_c, ScriptTok::RBrace, "to close the 'while' body");

      Append(sBlock, sCond);
      Emit(sBlock, OP_JUMPFALSE, 0, static_cast<SDWORD>(sBody.aCode.size()) + 2);
      Append(sBlock, sBody);
      Emit(sBlock, OP_JUMP, 0, -static_cast<SDWORD>(sBlock.aCode.size()) + 0);
      // the jump lands on the first condition instruction: delta is
      // -(instructions before it), and it was emitted after size-1 of them
      sBlock.aCode.back().data = -(static_cast<SDWORD>(sBlock.aCode.size()) - 1);
      return sBlock;
    }

  case ScriptTok::KwExit:
    Take(_c);
    Expect(_c, ScriptTok::Semicolon, "after 'exit'");
    MarkLine(_c, sBlock, sTok.line);
    Emit(sBlock, OP_EXIT);
    return sBlock;

  case ScriptTok::KwPause:
    {
      Take(_c);
      if (_c.psCurFunc != nullptr)
        Err(sTok, "'pause' is not allowed inside a function");
      Expect(_c, ScriptTok::LParen, "after 'pause'");
      const ScriptToken& sTime = Expect(_c, ScriptTok::Integer, "as the pause time");
      Expect(_c, ScriptTok::RParen, "to close 'pause'");
      Expect(_c, ScriptTok::Semicolon, "after 'pause'");
      MarkLine(_c, sBlock, sTok.line);
      Emit(sBlock, OP_PAUSE, 0, sTime.ival);
      return sBlock;
    }

  case ScriptTok::KwReturn:
    {
      Take(_c);
      if (_c.psCurFunc == nullptr)
        Err(sTok, "'return' is only allowed inside a function");
      MarkLine(_c, sBlock, sTok.line);
      if (!Accept(_c, ScriptTok::Semicolon))
      {
        Block sValue = ParseExpr(_c, 0);
        Expect(_c, ScriptTok::Semicolon, "after 'return'");
        if (_c.psCurFunc->returnType == VAL_VOID)
          Err(sTok, "a void function cannot return a value");
        if (!interpCheckEquiv(_c.psCurFunc->returnType, sValue.type))
          Err(sTok, std::string("return type mismatch: expected ") + TypeName(_c.psCurFunc->returnType) + ", got " + TypeName(sValue.type));
        Append(sBlock, sValue);
      }
      else if (_c.psCurFunc->returnType != VAL_VOID)
        Err(sTok, "this function must return a value");
      Emit(sBlock, OP_SCRIPTRET);
      return sBlock;
    }

  case ScriptTok::Ident:
    {
      /* A call or an assignment.  A call is an identifier naming a
         function followed by '('; everything else is an assignment. */
      UDWORD index = 0;
      const IdentKind kind = Resolve(_c, sTok.text, &index);
      if ((kind == IdentKind::Instinct || kind == IdentKind::ScriptFunc) && Peek(_c, 1).type == ScriptTok::LParen)
      {
        Take(_c);
        MarkLine(_c, sBlock, sTok.line);
        Block sCall = ParseCall(_c, sTok, kind, index, false);
        Expect(_c, ScriptTok::Semicolon, "after the call");
        Append(sBlock, sCall);
        return sBlock;
      }

      MarkLine(_c, sBlock, sTok.line);
      Place sPlace = ParsePlace(_c);
      const ScriptToken& sAssign = Expect(_c, ScriptTok::Assign, "in the assignment");
      Block sValue = ParseExpr(_c, 0);
      Expect(_c, ScriptTok::Semicolon, "after the assignment");
      Block sStore = EmitStore(_c, sAssign, sPlace, sValue);
      Append(sBlock, sStore);
      return sBlock;
    }

  default:
    Err(sTok, std::string("unexpected '") + ScriptTokName(sTok.type) + "' at the start of a statement");
  }
}

Block ParseStatementList(Compiler& _c)
{
  Block sBlock;
  while (Peek(_c).type != ScriptTok::RBrace && Peek(_c).type != ScriptTok::End)
  {
    Block sStmt = ParseStatement(_c);
    Append(sBlock, sStmt);
  }
  return sBlock;
}

/* ------------------------------------------------------------------ */
/* Declarations */

/* A type name: the int/bool base types, the trigger/event keywords used
   as types, or a user type from the game's table.  int and bool are
   keywords rather than table entries - asTypeTable holds only the user
   types - which is how the lexer this replaced handled them too. */
INTERP_TYPE ParseTypeName(Compiler& _c)
{
  const ScriptToken& sTok = Take(_c);
  if (sTok.type == ScriptTok::KwInt)
    return VAL_INT;
  if (sTok.type == ScriptTok::KwBool)
    return VAL_BOOL;
  if (sTok.type == ScriptTok::KwTrigger)
    return VAL_TRIGGER;
  if (sTok.type == ScriptTok::KwEvent)
    return VAL_EVENT;
  if (sTok.type == ScriptTok::Ident)
  {
    if (const auto it = _c.typeMap.find(sTok.text); it != _c.typeMap.end())
      return it->second;
    Err(sTok, "unknown type '" + sTok.text + "'");
  }
  Err(sTok, std::string("expected a type, got '") + ScriptTokName(sTok.type) + "'");
}

void ParseVarDecl(Compiler& _c, STORAGE_TYPE _storage)
{
  const INTERP_TYPE type = ParseTypeName(_c);

  do
  {
    const ScriptToken& sName = Expect(_c, ScriptTok::Ident, "as the variable name");
    UDWORD dummy = 0;
    if (Resolve(_c, sName.text, &dummy) != IdentKind::Unknown)
      Err(sName, "'" + sName.text + "' is already defined");

    VarSym sVar;
    sVar.type = type;
    sVar.storage = _storage;

    while (Accept(_c, ScriptTok::LBracket))
    {
      if (sVar.dims >= VAR_MAX_DIMENSIONS)
        Err(sName, "too many dimensions for array");
      const ScriptToken& sSize = Expect(_c, ScriptTok::Integer, "as the array size");
      if (sSize.ival <= 0 || sSize.ival >= VAR_MAX_ELEMENTS)
        Err(sSize, "invalid array size " + std::to_string(sSize.ival));
      Expect(_c, ScriptTok::RBracket, "after the array size");
      sVar.elements[sVar.dims] = sSize.ival;
      sVar.dims += 1;
    }

    if (sVar.dims == 0)
    {
      sVar.index = _c.numGlobals;
      _c.numGlobals += 1;
      _c.aVarNames.push_back(sName.text);
      _c.aVarStorage.push_back(_storage);
    }
    else
    {
      sVar.index = static_cast<UDWORD>(_c.asArrays.size());
      ARRAY_DATA sData;
      sData.base = 0; // filled in once all declarations are known
      sData.type = static_cast<UBYTE>(type);
      sData.dimensions = static_cast<UBYTE>(sVar.dims);
      for (SDWORD i = 0; i < VAR_MAX_DIMENSIONS; i++)
        sData.elements[i] = static_cast<UBYTE>(i < sVar.dims ? sVar.elements[i] : 0);
      _c.asArrays.push_back(sData);
      _c.aArrayNames.push_back(sName.text);
      _c.aArrayStorage.push_back(_storage);
    }
    _c.vars.emplace(sName.text, sVar);
  }
  while (Accept(_c, ScriptTok::Comma));

  Expect(_c, ScriptTok::Semicolon, "after the variable declaration");
}

void ParseFunctionDef(Compiler& _c)
{
  const ScriptToken& sKw = Take(_c); // 'function'
  (void)sKw;

  INTERP_TYPE returnType = VAL_VOID;
  if (!Accept(_c, ScriptTok::KwVoid))
  {
    if (Peek(_c).type == ScriptTok::Ident && Peek(_c, 1).type == ScriptTok::Ident)
      returnType = ParseTypeName(_c);
    // else: "function name(...)" with no type reads as void
  }

  const ScriptToken& sName = Expect(_c, ScriptTok::Ident, "as the function name");
  UDWORD dummy = 0;
  if (Resolve(_c, sName.text, &dummy) != IdentKind::Unknown)
    Err(sName, "'" + sName.text + "' is already defined");

  FuncSym sFunc;
  sFunc.name = sName.text;
  sFunc.returnType = returnType;
  sFunc.line = sName.line;
  sFunc.firstParamSlot = static_cast<UDWORD>(_c.numGlobals + _c.arraySize + _c.aParamTypes.size());

  _c.paramVars.clear();
  Expect(_c, ScriptTok::LParen, "to open the parameter list");
  if (!Accept(_c, ScriptTok::RParen))
  {
    do
    {
      if (sFunc.numParams >= INST_MAXPARAMS)
        Err(sName, "too many parameters");
      const INTERP_TYPE paramType = ParseTypeName(_c);
      const ScriptToken& sParam = Expect(_c, ScriptTok::Ident, "as the parameter name");
      if (_c.paramVars.count(sParam.text) != 0)
        Err(sParam, "duplicate parameter '" + sParam.text + "'");

      VarSym sVar;
      sVar.type = paramType;
      sVar.storage = ST_PRIVATE;
      sVar.index = sFunc.firstParamSlot + sFunc.numParams;
      _c.paramVars.emplace(sParam.text, sVar);

      sFunc.aParams[sFunc.numParams] = paramType;
      sFunc.numParams += 1;
    }
    while (Accept(_c, ScriptTok::Comma));
    Expect(_c, ScriptTok::RParen, "to close the parameter list");
  }

  /* Register before the body so the name is known (but recursion is
     refused at runtime, so a self-call will compile and then fail) */
  const UDWORD index = static_cast<UDWORD>(_c.funcs.size());
  _c.funcIds.emplace(sFunc.name, index);
  for (UWORD i = 0; i < sFunc.numParams; i++)
    _c.aParamTypes.push_back(sFunc.aParams[i]);
  _c.funcs.push_back(std::move(sFunc));

  Expect(_c, ScriptTok::LBrace, "to open the function body");
  _c.psCurFunc = &_c.funcs[index];
  Block sBody = ParseStatementList(_c);
  const ScriptToken& sClose = Expect(_c, ScriptTok::RBrace, "to close the function body");

  if (_c.funcs[index].returnType != VAL_VOID)
  {
    /* Conservative must-return rule: the body's last instruction must be
       an OP_SCRIPTRET (a trailing return, or an if/else whose arms all
       return end with one). */
    if (sBody.aCode.empty() || sBody.aCode.back().op != OP_SCRIPTRET)
      Err(sClose, "function '" + _c.funcs[index].name + "' must end in a return");
  }
  else
    Emit(sBody, OP_SCRIPTRET); // implicit return for void functions

  _c.funcs[index].sCode = std::move(sBody);
  _c.psCurFunc = nullptr;
  _c.paramVars.clear();
}

/* The inside of a trigger's parentheses; shared by trigger declarations
   and inline event triggers */
TriggerSym ParseTriggerSub(Compiler& _c)
{
  TriggerSym sTrig;
  const ScriptToken& sTok = Peek(_c);
  sTrig.line = sTok.line;

  switch (sTok.type)
  {
  case ScriptTok::KwWait:
    Take(_c);
    Expect(_c, ScriptTok::Comma, "after 'wait'");
    sTrig.type = TR_WAIT;
    sTrig.time = static_cast<UDWORD>(Expect(_c, ScriptTok::Integer, "as the wait time").ival);
    return sTrig;
  case ScriptTok::KwEvery:
    Take(_c);
    Expect(_c, ScriptTok::Comma, "after 'every'");
    sTrig.type = TR_EVERY;
    sTrig.time = static_cast<UDWORD>(Expect(_c, ScriptTok::Integer, "as the interval").ival);
    return sTrig;
  case ScriptTok::KwInit:
    Take(_c);
    sTrig.type = TR_INIT;
    return sTrig;
  default:
    break;
  }

  /* A callback name, or a boolean test expression */
  if (sTok.type == ScriptTok::Ident)
  {
    UDWORD index = 0;
    if (Resolve(_c, sTok.text, &index) == IdentKind::Callback)
    {
      Take(_c);
      const CALLBACK_SYMBOL& sCallback = asScrCallbackTab[index];
      sTrig.type = sCallback.type;
      if (Accept(_c, ScriptTok::Comma))
      {
        /* Parameterised callback: the trigger code pushes the params and
           calls the callback, which leaves the fired flag */
        if (sCallback.numParams == 0)
          Err(sTok, "callback '" + sTok.text + "' takes no parameters");
        UDWORD count = 0;
        do
        {
          const ScriptToken& sAt = Peek(_c);
          INTERP_TYPE paramType;
          if (Accept(_c, ScriptTok::KwRef))
          {
            const ScriptToken& sVarTok = Expect(_c, ScriptTok::Ident, "after 'ref'");
            const VarSym* psVar = FindVar(_c, sVarTok.text);
            if (psVar == nullptr || psVar->dims != 0)
              Err(sVarTok, "'ref' needs a script variable");
            if (psVar->storage == ST_EXTERN)
              Err(sVarTok, "cannot use external variables in this context");
            paramType = psVar->type | VAL_REF;
            Emit(sTrig.sCode, OP_PUSHREF, paramType, static_cast<SDWORD>(psVar->index));
          }
          else
          {
            Block sParam = ParseExpr(_c, 0);
            paramType = sParam.type;
            Append(sTrig.sCode, sParam);
          }
          if (count < sCallback.numParams && !interpCheckEquiv(sCallback.aParams[count], paramType))
            Err(sAt, "type mismatch for parameter " + std::to_string(count));
          count += 1;
        }
        while (Accept(_c, ScriptTok::Comma));
        if (count != sCallback.numParams)
          Err(sTok, "expected " + std::to_string(sCallback.numParams) + " parameters, got " + std::to_string(count));
        Emit(sTrig.sCode, OP_CALL);
        LastInstr(sTrig.sCode).arg.func = INSTR_CALLBACKFLAG | (index & INSTR_SLOTMASK);
      }
      else if (sCallback.numParams != 0)
        Err(sTok, "expected parameters for callback trigger");
      return sTrig;
    }
  }

  /* Boolean test with an interval */
  Block sTest = ParseExpr(_c, 0);
  if (sTest.type != VAL_BOOL)
    Err(sTok, "a code trigger's test must be BOOL");
  Expect(_c, ScriptTok::Comma, "after the trigger test");
  sTrig.type = TR_CODE;
  sTrig.time = static_cast<UDWORD>(Expect(_c, ScriptTok::Integer, "as the test interval").ival);
  sTrig.sCode = std::move(sTest);
  return sTrig;
}

void ParseTriggerDecl(Compiler& _c)
{
  Take(_c); // 'trigger'
  const ScriptToken& sName = Expect(_c, ScriptTok::Ident, "as the trigger name");
  UDWORD dummy = 0;
  if (Resolve(_c, sName.text, &dummy) != IdentKind::Unknown)
    Err(sName, "'" + sName.text + "' is already defined");
  Expect(_c, ScriptTok::LParen, "after the trigger name");
  TriggerSym sTrig = ParseTriggerSub(_c);
  Expect(_c, ScriptTok::RParen, "to close the trigger");
  Expect(_c, ScriptTok::Semicolon, "after the trigger");

  sTrig.name = sName.text;
  sTrig.line = sName.line;
  _c.triggerIds.emplace(sTrig.name, static_cast<UDWORD>(_c.triggers.size()));
  _c.triggers.push_back(std::move(sTrig));
}

void ParseEventDecl(Compiler& _c)
{
  Take(_c); // 'event'
  const ScriptToken& sName = Expect(_c, ScriptTok::Ident, "as the event name");

  UDWORD eventIdx;
  UDWORD resolved = 0;
  switch (Resolve(_c, sName.text, &resolved))
  {
  case IdentKind::Unknown:
    eventIdx = static_cast<UDWORD>(_c.events.size());
    _c.eventIds.emplace(sName.text, eventIdx);
    {
      EventSym sEvent;
      sEvent.name = sName.text;
      sEvent.line = sName.line;
      _c.events.push_back(std::move(sEvent));
    }
    break;
  case IdentKind::Event:
    eventIdx = resolved;
    if (_c.events[eventIdx].defined)
      Err(sName, "event '" + sName.text + "' is already defined");
    break;
  default:
    Err(sName, "'" + sName.text + "' is already defined");
  }

  /* Forward declaration */
  if (Accept(_c, ScriptTok::Semicolon))
    return;

  Expect(_c, ScriptTok::LParen, "after the event name");

  SDWORD link;
  const ScriptToken& sLink = Peek(_c);
  UDWORD trigIdx = 0;
  if (sLink.type == ScriptTok::KwInactive)
  {
    Take(_c);
    link = -1;
  }
  else if (sLink.type == ScriptTok::Ident && Resolve(_c, sLink.text, &trigIdx) == IdentKind::Trigger)
  {
    Take(_c);
    link = static_cast<SDWORD>(trigIdx);
  }
  else
  {
    /* An inline trigger declaration: appended as an anonymous trigger */
    TriggerSym sTrig = ParseTriggerSub(_c);
    sTrig.name = "";
    link = static_cast<SDWORD>(_c.triggers.size());
    _c.triggers.push_back(std::move(sTrig));
  }
  Expect(_c, ScriptTok::RParen, "to close the event's trigger");

  Expect(_c, ScriptTok::LBrace, "to open the event body");
  Block sBody = ParseStatementList(_c);
  Expect(_c, ScriptTok::RBrace, "to close the event body");

  EventSym& sEvent = _c.events[eventIdx];
  sEvent.trigger = link;
  sEvent.defined = true;
  sEvent.sCode = std::move(sBody);
}

/* ------------------------------------------------------------------ */
/* Program assembly */

void AppendBody(Compiler& _c, SCRIPT_CODE* _psProg, Block& _body, const std::string& _label, UDWORD _line)
{
  const UDWORD base = static_cast<UDWORD>(_psProg->aCode.size());

  if (_c.genDebug)
  {
    /* A labelled entry at the body's first instruction, so
       eventGetTriggerID / eventGetEventID can name it */
    SCRIPT_DEBUG sEntry;
    sEntry.offset = base;
    sEntry.line = _line;
    sEntry.pLabel = _label;
    _psProg->psDebug.push_back(std::move(sEntry));

    for (const SCRIPT_DEBUG& sSrc : _body.aDebug)
    {
      SCRIPT_DEBUG sShifted = sSrc;
      sShifted.offset += base;
      _psProg->psDebug.push_back(std::move(sShifted));
    }
  }

  _psProg->aCode.insert(_psProg->aCode.end(), _body.aCode.begin(), _body.aCode.end());
}

SCRIPT_CODE* BuildProgram(Compiler& _c)
{
  SCRIPT_CODE* psProg = new SCRIPT_CODE;

  psProg->numTriggers = static_cast<UWORD>(_c.triggers.size());
  psProg->numEvents = static_cast<UWORD>(_c.events.size());
  psProg->numFuncs = static_cast<UWORD>(_c.funcs.size());
  psProg->numGlobals = _c.numGlobals;
  psProg->numArrays = static_cast<UWORD>(_c.asArrays.size());
  psProg->arraySize = _c.arraySize;
  psProg->psArrayInfo = _c.asArrays;
  psProg->aParamTypes = _c.aParamTypes;

  for (TriggerSym& sTrig : _c.triggers)
  {
    psProg->pTriggerTab.push_back(static_cast<UDWORD>(psProg->aCode.size()));
    TRIGGER_DATA sData;
    sData.type = static_cast<UWORD>(sTrig.type);
    sData.code = !sTrig.sCode.aCode.empty();
    sData.time = sTrig.time;
    psProg->psTriggerData.push_back(sData);
    if (!sTrig.sCode.aCode.empty())
      AppendBody(_c, psProg, sTrig.sCode, sTrig.name, sTrig.line);
  }
  psProg->pTriggerTab.push_back(static_cast<UDWORD>(psProg->aCode.size()));

  for (EventSym& sEvent : _c.events)
  {
    if (!sEvent.defined)
      Neuron::Fatal("script compile error: event {} declared without being defined", sEvent.name);
    psProg->pEventTab.push_back(static_cast<UDWORD>(psProg->aCode.size()));
    psProg->pEventLinks.push_back(static_cast<SWORD>(sEvent.trigger));
    AppendBody(_c, psProg, sEvent.sCode, sEvent.name, sEvent.line);
  }
  psProg->pEventTab.push_back(static_cast<UDWORD>(psProg->aCode.size()));

  for (FuncSym& sFunc : _c.funcs)
  {
    psProg->pFuncTab.push_back(static_cast<UDWORD>(psProg->aCode.size()));
    SCRIPT_FUNC_DATA sData;
    sData.firstParamSlot = static_cast<UWORD>(sFunc.firstParamSlot);
    sData.numParams = sFunc.numParams;
    sData.returnType = sFunc.returnType;
    psProg->psFuncData.push_back(sData);
    AppendBody(_c, psProg, sFunc.sCode, sFunc.name, sFunc.line);
  }
  psProg->pFuncTab.push_back(static_cast<UDWORD>(psProg->aCode.size()));

  for (UWORD i = 0; i < _c.numGlobals; i++)
  {
    const VarSym& sVar = _c.vars.find(_c.aVarNames[i])->second;
    psProg->pGlobals.push_back(sVar.type);
  }

  if (_c.genDebug)
  {
    for (UWORD i = 0; i < _c.numGlobals; i++)
    {
      VAR_DEBUG sDebug;
      sDebug.pIdent = _c.aVarNames[i];
      sDebug.storage = _c.aVarStorage[i];
      psProg->psVarDebug.push_back(std::move(sDebug));
    }
    for (size_t i = 0; i < _c.aArrayNames.size(); i++)
    {
      ARRAY_DEBUG sDebug;
      sDebug.pIdent = _c.aArrayNames[i];
      sDebug.storage = _c.aArrayStorage[i];
      psProg->psArrayDebug.push_back(std::move(sDebug));
    }
  }

  return psProg;
}

void BuildTableMaps(Compiler& _c)
{
  if (asScrTypeTab)
  {
    for (UDWORD i = 0; asScrTypeTab[i].typeID != 0; i++)
      _c.typeMap.emplace(asScrTypeTab[i].pIdent, asScrTypeTab[i].typeID);
  }
  if (asScrInstinctTab)
  {
    for (UDWORD i = 0; asScrInstinctTab[i].pFunc != nullptr; i++)
      _c.instinctMap.emplace(asScrInstinctTab[i].pIdent, i);
  }
  /* These two tables terminate on a *named* sentinel - the game's tables
     end with {"CALLBACK LIST END", 0} and {"CONSTANT LIST END", VAL_VOID}
     - so pIdent is not the field to stop on. Callbacks additionally may
     have a null pFunc for the parameterless ones (CALL_GAMEINIT), which
     rules that field out too. */
  if (asScrCallbackTab)
  {
    for (UDWORD i = 0; asScrCallbackTab[i].type != 0; i++)
      _c.callbackMap.emplace(asScrCallbackTab[i].pIdent, i);
  }
  if (asScrConstantTab)
  {
    for (UDWORD i = 0; asScrConstantTab[i].type != VAL_VOID; i++)
      _c.constMap.emplace(asScrConstantTab[i].pIdent, i);
  }
  if (asScrExternalTab)
  {
    for (UDWORD i = 0; asScrExternalTab[i].pIdent != nullptr; i++)
      _c.externMap.emplace(asScrExternalTab[i].pIdent, i);
  }
}
} // namespace
} // namespace Neuron

/* Compile a script program */
BOOL scriptCompile(UBYTE* pData, UDWORD fileSize, SCRIPT_CODE** ppsProg, SCR_DEBUGTYPE debugType)
{
  using namespace Neuron;

  const std::vector<ScriptToken> aTokens = ScriptTokenise(pData, fileSize);

  Compiler c;
  c.pTokens = &aTokens;
  c.genDebug = (debugType == SCR_DEBUGINFO);
  BuildTableMaps(c);

  /* header: "link TYPE ;" entries - parsed and discarded, as before */
  while (Peek(c).type == ScriptTok::KwLink)
  {
    Take(c);
    ParseTypeName(c);
    Expect(c, ScriptTok::Semicolon, "after 'link'");
  }

  /* variable declarations */
  while (Peek(c).type == ScriptTok::KwPublic || Peek(c).type == ScriptTok::KwPrivate)
  {
    const STORAGE_TYPE storage = (Take(c).type == ScriptTok::KwPublic) ? ST_PUBLIC : ST_PRIVATE;
    ParseVarDecl(c, storage);
  }

  /* array bases and the total array size are known once declarations end */
  {
    UDWORD base = c.numGlobals;
    for (ARRAY_DATA& sArray : c.asArrays)
    {
      sArray.base = base;
      UDWORD size = 1;
      for (UBYTE dim = 0; dim < sArray.dimensions; dim++)
        size *= sArray.elements[dim];
      base += size;
    }
    c.arraySize = base - c.numGlobals;
  }

  /* function definitions */
  while (Peek(c).type == ScriptTok::KwFunction)
    ParseFunctionDef(c);

  /* trigger declarations */
  while (Peek(c).type == ScriptTok::KwTrigger)
    ParseTriggerDecl(c);

  /* event declarations and definitions */
  while (Peek(c).type == ScriptTok::KwEvent)
    ParseEventDecl(c);

  if (Peek(c).type != ScriptTok::End)
    Err(Peek(c), std::string("unexpected '") +
        (Peek(c).type == ScriptTok::Ident ? Peek(c).text : ScriptTokName(Peek(c).type)) +
        "' - declarations must come in the order: variables, functions, triggers, events");

  *ppsProg = BuildProgram(c);

  return TRUE;
}
