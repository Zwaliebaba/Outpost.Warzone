#include "pch.h"

/*
 * Frame.cpp
 *
 * The framework services that do not need a display: loading a file, saving
 * one, hashing a name, and the integer percentages.
 *
 * The window, the message pump and the cursors were here too until the
 * NeuronCore/NeuronClient split; they are Window.cpp in NeuronClient now, and
 * the frame counters are in GTime.cpp.
 */

#include "Frame.h"
#include "FrameResource.h"

#include <assert.h>

// string buffer for windows error string
static STRING winErrorString[255];

// Return a string for a windows error code
STRING* winErrorToString(SDWORD error)
{
  LPVOID lpMsgBuf;

  FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                // Default language
                (LPTSTR)&lpMsgBuf, 0, nullptr);

  // Copy the string.
  strncpy(winErrorString, static_cast<const char*>(lpMsgBuf), 254);
  winErrorString[255] = '0';

  // Free the buffer.
  LocalFree(lpMsgBuf);

  return winErrorString;
}

BOOL loadFile(STRING* pFileName, UBYTE** ppFileData, UDWORD* pFileSize) { return (loadFile2(pFileName, ppFileData, pFileSize,TRUE)); }

/* Load the file with name pointed to by pFileName into a memory buffer. */
// if allocate mem is true then the memory is allocated ... else it is already in ppFileData, and the max size is in pFileSize ... this is adjusted to the actual loaded file size
//   
BOOL loadFile2(STRING* pFileName, UBYTE** ppFileData, UDWORD* pFileSize, BOOL AllocateMem)
{
  FILE* pFileHandle = fopen(pFileName, "rb");
  if (pFileHandle == nullptr)
  {
    Neuron::Fatal("Couldn't open {}", pFileName);
    return FALSE;
  }

  /* Get the length of the file */
  if (fseek(pFileHandle, 0, SEEK_END) != 0)
  {
    Neuron::Fatal("SEEK_END failed for {}", pFileName);
    return FALSE;
  }
  UDWORD FileSize = ftell(pFileHandle);
  if (fseek(pFileHandle, 0, SEEK_SET) != 0)
  {
    Neuron::Fatal("SEEK_SET failed for {}", pFileName);
    return FALSE;
  }

  if (AllocateMem == TRUE)
  {
    /* Allocate a buffer to store the data and a terminating zero */
    // we don't want this popping up in the tools (makewdg)
    *ppFileData = new (std::nothrow) UBYTE[(FileSize) + 1];
    if (*ppFileData == nullptr)
    {
      Neuron::Fatal("Out of memory");
      return FALSE;
    }
  }
  else
  {
    if (FileSize > *pFileSize)
    {
      Neuron::Fatal("no room for file");
      return FALSE;
    }
    assert(*ppFileData!=NULL);
  }
  /* Load the file data */
  if (fread(*ppFileData, 1, FileSize, pFileHandle) != FileSize)
  {
    Neuron::Fatal("Read failed for {}", pFileName);
    return FALSE;
  }

  if (fclose(pFileHandle) != 0)
  {
    Neuron::Fatal("Close failed for {}", pFileName);
    return FALSE;
  }

  // Add the terminating zero
  *((*ppFileData) + FileSize) = 0;
  *pFileSize = FileSize; // always set to correct size
  return TRUE;
}

// load a file from disk into a fixed memory buffer
BOOL loadFileToBuffer(STRING* pFileName, UBYTE* pFileBuffer, UDWORD bufferSize, UDWORD* pSize)
{
  DWORD bytesRead;

  // try and open the file
  HANDLE hFile = CreateFile(pFileName, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (hFile == INVALID_HANDLE_VALUE)
  {
    Neuron::Fatal("Couldn't open {}\n{}", pFileName, winErrorToString(GetLastError()));
    return FALSE;
  }

  // get the size of the file
  *pSize = GetFileSize(hFile, nullptr);
  if (*pSize >= bufferSize)
  {
    Neuron::Fatal("file too big !!:{} size {}\n", pFileName, *pSize);
    return FALSE;
  }

  // load the file into the buffer
  BOOL retVal = ReadFile(hFile, pFileBuffer, *pSize, &bytesRead, nullptr);
  if (!retVal || *pSize != bytesRead)
  {
    Neuron::Fatal("Couldn't read data from {}\n{}", pFileName, winErrorToString(GetLastError()));
    return FALSE;
  }
  pFileBuffer[*pSize] = 0;

  retVal = CloseHandle(hFile);
  if (!retVal)
  {
    Neuron::Fatal("Couldn't close {}\n{}", pFileName, winErrorToString(GetLastError()));
    return FALSE;
  }

  return TRUE;
}

// as above but returns quietly if no file found
BOOL loadFileToBufferNoError(STRING* pFileName, UBYTE* pFileBuffer, UDWORD bufferSize, UDWORD* pSize)
{
  DWORD bytesRead;

  // try and open the file
  HANDLE hFile = CreateFile(pFileName, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (hFile == INVALID_HANDLE_VALUE)
    return FALSE;

  // get the size of the file
  *pSize = GetFileSize(hFile, nullptr);
  if (*pSize >= bufferSize)
    return FALSE;

  // load the file into the buffer
  BOOL retVal = ReadFile(hFile, pFileBuffer, *pSize, &bytesRead, nullptr);
  if (!retVal || *pSize != bytesRead)
    return FALSE;
  pFileBuffer[*pSize] = 0;

  retVal = CloseHandle(hFile);
  if (!retVal)
    return FALSE;

  return TRUE;
}

/* Save the data in the buffer into the given file */
BOOL saveFile(STRING* pFileName, UBYTE* pFileData, UDWORD fileSize)
{
  /* open the file */
  FILE* pFile = fopen(pFileName, "wb");
  if (!pFile)
  {
    Neuron::Fatal("Couldn't open {}", pFileName);
    return FALSE;
  }

  if (fwrite(pFileData, fileSize, 1, pFile) != 1)
  {
    Neuron::Fatal("Write failed for {}: {}", pFileName, winErrorToString(GetLastError()) );
    return FALSE;
  }

  if (fclose(pFile) != 0)
  {
    Neuron::Fatal("Close failed for {}", pFileName);
    return FALSE;
  }

  return TRUE;
}

/* next four used in HashString / HashStringIgnoreCase */
#define	BITS_IN_int		32
#define	THREE_QUARTERS	((UINT) ((BITS_IN_int * 3) / 4))
#define	ONE_EIGHTH		((UINT) (BITS_IN_int / 8))
#define	HIGH_BITS		( ~((UINT)(~0) >> ONE_EIGHTH ))

///////////////////////////////////////////////////////////////////

/***************************************************************************/
/*
 * HashString
 *
 * Adaptation of Peter Weinberger's (PJW) generic hashing algorithm listed
 * in Binstock+Rex, "Practical Algorithms" p 69.
 *
 * Accepts string and returns hashed integer.
 */
/***************************************************************************/
UINT HashString(const char* String)
{
  UINT iHashValue, i;
  auto c = String;

  assert(String!=NULL);
  assert(*String!=0x0);

  for (iHashValue = 0; *c; ++c)
  {
    iHashValue = (iHashValue << ONE_EIGHTH) + *c;

    if ((i = iHashValue & HIGH_BITS) != 0)
      iHashValue = (iHashValue ^ (i >> THREE_QUARTERS)) & ~HIGH_BITS;
  }

  return iHashValue;
}

UINT HashStringIgnoreCase(const char* String)
{
  UINT iHashValue, i;
  auto c = String;

  assert(String!=NULL);
  assert(*String!=0x0);

  for (iHashValue = 0; *c; ++c)
  {
    iHashValue = (iHashValue << ONE_EIGHTH) + ((*c) & (0xdf));

    if ((i = iHashValue & HIGH_BITS) != 0)
      iHashValue = (iHashValue ^ (i >> THREE_QUARTERS)) & ~HIGH_BITS;
  }

  return iHashValue;
}

// Examine a filename for the last dot and slash
// and so giving the extension of the file and the directory
//
// PosOfDot and/of PosOfSlash can be NULL and then nothing will be stored
//
void ScanFilename(char* Fullname, int* PosOfDot, int* PosOfSlash)
{
  int DotPos = -1;
  int SlashPos = -1;
  int Pos;

  int Namelength = static_cast<int>(strlen(Fullname));

  for (Pos = Namelength; Pos >= 0; Pos--)
  {
    if (Fullname[Pos] == '.')
    {
      DotPos = Pos;
      break;
    }
  }

  for (Pos = Namelength; Pos >= 0; Pos--)
  {
    if (Fullname[Pos] == '\\')
    {
      SlashPos = Pos;
      break;
    }
  }

  if (PosOfDot != nullptr)
    *PosOfDot = DotPos;

  if (PosOfSlash != nullptr)
    *PosOfSlash = SlashPos;
}

#ifdef DEBUG

SDWORD PercentFunc(char* File, UDWORD Line, SDWORD a, SDWORD b)
{
  if (b)
    return (a * 100) / b;
  Neuron::DebugTrace("Divide by 0 (PERCENT) in {},line {}\n",File,Line);

  return 100;
}

SDWORD PerNumFunc(char* File, UDWORD Line, SDWORD range, SDWORD a, SDWORD b)
{
  if (b)
    return (a * range) / b;

  Neuron::DebugTrace("Divide by 0 (PERNUM) in {},line {}\n",File,Line);

  return range;
}

#endif

