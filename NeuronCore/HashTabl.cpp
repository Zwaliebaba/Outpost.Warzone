#include "pch.h"
#include "Frame.h"

#include "HashTabl.h"

extern void* g_ElementToBeRemoved;

/***************************************************************************/

#define	HASHTEST		1

/* next four used in HashPJW */
#define	BITS_IN_int		32
#define	THREE_QUARTERS	((UINT) ((BITS_IN_int * 3) / 4))
#define	ONE_EIGHTH		((UINT) (BITS_IN_int / 8))

#define	HIGH_BITS		((UINT)( ~((0xffffffff) >> ONE_EIGHTH )))

/***************************************************************************/

UINT HashTest(int iKey1, int iKey2) { return static_cast<UINT>(iKey1) + iKey2; }

/***************************************************************************/
/*
 * HashPJW
 *
 * Adaptation of Peter Weinberger's (PJW) generic hashing algorithm listed
 * in Binstock+Rex, "Practical Algorithms" p 69.
 *
 * Accepts element pointer and returns hashed integer.
 */
/***************************************************************************/

UINT HashPJW(int iKey1, int iKey2)
{
  UINT iHashValue, i;
  auto c = (CHAR*)iKey1;

  /* don't use second key in this one */
  iKey2 = UNUSED_KEY;

  for (iHashValue = 0; *c; ++c)
  {
    iHashValue = (iHashValue << ONE_EIGHTH) + *c;

    if ((i = iHashValue & HIGH_BITS) != 0) { iHashValue = (iHashValue ^ (i >> THREE_QUARTERS)) & ~HIGH_BITS; }
  }

  return iHashValue;
}

/***************************************************************************/

BOOL hashTable_Create(HASHTABLE** ppsTable, UDWORD udwTableSize, UDWORD udwInitElements, UDWORD udwExtElements, UDWORD udwElementSize)
{
  UDWORD udwSize;

  /* allocate and init table */

  (*ppsTable) = new (std::nothrow) HASHTABLE[1];
  udwSize = udwTableSize * sizeof(HASHNODE*);
  (*ppsTable)->ppsNode = new (std::nothrow) HASHNODE*[udwTableSize];
  memset((*ppsTable)->ppsNode, 0, udwSize);

  /* init members */
  (*ppsTable)->udwTableSize = udwTableSize;
  (*ppsTable)->udwElements = udwInitElements;
  (*ppsTable)->udwExtElements = udwExtElements;
  (*ppsTable)->udwElementSize = udwElementSize;
  (*ppsTable)->sdwCurIndex = 0;
  (*ppsTable)->psNextNode = (*ppsTable)->ppsNode[0];
  (*ppsTable)->pFreeFunc = nullptr;

  /* set hash function to internal */
#if HASHTEST
  hashTable_SetHashFunction((*ppsTable), HashTest);
#else
  hashTable_SetHashFunction((*ppsTable), HashPJW);
#endif

  return TRUE;
}

/***************************************************************************/

void hashTable_Destroy(HASHTABLE* psTable)
{
  hashTable_Clear(psTable);

  /* free table */
  delete[] psTable->ppsNode;
  psTable->ppsNode = nullptr;
  delete[] psTable;
  psTable = nullptr;
}

/***************************************************************************/
/*
 * hashTable_Clear
 *
 * Returns all nodes from hash table to free node list
 */
/***************************************************************************/

void hashTable_Clear(HASHTABLE* psTable)
{
  HASHNODE *psNode, *psNodeTmp;
  UDWORD i;

  /* free nodes */
  for (i = 0; i < psTable->udwTableSize; i++)
  {
    /* free table entry nodelist */
    psNode = psTable->ppsNode[i];
    while (psNode != nullptr)
    {
      /* do free-element callback if set */
      if (psTable->pFreeFunc != nullptr)
        (psTable->pFreeFunc)(psNode->psElement);

      /* free element */
      delete[] static_cast<UBYTE*>(psNode->psElement);

      psNodeTmp = psNode->psNext;
      delete psNode;
      psNode = psNodeTmp;
    }

    /* set table entry to NULL */
    psTable->ppsNode[i] = nullptr;
  }
}

/***************************************************************************/

void hashTable_SetHashFunction(HASHTABLE* psTable, HASHFUNC pHashFunc)
{
  psTable->pHashFunc = pHashFunc;
}

/***************************************************************************/

void hashTable_SetFreeElementFunction(HASHTABLE* psTable, HASHFREEFUNC pFreeFunc)
{
  psTable->pFreeFunc = pFreeFunc;
}

/***************************************************************************/
/*
 * hashTable_GetElement
 *
 * Gets free node from heap and returns element pointer
 */
/***************************************************************************/

void* hashTable_GetElement(HASHTABLE* psTable)
{
  // returns NULL if the allocation fails
  return new (std::nothrow) UBYTE[psTable->udwElementSize];
}

/***************************************************************************/

UDWORD hashTable_GetHashKey(HASHTABLE* psTable, int iKey1, int iKey2)
{
  /* get hashed index */
  return (psTable->pHashFunc)(iKey1, iKey2) % psTable->udwTableSize;
}

/***************************************************************************/

void hashTable_InsertElement(HASHTABLE* psTable, void* psElement, int iKey1, int iKey2)
{
  UDWORD udwHashIndex;
  HASHNODE* psNode;

  /* get hashed index */
  udwHashIndex = hashTable_GetHashKey(psTable, iKey1, iKey2);

  psNode = new (std::nothrow) HASHNODE;

  /* set node elements */
  psNode->iKey1 = iKey1;
  psNode->iKey2 = iKey2;
  psNode->psElement = psElement;

  /* add new node to head of list */
  psNode->psNext = psTable->ppsNode[udwHashIndex];
  psTable->ppsNode[udwHashIndex] = psNode;
}

/***************************************************************************/
/*
 * hashTable_FindElement
 *
 * Calculates hash index from keys and returns element in hash table
 */
/***************************************************************************/

void* hashTable_FindElement(HASHTABLE* psTable, int iKey1, int iKey2)
{
  UDWORD udwHashIndex;
  HASHNODE* psNode;

  /* get hashed index */
  udwHashIndex = hashTable_GetHashKey(psTable, iKey1, iKey2);

  /* check hash index within bounds */
  ASSERT((udwHashIndex>=0 && udwHashIndex<psTable->udwTableSize,
    "hashTable_GetElement: hash value %i too large for table size %i\n", udwHashIndex, psTable->udwTableSize));

  psNode = psTable->ppsNode[udwHashIndex];

  /* loop through node list to find element match */
  while (psNode != nullptr && !(psNode->iKey1 == iKey1 && psNode->iKey2 == iKey2)) { psNode = psNode->psNext; }

  if (psNode == nullptr)
    return FALSE;
  return psNode->psElement;
}

/***************************************************************************/

static void hashTable_SetNextNode(HASHTABLE* psTable, BOOL bMoveToNextNode)
{
  if ((bMoveToNextNode == TRUE) && (psTable->psNextNode != nullptr))
  {
    /* get next node */
    psTable->psNextNode = psTable->psNextNode->psNext;

    /* if next node NULL increment index */
    if (psTable->psNextNode == nullptr)
      psTable->sdwCurIndex++;
  }

  /* search through table for next allocated node */
  while (psTable->sdwCurIndex < psTable->udwTableSize && psTable->psNextNode == nullptr)
  {
    psTable->psNextNode = psTable->ppsNode[psTable->sdwCurIndex];
    if (psTable->psNextNode == nullptr)
      psTable->sdwCurIndex++;
  }

  /* reset pointer if table overrun */
  if (psTable->sdwCurIndex >= psTable->udwTableSize)
    psTable->psNextNode = nullptr;
}

/***************************************************************************/

BOOL hashTable_RemoveElement(HASHTABLE* psTable, void* psElement, int iKey1, int iKey2)
{
  UDWORD udwHashIndex;
  HASHNODE *psNode, *psPrev;

  /* get hashed index */
  udwHashIndex = hashTable_GetHashKey(psTable, iKey1, iKey2);

  /* init previous node pointer */
  psPrev = nullptr;

  /* get first node in table slot */
  psNode = psTable->ppsNode[udwHashIndex];

  /* loop through node list to find element match */
  while (psNode != nullptr && !(psElement == psNode->psElement && psNode->iKey1 == iKey1 && psNode->iKey2 == iKey2))
  {
    psPrev = psNode;
    psNode = psNode->psNext;
  }

  if (psNode == nullptr)
    return FALSE;
  /* remove from hash table */
  if (psPrev == nullptr)
    psTable->ppsNode[udwHashIndex] = psNode->psNext;
  else
    psPrev->psNext = psNode->psNext;

  /* set next node pointer to this one if necessary */
  if (psTable->psNextNode == psNode)
    psTable->psNextNode = psPrev;

  /* setup next node pointer */
  hashTable_SetNextNode(psTable, TRUE);

  delete[] static_cast<UBYTE*>(psNode->psElement);

  delete psNode;

  return TRUE;
}

/***************************************************************************/

void* hashTable_GetNext(HASHTABLE* psTable)
{
  void* psElement;

  if (psTable->psNextNode == nullptr)
    return nullptr;
  psElement = psTable->psNextNode->psElement;

  /* setup next node pointer */
  hashTable_SetNextNode(psTable, TRUE);

  return psElement;
}

/***************************************************************************/

void* hashTable_GetFirst(HASHTABLE* psTable)
{
  /* init current index and node to start of table */
  psTable->sdwCurIndex = 0;
  psTable->psNextNode = psTable->ppsNode[0];

  /* search through table for first allocated node */
  hashTable_SetNextNode(psTable, FALSE);

  /* return it */
  return hashTable_GetNext(psTable);
}

/***************************************************************************/
