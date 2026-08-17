/***************************************************************************/

#ifndef _HASHTABL_H_
#define _HASHTABL_H_

/***************************************************************************/

#include "Frame.h"

/***************************************************************************/
/* defines
 */

/* flags key not used in hash function */
#define	UNUSED_KEY	-747

/***************************************************************************/
/* macros
 */

/* Hash keys hold object addresses as often as they hold numbers - the
 * animation and projectile tables key on the object's address, and
 * HashPJW casts the key straight back to a char* - so the key type is
 * pointer-width. It was int, which truncated every key on a 64 bit
 * build and made HashPJW dereference a rebuilt-from-half address. */
using HASH_KEY = std::intptr_t;

using HASHFUNC = UDWORD(*)(HASH_KEY iKey1, HASH_KEY iKey2);
using HASHFREEFUNC = void(*)(void* psElement);

/***************************************************************************/
/* structs
 */

using HASHNODE = struct HASHNODE
{
  HASH_KEY iKey1;
  HASH_KEY iKey2;
  void* psElement;
  struct HASHNODE* psNext;
};

using HASHTABLE = struct HASHTABLE
{
  HASHNODE** ppsNode;
  HASHNODE* psNextNode;
  HASHFUNC pHashFunc;
  HASHFREEFUNC pFreeFunc;
  UDWORD udwTableSize;
  UDWORD udwElements;
  UDWORD udwExtElements;
  UDWORD udwElementSize;
  UDWORD sdwCurIndex;
};

/***************************************************************************/
/* functions
 */

BOOL hashTable_Create(HASHTABLE** ppsTable, UDWORD udwTableSize, UDWORD udwInitElements, UDWORD udwExtElements, UDWORD udwElementSize);
void hashTable_Destroy(HASHTABLE* psTable);
void hashTable_Clear(HASHTABLE* psTable);

void* hashTable_GetElement(HASHTABLE* psTable);
void hashTable_InsertElement(HASHTABLE* psTable, void* psElement, HASH_KEY iKey1, HASH_KEY iKey2);
BOOL hashTable_RemoveElement(HASHTABLE* psTable, void* psElement, HASH_KEY iKey1, HASH_KEY iKey2);
void* hashTable_FindElement(HASHTABLE* psTable, HASH_KEY iKey1, HASH_KEY iKey2);

void* hashTable_GetFirst(HASHTABLE* psTable);
void* hashTable_GetNext(HASHTABLE* psTable);

void hashTable_SetHashFunction(HASHTABLE* psTable, HASHFUNC pHashFunc);
void hashTable_SetFreeElementFunction(HASHTABLE* psTable, HASHFREEFUNC pFreeFunc);

/***************************************************************************/

#endif	// _HASHTABL_H_

/***************************************************************************/
