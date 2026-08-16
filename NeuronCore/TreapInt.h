/*
 * TreapInt.h
 *
 * Internal function calls for the treap system to allow it
 * to be used by the memory system.
 *
 */
#ifndef _treapint_h
#define _treapint_h

/* Recursive function to add an object to a tree */
extern void treapAddNode(TREAP_NODE** ppsRoot, TREAP_NODE* psNew, TREAP_CMP cmp);

/* Recursively find and remove a node from the tree */
extern TREAP_NODE* treapDelRec(TREAP_NODE** ppsRoot, UDWORD key, TREAP_CMP cmp);

/* Recurisvely find an object in a treap */
extern void* treapFindRec(TREAP_NODE* psRoot, UDWORD key, TREAP_CMP cmp);

/* Recursively display the treap structure */
extern void treapDisplayRec(TREAP_NODE* psRoot, UDWORD indent);

#endif
