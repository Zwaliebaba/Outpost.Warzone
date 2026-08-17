/*
 * CodePrint.h
 *
 * Routines for displaying compiled scripts
 *
 */
#ifndef _codeprint_h
#define _codeprint_h

/* Display a value type */
extern void cpPrintType(INTERP_TYPE type);

/* Display a value  */
extern void cpPrintVal(INTERP_VAL* psVal);

/* Print the name of an OP_CALL / OP_SCRIPTCALL callee from its packed form */
extern void cpPrintCallee(UDWORD packed);

/* Print the name of an OP_VARCALL callee from its packed form */
extern void cpPrintVarCallee(UDWORD packed);

/* Display a maths operator */
extern void cpPrintMathsOp(UDWORD opcode);

#endif
