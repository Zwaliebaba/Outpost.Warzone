#include "pch.h"

// Report unused strings
#include "Types.h"
#include <string>

#include "Treap.h"
#include "StrRes.h"

/* The id number of ID strings allocated by the system is ORed with this */
#define ID_ALLOC	0x80000000

/* Allocate a string block */
static BOOL strresAllocBlock(STR_BLOCK** ppsBlock, UDWORD size)
{
  *ppsBlock = new (std::nothrow) STR_BLOCK[1];
  if (!*ppsBlock)
  {
    Neuron::Fatal("strresAllocBlock: Out of memory - 1");
    return FALSE;
  }

  (*ppsBlock)->apStrings = new (std::nothrow) STRING*[size];
  if (!(*ppsBlock)->apStrings)
  {
    Neuron::Fatal("strresAllocBlock: Out of memory - 2");
    delete[] *ppsBlock;
    return FALSE;
  }
  memset((*ppsBlock)->apStrings, 0, sizeof(STRING*) * size);

#ifdef DEBUG
  (*ppsBlock)->aUsage = new (std::nothrow) UDWORD[size];
  memset((*ppsBlock)->aUsage, 0, sizeof(UDWORD) * size);
#endif

  return TRUE;
}

/* Initialise the string system */
BOOL strresCreate(STR_RES** ppsRes, UDWORD init, UDWORD ext)
{
  STR_RES* psRes;

  psRes = new (std::nothrow) STR_RES[1];
  if (!psRes)
  {
    Neuron::Fatal("strresCreate: Out of memory");
    return FALSE;
  }
  psRes->init = init;
  psRes->ext = ext;
  psRes->nextID = 0;

  if (!TREAP_CREATE(&psRes->psIDTreap, treapStringCmp, init, ext))
  {
    Neuron::Fatal("strresCreate: Out of memory");
    delete[] psRes;
    psRes = nullptr;
    return FALSE;
  }

  if (!strresAllocBlock(&psRes->psStrings, init))
  {
    TREAP_DESTROY(psRes->psIDTreap);
    delete[] psRes;
    psRes = nullptr;
    return FALSE;
  }
  psRes->psStrings->psNext = nullptr;
  psRes->psStrings->idStart = 0;
  psRes->psStrings->idEnd = init - 1;

  *ppsRes = psRes;

  return TRUE;
}

/* Release the id strings, but not the strings themselves,
 * (they can be accessed only by id number).
 */
void strresReleaseIDStrings(STR_RES* psRes)
{
  STR_ID* psID;

  for (psID = static_cast<STR_ID*>(TREAP_GETSMALLEST(psRes->psIDTreap)); psID; psID = static_cast<STR_ID*>(
         TREAP_GETSMALLEST(psRes->psIDTreap)))
  {
    TREAP_DEL(psRes->psIDTreap, (TREAP_KEY)psID->pIDStr);
    if (psID->id & ID_ALLOC)
    {
      delete[] psID->pIDStr;
      psID->pIDStr = nullptr;
      delete[] psID;
      psID = nullptr;
    }
  }
}

/* Shutdown the string system */
void strresDestroy(STR_RES* psRes)
{
  STR_BLOCK *psBlock, *psNext = nullptr;
  UDWORD i;

  // Free the string id's
  strresReleaseIDStrings(psRes);

  // Free the strings themselves
  for (psBlock = psRes->psStrings; psBlock; psBlock = psNext)
  {
    for (i = psBlock->idStart; i <= psBlock->idEnd; i++)
    {
#ifdef DEBUG_GROUP0
      if (psBlock->aUsage[i - psBlock->idStart] == 0 && i != 0 && i < psRes->nextID)
      {
      }
#endif
      if (psBlock->apStrings[i - psBlock->idStart]) { delete[] psBlock->apStrings[i - psBlock->idStart]; }
#ifdef DEBUG
      else if (i < psRes->nextID)
        Neuron::DebugTrace("strresDestroy: No string loaded for id {}\n", i);
#endif
    }
    psNext = psBlock->psNext;
    delete[] psBlock->apStrings;
    psBlock->apStrings = nullptr;
#ifdef DEBUG
    delete[] psBlock->aUsage;
    psBlock->aUsage = nullptr;
#endif
    delete[] psBlock;
    psBlock = nullptr;
  }

  // Release the treap and free the final memory
  TREAP_DESTROY(psRes->psIDTreap);
  delete[] psRes;
  psRes = nullptr;
}

/* Load a list of string ID's from a memory buffer */
BOOL strresLoadFixedID(STR_RES* psRes, STR_ID* psID, UDWORD numID)
{
  UDWORD i;

  for (i = 0; i < numID; i++)
  {
    DEBUG_ASSERT_TEXT(psID->id == psRes->nextID, "strresLoadFixedID: id out of sequence");

    // Store the ID string
    if (!TREAP_ADD(psRes->psIDTreap, (TREAP_KEY)psID->pIDStr, psID))
    {
      Neuron::Fatal("strresLoadFixedID: Out of memory");
      return FALSE;
    }

    psID += 1;
    psRes->nextID += 1;
  }

  return TRUE;
}

/* Return the ID number for an ID string */
BOOL strresGetIDNum(STR_RES* psRes, STRING* pIDStr, UDWORD* pIDNum)
{
  STR_ID* psID;

  psID = static_cast<STR_ID*>(TREAP_FIND(psRes->psIDTreap, (TREAP_KEY)pIDStr));
  if (!psID)
  {
    *pIDNum = 0;
    return FALSE;
  }

  if (psID->id & ID_ALLOC)
    *pIDNum = psID->id & ~ID_ALLOC;
  else
    *pIDNum = psID->id;
  return TRUE;
}

/* Return the ID stored ID string that matches the string passed in */
BOOL strresGetIDString(STR_RES* psRes, STRING* pIDStr, STRING** ppStoredID)
{
  STR_ID* psID;

  psID = static_cast<STR_ID*>(TREAP_FIND(psRes->psIDTreap, (TREAP_KEY)pIDStr));
  if (!psID)
  {
    *ppStoredID = nullptr;
    return FALSE;
  }

  *ppStoredID = psID->pIDStr;

  return TRUE;
}

/* Store a string */
BOOL strresStoreString(STR_RES* psRes, STRING* pID, STRING* pString)
{
  STR_ID* psID;
  STRING* pNew;
  STR_BLOCK* psBlock;
  UDWORD id;

  // Find the id for the string
  psID = static_cast<STR_ID*>(TREAP_FIND(psRes->psIDTreap, (TREAP_KEY)pID));
  if (!psID)
  {
    // No ID yet so generate a new one
    psID = new (std::nothrow) STR_ID[1];
    if (!psID)
    {
      Neuron::Fatal("strresStoreString: Out of memory");
      return FALSE;
    }
    psID->pIDStr = new (std::nothrow) STRING[(stringLen(pID) + 1)];
    if (!psID->pIDStr)
    {
      Neuron::Fatal("strresStoreString: Out of memory");
      delete[] psID;
      psID = nullptr;
      return FALSE;
    }
    stringCpy(psID->pIDStr, pID);
    psID->id = psRes->nextID | ID_ALLOC;
    psRes->nextID += 1;
    TREAP_ADD(psRes->psIDTreap, (TREAP_KEY)psID->pIDStr, psID);
  }
  id = psID->id & ~ID_ALLOC;

  // Find the block to store the string in
  for (psBlock = psRes->psStrings; psBlock->idEnd < id; psBlock = psBlock->psNext)
  {
    if (!psBlock->psNext)
    {
      // Need to allocate a new string block
      if (!strresAllocBlock(&psBlock->psNext, psRes->ext))
        return FALSE;
      psBlock->psNext->idStart = psBlock->idEnd + 1;
      psBlock->psNext->idEnd = psBlock->idEnd + psRes->ext;
      psBlock->psNext->psNext = nullptr;
    }
  }

  // Put the new string in the string block
  if (psBlock->apStrings[psID->id - psBlock->idStart] != nullptr)
  {
    Neuron::Fatal("strresStoreString: Duplicate string for id: {}", psID->pIDStr);
    return FALSE;
  }

  // Allocate a copy of the string
  pNew = new (std::nothrow) STRING[(stringLen(pString) + 1)];
  if (!pNew)
  {
    Neuron::Fatal("strresStoreString: Out of memory");
    return FALSE;
  }
  stringCpy(pNew, pString);
  psBlock->apStrings[psID->id - psBlock->idStart] = pNew;

  return TRUE;
}

/* Get the string from an ID number */
STRING* strresGetString(STR_RES* psRes, UDWORD id)
{
  STR_BLOCK* psBlock;

  // find the block the string is in
  for (psBlock = psRes->psStrings; psBlock && psBlock->idEnd < id; psBlock = psBlock->psNext);

  if (!psBlock)
  {
    DEBUG_ASSERT_TEXT(FALSE, "strresGetString: String not found");
    // Return the default string
    return psRes->psStrings->apStrings[0];
  }

  DEBUG_ASSERT_TEXT(psBlock->apStrings[id - psBlock->idStart] != NULL, "strresGetString: String not found");

#ifdef DEBUG
  psBlock->aUsage[id - psBlock->idStart] += 1;
#endif

  if (psBlock->apStrings[id - psBlock->idStart] == nullptr)
  {
    // Return the default string
    return psRes->psStrings->apStrings[0];
  }

  return psBlock->apStrings[id - psBlock->idStart];
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
    std::string id(idStart, p);

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
    std::string value(strStart, p);
    p += 1;

    if (!strresStoreString(psRes, const_cast<STRING*>(id.c_str()), const_cast<STRING*>(value.c_str())))
      return FALSE;
  }

  return TRUE;
}

/* Return the the length of a STRING */
UDWORD stringLen(STRING* pStr)
{
  UDWORD count = 0;

  while (*pStr++) { count += 1; }

  return count;
}

/* Copy a STRING */
void stringCpy(STRING* pDest, STRING* pSrc)
{
  do { *pDest++ = *pSrc; }
  while (*pSrc++);
}

/* Get the ID number for a string*/
UDWORD strresGetIDfromString(STR_RES* psRes, STRING* pString)
{
  STR_BLOCK *psBlock, *psNext = nullptr;
  UDWORD i;

  // Search through all the blocks to find the string
  for (psBlock = psRes->psStrings; psBlock; psBlock = psNext)
  {
    for (i = psBlock->idStart; i <= psBlock->idEnd; i++)
    {
      if (psBlock->apStrings[i - psBlock->idStart])
      {
        if (!strcmp(psBlock->apStrings[i - psBlock->idStart], pString))
          return i;
      }
    }
    psNext = psBlock->psNext;
  }
  return 0;
}
