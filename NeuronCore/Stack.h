/*
 * Stack.h
 *
 * Interface to the stack system
 */
#ifndef _stack_h
#define _stack_h

#include <initializer_list>

/* One destination for stackPopParams.  Which constructor the call site
   selects fixes the store width at compile time: 32-bit scalars stay
   32-bit stores, and object/string/ref destinations - anything passed as
   a pointer-to-pointer - get pointer-wide stores.  The old varargs form
   wrote every parameter as 4 bytes, which truncated object pointers on
   x64 through destinations that were really DROID** or STRING**. */
struct ScriptParam
{
  INTERP_TYPE type;
  bool wide;
  void* pDest;

  ScriptParam(INTERP_TYPE _type, SDWORD* _dest) : type(_type), wide(false), pDest(_dest) {}
  ScriptParam(INTERP_TYPE _type, UDWORD* _dest) : type(_type), wide(false), pDest(_dest) {}

  template <typename T>
  ScriptParam(INTERP_TYPE _type, T** _dest) : type(_type), wide(true), pDest(_dest) {}
};

/* Initialise the stack */
extern BOOL stackInitialise(void);

/* Shutdown the stack */
extern void stackShutDown(void);

/* Push a value onto the stack */
extern BOOL stackPush(INTERP_VAL* psVal);

/* Pop a value off the stack */
extern BOOL stackPop(INTERP_VAL* psVal);

/* Pop a value off the stack, checking that the type matches what is passed in */
extern BOOL stackPopType(INTERP_VAL* psVal);

/* Look at a value on the stack without removing it.
 * index is how far down the stack to look.
 * Index 0 is the top entry on the stack.
 */
extern BOOL stackPeek(INTERP_VAL* psVal, UDWORD index);

/* Print the top value on the stack */
extern void stackPrintTop(void);

/* Check if the stack is empty */
extern BOOL stackEmpty(void);

/* Do binary operations on the top of the stack
 * This effectively pops two values and pushes the result
 */
extern BOOL stackBinaryOp(OPCODE opcode);

/* Perform a unary operation on the top of the stack
 * This effectively pops a value and pushes the result
 */
extern BOOL stackUnaryOp(OPCODE opcode);

/* Reset the stack to an empty state */
extern void stackReset(void);

#endif
