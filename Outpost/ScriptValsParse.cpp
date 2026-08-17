/*
 * ScriptValsParse.cpp
 *
 * The native .vlo value-file parser: binds a compiled script to a context
 * and pours values into its globals.  Replaces the generated
 * ScriptVals_y.cpp / ScriptVals_l.cpp.  Grammar and per-type value
 * resolution are specified in Docs/ScriptLanguage.md section 8.
 */
#include "pch.h"

#include "Frame.h"
#include "FrameResource.h"
#include "Script.h"
#include "ScriptLex.h"
#include "ScriptTabs.h"
#include "ScriptVals.h"
#include "Objects.h"
#include "GTime.h"
#include "Droid.h"
#include "Structure.h"
#include "Message.h"
#include "AudioSystem.h"
#include "Levels.h"
#include "Research.h"

#include <string>
#include <vector>

namespace
{
using Neuron::ScriptTok;
using Neuron::ScriptToken;

struct VloParser
{
  const std::vector<ScriptToken>* pTokens = nullptr;
  size_t pos = 0;
  SCRIPT_CODE* psScript = nullptr;
  SCRIPT_CONTEXT* psContext = nullptr;
};

const ScriptToken& Peek(VloParser& _p)
{
  return (*_p.pTokens)[_p.pos < _p.pTokens->size() ? _p.pos : _p.pTokens->size() - 1];
}

const ScriptToken& Take(VloParser& _p)
{
  const ScriptToken& sTok = Peek(_p);
  if (sTok.type != ScriptTok::End)
    _p.pos += 1;
  return sTok;
}

[[noreturn]] void Err(const ScriptToken& _tok, const std::string& _what)
{
  Neuron::Fatal("script value error at line {} col {}: {}", _tok.line, _tok.col, _what);
}

const ScriptToken& Expect(VloParser& _p, ScriptTok _type, const char* _context)
{
  const ScriptToken& sTok = Peek(_p);
  if (sTok.type != _type)
    Err(sTok, std::string("expected '") + Neuron::ScriptTokName(_type) + "' " + _context + ", got '" +
        (sTok.type == ScriptTok::Ident ? sTok.text : Neuron::ScriptTokName(sTok.type)) + "'");
  return Take(_p);
}

bool Accept(VloParser& _p, ScriptTok _type)
{
  if (Peek(_p).type != _type)
    return false;
  Take(_p);
  return true;
}

/* Look up a script variable or array by name through the always-generated
   debug records */
bool LookUpVar(VloParser& _p, const std::string& _name, UDWORD* _pIndex)
{
  for (UDWORD i = 0; i < _p.psScript->numGlobals; i++)
  {
    if (_p.psScript->psVarDebug[i].pIdent == _name)
    {
      *_pIndex = i;
      return true;
    }
  }
  return false;
}

bool LookUpArray(VloParser& _p, const std::string& _name, UDWORD* _pIndex)
{
  for (UDWORD i = 0; i < _p.psScript->numArrays; i++)
  {
    if (_p.psScript->psArrayDebug[i].pIdent == _name)
    {
      *_pIndex = i;
      return true;
    }
  }
  return false;
}

/* "name[i][j]" - resolves the element's context slot, bounds-checked */
UDWORD ParseArraySlot(VloParser& _p, const ScriptToken& _name, UDWORD _array)
{
  const ARRAY_DATA& sInfo = _p.psScript->psArrayInfo[_array];
  SDWORD aIndexes[VAR_MAX_DIMENSIONS];
  SDWORD dims = 0;

  while (Accept(_p, ScriptTok::LBracket))
  {
    if (dims >= VAR_MAX_DIMENSIONS)
      Err(_name, "too many dimensions for array");
    aIndexes[dims] = Expect(_p, ScriptTok::Integer, "as the array index").ival;
    Expect(_p, ScriptTok::RBracket, "after the array index");
    dims += 1;
  }

  if (dims != sInfo.dimensions)
    Err(_name, "invalid number of dimensions for array initialiser");
  for (SDWORD i = 0; i < dims; i++)
  {
    if (aIndexes[i] < 0 || aIndexes[i] >= sInfo.elements[i])
      Err(_name, "invalid index for dimension " + std::to_string(i));
  }

  UDWORD slot = 0;
  UDWORD size = 1;
  for (SDWORD i = dims - 1; i >= 0; i--)
  {
    slot += static_cast<UDWORD>(aIndexes[i]) * size;
    size *= sInfo.elements[i];
  }
  return sInfo.base + slot;
}

/* One "var TYPE value" binding */
void ParseBinding(VloParser& _p)
{
  const ScriptToken sName = Expect(_p, ScriptTok::Ident, "as the variable name");
  UDWORD index = 0;
  if (LookUpVar(_p, sName.text, &index))
  {
    // plain variable
  }
  else if (LookUpArray(_p, sName.text, &index))
    index = ParseArraySlot(_p, sName, index);
  else
    Err(sName, "unknown script variable '" + sName.text + "'");

  /* int and bool are lexer keywords rather than entries in asTypeTable,
     which holds only the user types - the same split the generated parser
     had.  The corpus leans on it heavily: int/INT appear 7,443 times
     across the value files, against 5,107 uses of all the user types. */
  const ScriptToken sType = Take(_p);
  INTERP_TYPE type = VAL_VOID;
  if (sType.type == ScriptTok::KwInt)
    type = VAL_INT;
  else if (sType.type == ScriptTok::KwBool)
    type = VAL_BOOL;
  else if (sType.type == ScriptTok::Ident)
  {
    bool found = false;
    for (TYPE_SYMBOL* psCurr = asTypeTable; psCurr->typeID != 0; psCurr++)
    {
      if (sType.text == psCurr->pIdent)
      {
        type = psCurr->typeID;
        found = true;
        break;
      }
    }
    if (!found)
      Err(sType, "unknown type '" + sType.text + "'");
  }
  else
    Err(sType, std::string("expected a type, got '") + Neuron::ScriptTokName(sType.type) + "'");

  /* The value token: an integer (with optional minus), a boolean, or a
     quoted string resolved per type */
  const ScriptToken& sValue = Peek(_p);
  bool isInt = false, isBool = false, isText = false;
  SDWORD ival = 0;
  std::string text;
  if (Accept(_p, ScriptTok::Minus))
  {
    ival = -Expect(_p, ScriptTok::Integer, "after '-'").ival;
    isInt = true;
  }
  else if (sValue.type == ScriptTok::Integer)
  {
    ival = Take(_p).ival;
    isInt = true;
  }
  else if (sValue.type == ScriptTok::KwTrue || sValue.type == ScriptTok::KwFalse)
  {
    ival = (Take(_p).type == ScriptTok::KwTrue) ? 1 : 0;
    isBool = true;
  }
  else if (sValue.type == ScriptTok::Text)
  {
    text = Take(_p).text;
    isText = true;
  }
  else
    Err(sValue, "expected a value");

  const auto typeMismatch = [&]() -> void { Err(sName, "type mismatch for variable '" + sName.text + "'"); };
  const auto notFound = [&](const char* _what) -> void { Err(sName, std::string(_what) + " '" + text + "' not found"); };

  INTERP_VAL sVal;
  sVal.type = type;
  sVal.v.oval = nullptr;

  BASE_OBJECT* psObj;
  SDWORD compIndex;

  switch (type)
  {
  case VAL_INT:
    if (!isInt)
      typeMismatch();
    sVal.v.ival = ival;
    break;
  case VAL_BOOL:
    if (!isBool)
      typeMismatch();
    sVal.v.bval = ival;
    break;

  case ST_DROID:
  case ST_STRUCTURE:
  case ST_FEATURE:
    {
      if (!isInt)
        typeMismatch();
      if (!scrvGetBaseObj(static_cast<UDWORD>(ival), &psObj))
        Err(sName, "object id " + std::to_string(ival) + " not found");
      const OBJECT_TYPE want = (type == ST_DROID) ? OBJ_DROID : (type == ST_STRUCTURE) ? OBJ_STRUCTURE : OBJ_FEATURE;
      if (psObj->type != want)
        Err(sName, "object id " + std::to_string(ival) + " has the wrong object type");
      sVal.v.oval = psObj;
      break;
    }

  case ST_DROIDID:
  case ST_STRUCTUREID:
    {
      if (!isInt)
        typeMismatch();
      if (!scrvGetBaseObj(static_cast<UDWORD>(ival), &psObj))
        Err(sName, "object id " + std::to_string(ival) + " not found");
      const OBJECT_TYPE want = (type == ST_DROIDID) ? OBJ_DROID : OBJ_STRUCTURE;
      if (psObj->type != want)
        Err(sName, "object id " + std::to_string(ival) + " has the wrong object type");
      sVal.v.ival = ival;
      break;
    }

  case ST_FEATURESTAT:
    if (!isText)
      typeMismatch();
    compIndex = getFeatureStatFromName(const_cast<STRING*>(text.c_str()));
    if (compIndex == -1)
      notFound("feature stat");
    sVal.v.ival = compIndex;
    break;
  case ST_STRUCTURESTAT:
    if (!isText)
      typeMismatch();
    compIndex = getStructStatFromName(const_cast<STRING*>(text.c_str()));
    if (compIndex == -1)
      notFound("structure stat");
    sVal.v.ival = compIndex;
    break;

  case ST_BODY:
  case ST_PROPULSION:
  case ST_ECM:
  case ST_SENSOR:
  case ST_CONSTRUCT:
  case ST_REPAIR:
  case ST_BRAIN:
  case ST_WEAPON:
    {
      if (!isText)
        typeMismatch();
      UDWORD compType = 0;
      switch (type)
      {
      case ST_BODY: compType = COMP_BODY;
        break;
      case ST_PROPULSION: compType = COMP_PROPULSION;
        break;
      case ST_ECM: compType = COMP_ECM;
        break;
      case ST_SENSOR: compType = COMP_SENSOR;
        break;
      case ST_CONSTRUCT: compType = COMP_CONSTRUCT;
        break;
      case ST_REPAIR: compType = COMP_REPAIRUNIT;
        break;
      case ST_BRAIN: compType = COMP_BRAIN;
        break;
      default: compType = COMP_WEAPON;
        break;
      }
      compIndex = getCompFromResName(compType, const_cast<STRING*>(text.c_str()));
      if (compIndex == -1)
        notFound("component");
      sVal.v.ival = compIndex;
      break;
    }

  case ST_TEMPLATE:
    if (!isText)
      typeMismatch();
    sVal.v.oval = getTemplateFromName(const_cast<STRING*>(text.c_str()));
    if (sVal.v.oval == nullptr)
      notFound("template");
    break;

  case ST_INTMESSAGE:
    if (!isText)
      typeMismatch();
    sVal.v.oval = getViewData(const_cast<STRING*>(text.c_str()));
    if (sVal.v.oval == nullptr)
      notFound("message");
    break;

  case ST_TEXTSTRING:
    {
      if (!isText)
        typeMismatch();
      STRING* pString = nullptr;
      if (!scrvGetString(const_cast<STRING*>(text.c_str()), &pString))
        notFound("string");
      sVal.v.sval = pString;
      break;
    }

  case ST_LEVEL:
    {
      if (!isText)
        typeMismatch();
      LEVEL_DATASET* psLevel = nullptr;
      // just check the level exists
      if (!levFindDataSet(const_cast<STRING*>(text.c_str()), &psLevel))
        notFound("level");
      sVal.v.sval = psLevel->pName;
      break;
    }

  case ST_SOUND:
    if (!isText)
      typeMismatch();
    compIndex = AudioSystem::TrackId(text.c_str());
    if (compIndex == SAMPLE_NOT_FOUND)
      AudioSystem::SetTrackVals(text.c_str(), false, compIndex, 100, 1, 1800);
    sVal.v.ival = compIndex;
    break;

  case ST_RESEARCH:
    if (!isText)
      typeMismatch();
    sVal.v.oval = getResearch(const_cast<STRING*>(text.c_str()), TRUE);
    if (sVal.v.oval == nullptr)
      notFound("research");
    break;

  default:
    Err(sType, "type '" + sType.text + "' cannot be initialised from a value file");
  }

  if (!eventSetContextVar(_p.psContext, index, &sVal))
    Err(sName, "set value failed for '" + sName.text + "'");
}

/* "script \"name.slo\"" - resolve the compiled script resource */
SCRIPT_CODE* ResolveScript(VloParser& _p)
{
  const ScriptToken& sName = Expect(_p, ScriptTok::Text, "as the script file name");
  std::string name = sName.text;

  /* Legacy PSX data named .blo files; the resource type behind them is
     long gone, so they resolve as the .slo of the same name */
  if (name.size() > 3 && name.compare(name.size() - 3, 3, "blo") == 0)
    name[name.size() - 3] = 's';

  SCRIPT_CODE* psScript = static_cast<SCRIPT_CODE*>(resGetData("SCRIPT", const_cast<STRING*>(name.c_str())));
  if (psScript == nullptr)
    Err(sName, "script file '" + sName.text + "' not found");
  return psScript;
}
} // namespace

// Load a script value file
BOOL scrvLoad(UBYTE* pData, UDWORD size)
{
  const std::vector<ScriptToken> aTokens = Neuron::ScriptTokenise(pData, size);

  VloParser p;
  p.pTokens = &aTokens;

  while (Peek(p).type != ScriptTok::End)
  {
    Expect(p, ScriptTok::KwScript, "to start an entry");
    const ScriptToken& sName = Peek(p);
    p.psScript = ResolveScript(p);

    if (Accept(p, ScriptTok::KwRun))
    {
      if (!eventNewContext(p.psScript, CR_RELEASE, &p.psContext))
        Err(sName, "couldn't create context");
      if (!scrvAddContext(const_cast<STRING*>(sName.text.c_str()), p.psContext, SCRV_EXEC))
        Err(sName, "couldn't store context");

      Expect(p, ScriptTok::LBrace, "after 'run'");
      while (!Accept(p, ScriptTok::RBrace))
        ParseBinding(p);

      if (!eventRunContext(p.psContext, gameTime / SCR_TICKRATE))
        Err(sName, "couldn't run context");
    }
    else
    {
      Expect(p, ScriptTok::KwStore, "after the script name");
      const ScriptToken& sId = Expect(p, ScriptTok::Text, "as the context id");
      if (!eventNewContext(p.psScript, CR_NORELEASE, &p.psContext))
        Err(sName, "couldn't create context");
      if (!scrvAddContext(const_cast<STRING*>(sId.text.c_str()), p.psContext, SCRV_NOEXEC))
        Err(sName, "couldn't store context");
    }
  }

  return TRUE;
}
