#include "pch.h"
/*
 * Data.c
 *
 * Data loading functions used by the framework resource module
 *
 */

#include <assert.h>

#include "Frame.h"
//render library
#include "PieDef.h"
#include "PieState.h"
#include "Pcx.h"
#include "BitImage.h"

#include "Texture.h"
#include "WarzoneConfig.h"
#include "Tex.h"
#include "TextDraw.h"

#include "FrameResource.h"
#include "Stats.h"
#include "Structure.h"
#include "Feature.h"
#include "Research.h"
#include "Data.h"
#include "Text.h"
#include "Droid.h"
#include "Function.h"
#include "Message.h"
#include "Script.h"
#include "ScriptVals.h"
#include "Display3D.h"
#include "Game.h"
#include "Objects.h"
#include "Display.h"
#include "Audio.h"
#include "Anim.h"
#include "Parser.h"
#include "Levels.h"
#include "Mechanics.h"
#include "Display3D.h"
#include "Display3Ddef.h"
#include "Init.h"

#include "MultiPlay.h"
#include "NetPlay.h"
#include "IMD.h"									// tpAddPIE

/**********************************************************
 *
 * Local Variables
 *
 *********************************************************/

BOOL bTilesPCXLoaded = FALSE;

// whether a save game is currently being loaded
BOOL saveFlag = FALSE;
extern STRING aCurrResDir[255]; // Arse

UDWORD cheatHash[CHEAT_MAXCHEAT];

/**********************************************************
 *
 * Source
 *
 *********************************************************/

void calcCheatHash(UBYTE* pBuffer, UDWORD size, UDWORD cheat)
{
  if (!bMultiPlayer)
    return;

  // create the hash for that data block.
  cheatHash[cheat] = cheatHash[cheat] ^ NEThashBuffer(pBuffer, size);
}

void resetCheatHash()
{
  for (UDWORD i = 0; i < CHEAT_MAXCHEAT; i++)
    cheatHash[i] = 0;
}

/**********************************************************/

void dataSetSaveFlag(void) { saveFlag = TRUE; }
void dataClearSaveFlag(void) { saveFlag = FALSE; }

/* Load the body stats */
BOOL bufferSBODYLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  calcCheatHash(pBuffer, size,CHEAT_SBODY);
  if (!loadBodyStats((SBYTE*)pBuffer, size))
    return FALSE;

  if (!allocComponentList(COMP_BODY, numBodyStats))
    return FALSE;

  // set a dummy value so the release function gets called
  *ppData = (void*)1;
  return TRUE;
}

void dataReleaseStats(void* pData)
{
  UNUSEDPARAMETER(pData);

  freeComponentLists();
  statsShutDown();
}

/* Load the weapon stats */
BOOL bufferSWEAPONLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  calcCheatHash(pBuffer, size, CHEAT_SWEAPON);
  if (!loadWeaponStats((SBYTE*)pBuffer, size))
    return FALSE;

  if (!allocComponentList(COMP_WEAPON, numWeaponStats))
    return FALSE;

  //not interested in this value
  *ppData = nullptr;
  return TRUE;
}

/* Load the constructor stats */
BOOL bufferSCONSTRLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  calcCheatHash(pBuffer, size,CHEAT_SCONSTR);
  if (!loadConstructStats((SBYTE*)pBuffer, size))
    return FALSE;

  if (!allocComponentList(COMP_CONSTRUCT, numConstructStats))
    return FALSE;

  //not interested in this value
  *ppData = nullptr;
  return TRUE;
}

/* Load the ECM stats */
BOOL bufferSECMLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  calcCheatHash(pBuffer, size,CHEAT_SECM);

  if (!loadECMStats((SBYTE*)pBuffer, size))
    return FALSE;

  if (!allocComponentList(COMP_ECM, numECMStats))
    return FALSE;

  //not interested in this value
  *ppData = nullptr;
  return TRUE;
}

/* Load the Propulsion stats */
BOOL bufferSPROPLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  calcCheatHash(pBuffer, size,CHEAT_SPROP);

  if (!loadPropulsionStats((SBYTE*)pBuffer, size))
    return FALSE;

  if (!allocComponentList(COMP_PROPULSION, numPropulsionStats))
    return FALSE;

  //not interested in this value
  *ppData = nullptr;
  return TRUE;
}

/* Load the Sensor stats */
BOOL bufferSSENSORLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  calcCheatHash(pBuffer, size,CHEAT_SSENSOR);

  if (!loadSensorStats((SBYTE*)pBuffer, size))
    return FALSE;

  if (!allocComponentList(COMP_SENSOR, numSensorStats))
    return FALSE;

  //not interested in this value
  *ppData = nullptr;
  return TRUE;
}

/* Load the Repair stats */
BOOL bufferSREPAIRLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  calcCheatHash(pBuffer, size,CHEAT_SREPAIR);

  if (!loadRepairStats((SBYTE*)pBuffer, size))
    return FALSE;

  if (!allocComponentList(COMP_REPAIRUNIT, numRepairStats))
    return FALSE;

  //not interested in this value
  *ppData = nullptr;
  return TRUE;
}

/* Load the Brain stats */
BOOL bufferSBRAINLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  calcCheatHash(pBuffer, size,CHEAT_SBRAIN);

  if (!loadBrainStats((SBYTE*)pBuffer, size))
    return FALSE;

  if (!allocComponentList(COMP_BRAIN, numBrainStats))
    return FALSE;

  //not interested in this value
  *ppData = nullptr;
  return TRUE;
}

/* Load the Program stats */
/*BOOL bufferSPROGRAMLoad(UBYTE *pBuffer, UDWORD size, void **ppData)
{
	if (!loadProgramStats((SBYTE*)pBuffer, size))
	{
		return FALSE;
	}

	if (!allocComponentList(COMP_PROGRAM, numProgramStats))
	{
		return FALSE;
	}

	//not interested in this value
	*ppData = NULL;
	return TRUE;
}*/

/* Load the PropulsionType stats */
BOOL bufferSPROPTYPESLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  calcCheatHash(pBuffer, size,CHEAT_SPROPTY);

  if (!loadPropulsionTypes((SBYTE*)pBuffer, size))
    return FALSE;

  //not interested in this value
  *ppData = nullptr;
  return TRUE;
}

/* Load the propulsion type sound stats */
BOOL bufferSPROPSNDLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  if (!loadPropulsionSounds((SBYTE*)pBuffer, size))
    return FALSE;

  //not interested in this value
  *ppData = nullptr;
  return TRUE;
}

/* Load the SSPECABIL stats */
BOOL bufferSSPECABILLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  if (!loadSpecialAbility((SBYTE*)pBuffer, size))
    return FALSE;

  //not interested in this value
  *ppData = nullptr;
  return TRUE;
}

/* Load the STERRTABLE stats */
BOOL bufferSTERRTABLELoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  calcCheatHash(pBuffer, size,CHEAT_STERRT);

  if (!loadTerrainTable((SBYTE*)pBuffer, size))
    return FALSE;

  //not interested in this value
  *ppData = nullptr;
  return TRUE;
}

/* Load the body/propulsion IMDs stats */
BOOL bufferSBPIMDLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  if (!loadBodyPropulsionIMDs((SBYTE*)pBuffer, size))
    return FALSE;

  //not interested in this value
  *ppData = nullptr;
  return TRUE;
}

/* Load the weapon sound stats */
BOOL bufferSWEAPSNDLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  if (!loadWeaponSounds((SBYTE*)pBuffer, size))
    return FALSE;

  //not interested in this value
  *ppData = nullptr;
  return TRUE;
}

/* Load the Weapon Effect modifier stats */
BOOL bufferSWEAPMODLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  calcCheatHash(pBuffer, size,CHEAT_SWEAPMOD);

  if (!loadWeaponModifiers((SBYTE*)pBuffer, size))
    return FALSE;

  //not interested in this value
  *ppData = nullptr;
  return TRUE;
}

/* Load the Template stats */
BOOL bufferSTEMPLLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  calcCheatHash(pBuffer, size,CHEAT_STEMP);

  if (!loadDroidTemplates((SBYTE*)pBuffer, size))
    return FALSE;

  // set a dummy value so the release function gets called
  *ppData = (void*)1;
  return TRUE;
}

// release the templates
void dataSTEMPLRelease(void* pData)
{
  UNUSEDPARAMETER(pData);

  //free the storage allocated to the droid templates
  droidTemplateShutDown();
}

/* Load the Template weapons stats */
BOOL bufferSTEMPWEAPLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  calcCheatHash(pBuffer, size,CHEAT_STEMPWEAP);

  if (!loadDroidWeapons((SBYTE*)pBuffer, size))
    return FALSE;

  //not interested in this value
  *ppData = nullptr;
  return TRUE;
}

/* Load the Template programs stats */
/*BOOL bufferSTEMPPROGLoad(UBYTE *pBuffer, UDWORD size, void **ppData)
{
	if (!loadDroidPrograms((SBYTE*)pBuffer, size))
	{
		return FALSE;
	}

	//not interested in this value
	*ppData = NULL;
	return TRUE;
}*/

/* Load the Structure stats */
BOOL bufferSSTRUCTLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  calcCheatHash(pBuffer, size,CHEAT_SSTRUCT);

  if (!loadStructureStats((SBYTE*)pBuffer, size))
    return FALSE;

  if (!allocStructLists())
    return FALSE;

  // set a dummy value so the release function gets called
  *ppData = (void*)1;
  return TRUE;
}

// release the structure stats
void dataSSTRUCTRelease(void* pData)
{
  UNUSEDPARAMETER(pData);

  freeStructureLists();
  structureStatsShutDown();
}

/* Load the Structure Weapons stats */
BOOL bufferSSTRWEAPLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  calcCheatHash(pBuffer, size,CHEAT_SSTRWEAP);

  if (!loadStructureWeapons((SBYTE*)pBuffer, size))
    return FALSE;

  //not interested in this value
  *ppData = nullptr;
  return TRUE;
}

/* Load the Structure Functions stats */
BOOL bufferSSTRFUNCLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  calcCheatHash(pBuffer, size,CHEAT_STRFUNC);

  if (!loadStructureFunctions((SBYTE*)pBuffer, size))
    return FALSE;

  //not interested in this value
  *ppData = nullptr;
  return TRUE;
}

/* Load the Structure strength modifier stats */
BOOL bufferSSTRMODLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  calcCheatHash(pBuffer, size,CHEAT_SSTRMOD);

  if (!loadStructureStrengthModifiers((SBYTE*)pBuffer, size))
    return FALSE;

  //not interested in this value
  *ppData = nullptr;
  return TRUE;
}

/* Load the Feature stats */
BOOL bufferSFEATLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  calcCheatHash(pBuffer, size,CHEAT_SFEAT);

  if (!loadFeatureStats((SBYTE*)pBuffer, size))
    return FALSE;

  // set a dummy value so the release function gets called
  *ppData = (void*)1;
  return TRUE;
}

// free the feature stats
void dataSFEATRelease(void* pData)
{
  UNUSEDPARAMETER(pData);

  featureStatsShutDown();
}

/* Load the Functions stats */
BOOL bufferSFUNCLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  calcCheatHash(pBuffer, size,CHEAT_SFUNC);

  if (!loadFunctionStats((SBYTE*)pBuffer, size))
    return FALSE;

  //adjust max values of stats used in the design screen due to any possible upgrades
  adjustMaxDesignStats();

  // set a dummy value so the release function gets called
  *ppData = (void*)1;
  return TRUE;
}

// release the function stats
void dataSFUNCRelease(void* pData)
{
  UNUSEDPARAMETER(pData);

  FunctionShutDown();
}

// release the research stats
void dataRESCHRelease(void* pData)
{
  UNUSEDPARAMETER(pData);

  //free the storage allocated to the stats
  ResearchShutDown();
}

/* Load the Research stats */
BOOL bufferRESCHLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  calcCheatHash(pBuffer, size,CHEAT_RESCH);

  //check to see if already loaded
  if (numResearch > 0)
  {
    //release previous data before loading in the new
    dataRESCHRelease(nullptr);
  }

  if (!loadResearch((SBYTE*)pBuffer, size))
    return FALSE;

  /* set a dummy value so the release function gets called - the Release 
    function is now called when load up the next set
    pass back NULL so that can load the same name file for the next campaign*/
  *ppData = nullptr;
  return TRUE;
}

/* Load the research pre-requisites */
BOOL bufferRPREREQLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  calcCheatHash(pBuffer, size,CHEAT_RPREREQ);

  if (!loadResearchPR((SBYTE*)pBuffer, size))
    return FALSE;

  //not interested in this value
  *ppData = nullptr;
  return TRUE;
}

/* Load the research components required */
/*BOOL bufferRCOMPREQLoad(UBYTE *pBuffer, UDWORD size, void **ppData)
{
	//DON'T DO ANYTHING WITH IT SINCE SHOULDN'T BE LOADING ANYMORE - AB 20/04/98
	if (!loadResearchArtefacts((SBYTE*)pBuffer, size, REQ_LIST))
	{
		return FALSE;
	}

	//not interested in this value
	*ppData = NULL;
	return TRUE;
}*/

/* Load the research components made redundant */
BOOL bufferRCOMPREDLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  calcCheatHash(pBuffer, size,CHEAT_RCOMPRED);

  if (!loadResearchArtefacts((SBYTE*)pBuffer, size, RED_LIST))
    return FALSE;

  //not interested in this value
  *ppData = nullptr;
  return TRUE;
}

/* Load the research component results */
BOOL bufferRCOMPRESLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  calcCheatHash(pBuffer, size,CHEAT_RCOMPRES);

  if (!loadResearchArtefacts((SBYTE*)pBuffer, size, RES_LIST))
    return FALSE;

  //not interested in this value
  *ppData = nullptr;
  return TRUE;
}

/* Load the research structures required */
BOOL bufferRSTRREQLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  calcCheatHash(pBuffer, size,CHEAT_RSTRREQ);

  if (!loadResearchStructures((SBYTE*)pBuffer, size, REQ_LIST))
    return FALSE;

  //not interested in this value
  *ppData = nullptr;
  return TRUE;
}

/* Load the research structures made redundant */
BOOL bufferRSTRREDLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  calcCheatHash(pBuffer, size,CHEAT_RSTRRED);

  if (!loadResearchStructures((SBYTE*)pBuffer, size, RED_LIST))
    return FALSE;

  //not interested in this value
  *ppData = nullptr;
  return TRUE;
}

/* Load the research structure results */
BOOL bufferRSTRRESLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  calcCheatHash(pBuffer, size,CHEAT_RSTRRES);

  if (!loadResearchStructures((SBYTE*)pBuffer, size, RES_LIST))
    return FALSE;

  //not interested in this value
  *ppData = nullptr;
  return TRUE;
}

/* Load the research functions */
BOOL bufferRFUNCLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  calcCheatHash(pBuffer, size,CHEAT_RFUNC);

  if (!loadResearchFunctions((SBYTE*)pBuffer, size))
    return FALSE;

  //not interested in this value
  *ppData = nullptr;
  return TRUE;
}

/* Load the message viewdata */
BOOL bufferSMSGLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  VIEWDATA* pViewData = loadViewData((SBYTE*)pBuffer, size);
  if (!pViewData)
    return FALSE;

  // set the pointer so the release function gets called with it
  *ppData = static_cast<void*>(pViewData);
  return TRUE;
}

// release the message viewdata
void dataSMSGRelease(void* pData) { viewDataShutDown(static_cast<VIEWDATA*>(pData)); }

/* Load an imd */
BOOL dataIMDLoad(STRING* pFile, void** ppData)
{
  iIMDShape* psIMD = iV_IMDLoad(pFile,FALSE);
  if (psIMD == nullptr)
  {
    DBERROR(("Please check that both file %s and it's texture file are present", pFile));
    return FALSE;
  }

  *ppData = psIMD;
  return TRUE;
}

/* Load an imd */
BOOL dataIMDBufferLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  iIMDShape* psIMD;
  char BinaryPieLetters[] = {"BPIE"}; // Header for binary pie files

  UBYTE* pBufferPosition;

  pBufferPosition = pBuffer;

  BOOL BinaryPIE = TRUE;
  // Check for binary PIE files

  for (int Letter = 0; Letter < 4; Letter++)
  {
    if (pBufferPosition[Letter] != BinaryPieLetters[Letter]) // if any on the letters are incorrect then it can't be a binary pie
    {
      BinaryPIE = FALSE;
      break; // no point in continuing			
    }
  }

  if (BinaryPIE == FALSE)
  {
    psIMD = iV_ProcessIMD(&pBufferPosition, pBuffer + size, (UBYTE*)"", (UBYTE*)"",FALSE);
#ifndef FINALBUILD
    tpAddPIE(GetLastResourceFilename(), psIMD);
#endif
    if (psIMD == nullptr)
    {
      DBERROR(("IMD load failed - %s", GetLastResourceFilename()));
      return FALSE;
    }
  }
  else
  {
    psIMD = iV_ProcessBPIE((iIMDShape*)(pBuffer + 4), size);
#ifndef FINALBUILD
    tpAddPIE(GetLastResourceFilename(), psIMD);
#endif
    if (psIMD == nullptr)
    {
      DBERROR(("BinaryPIE load failed - %s",GetLastResourceFilename() ));
      return (FALSE);
    }
  }

  *ppData = psIMD;
  return TRUE;
}

/* Release an imd */

BOOL dataIMGPAGELoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  UNUSEDPARAMETER(size);

  auto psSprite = static_cast<iSprite*>(MALLOC(sizeof(iSprite)));
  if (!psSprite)
    return FALSE;

  if (!iV_PCXLoadMem((int8*)(SBYTE*)pBuffer, psSprite, nullptr))
  {
    DBERROR(("IMGPAGE load failed"));
    FREE(psSprite);
    return FALSE;
  }

  *ppData = psSprite;

  return TRUE;
}

void dataIMGPAGERelease(void* pData)
{
  auto psSprite = static_cast<iSprite*>(pData);
  FREE(psSprite->bmp);
  FREE(psSprite);
}

// Tertiles loader. This version for software renderer.
BOOL dataTERTILESLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  UNUSEDPARAMETER(size);

  *ppData = nullptr;
  return TRUE;
}

void dataTERTILESRelease(void* pData)
{
  auto psSprite = static_cast<iSprite*>(pData);

  freeTileTextures();
  FREE(psSprite->bmp);
  bTilesPCXLoaded = FALSE;
}

// Tertiles loader. This version for hardware renderer.
BOOL dataHWTERTILESLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  UNUSEDPARAMETER(size);

  // tile loader.
  if (bTilesPCXLoaded)
  {
    DBPRINTF(("Reloading terrain tiles\n"));

    if (!pie_PCXLoadMemToBuffer((int8*)(SBYTE*)pBuffer, &tilesPCX, nullptr))
    {
      DBERROR(("HWTERTILES reload failed"));
      return FALSE;
    }
  }
  else
  {
    DBPRINTF(("Loading terrain tiles\n"));
    if (!iV_PCXLoadMem((int8*)(SBYTE*)pBuffer, &tilesPCX, nullptr))
    {
      DBERROR(("HWTERTILES load failed"));
      return FALSE;
    }
  }

  getTileRadarColours();
  // make several 256 * 256 pages
  if (bTilesPCXLoaded)
    remakeTileTexturePages(tilesPCX.width, tilesPCX.height,TILE_WIDTH, TILE_HEIGHT, tilesPCX.bmp);
  else
    makeTileTexturePages(tilesPCX.width, tilesPCX.height,TILE_WIDTH, TILE_HEIGHT, tilesPCX.bmp);
  //	else
  //		/* Squirt the tiles into a nice long thin bitmap */
  //			if(!remakeTileTextures())
  //		else
  //			if(!makeTileTextures())

  if (bTilesPCXLoaded)
    *ppData = nullptr;
  else
  {
    bTilesPCXLoaded = TRUE;
    *ppData = &tilesPCX;
  }
  DBPRINTF(("HW Tiles loaded\n"));
  return TRUE;
}

void dataHWTERTILESRelease(void* pData)
{
  auto psSprite = static_cast<iSprite*>(pData);

  freeTileTextures();
  FREE(psSprite->bmp);
  bTilesPCXLoaded = FALSE;
  pie_TexShutDown();
}

BOOL dataIMGLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  IMAGEFILE* ImageFile = iV_LoadImageFile(pBuffer, size);
  if (ImageFile == nullptr)
    return FALSE;

  *ppData = ImageFile;

  return TRUE;
}

void dataIMGRelease(void* pData) { iV_FreeImageFile(static_cast<IMAGEFILE*>(pData)); }

/* Load a PCX to an iSprite */
//
//
//	if (!iV_PCXLoad(pFile, psSprite, sPal))
//
//

#define TEXTUREWIDTH (256)
#define TEXTUREHEIGHT (256)

/* Load a texturepage into memory */
// PC ONLY VERSION

BOOL bufferTexPageLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  STRING texfile[255];
  SDWORD i;
  size; // why?

  // generate a texture page name in "page-xx" format
  strncpy(texfile, GetLastResourceFilename(), 254);
  texfile[254] = 0;
  resToLower(texfile);

  DBPRINTF(("%s texturepage ...\n",texfile));

  if (war_GetAdditive()) //(war_GetTranslucent())
  {
    //hardware
    if (strstr(texfile, "soft") != nullptr) //and this is a software textpage
    {
      //so dont load it
      *ppData = nullptr;
      return TRUE;
    }
  }
  else
  {
    //software or old d3d card
    if (strstr(texfile, "hard") != nullptr) //and this is a hardware textpage
    {
      //so dont load it
      *ppData = nullptr;
      return TRUE;
    }
  }

  if (strncmp(texfile, "page-", 5) == 0)
  {
    for (i = 5; i < static_cast<SDWORD>(strlen(texfile)); i++)
    {
      if (!isdigit(texfile[i]))
        break;
    }
    texfile[i] = 0;
  }
  SetLastResourceFilename(texfile);
  SetLastResourceHash(texfile);

  DBPRINTF(("%s texturepage added (len=%d)\n",texfile,strlen(texfile)));

  // see if this texture page has already been loaded
  if (resPresent("TEXPAGE", texfile))
  {
    // replace the old texture page with the new one
    SDWORD id = pie_ReloadTexPage(texfile, pBuffer);
    ASSERT((id >=0,"pie_ReloadTexPage failed"));
    *ppData = nullptr;
  }
  else
  {
    auto NewTexturePage = static_cast<TEXTUREPAGE*>(MALLOC(sizeof(TEXTUREPAGE)));
    if (!NewTexturePage)
      return FALSE;

    NewTexturePage->Texture = nullptr;
    NewTexturePage->Palette = nullptr;

    auto psPal = static_cast<iPalette*>(MALLOC(sizeof(iPalette)));
    if (!psPal)
      return FALSE;

    auto psSprite = static_cast<iSprite*>(MALLOC(sizeof(iSprite)));
    if (!psSprite)
      return FALSE;

    if (!iV_PCXLoadMem((int8*)(SBYTE*)pBuffer, psSprite, nullptr))
    {
      FREE(psSprite);
      return FALSE;
    }

    NewTexturePage->Texture = psSprite;
    NewTexturePage->Palette = psPal;

    //Hack mar8 to load	textures in order	
    /*	for(i=0;i<_TEX_INDEX;i++)
      {	
        if (stricmp(texfile,_TEX_PAGE[i].name) != 0)
        {
          bFound = TRUE;
          break;
        }
      }
      if (!bFound) 
    */
    {
      pie_AddBMPtoTexPages(psSprite, texfile, 1, FALSE, TRUE);
    }
    //Hack end

    *ppData = NewTexturePage;
  }

  return TRUE;
}

BOOL bufferTexPageLoadSoftOnly(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  if (!war_GetTranslucent())
    return bufferTexPageLoad(pBuffer, size, ppData);
  *ppData = nullptr;
  return TRUE;
}

BOOL bufferTexPageLoadHardOnly(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  if (war_GetTranslucent())
    return bufferTexPageLoad(pBuffer, size, ppData);
  *ppData = nullptr;
  return TRUE;
}

/* Release an iSprite */
void dataISpriteRelease(void* pData)
{
  auto psSprite = static_cast<iSprite*>(pData);

  FREE(psSprite->bmp);
  FREE(psSprite);
}

/* Release a texPage */
void dataTexPageRelease(void* pData)
{
  auto Tpage = static_cast<TEXTUREPAGE*>(pData);

  // We need to handle null texpage data 
  if (Tpage == nullptr)
    return;

  if (Tpage->Texture != nullptr)
  {
    if (Tpage->Texture->bmp != nullptr)
    FREE(Tpage->Texture->bmp);
    FREE(Tpage->Texture);
  }
  if (Tpage->Palette != nullptr)
  FREE(Tpage->Palette);

  FREE(pData);
}

/* Load an audio file */
BOOL dataAudioLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  TRACK* psTrack;

  if (audio_Disabled() == TRUE)
  {
    *ppData = nullptr;
    return TRUE;
  }
  if ((psTrack = static_cast<TRACK*>(audio_LoadTrackFromBuffer(pBuffer, size))) == nullptr)
    return FALSE;

  /* save track data */
  *ppData = psTrack;

  return TRUE;
}

void dataAudioRelease(void* pData)
{
  if (audio_Disabled() == TRUE)
    UNUSEDPARAMETER(pData);
  else
  {
    auto psTrack = static_cast<TRACK*>(pData);

    audio_ReleaseTrack(psTrack);
    FREE(psTrack);
  }
}

/* Load an audio file */
BOOL dataAudioCfgLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  *ppData = nullptr;

  if (audio_Disabled() == FALSE && ParseResourceFile(pBuffer, size) == FALSE)
    return FALSE;
  return TRUE;
}

/* Load an anim file */
BOOL dataAnimLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  BASEANIM* psAnim;

  size;

  if ((psAnim = anim_LoadFromBuffer(pBuffer, size)) == nullptr)
    return FALSE;

  /* copy anim for return */
  *ppData = psAnim;

  return TRUE;
}

/* Load an audio config file */
BOOL dataAnimCfgLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  *ppData = nullptr;

  if (ParseResourceFile(pBuffer, size) == FALSE)
    return FALSE;

  return TRUE;
}

void dataAnimRelease(void* pData) { anim_ReleaseAnim(static_cast<BASEANIM*>(pData)); }

/* Load a string resource file */
BOOL dataStrResLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  // recreate the string resource if it was freed by a WRF release
  if (psStringRes == nullptr)
  {
    if (!stringsInitialise())
      return FALSE;
  }

  if (!strresLoad(psStringRes, pBuffer, size))
    return FALSE;

  *ppData = psStringRes;
  return TRUE;
}

void dataStrResRelease(void* pData)
{
  UNUSEDPARAMETER(pData);

  if (psStringRes != nullptr)
  {
    strresDestroy(psStringRes);
    psStringRes = nullptr;
  }
}

/* Load a script file */
// All scripts, binary or otherwise are now passed through this routine
BOOL dataScriptLoad(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  SCRIPT_CODE* psProg = nullptr;
  BLOCK_HEAP* psHeap;
  BOOL printHack = FALSE;

  calcCheatHash(pBuffer, size,CHEAT_SCRIPT);

#ifndef NOSCRIPT
  DBPRINTF(("COMPILING SCRIPT ...%s\n",GetLastResourceFilename()));
  // make sure the memory system uses normal malloc for a compile
  psHeap = memGetBlockHeap();
  memSetBlockHeap(nullptr);

  if (!scriptCompile(pBuffer, size, &psProg, SCRIPTTYPE)) // see script.h
  {
    DBERROR(("Script %s did not compile", GetLastResourceFilename()));
    return FALSE;
  }
  memSetBlockHeap(psHeap);

  if (printHack)
    cpPrintProgram(psProg);

  *ppData = psProg;
#endif
  return TRUE;
}

// Load a script variable values file
BOOL dataScriptLoadVals(UBYTE* pBuffer, UDWORD size, void** ppData)
{
  *ppData = nullptr;

  calcCheatHash(pBuffer, size,CHEAT_SCRIPTVAL);

  // don't load anything if a saved game is being loaded
  if (saveFlag)
    return TRUE;

  DBPRINTF(("Loading script data %s\n",GetLastResourceFilename()));

  if (!scrvLoad(pBuffer, size))
    return FALSE;

  *ppData = nullptr;
  return TRUE;
}

BOOL dataSaveGameLoad(STRING* pFile, void** ppData)
{
  if (!stageTwoInitialise())
    return FALSE;

  if (!loadGameInit(pFile,TRUE))
    return FALSE;
  if (!loadGame(pFile, !KEEPOBJECTS, FREEMEM,TRUE))
    return FALSE;

  if (!newMapInitialise())
    return FALSE;

  //not interested in this value
  *ppData = nullptr;
  return TRUE;
}

// New reduced resource type ... specially for PSX
// These are statically defined in data.c
// this is also defined in frameresource.c - needs moving to a .h file
using RES_TYPE_MIN = struct
{
  STRING* aType; // points to the string defining the type (e.g. SCRIPT) - NULL indicates end of list
  RES_BUFFERLOAD buffLoad; // routine to process the data for this type 
  RES_FREE release; // routine to release the data (NULL indicates none)
  void* ResourceData; // Linked list of data - set to null initially
  UDWORD HashedType; // hashed version of aType
};

static RES_TYPE_MIN ResourceTypes[] = {
  {"SWEAPON", bufferSWEAPONLoad, nullptr}, {"SBODY", bufferSBODYLoad, dataReleaseStats}, {"SBRAIN", bufferSBRAINLoad, nullptr},
  {"SPROP", bufferSPROPLoad, nullptr}, {"SSENSOR", bufferSSENSORLoad, nullptr}, {"SECM", bufferSECMLoad, nullptr},
  {"SREPAIR", bufferSREPAIRLoad, nullptr},
  //{"SPROGRAM", bufferSPROGRAMLoad, NULL},
  {"SCONSTR", bufferSCONSTRLoad, nullptr}, {"SPROPTYPES", bufferSPROPTYPESLoad, nullptr}, {"SPROPSND", bufferSPROPSNDLoad, nullptr},
  {"STERRTABLE", bufferSTERRTABLELoad, nullptr}, {"SSPECABIL", bufferSSPECABILLoad, nullptr}, {"SBPIMD", bufferSBPIMDLoad, nullptr},
  {"SWEAPSND", bufferSWEAPSNDLoad, nullptr}, {"SWEAPMOD", bufferSWEAPMODLoad, nullptr}, {"STEMPL", bufferSTEMPLLoad, dataSTEMPLRelease},
  //template and associated files
  {"STEMPWEAP", bufferSTEMPWEAPLoad, nullptr},
  //{"STEMPPROG", bufferSTEMPPROGLoad, NULL},
  {"SSTRUCT", bufferSSTRUCTLoad, dataSSTRUCTRelease}, //structure stats and associated files
  {"SSTRFUNC", bufferSSTRFUNCLoad, nullptr}, {"SSTRWEAP", bufferSSTRWEAPLoad, nullptr}, {"SSTRMOD", bufferSSTRMODLoad, nullptr},
  {"SFEAT", bufferSFEATLoad, dataSFEATRelease}, //feature stats file
  {"SFUNC", bufferSFUNCLoad, dataSFUNCRelease}, //function stats file
  {"RESCH", bufferRESCHLoad, dataRESCHRelease}, //research stats files
  {"RPREREQ", bufferRPREREQLoad, nullptr}, {"RCOMPRED", bufferRCOMPREDLoad, nullptr}, {"RCOMPRES", bufferRCOMPRESLoad, nullptr},
  {"RSTRREQ", bufferRSTRREQLoad, nullptr}, {"RSTRRED", bufferRSTRREDLoad, nullptr}, {"RSTRRES", bufferRSTRRESLoad, nullptr},
  {"RFUNC", bufferRFUNCLoad, nullptr}, {"SMSG", bufferSMSGLoad, dataSMSGRelease}, {"SCRIPT", dataScriptLoad, (RES_FREE)scriptFreeCode},
  {"SCRIPTVAL", dataScriptLoadVals, nullptr}, {"STR_RES", dataStrResLoad, dataStrResRelease},
  {"IMGPAGE", dataIMGPAGELoad, dataIMGPAGERelease}, {"TERTILES", dataTERTILESLoad, dataTERTILESRelease},
  // freed by 3d shutdow},// Tertiles Files. This version used when running with software renderer.
  {"HWTERTILES", dataHWTERTILESLoad, dataHWTERTILESRelease},
  // freed by 3d shutdow},// Tertiles Files. This version used when running with hardware renderer.
  {"AUDIOCFG", dataAudioCfgLoad, nullptr}, {"WAV", dataAudioLoad, dataAudioRelease}, {"ANI", dataAnimLoad, dataAnimRelease},
  {"ANIMCFG", dataAnimCfgLoad, nullptr}, {"IMG", dataIMGLoad, dataIMGRelease}, {"TEXPAGE", bufferTexPageLoad, dataTexPageRelease},
  {"IMD", dataIMDBufferLoad, (RES_FREE)iV_IMDRelease}, {nullptr, nullptr, nullptr} // indicates end of list
};

/* Pass all the data loading functions to the framework library */
BOOL dataInitLoadFuncs(void)
{
  resetCheatHash();

  RES_TYPE_MIN* CurrentType = ResourceTypes; // point to the first entry

  // While there are still some entries in the list
  while (true)
  {
    if (CurrentType->aType == nullptr)
      break; // if we are at end of list exit 

    if (!resAddBufferLoad(CurrentType->aType, CurrentType->buffLoad, CurrentType->release))
      return FALSE; // error whilst adding a buffer load
    CurrentType++;
  }

  // Now add the only file load left!
  if (!resAddFileLoad("SAVEGAME", dataSaveGameLoad, nullptr))
    return FALSE;

  return TRUE;
}
