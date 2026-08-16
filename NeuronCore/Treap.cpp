#include "pch.h"

/* Allow frame header files to be singly included */
#define FRAME_LIB_INCLUDE

#include "Types.h"
#include "Treap.h"

/* Position of the last call */
static SDWORD cLine;
static STRING* pCFile;
static STRING pCFileNone[] = "None";

void treapSetCallPos(const STRING* pFileName, SDWORD lineNumber)
{
  cLine = lineNumber;

  pCFile = new (std::nothrow) STRING[strlen(pFileName) + 1];
  if (pCFile)
    strcpy(pCFile, pFileName);
  else
    pCFile = pCFileNone;
}

/* Default comparison function - assumes keys are ints */
static SDWORD defaultCmp(UDWORD key1, UDWORD key2)
{
  if (key1 < key2)
    return -1;
  if (key1 > key2)
    return 1;

  return 0;
}

/* A useful comparison function - keys are string pointers */
SDWORD treapStringCmp(UDWORD key1, UDWORD key2)
{
  SDWORD result;
  auto pStr1 = (STRING*)key1;
  auto pStr2 = (STRING*)key2;

  result = strcmp(pStr1, pStr2);
  if (result < 0)
    return -1;
  if (result > 0)
    return 1;
  return 0;
}

/* Function to create a treap
 * Pass in key comparison function,
 * initial number of nodes to allocate,
 * number of additional nodes to allocate when extending.
 */
BOOL treapCreate(TREAP** ppsTreap, TREAP_CMP cmp, UDWORD init, UDWORD ext)
{
  *ppsTreap = new (std::nothrow) TREAP[1];
  if (!(*ppsTreap))
  {
    Neuron::Fatal("treapCreate: Out of memory");
    return FALSE;
  }

  // Store the comparison function if there is one, use the default otherwise
  if (cmp)
    (*ppsTreap)->cmp = cmp;
  else
    (*ppsTreap)->cmp = defaultCmp;

  // Initialise the tree to nothing
  (*ppsTreap)->psRoot = nullptr;

#if DEBUG_TREAP
  // Store the call location
  (*ppsTreap)->pFile = pCFile;
  (*ppsTreap)->line = cLine;
#endif
}

/* Rotate a tree to the right
 * (Make left sub tree the root and the root the right sub tree)
 */
static void treapRotRight(TREAP_NODE** ppsRoot)
{
  TREAP_NODE* psNewRoot;

  psNewRoot = (*ppsRoot)->psLeft;
  (*ppsRoot)->psLeft = psNewRoot->psRight;
  psNewRoot->psRight = *ppsRoot;
  *ppsRoot = psNewRoot;
}

/* Rotate a tree to the left
 * (Make right sub tree the root and the root the left sub tree)
 */
static void treapRotLeft(TREAP_NODE** ppsRoot)
{
  TREAP_NODE* psNewRoot;

  psNewRoot = (*ppsRoot)->psRight;
  (*ppsRoot)->psRight = psNewRoot->psLeft;
  psNewRoot->psLeft = *ppsRoot;
  *ppsRoot = psNewRoot;
}

/* Recursive function to add an object to a tree */
void treapAddNode(TREAP_NODE** ppsRoot, TREAP_NODE* psNew, TREAP_CMP cmp)
{
  if (*ppsRoot == nullptr)
  {
    // Make the node the root of the tree
    *ppsRoot = psNew;
  }
  else if (cmp(psNew->key, (*ppsRoot)->key) < 0)
  {
    // Node less than root, insert to the left of the tree
    treapAddNode(&(*ppsRoot)->psLeft, psNew, cmp);

    // Sort the priority
    if ((*ppsRoot)->priority > (*ppsRoot)->psLeft->priority)
    {
      // rotate tree right
      treapRotRight(ppsRoot);
    }
  }
  else
  {
    // Node greater than root, insert to the right of the tree
    treapAddNode(&(*ppsRoot)->psRight, psNew, cmp);

    // Sort the priority
    if ((*ppsRoot)->priority > (*ppsRoot)->psRight->priority)
    {
      // rotate tree left
      treapRotLeft(ppsRoot);
    }
  }
}

/* Add an object to a treap
 */
BOOL treapAdd(TREAP* psTreap, UDWORD key, void* pObj)
{
  TREAP_NODE* psNew;

  psNew = new (std::nothrow) TREAP_NODE;
  if (psNew == nullptr)
    return FALSE;
  psNew->priority = static_cast<UDWORD>(rand());
  psNew->key = key;
  psNew->pObj = pObj;
  psNew->psLeft = nullptr;
  psNew->psRight = nullptr;
#if DEBUG_TREAP
  // Store the call location
  psNew->pFile = pCFile;
  psNew->line = cLine;
#endif

  treapAddNode(&psTreap->psRoot, psNew, psTreap->cmp);

  return TRUE;
}

/* Recursively find and remove a node from the tree */
TREAP_NODE* treapDelRec(TREAP_NODE** ppsRoot, UDWORD key, TREAP_CMP cmp)
{
  TREAP_NODE* psFound;

  if (*ppsRoot == nullptr)
  {
    // not found
    return nullptr;
  }

  switch (cmp(key, (*ppsRoot)->key))
  {
  case -1:
    // less than
    return treapDelRec(&(*ppsRoot)->psLeft, key, cmp);
    break;
  case 1:
    // greater than
    return treapDelRec(&(*ppsRoot)->psRight, key, cmp);
    break;
  case 0:
    // equal - either remove or push down the tree to balance it
    {
      // equal - either remove or push down the tree to balance it
      if ((*ppsRoot)->psLeft == nullptr && (*ppsRoot)->psRight == nullptr)
      {
        // no sub trees, remove
        psFound = *ppsRoot;
        *ppsRoot = nullptr;
        return psFound;
      }
      if ((*ppsRoot)->psLeft == nullptr)
      {
        // one sub tree, replace
        psFound = *ppsRoot;
        *ppsRoot = psFound->psRight;
        return psFound;
      }
      if ((*ppsRoot)->psRight == nullptr)
      {
        // one sub tree, replace
        psFound = *ppsRoot;
        *ppsRoot = psFound->psLeft;
        return psFound;
      }
      // two sub trees, push the node down and recurse
      if ((*ppsRoot)->psLeft->priority > (*ppsRoot)->psRight->priority)
      {
        // rotate right and recurse
        treapRotLeft(ppsRoot);
        return treapDelRec(&(*ppsRoot)->psLeft, key, cmp);
      }
      // rotate left and recurse
      treapRotRight(ppsRoot);
      return treapDelRec(&(*ppsRoot)->psRight, key, cmp);
    }
    break;
  default: DEBUG_ASSERT_TEXT(FALSE, "treapDelRec: invalid return from comparison");
    break;
  }
}

/* Remove an object from the treap */
BOOL treapDel(TREAP* psTreap, UDWORD key)
{
  TREAP_NODE* psDel;

  // Find the node to remove
  psDel = treapDelRec(&psTreap->psRoot, key, psTreap->cmp);
  if (!psDel)
    return FALSE;

  // Release the node
#ifdef DEBUG
  delete[] psDel->pFile;
  psDel->pFile = nullptr;
#endif
  delete psDel;

  return TRUE;
}

/* Recurisvely find an object in a treap */
void* treapFindRec(TREAP_NODE* psRoot, UDWORD key, TREAP_CMP cmp)
{
  if (psRoot == nullptr)
    return nullptr;

  switch (cmp(key, psRoot->key))
  {
  case 0:
    // equal
    return psRoot->pObj;
    break;
  case -1:
    return treapFindRec(psRoot->psLeft, key, cmp);
    break;
  case 1:
    return treapFindRec(psRoot->psRight, key, cmp);
    break;
  default: DEBUG_ASSERT_TEXT(FALSE, "treapFindRec: invalid return from comparison");
    break;
  }
}

/* Find an object in a treap */
void* treapFind(TREAP* psTreap, UDWORD key) { return treapFindRec(psTreap->psRoot, key, psTreap->cmp); }

#if DEBUG_TREAP
/* Recursively print out where the nodes were allocated */
static void treapReportRec(TREAP_NODE* psRoot)
{
  if (psRoot)
  {
    Neuron::DebugTrace("   {}, line {}\n", psRoot->pFile, psRoot->line);
    treapReportRec(psRoot->psLeft);
    treapReportRec(psRoot->psRight);
  }
}
#endif

/* Recursively free a treap */
static void treapDestroyRec(TREAP_NODE* psRoot)
{
  if (psRoot == nullptr)
    return;

  // free the sub branches
  treapDestroyRec(psRoot->psLeft);
  treapDestroyRec(psRoot->psRight);

  // free the root
  delete psRoot;
}

/* Release all the nodes in the treap */
void treapReset(TREAP* psTreap)
{
  treapDestroyRec(psTreap->psRoot);
  psTreap->psRoot = nullptr;
}

/* Destroy a treap and release all the memory associated with it */
void treapDestroy(TREAP* psTreap)
{
#if DEBUG_TREAP
  if (psTreap->psRoot)
  {
    Neuron::DebugTrace("treapDestroy: {}, line {} : nodes still in the tree\n", psTreap->pFile, psTreap->line);
    treapReportRec(psTreap->psRoot);
  }
  delete[] psTreap->pFile;
  psTreap->pFile = nullptr;
#endif

  treapDestroyRec(psTreap->psRoot);
  delete[] psTreap;
  psTreap = nullptr;
}

/* Recursively display the treap structure */
void treapDisplayRec(TREAP_NODE* psRoot, UDWORD indent)
{
  UDWORD i;

  // Display the root
#if DEBUG_TREAP
  Neuron::DebugTrace("{}, line {} : {},{}\n", psRoot->pFile, psRoot->line, psRoot->key, psRoot->priority);
#else
  Neuron::DebugTrace("{},{}\n", psRoot->key, psRoot->priority);
#endif

  // Display the left of the tree
  if (psRoot->psLeft)
  {
    for (i = 0; i < indent; i++)
      Neuron::DebugTrace("   ");
    Neuron::DebugTrace("L ");
    treapDisplayRec(psRoot->psLeft, indent + 1);
  }

  // Display the right of the tree
  if (psRoot->psRight)
  {
    for (i = 0; i < indent; i++)
      Neuron::DebugTrace("   ");
    Neuron::DebugTrace("R ");
    treapDisplayRec(psRoot->psRight, indent + 1);
  }
}

/* Display the treap structure using DBPRINTF */
void treapDisplay(TREAP* psTreap)
{
  if (psTreap->psRoot)
    treapDisplayRec(psTreap->psRoot, 0);
}

void* treapGetSmallestRec(TREAP_NODE* psRoot)
{
  if (psRoot->psLeft == nullptr)
    return psRoot->pObj;

  return treapGetSmallestRec(psRoot->psLeft);
}

/* Return the object with the smallest key in the treap
 * This is useful if the objects in the treap need to be
 * deallocated.  i.e. getSmallest, delete from treap, free memory
 */
void* treapGetSmallest(TREAP* psTreap)
{
  if (psTreap->psRoot == nullptr)
    return nullptr;

  return treapGetSmallestRec(psTreap->psRoot);
}
