/*
 * Frame.h
 *
 * The framework library initialisation and shutdown routines.
 *
 */
#ifndef _frame_h
#define _frame_h

#pragma warning (disable : 4201 4214 4115 4514)
#include <windows.h>
#pragma warning (default : 4201 4214 4115)

// Mem.h used to pull this in for everything that includes Frame.h
#include <stdlib.h>

#include "Types.h"

/* Integer percentages. These lived in Fractions.h, which has gone - they were
 * never fraction arithmetic, and the functions they call in debug builds to
 * report a divide by zero are in Frame.cpp.
 */
#ifdef DEBUG

SDWORD PercentFunc(char* File, UDWORD Line, SDWORD a, SDWORD b);
SDWORD PerNumFunc(char* File, UDWORD Line, SDWORD range, SDWORD a, SDWORD b);

#define PERCENT(a,b)       PercentFunc(__FILE__,__LINE__,a,b)
#define PERNUM(range,a,b)  PerNumFunc(__FILE__,__LINE__,range,a,b)

#else

#define PERCENT(a,b)       (((a)*100)/(b))
#define PERNUM(range,a,b)  (((a)*range)/(b))

#endif

/* Load the file with name pointed to by pFileName into a memory buffer. */
extern BOOL loadFile(STRING* pFileName, // The filename
                     UBYTE** ppFileData, // A buffer containing the file contents
                     UDWORD* pFileSize); // The size of this buffer

/* Load the file with name pointed to by pFileName into a memory buffer. */
// if allocate mem is true then the memory is allocated ... else it is already in ppFileData, and the max size is in pFileSize ... this is adjusted to the actual loaded file size
//   
BOOL loadFile2(STRING* pFileName, UBYTE** ppFileData, UDWORD* pFileSize, BOOL AllocateMem);

/* Save the data in the buffer into the given file */
extern BOOL saveFile(STRING* pFileName, UBYTE* pFileData, UDWORD fileSize);

// load a file from disk into a fixed memory buffer
extern BOOL loadFileToBuffer(STRING* pFileName, UBYTE* pFileBuffer, UDWORD bufferSize, UDWORD* pSize);
// as above but returns quietly if no file found
extern BOOL loadFileToBufferNoError(STRING* pFileName, UBYTE* pFileBuffer, UDWORD bufferSize, UDWORD* pSize);

// Return a string for a windows error code
extern STRING* winErrorToString(SDWORD error);

extern SDWORD ftol(float f);

UINT HashString(const char* String);
UINT HashStringIgnoreCase(const char* String);

#endif
