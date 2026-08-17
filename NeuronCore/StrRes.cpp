#include "pch.h"

#include "Types.h"
#include "StrRes.h"

/* The string resource system.
 *
 * This was a treap keyed on the *address* of the ID string, plus a chain
 * of fixed-size string blocks walked linearly on every lookup, plus an
 * ID_ALLOC flag bit distinguishing ids the system allocated from ids the
 * game's fixed-keyword table owns.  It is now a vector indexed by id and
 * a map from keyword to id; the flag bit is gone because every entry owns
 * its own copy of both halves.
 *
 * Element addresses have to be stable: strresGetString's result is stored
 * by widgets, view data and script string values for the session, and the
 * map's keys are string_views into the entries themselves.  The
 * unique_ptr elements are what guarantees that.
 */

namespace
{
/* Find or create the entry for a keyword */
STR_ENTRY* strresEntry(STR_RES* psRes, const STRING* pIDStr)
{
  const auto it = psRes->idMap.find(std::string_view(pIDStr));
  if (it != psRes->idMap.end())
    return psRes->aEntries[it->second].get();

  auto psEntry = std::make_unique<STR_ENTRY>();
  psEntry->name = pIDStr;

  const UDWORD id = static_cast<UDWORD>(psRes->aEntries.size());
  STR_ENTRY* psResult = psEntry.get();
  psRes->aEntries.push_back(std::move(psEntry));
  psRes->idMap.emplace(std::string_view(psResult->name), id);

  return psResult;
}
} // namespace

/* Initialise the string system */
BOOL strresCreate(STR_RES** ppsRes)
{
  *ppsRes = new (std::nothrow) STR_RES;
  if (!*ppsRes)
  {
    Neuron::Fatal("strresCreate: Out of memory");
    return FALSE;
  }

  return TRUE;
}

/* Shutdown the string system */
void strresDestroy(STR_RES* psRes)
{
#ifdef DEBUG
  for (UDWORD i = 0; i < psRes->aEntries.size(); i++)
  {
    if (!psRes->aEntries[i]->hasText)
      Neuron::DebugTrace("strresDestroy: No string loaded for id {} ({})\n", i, psRes->aEntries[i]->name);
  }
#endif

  delete psRes;
}

/* Load a list of string ID's from a memory buffer */
BOOL strresLoadFixedID(STR_RES* psRes, const STR_ID* psID, UDWORD numID)
{
  for (UDWORD i = 0; i < numID; i++)
  {
    DEBUG_ASSERT_TEXT(psID->id == psRes->aEntries.size(), "strresLoadFixedID: id out of sequence");

    /* The keyword table owns its own strings; the entry takes a copy, so
       the two lifetimes stop being entangled. */
    strresEntry(psRes, psID->pIDStr);

    psID += 1;
  }

  return TRUE;
}

/* Return the ID number for an ID string */
BOOL strresGetIDNum(STR_RES* psRes, const STRING* pIDStr, UDWORD* pIDNum)
{
  const auto it = psRes->idMap.find(std::string_view(pIDStr));
  if (it == psRes->idMap.end())
  {
    *pIDNum = 0;
    return FALSE;
  }

  *pIDNum = it->second;
  return TRUE;
}

/* Return the stored ID string that matches the string passed in */
BOOL strresGetIDString(STR_RES* psRes, const STRING* pIDStr, STRING** ppStoredID)
{
  const auto it = psRes->idMap.find(std::string_view(pIDStr));
  if (it == psRes->idMap.end())
  {
    *ppStoredID = nullptr;
    return FALSE;
  }

  *ppStoredID = psRes->aEntries[it->second]->name.data();

  return TRUE;
}

/* Store a string */
BOOL strresStoreString(STR_RES* psRes, const STRING* pID, const STRING* pString)
{
  STR_ENTRY* psEntry = strresEntry(psRes, pID);

  if (psEntry->hasText)
  {
    Neuron::Fatal("strresStoreString: Duplicate string for id: {}", psEntry->name);
    return FALSE;
  }

  psEntry->text = pString;
  psEntry->hasText = true;

  return TRUE;
}

/* Get the string from an ID number */
STRING* strresGetString(STR_RES* psRes, UDWORD id)
{
  DEBUG_ASSERT_TEXT(id < psRes->aEntries.size(), "strresGetString: String not found");

  /* Out of range, or a keyword no string file ever supplied text for:
     return the default string, which is id 0 by convention. */
  if (id >= psRes->aEntries.size() || !psRes->aEntries[id]->hasText)
  {
    DEBUG_ASSERT_TEXT(!psRes->aEntries.empty(), "strresGetString: no strings loaded");
    return psRes->aEntries.empty() ? nullptr : psRes->aEntries[0]->text.data();
  }

  return psRes->aEntries[id]->text.data();
}

/* Load a string resource file.

   The format is id/string pairs - `IDENT "text"` - with C and C++
   comments.  Measured over the nine shipped files (3,186 pairs), ids use
   only [A-Za-z0-9_-].  This replaced the generated StrRes_y/StrRes_l
   parser, the tree's last yacc/lex output. */
BOOL strresLoad(STR_RES* psRes, UBYTE* pData, UDWORD size)
{
  const char* p = reinterpret_cast<const char*>(pData);
  const char* const end = p + size;
  UDWORD line = 1;

  const auto isIdChar = [](char c)
  {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
  };

  while (p < end)
  {
    const char c = *p;

    if (c == '\n')
    {
      line += 1;
      p += 1;
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\r')
    {
      p += 1;
      continue;
    }
    if (c == '/' && p + 1 < end && p[1] == '/')
    {
      while (p < end && *p != '\n')
        p += 1;
      continue;
    }
    if (c == '/' && p + 1 < end && p[1] == '*')
    {
      p += 2;
      while (p + 1 < end && !(p[0] == '*' && p[1] == '/'))
      {
        if (*p == '\n')
          line += 1;
        p += 1;
      }
      if (p + 1 >= end)
      {
        Neuron::Fatal("strresLoad: unterminated comment at line {}", line);
        return FALSE;
      }
      p += 2;
      continue;
    }

    // An id followed by its quoted string
    if (!isIdChar(c))
    {
      Neuron::Fatal("strresLoad: unexpected character '{}' at line {}", c, line);
      return FALSE;
    }
    const char* idStart = p;
    while (p < end && isIdChar(*p))
      p += 1;
    const std::string id(idStart, p);

    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r'))
      p += 1;
    if (p >= end || *p != '"')
    {
      Neuron::Fatal("strresLoad: expected a quoted string after '{}' at line {}", id, line);
      return FALSE;
    }
    p += 1;
    const char* strStart = p;
    while (p < end && *p != '"')
    {
      if (*p == '\n')
        line += 1;
      p += 1;
    }
    if (p >= end)
    {
      Neuron::Fatal("strresLoad: unterminated string for '{}' at line {}", id, line);
      return FALSE;
    }
    const std::string value(strStart, p);
    p += 1;

    if (!strresStoreString(psRes, id.c_str(), value.c_str()))
      return FALSE;
  }

  return TRUE;
}
