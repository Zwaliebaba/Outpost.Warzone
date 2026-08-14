#include "pch.h"
#include <assert.h>
#include "Frame.h"
#include "Effects.h"
#include "Structure.h"
#include "MessageDef.h"
#include "MiscIMD.h"

/* Our great big array of imds */
MISC_IMD miscImds[] = {
  // Previous imd pointer names
  {nullptr, "fxsexp"}, // explosionSmallImd	MI_EXPLOSION_SMALL       
  {nullptr, "fxlexp"}, // explosionMediumImd	MI_EXPLOSION_MEDIUM      
  {nullptr, "fxdust"}, // constructionImd		MI_CONSTRUCTION          
  {nullptr, "fxsmoke"}, // smallSmokeImd		MI_SMALL_SMOKE           
  {nullptr, "parthead"}, // babaHeadImd			MI_BABA_HEAD             
  {nullptr, "partlegs"}, // babaLegsImd			MI_BABA_LEGS             
  {nullptr, "partarm"}, // babaArmImd			MI_BABA_ARM              
  {nullptr, "partbody"}, // babaBodyImd			MI_BABA_BODY             
  {nullptr, "cybitrkt"}, // cyborgHeadImd		MI_CYBORG_HEAD           
  {nullptr, "cybitlg1"}, // cyborgLegsImd		MI_CYBORG_LEGS           
  {nullptr, "cybitgun"}, // cyborgArmImd			MI_CYBORG_ARM            
  {nullptr, "cybitbod"}, // cyborgBodyImd		MI_CYBORG_BODY           
  {nullptr, "fxatexp"}, // waterImd				MI_WATER                 
  {nullptr, "fxssteam"}, // droidDamageImd		MI_DROID_DAMAGE          
  {nullptr, "fxssteam"}, // smallSteamImd		MI_SMALL_STEAM           
  {nullptr, "fxplasma"}, // plasmaImd			MI_PLASMA                
  {nullptr, "fxblip"}, // blipImd				MI_BLIP                  
  {nullptr, "cyshadow"}, // shadowImd			MI_SHADOW                
  {nullptr, "Mitrnshd"}, // transporterShadowImd	MI_TRANSPORTER_SHADOW    
  {nullptr, "fxblood"}, // bloodImd				MI_BLOOD                 
  {nullptr, "fxssmoke"}, // trailImd				MI_TRAIL                 
  {nullptr, "fxft"}, // flameImd				MI_FLAME                 
  {nullptr, "fxpower"}, // teslaImd				MI_TESLA                 
  {nullptr, "fxmflare"}, // mFlareImd			MI_MFLARE                
  {nullptr, "mirain"}, // rainImd				MI_RAIN                  
  {nullptr, "misnow"}, // snowImd				MI_SNOW                  
  {nullptr, "fxssplsh"}, // splashImd			MI_SPLASH                
  {nullptr, "fxexpdrt"}, // kickImd				MI_KICK                  
  {nullptr, "fxlightr"}, // landingImd			MI_LANDING               
  {nullptr, "fxl3dshk"}, // shockImd				MI_SHOCK  
  {nullptr, "blipenm"}, // proximityImds[0]	   	MI_BLIP_ENEMY    
  {nullptr, "blipres"}, // proximityImds[1]	   	MI_BLIP_RESOURCE 
  {nullptr, "blipart"}, // proximityImds[2]	   	MI_BLIP_ARTEFACT 
  {nullptr, "miwrek1"}, // wreckageImds[0]		MI_WRECK0 
  {nullptr, "miwrek2"}, // wreckageImds[1]		MI_WRECK1 
  {nullptr, "miwrek3"}, // wreckageImds[2]		MI_WRECK2 
  {nullptr, "miwrek4"}, // wreckageImds[3]		MI_WRECK3 
  {nullptr, "miwrek5"}, // wreckageImds[4]		MI_WRECK4 
  {nullptr, "midebr1"}, // debrisImds[0]		MI_DEBRIS0  
  {nullptr, "midebr2"}, // debrisImds[1]		MI_DEBRIS1  
  {nullptr, "midebr3"}, // debrisImds[2]		MI_DEBRIS2  
  {nullptr, "midebr4"}, // debrisImds[3]		MI_DEBRIS3  
  {nullptr, "midebr5"}, // debrisImds[4]		MI_DEBRIS4  
  {nullptr, "fxflecht"}, // met hit - for repair centre MI_FIREWORK

  {nullptr, "END_OF_IMD_LIST"}
};

// -------------------------------------------------------------------------------
// Load up all the imds into an array
BOOL multiLoadMiscImds(void)
{
  UDWORD i = 0;
  BOOL bMoreToProcess = TRUE;
  char name[15]; // hopefully!

  /* Go thru' the list */
  while (bMoreToProcess)
  {
    sprintf(name, miscImds[i].pName);
    strcat(name, ".pie");
    /* see if the resource loader can find it */
    miscImds[i].pImd = static_cast<iIMDShape*>(resGetData("IMD", name));
    /* If it didn't get it then... */
    if (!miscImds[i].pImd)
    {
      /* Say which one and return FALSE */
      DBERROR(("Can't find misselaneous PIE file : %s",miscImds[i].pName));
      DEBUG_ASSERT_TEXT(0, "NULL PIE");
      return (FALSE);
    }
    /*	If the next one's the end one, then get out now.
      This is cos strcmp will return 0 only at end of list 
    */
    bMoreToProcess = strcmp(miscImds[++i].pName, "END_OF_IMD_LIST");
  }
  return TRUE;
}

// -------------------------------------------------------------------------------
// Returns a pointer to the imd from a #define number passed in - see above
iIMDShape* getImdFromIndex(UDWORD index)
{
  DEBUG_ASSERT_TEXT(index<MI_TOO_MANY, "Out of range index in getImdFromIndex");

  return (miscImds[index].pImd);
}

// -------------------------------------------------------------------------------
iIMDShape* getRandomWreckageImd(void)
{
  iIMDShape* WreckageIMD;

  // Get a random wreckage
  WreckageIMD = (getImdFromIndex(MI_WRECK0 + rand() % ((MI_WRECK4 - MI_WRECK0) + 1)));

  // if it doesn't exsist (cam2) then get the first one
  if (WreckageIMD == nullptr)
    WreckageIMD = (getImdFromIndex(MI_WRECK0));
  assert(WreckageIMD);

  return (WreckageIMD);
}

// -------------------------------------------------------------------------------
iIMDShape* getRandomDebrisImd(void)
{
  iIMDShape* DebrisIMD;

  DebrisIMD = getImdFromIndex(MI_DEBRIS0 + rand() % ((MI_DEBRIS4 - MI_DEBRIS0) + 1));

  DEBUG_ASSERT_TEXT(DebrisIMD != NULL, "getRandomDebrisImd : NULL PIE");

  return DebrisIMD;
}

// -------------------------------------------------------------------------------

iIMDShape* pAssemblyPointIMDs[NUM_FLAG_TYPES][MAX_FACTORY];

BOOL initMiscImds(void)
{
  STRING facName[] = "MINUM0.pie";
  STRING cybName[] = "MICNUM0.pie";
  STRING vtolName[] = "MIVNUM0.pie";
  STRING pieNum[2];
  UDWORD i;

  /* Do the new loading system */
  multiLoadMiscImds();

  /* Now load the multi array stuff */
  for (i = 0; i < MAX_FACTORY; i++)
  {
    sprintf(pieNum, "%d", i + 1);
    facName[5] = *pieNum;
    pAssemblyPointIMDs[FACTORY_FLAG][i] = static_cast<iIMDShape*>(resGetData("IMD", facName));
    if (!pAssemblyPointIMDs[FACTORY_FLAG][i])
    {
      DBERROR(("Can't find assembly point graphic for factory"));
      return (FALSE);
    }
    cybName[6] = *pieNum;
    pAssemblyPointIMDs[CYBORG_FLAG][i] = static_cast<iIMDShape*>(resGetData("IMD", cybName));
    if (!pAssemblyPointIMDs[CYBORG_FLAG][i])
    {
      DBERROR(("Can't find assembly point graphic for cyborg factory"));
      return (FALSE);
    }
    vtolName[6] = *pieNum;
    pAssemblyPointIMDs[VTOL_FLAG][i] = static_cast<iIMDShape*>(resGetData("IMD", vtolName));
    if (!pAssemblyPointIMDs[VTOL_FLAG][i])
    {
      DBERROR(("Can't find assembly point graphic for vtol factory"));
      return (FALSE);
    }
    pAssemblyPointIMDs[REPAIR_FLAG][i] = static_cast<iIMDShape*>(resGetData("IMD", "mirnum1.pie"));
    if (!pAssemblyPointIMDs[REPAIR_FLAG][i])
    {
      DBERROR(("Can't find assembly point graphic for repair facility"));
      return (FALSE);
    }
  }

  return (TRUE);
}
