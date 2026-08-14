/***************************************************************************/
/*
 * Priority queue code
 *
 */
/***************************************************************************/

#ifndef _PQUEUE_H_
#define _PQUEUE_H_

/***************************************************************************/

using QUEUE_CLEAR_FUNC = void(*)(void* psElement);

using QUEUE_NODE = struct QUEUE_NODE
{
  void* psElement;
  int iPriority;
  struct QUEUE_NODE* psPrev;
  struct QUEUE_NODE* psNext;
};

using QUEUE = struct QUEUE
{
  QUEUE_NODE* psFreeNodeList;
  QUEUE_NODE* psNodeQHead;
  QUEUE_NODE* psCurNode;
  int iElementSize;
  int iMaxElements;
  int iFreeNodes;
  int iQueueNodes;

  QUEUE_CLEAR_FUNC pfClearFunc;
};

/***************************************************************************/

extern BOOL queue_Init(QUEUE** ppQueue, int iMaxElements, int iElementSize, QUEUE_CLEAR_FUNC pfClearFunc);
extern void queue_Destroy(QUEUE* pQueue);
extern void queue_Clear(QUEUE* pQueue);

extern void queue_Enqueue(QUEUE* pQueue, void* psElement, int iPriority);
extern void* queue_Dequeue(QUEUE* pQueue);
extern QUEUE_NODE* queue_FindElement(QUEUE* pQueue, void* psElement);
extern BOOL queue_RemoveElement(QUEUE* pQueue, void* psElement);
extern void* queue_GetHead(QUEUE* pQueue);
extern void* queue_GetNext(QUEUE* pQueue);
extern BOOL queue_RemoveCurrent(QUEUE* pQueue);
extern BOOL queue_RemoveNode(QUEUE* pQueue, QUEUE_NODE* psNode);
extern SDWORD queue_GetFreeElements(QUEUE* pQueue);

/***************************************************************************/

#endif	// _PQUEUE_H_

/***************************************************************************/
