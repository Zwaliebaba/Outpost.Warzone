#include "pch.h"
/***************************************************************************/

#include <limits.h>
#include <memory.h>

#include "Frame.h"

#include "PtrList.h"

/***************************************************************************/

extern void* g_ElementToBeRemoved;

/***************************************************************************/

static CRITICAL_SECTION critSecAudio;
/***************************************************************************/

static void ptrList_Init(PTRLIST* ptrList);

/***************************************************************************/

BOOL ptrList_Create(PTRLIST** ppsList, UDWORD udwInitElements, UDWORD udwExtElements, UDWORD udwElementSize)
{
  /* create ptr list struct */
  (*ppsList) = new (std::nothrow) PTRLIST[1];

  /* init members */
  (*ppsList)->udwElements = udwInitElements;
  (*ppsList)->udwExtElements = udwExtElements;
  (*ppsList)->udwElementSize = udwElementSize;

  ptrList_Init(*ppsList);
  InitializeCriticalSection(&critSecAudio);
  return TRUE;
}

/***************************************************************************/

void ptrList_Destroy(PTRLIST* ptrList)
{
  ptrList_Clear(ptrList);

  /* free struct */
  delete[] ptrList;
  ptrList = nullptr;
  DeleteCriticalSection(&critSecAudio);
}

/***************************************************************************/

static void ptrList_Init(PTRLIST* ptrList)
{
  ptrList->sdwCurIndex = 0;
  ptrList->psCurNode = nullptr;
  ptrList->psNode = nullptr;
  ptrList->bDontGetNext = FALSE;
}

/***************************************************************************/
/*
 * ptrList_Clear
 *
 * Returns all nodes from hash table to free node list
 */
/***************************************************************************/

void ptrList_Clear(PTRLIST* ptrList)
{
  LISTNODE *psNode, *psNodeTmp;

  /* free nodes */
  psNode = ptrList->psNode;

  while (psNode != nullptr)
  {
    delete[] static_cast<UBYTE*>(psNode->psElement);

    psNodeTmp = psNode->psNext;
    delete psNode;
    psNode = psNodeTmp;
  }

  ptrList_Init(ptrList);
}

/***************************************************************************/
/*
 * ptrList_GetElement
 *
 * Gets free node from heap and returns element pointer
 */
/***************************************************************************/

void* ptrList_GetElement(PTRLIST* ptrList)
{
  void* psElement;

  psElement = new (std::nothrow) UBYTE[ptrList->udwElementSize];

  return psElement;
}

/***************************************************************************/
/*
 * ptrList_FreeElement 
 *
 * Free element that was allocated using ptrList_GetElement without
 * inserting in list: will fail if element not allocated from ptrList
 */
/***************************************************************************/

void ptrList_FreeElement(PTRLIST* ptrList, void* psElement)
{
  (void)ptrList;
  delete[] static_cast<UBYTE*>(psElement);
}

/***************************************************************************/

void ptrList_InsertElement(PTRLIST* ptrList, void* psElement, SDWORD sdwKey)
{
  LISTNODE *psNode, *psCurNode, *psPrevNode;

  psNode = new (std::nothrow) LISTNODE;

  /* set node elements */
  psNode->sdwKey = sdwKey;
  psNode->psElement = psElement;
  psNode->psNext = nullptr;

  psPrevNode = nullptr;
  psCurNode = ptrList->psNode;
  EnterCriticalSection(&critSecAudio);

  /* find correct position to insert node */
  while (psCurNode != nullptr)
  {
    if (psCurNode->sdwKey < sdwKey)
      break;

    psPrevNode = psCurNode;
    psCurNode = psCurNode->psNext;
  }

  /* insert node */
  if (psPrevNode == nullptr)
  {
    ptrList->psNode = psNode;

    if (psCurNode != nullptr)
      psNode->psNext = psCurNode;
  }
  else
  {
    psPrevNode->psNext = psNode;
    psNode->psNext = psCurNode;
  }
  LeaveCriticalSection(&critSecAudio);
}

/***************************************************************************/

BOOL ptrList_RemoveElement(PTRLIST* ptrList, void* psElement, SDWORD sdwKey)
{
  LISTNODE *psCurNode, *psPrevNode;
  BOOL bOK;

  psPrevNode = nullptr;
  psCurNode = ptrList->psNode;
  EnterCriticalSection(&critSecAudio);

  /* find correct position to insert node */
  while (psCurNode != nullptr && !(psCurNode->sdwKey == sdwKey && psCurNode->psElement == psElement))
  {
    psPrevNode = psCurNode;
    psCurNode = psCurNode->psNext;
  }

  if (psCurNode == nullptr)
    bOK = FALSE;
  else
  {
    ASSERT((psCurNode->psElement == psElement,"ptrList_RemoveElement: removing wrong element!\n"));

    /* remove from list */
    if (psPrevNode == nullptr)
      ptrList->psNode = psCurNode->psNext;
    else
      psPrevNode->psNext = psCurNode->psNext;

    /* check whether table current node pointer is this node */
    if (ptrList->psCurNode == psCurNode)
    {
      /* point it to the previous node if valid */
      if (psPrevNode == nullptr)
      {
        /* set next node and set flag */
        ptrList->psCurNode = psCurNode->psNext;
        ptrList->bDontGetNext = TRUE;
      }
      else
        ptrList->psCurNode = psPrevNode;
    }

    ASSERT((psCurNode->psElement == psElement, "ptrList_RemoveElement: removing wrong element!\n"));
    delete[] static_cast<UBYTE*>(psCurNode->psElement);

    delete psCurNode;

    bOK = TRUE;
  }
  LeaveCriticalSection(&critSecAudio);
  return bOK;
}

/***************************************************************************/

void* ptrList_GetNext(PTRLIST* ptrList)
{
  void* pElement = nullptr;
  EnterCriticalSection(&critSecAudio);
  if (ptrList == nullptr)
    pElement = nullptr;

  if (ptrList->psCurNode == nullptr)
    pElement = nullptr;
  else
  {
    if (ptrList->bDontGetNext == TRUE)
      ptrList->bDontGetNext = FALSE;
    else
      ptrList->psCurNode = ptrList->psCurNode->psNext;

    if (ptrList->psCurNode == nullptr)
      pElement = nullptr;
    else
      pElement = ptrList->psCurNode->psElement;
  }
  LeaveCriticalSection(&critSecAudio);
  return pElement;
}

/***************************************************************************/

void* ptrList_GetFirst(PTRLIST* ptrList)
{
  void* pElement = nullptr;
  EnterCriticalSection(&critSecAudio);
  ptrList->bDontGetNext = FALSE;
  ptrList->psCurNode = ptrList->psNode;

  if (ptrList->psCurNode == nullptr)
    pElement = nullptr;
  else
    pElement = ptrList->psCurNode->psElement;
  LeaveCriticalSection(&critSecAudio);
  return pElement;
}

/***************************************************************************/
