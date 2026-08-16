#include "pch.h"
#include "Frame.h"
#include "FrameResource.h"
#include "ResLY.h"

// Local prototypes
static void ReleaseWRF(UBYTE** pBuffer);

static RES_TYPE* psResTypes = nullptr;

// check to see if RES_TYPE entry is valie

#define resValidType(Type) (Type)
#define resNextType(Type)  (Type->psNext)
#define resGetResDataPointer(psRes) (psRes->pData)
#define resGetResBlockID(psRes) (psRes->blockID)

/* The initial resource directory and the current resource directory */
STRING aResDir[FILE_MAXCHAR];
STRING aCurrResDir[FILE_MAXCHAR];

// the current resource block ID
static SDWORD resBlockID;

// buffer to load file data into
static UBYTE* pFileBuffer = nullptr;
static SDWORD fileBufferSize = 0;

void ResetResourceFile(void);

// callback to resload screen.
static RESLOAD_CALLBACK resLoadCallback = nullptr;
static RESPRELOAD_CALLBACK resPreLoadCallback = nullptr;

void resSetPreLoadCallback(RESPRELOAD_CALLBACK funcToCall) { resPreLoadCallback = funcToCall; }

/* set the callback function for the res loader*/
VOID resSetLoadCallback(RESLOAD_CALLBACK funcToCall) { resLoadCallback = funcToCall; }

/* do the callback for the resload display function */
VOID resDoResLoadCallback()
{
  if (resLoadCallback)
    resLoadCallback();
}

/* Initialise the resource module */
BOOL resInitialise(void)
{
  DEBUG_ASSERT_TEXT(psResTypes == NULL, "resInitialise: resource module hasn't been shut down??");
  psResTypes = nullptr;

  resBlockID = 0;

  resLoadCallback = nullptr;
  resPreLoadCallback = nullptr;

  ResetResourceFile();

  return TRUE;
}

/* Shutdown the resource module */
void resShutDown(void)
{
  if (psResTypes != nullptr)
  {
    Neuron::DebugTrace("resShutDown: warning resources still allocated");
    resReleaseAll();
  }
}

// set the base resource directory
void resSetBaseDir(STRING* pResDir) { strncpy(aResDir, pResDir, FILE_MAXCHAR - 1); }

/* Parse the res file */
BOOL resLoad(STRING* pResFile, SDWORD blockID, UBYTE* pLoadBuffer, SDWORD bufferSize)
{
  UBYTE* pBuffer;
  UDWORD size;

  strcpy(aCurrResDir, aResDir);

  // Note the buffer for file data
  pFileBuffer = pLoadBuffer;
  fileBufferSize = bufferSize;
  // Note the block id number
  resBlockID = blockID;

  // Load the RES file
  if (!LoadWRF(pResFile, &pBuffer, &size))
    return FALSE;

  // and parse it
  resSetInputBuffer(pBuffer, size);
  if (res_parse() != 0)
    return FALSE;

  ReleaseWRF(&pBuffer);

  return TRUE;
}

/* Allocate a RES_TYPE structure */
static BOOL resAlloc(STRING* pType, RES_TYPE** ppsFunc)
{
  RES_TYPE* psT;

#ifdef DEBUG
  // Check for a duplicate type
  for (psT = psResTypes; psT; psT = psT->psNext)
  {
    DEBUG_ASSERT_TEXT(strcmp(psT->aType, pType) != 0, "resAlloc: Duplicate function for type: {}", pType);
  }
#endif

  // Allocate the memory
  psT = new (std::nothrow) RES_TYPE[1];
  if (!psT)
  {
    Neuron::Fatal("resAlloc: Out of memory");
    return FALSE;
  }

  // setup the structure
  strncpy(psT->aType, pType, RESTYPE_MAXCHAR - 1);
  psT->aType[RESTYPE_MAXCHAR - 1] = 0;

  psT->HashedType = HashString(psT->aType); // store a hased version for super speed !

  psT->psRes = nullptr;

  *ppsFunc = psT;

  return TRUE;
}

/* Add a buffer load function for a file type */
BOOL resAddBufferLoad(STRING* pType, RES_BUFFERLOAD buffLoad, RES_FREE release)
{
  RES_TYPE* psT;

  if (!resAlloc(pType, &psT))
    return FALSE;

  psT->buffLoad = buffLoad;
  psT->release = release;

  psT->psNext = psResTypes;
  psResTypes = psT;

  return TRUE;
}

// Make a string lower case
void resToLower(STRING* pStr)
{
  while (*pStr != 0)
  {
    if (isupper(*pStr))
      *pStr = static_cast<STRING>(*pStr - (STRING)('A' - 'a'));
    pStr += 1;
  }
}

static char LastResourceFilename[FILE_MAXCHAR];

// Returns the filename of the last resource file loaded
char* GetLastResourceFilename(void) { return (LastResourceFilename); }

// Set the resource name of the last resource file loaded
void SetLastResourceFilename(const char* pName)
{
  strncpy(LastResourceFilename, pName, FILE_MAXCHAR - 1);
  LastResourceFilename[FILE_MAXCHAR - 1] = 0;
}

static UDWORD LastHashName;

// Returns the filename of the last resource file loaded
UDWORD GetLastHashName(void) { return (LastHashName); }

// Set the resource name of the last resource file loaded
void SetLastHashName(UDWORD HashName) { LastHashName = HashName; }

// load a file from disk into a fixed memory buffer
BOOL resLoadFromDisk(STRING* pFileName, UBYTE** ppBuffer, UDWORD* pSize)
{
  HANDLE hFile;
  DWORD bytesRead;
  BOOL retVal;

  *ppBuffer = pFileBuffer;

  // try and open the file
  hFile = CreateFile(pFileName, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (hFile == INVALID_HANDLE_VALUE)
  {
    Neuron::Fatal("Couldn't open {}\n{}", pFileName, winErrorToString(GetLastError()));
    return FALSE;
  }

  // get the size of the file
  *pSize = GetFileSize(hFile, nullptr);
  if (*pSize >= static_cast<UDWORD>(fileBufferSize))
  {
    Neuron::Fatal("file too big !!:{} size {}\n", pFileName, *pSize);
    return FALSE;
  }

  // load the file into the buffer
  retVal = ReadFile(hFile, pFileBuffer, *pSize, &bytesRead, nullptr);
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

// Structure for each file currently in use in the resource  ... probably only going to be one ... but we will handle upto MAXLOADEDRESOURCE
using RESOURCEFILE = struct
{
  UBYTE* pBuffer; // a pointer to the data
  UDWORD size; // number of bytes
  UBYTE type; // what type of resource is it
};

#define RESFILETYPE_EMPTY (0)			// empty entry
#define RESFILETYPE_PC_SBL (1)			// Johns SBL stuff
#define RESFILETYPE_LOADED (2)			// Loaded from a file (!)

#define MAXLOADEDRESOURCES (6)
static RESOURCEFILE LoadedResourceFiles[MAXLOADEDRESOURCES];

// Clear out the resource list ... needs to be called during init.
void ResetResourceFile(void)
{
  UWORD i;

  for (i = 0; i < MAXLOADEDRESOURCES; i++)
    LoadedResourceFiles[i].type = RESFILETYPE_EMPTY;
}

// Returns an empty resource entry or -1 if none exsist
SDWORD FindEmptyResourceFile(void)
{
  UWORD i;
  for (i = 0; i < MAXLOADEDRESOURCES; i++)
  {
    if (LoadedResourceFiles[i].type == RESFILETYPE_EMPTY)
      return (i);
  }
  return (-1); // ERROR
}

// Get a resource data file ... either loads it or just returns a pointer
BOOL RetreiveResourceFile(char* ResourceName, RESOURCEFILE** NewResource)
{
  SDWORD ResID;
  RESOURCEFILE* ResData;
  UDWORD size;
  UBYTE* pBuffer;

  ResID = FindEmptyResourceFile();
  if (ResID == -1)
    return (FALSE); // all resource files are full

  ResData = &LoadedResourceFiles[ResID];
  *NewResource = ResData;

  if (pFileBuffer && resLoadFromDisk(ResourceName, &pBuffer, &size))
  {
    ResData->type = RESFILETYPE_PC_SBL;
    ResData->size = size;
    ResData->pBuffer = pBuffer;
    return (TRUE);
  }

  if (!loadFile(ResourceName, &pBuffer, &size))
    return FALSE;

  ResData->type = RESFILETYPE_LOADED;
  ResData->size = size;
  ResData->pBuffer = pBuffer;
  return (TRUE);
}

// Free up the file depending on what type it is
void FreeResourceFile(RESOURCEFILE* OldResource)
{
  switch (OldResource->type)
  {
  case RESFILETYPE_LOADED: delete[] OldResource->pBuffer;

    break;
  }

  // Remove from the list
  OldResource->type = RESFILETYPE_EMPTY;
}

void resDataInit(RES_DATA* psRes, const STRING* DebugName, UDWORD DataIDHash, void* pData, UDWORD BlockID)
{
  psRes->pData = pData;
  psRes->blockID = resBlockID;

  psRes->HashedID = DataIDHash;

#ifdef DEBUG
  strcpy(psRes->aID, DebugName);
  psRes->usage = 0;
#endif
}

/* Call the load function for a file */
BOOL resLoadFile(STRING* pType, STRING* pFile)
{
  RES_TYPE* psT;
  void* pData;
  RES_DATA* psRes;
  STRING aFileName[FILE_MAXCHAR];

  BOOL loadresource;

  loadresource = TRUE;
  if (resPreLoadCallback)
    loadresource = resPreLoadCallback(pType, pFile, aCurrResDir);

  if (loadresource == TRUE)
  {
    UDWORD HashedType = HashString(pType);

    for (psT = psResTypes; resValidType(psT); psT = resNextType(psT))
    {
      if (psT->HashedType == HashedType)
        break;
    }

    if (psT == nullptr)
    {
      Neuron::Fatal("resLoadFile: Unknown type: {}", pType);
      return FALSE;
    }

    {
      UDWORD HashedName = HashStringIgnoreCase(pFile);
      for (psRes = psT->psRes; psRes; psRes = psRes->psNext)
      {
        if (psRes->HashedID == HashedName)
        {
          Neuron::DebugTrace("resLoadFile: Duplicate file name: {} (hash {:x}) for type {}",pFile, HashedName, psT->aType);

          // assume that they are actually both the same and silently fail
          // lovely little hack to allow some files to be loaded from disk (believe it or not!).
          return TRUE;
        }
      }
    }

    // Create the file name
    if (strlen(aCurrResDir) + strlen(pFile) + 1 >= FILE_MAXCHAR)
    {
      Neuron::Fatal("resLoadFile: Filename too long!!\n{}{}", aCurrResDir, pFile);
      return FALSE;
    }
    strcpy(aFileName, aCurrResDir);
    strcat(aFileName, pFile);

    strcpy(LastResourceFilename, pFile); // Save the filename in case any routines need it
    resToLower(LastResourceFilename);
    SetLastHashName(HashStringIgnoreCase(LastResourceFilename));

    // load the resource
    if (psT->buffLoad)
    {
      RESOURCEFILE* Resource;
      BOOL Result;

      Result = RetreiveResourceFile(aFileName, &Resource);
      if (Result == FALSE)
      {
        Neuron::Fatal("resLoadFile: Unable to retreive resource - {}",aFileName);
        return (FALSE);
      }

      // Now process the buffer data
      if (!psT->buffLoad(Resource->pBuffer, Resource->size, &pData))
        return FALSE;

      FreeResourceFile(Resource);

      resDoResLoadCallback(); // do callback.
    }
    else
    {
      Neuron::Fatal("resLoadFile:  No load functions for this type ({})\n",pType);
      return FALSE;
    }

    // Set up the resource structure if there is something to store
    if (pData != nullptr)
    {
      psRes = new (std::nothrow) RES_DATA[1];
      if (!psRes)
      {
        Neuron::Fatal("resLoadFile: Out of memory");
        psT->release(pData);
        return FALSE;
      }
      resDataInit(psRes, LastResourceFilename, HashStringIgnoreCase(LastResourceFilename), pData, resBlockID);

      // Add the resource to the list
      psRes->psNext = psT->psRes;
      psT->psRes = psRes;
    }
  }
  return TRUE;
}

/* Return the resource for a type and hashedname */
void* resGetDataFromHash(const STRING* pType, UDWORD HashedID)
{
  RES_TYPE* psT;
  RES_DATA* psRes;
  UDWORD HashedType;
  // Find the correct type

  HashedType = HashString(pType); // la da la

  for (psT = psResTypes; resValidType(psT); psT = resNextType(psT))
  {
    if (psT->HashedType == HashedType)
      break;
  }
  if (psT == nullptr)
  {
    DEBUG_ASSERT_TEXT(FALSE, "resGetData: Unknown type: {}", pType);
    return nullptr;
  }

  {
    for (psRes = psT->psRes; psRes; psRes = psRes->psNext)
    {
      if (psRes->HashedID == HashedID)
      {
        /* We found it */
        break;
      }
    }
  }

  if (psRes == nullptr)
  {
    DEBUG_ASSERT_TEXT(FALSE, "resGetDataFromHash: Unknown ID:");
    return nullptr;
  }

#ifdef DEBUG
  psRes->usage += 1;
#endif

  return resGetResDataPointer(psRes);
}

/* Return the resource for a type and ID */
void* resGetData(const STRING* pType, const STRING* pID)
{
  RES_TYPE* psT;
  RES_DATA* psRes;
  UDWORD HashedType;
  // Find the correct type

  HashedType = HashString(pType); // la da la

  for (psT = psResTypes; resValidType(psT); psT = resNextType(psT))
  {
    if (psT->HashedType == HashedType)
      break;
  }
  if (psT == nullptr)
  {
    DEBUG_ASSERT_TEXT(FALSE, "resGetData: Unknown type: {}", pType);
    return nullptr;
  }

  {
    UDWORD HashedID = HashStringIgnoreCase(pID);
    for (psRes = psT->psRes; psRes; psRes = psRes->psNext)
    {
      if (psRes->HashedID == HashedID)
      {
        /* We found it */
        break;
      }
    }
  }

  if (psRes == nullptr)
  {
    DEBUG_ASSERT_TEXT(FALSE, "resGetData: Unknown ID: {}", pID);
    return nullptr;
  }

#ifdef DEBUG
  psRes->usage += 1;
#endif

  return resGetResDataPointer(psRes);
}

BOOL resGetHashfromData(STRING* pType, void* pData, UDWORD* pHash)
{
  RES_TYPE* psT;
  RES_DATA* psRes;

  // Find the correct type
  UDWORD HashedType = HashString(pType);

  for (psT = psResTypes; resValidType(psT); psT = resNextType(psT))
  {
    if (psT->HashedType == HashedType)
      break;
  }

  if (psT == nullptr)
  {
    DEBUG_ASSERT_TEXT(FALSE, "resGetHashfromData: Unknown type: {:x}", HashedType);
    return FALSE;
  }

  // Find the resource
  for (psRes = psT->psRes; psRes; psRes = psRes->psNext)
  {
    if (resGetResDataPointer(psRes) == pData)
      break;
  }

  if (psRes == nullptr)
  {
    DEBUG_ASSERT_TEXT(FALSE, "resGetHashfromData:: couldn't find data for type {:x}\n", HashedType);
    return FALSE;
  }

  *pHash = psRes->HashedID;

  return TRUE;
}


/* Simply returns true if a resource is present */
BOOL resPresent(const STRING* pType, const STRING* pID)
{
  RES_TYPE* psT;
  RES_DATA* psRes;

  // Find the correct type
  UDWORD HashedType = HashString(pType);

  for (psT = psResTypes; resValidType(psT); psT = resNextType(psT))
  {
    if (psT->HashedType == HashedType)
      break;
  }

  /* Bow out if unrecognised type */
  if (psT == nullptr)
    return FALSE;

  {
    UDWORD HashedID = HashStringIgnoreCase(pID);
    for (psRes = psT->psRes; psRes; psRes = psRes->psNext)
    {
      if (psRes->HashedID == HashedID)
      {
        /* We found it */
        break;
      }
    }
  }

  /* Did we find it? */
  if (psRes != nullptr)
    return (TRUE);

  return (FALSE);
}

/* Release all the resources currently loaded and the resource load functions */
void resReleaseAll(void)
{
  RES_TYPE *psT, *psNT;
  RES_DATA *psRes, *psNRes;

  for (psT = psResTypes; resValidType(psT); psT = psNT)
  {
    for (psRes = psT->psRes; psRes; psRes = psNRes)
    {
#ifdef DEBUG
      if (psRes->usage == 0)
        Neuron::DebugTrace("{} resource: {}({:04x}) not used\n", psT->aType, psRes->aID,psRes->HashedID);
#endif
      if (psT->release != nullptr)
        psT->release(resGetResDataPointer(psRes));
      else
        DEBUG_ASSERT_TEXT(FALSE, "resReleaseAll: NULL release function");
      psNRes = psRes->psNext;
      delete[] psRes;
      psRes = nullptr;
    }
    psNT = resNextType(psT);
    delete[] psT;
    psT = nullptr;
  }

  psResTypes = nullptr;
}

// release the data for a particular block ID
void resReleaseBlockData(SDWORD blockID)
{
  RES_TYPE *psT, *psNT;
  RES_DATA *psPRes, *psRes, *psNRes;

  for (psT = psResTypes; resValidType(psT); psT = psNT)
  {
    psPRes = nullptr;
    for (psRes = psT->psRes; psRes; psRes = psNRes)
    {
      DEBUG_ASSERT_TEXT(psRes != NULL, "resReleaseBlockData: null pointer passed into loop");

      if (resGetResBlockID(psRes) == blockID)
      {
#ifdef DEBUG
        if (psRes->usage == 0)
          Neuron::DebugTrace("{} resource: {:x} not used\n", psT->aType, psRes->HashedID);
#endif
        if (psT->release != nullptr)
          psT->release(resGetResDataPointer(psRes));
        else
          DEBUG_ASSERT_TEXT(FALSE, "resReleaseAllData: NULL release function");

        psNRes = psRes->psNext;
        delete[] psRes;
        psRes = nullptr;

        if (psPRes == nullptr)
          psT->psRes = psNRes;
        else
          psPRes->psNext = psNRes;
      }
      else
      {
        psPRes = psRes;
        psNRes = psRes->psNext;
      }
      DEBUG_ASSERT_TEXT(psNRes != (RES_DATA *)0xdddddddd, "resReleaseBlockData: next data (next pointer) already freed");
    }
    psNT = resNextType(psT);
    DEBUG_ASSERT_TEXT(psNT != (RES_TYPE *)0xdddddddd, "resReleaseBlockData: next data (next pointer) already freed");
  }
}

/* Release all the resources currently loaded but keep the resource load functions */
void resReleaseAllData(void)
{
  RES_TYPE *psT, *psNT;
  RES_DATA *psRes, *psNRes;

  for (psT = psResTypes; resValidType(psT); psT = psNT)
  {
    for (psRes = psT->psRes; psRes; psRes = psNRes)
    {
#ifdef DEBUG
      if (psRes->usage == 0)
        Neuron::DebugTrace("{} resource: {:x} not used\n", psT->aType, psRes->HashedID);
#endif

      if (psT->release != nullptr)
        psT->release(resGetResDataPointer(psRes));
      else
        DEBUG_ASSERT_TEXT(FALSE, "resReleaseAllData: NULL release function");

      psNRes = psRes->psNext;
      delete[] psRes;
      psRes = nullptr;
    }
    psT->psRes = nullptr;
    psNT = resNextType(psT);
  }
}

// allocate memory for a wrf, and load it
BOOL LoadWRF(char* pResFile, UBYTE** pBuffer, UDWORD* size)
{
  return (loadFile2(pResFile, pBuffer, size,TRUE));
}

static void ReleaseWRF(UBYTE** pBuffer)
{
  // now free up the memory for the .wrf file
  delete[] *pBuffer;
}
