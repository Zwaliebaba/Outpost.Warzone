/*
 * StrRes.h
 *
 * String resource interface functions
 *
 */
#ifndef _strres_h
#define _strres_h

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Types.h"

/* An ID entry, as the game's fixed-keyword table declares them
 * (Outpost/Text.cpp).  The id must equal the entry's position in that
 * table; strresLoadFixedID asserts it.
 */
using STR_ID = struct _str_id
{
  UDWORD id;
  const STRING* pIDStr;
};

/* One string: its ID keyword and its text.
 *
 * Entries are held by unique_ptr so their addresses never move.  Both
 * halves are handed out as raw pointers that callers keep for the
 * session - widgets, view data and script string values all store the
 * result of strresGetString - so stability is a requirement, not a
 * convenience.
 */
using STR_ENTRY = struct _str_entry
{
  std::string name; // the ID keyword
  std::string text; // the string itself
  bool hasText = false; // whether text has been stored yet
};

/* A String Resource */
using STR_RES = struct _str_res
{
  std::vector<std::unique_ptr<STR_ENTRY>> aEntries; // indexed by id
  std::unordered_map<std::string_view, UDWORD> idMap; // keyword -> id
};

/* Create a string resource object */
extern BOOL strresCreate(STR_RES** ppsRes);

/* Release a string resource object */
extern void strresDestroy(STR_RES* psRes);

/* Load a list of string ID's from a memory buffer
 * id == 0 should be a default string which is returned if the
 * requested string is not found.
 */
extern BOOL strresLoadFixedID(STR_RES* psRes, const STR_ID* psID, UDWORD numID);

/* Return the ID number for an ID string */
extern BOOL strresGetIDNum(STR_RES* psRes, const STRING* pIDStr, UDWORD* pIDNum);

/* Return the stored ID string that matches the string passed in */
extern BOOL strresGetIDString(STR_RES* psRes, const STRING* pIDStr, STRING** ppStoredID);

/* Get the string from an ID number */
extern STRING* strresGetString(STR_RES* psRes, UDWORD id);

/* Store a string */
extern BOOL strresStoreString(STR_RES* psRes, const STRING* pID, const STRING* pString);

/* Load a string resource file */
extern BOOL strresLoad(STR_RES* psRes, UBYTE* pData, UDWORD size);

#endif
