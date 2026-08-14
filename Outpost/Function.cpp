#include "pch.h"
/*
 * Function.c
 *
 * Store function stats for the Structures etc.
 *
 */
#include <stdio.h>

#include "Frame.h"
#include "Function.h"
#include "Stats.h"
#include "Structure.h"
#include "Text.h"
#include "Research.h"
#include "Droid.h"
#include "Group.h"

#include "MultiPlay.h"

//holder for all functions
FUNCTION** asFunctions;
UDWORD numFunctions;

//lists the current Upgrade level that can be applied to a structure through research

/*Returns the Function type based on the string - used for reading in data */
static UDWORD functionType(char* pType);
static BOOL storeName(FUNCTION* pFunction, STRING* pNameToStore);
static BOOL loadUpgradeFunction(SBYTE* pData, UBYTE type);

//array of functions pointers for each load function
BOOL (*pLoadFunction[NUMFUNCTIONS])(SBYTE* pData) = //, UDWORD functionType) = 
{
  loadProduction, loadProductionUpgradeFunction, loadResearchFunction, loadResearchUpgradeFunction, loadPowerGenFunction,
  loadResourceFunction, loadRepairDroidFunction, loadWeaponUpgradeFunction, loadWallFunction, loadStructureUpgradeFunction,
  loadWallDefenceUpgradeFunction, loadPowerUpgradeFunction, loadRepairUpgradeFunction, loadDroidRepairUpgradeFunction,
  loadDroidECMUpgradeFunction, loadDroidBodyUpgradeFunction, loadDroidSensorUpgradeFunction, loadDroidConstUpgradeFunction,
  loadReArmFunction, loadReArmUpgradeFunction,
  //loadDefensiveStructFunction,
  //loadRadarMapFunction,
  //loadPowerRegFunction,
  //loadPowerRelayFunction,
  //loadArmourUpgradeFunction,
  //loadRepairUpgradeFunction,
  //loadResistanceUpgradeFunction,
  //	loadFunction,					
  //	loadFunction,
  //	loadFunction,
  //loadBodyUpgradeFunction,
  //loadHQFunction,
};

BOOL loadFunctionStats(SBYTE* pFunctionData, UDWORD bufferSize)
{
  SBYTE* pStartFunctionData;
  UDWORD totalFunctions = 0, i, type; //, player;
  STRING FunctionType[MAX_NAME_SIZE];
  FUNCTION** pStartList;

  //keep the start so we release it at the end
  pStartFunctionData = pFunctionData;

  totalFunctions = numCR((UBYTE*)pFunctionData, bufferSize);

  //allocate storage for the Function pointer array
  asFunctions = new (std::nothrow) FUNCTION*[totalFunctions];
  if (!asFunctions)
  {
    DBERROR(("Out of memory"));
    return FALSE;
  }
  pStartList = asFunctions;
  //initialise the storage
  memset(asFunctions, 0, totalFunctions * sizeof(FUNCTION*));
  numFunctions = 0;
  //numProductionUpgrades =	numResearchUpgrades = 0;//numArmourUpgrades = 
  //numRepairUpgrades = numResistanceUpgrades = numBodyUpgrades = 

  for (i = 0; i < totalFunctions; i++)
  {
    //read the data into the storage - the data is delimeted using comma's
    FunctionType[0] = '\0';
    sscanf(pFunctionData, "%[^',']", &FunctionType);
    type = functionType(FunctionType);
    pFunctionData += (strlen(FunctionType) + 1);

    if (!(pLoadFunction[type](pFunctionData)))
      return FALSE;
    //increment the pointer to the start of the next record
    pFunctionData = strchr(pFunctionData, '\n') + 1;
  }
  //set the function list pointer to the start				
  asFunctions = pStartList;

  //create Upgrade arrays
  /*apProductionUpgrades[player] = (FUNCTION_UPGRADE *) MALLOC(numProductionUpgrades * 
    sizeof(FUNCTION_UPGRADE));
  if (!apProductionUpgrades[player])
  {
    DBERROR(("Out of memory"));
    return FALSE;
  }

  apResearchUpgrades[player] = (FUNCTION_UPGRADE *) MALLOC(numResearchUpgrades * 
    sizeof(FUNCTION_UPGRADE));
  if (!apResearchUpgrades[player])
  {
    DBERROR(("Out of memory"));
    return FALSE;
  }*/

  /*apBodyUpgrades[player] = (FUNCTION_UPGRADE *) MALLOC(numBodyUpgrades * 
    sizeof(FUNCTION_UPGRADE));
  if (!apBodyUpgrades[player])
  {
    DBERROR(("Out of memory"));
    return FALSE;
  }*/

  /*apArmourUpgrades[player] = (FUNCTION_UPGRADE *) MALLOC(numArmourUpgrades * 
    sizeof(FUNCTION_UPGRADE));
  if (!apArmourUpgrades[player])
  {
    DBERROR(("Out of memory"));
    return FALSE;
  }*/

  /*apRepairUpgrades[player] = (FUNCTION_UPGRADE *) MALLOC(numRepairUpgrades * 
    sizeof(FUNCTION_UPGRADE));
  if (!apRepairUpgrades[player])
  {
    DBERROR(("Out of memory"));
    return FALSE;
  }*/

  /*apResistanceUpgrades[player] = (FUNCTION_UPGRADE *) MALLOC(numResistanceUpgrades * 
    sizeof(FUNCTION_UPGRADE));
  if (!apResistanceUpgrades[player])
  {
    DBERROR(("Out of memory"));
    return FALSE;
  }*/
  /*apWeaponUpgrades[player] = (FUNCTION_UPGRADE *) MALLOC(numWeaponUpgrades * 
    sizeof(FUNCTION_UPGRADE));
  if (!apWeaponUpgrades[player])
  {
    DBERROR(("Out of memory"));
    return FALSE;
  }*/

  //numProductionUpgrades =	numResearchUpgrades = 0;//numArmourUpgrades = 
  //numRepairUpgrades = numResistanceUpgrades = numBodyUpgrades = 
  //now fill the Upgrade arrays
  //	switch ((*pStartList)->type)
  /*case (PRODUCTION_UPGRADE_TYPE):
  {
    for (player = 0; player < MAX_PLAYERS; player++)
    {
      apProductionUpgrades[player][numProductionUpgrades].functionInc = i;
      apProductionUpgrades[player][numProductionUpgrades].available = FALSE;
    }
    numProductionUpgrades++;
    break;
  }*/
  /*case (RESEARCH_UPGRADE_TYPE):
  {
    for (player = 0; player < MAX_PLAYERS; player++)
    {
      apResearchUpgrades[player][numResearchUpgrades].functionInc = i;
      apResearchUpgrades[player][numResearchUpgrades].available = FALSE;
    }
    numResearchUpgrades++;
    break;
  }*/
  /*case (ARMOUR_UPGRADE_TYPE):
  {
    for (player = 0; player < MAX_PLAYERS; player++)
    {
      apArmourUpgrades[player][numArmourUpgrades].functionInc = i;
      apArmourUpgrades[player][numArmourUpgrades].available = FALSE;
    }
    numArmourUpgrades++;
    break;
  }*/
  /*case (BODY_UPGRADE_TYPE):
  {
    for (player = 0; player < MAX_PLAYERS; player++)
    {
      apBodyUpgrades[player][numBodyUpgrades].functionInc = i;
      apBodyUpgrades[player][numBodyUpgrades].available = FALSE;
    }
    numBodyUpgrades++;
    break;
  }*/
  /*case (REPAIR_UPGRADE_TYPE):
  {
    for (player = 0; player < MAX_PLAYERS; player++)
    {
      apRepairUpgrades[player][numRepairUpgrades].functionInc = i;
      apRepairUpgrades[player][numRepairUpgrades].available = FALSE;
    }
    numRepairUpgrades++;
    break;
  }*/
  /*case (RESISTANCE_UPGRADE_TYPE):
  {
    for (player = 0; player < MAX_PLAYERS; player++)
    {
      apResistanceUpgrades[player][numResistanceUpgrades].functionInc = i;
      apResistanceUpgrades[player][numResistanceUpgrades].available = FALSE;
    }
    numResistanceUpgrades++;
    break;
  }*/
  /*case (WEAPON_UPGRADE_TYPE):
  {
    for (player = 0; player < MAX_PLAYERS; player++)
    {
      apWeaponUpgrades[player][numWeaponUpgrades].functionInc = i;
      apWeaponUpgrades[player][numWeaponUpgrades].available = FALSE;
    }
    numWeaponUpgrades++;
    break;
  }*/
  //default:
  //do nothing
  //	}//end of switch
  return TRUE;
}

// Allocate storage for the name
BOOL storeName(FUNCTION* pFunction, STRING* pNameToStore)
{
#ifdef HASH_NAMES
  pFunction->NameHash = HashString(pNameToStore);
#else
  pFunction->pName = new (std::nothrow) STRING[strlen(pNameToStore)+1];
  if (pFunction->pName == nullptr)
  {
    DBERROR(("Function Name - Out of memory"));
    return FALSE;
  }
  strcpy(pFunction->pName, pNameToStore);
#endif
  return TRUE;
}

/*BOOL loadFunction(SBYTE *pData, UDWORD functionType)
{
	FUNCTION*				psFunction;
	STRING					functionName[50];

	//allocate storage
	psFunction = (FUNCTION *)MALLOC(sizeof(FUNCTION));
	if (psFunction == NULL)
	{
		DBERROR(("Function - Out of memory"));
		return FALSE;
	}

	//store the pointer in the Function Array
	*asFunctions = (FUNCTION*)psFunction;
	numFunctions++;
	asFunctions++;

	//set the type of function
	switch (functionType)
	{
	case DROID_DESIGN_TYPE:
		psFunction->type = DROID_DESIGN_TYPE;
		break;
	case MAP_MARKER_TYPE:
		psFunction->type = MAP_MARKER_TYPE;
		break;
	case SKY_DOME_MAP_TYPE:
		psFunction->type = SKY_DOME_MAP_TYPE;
		break;
	default:
		DBERROR(("Unknown Function Type"));
		return FALSE;
	}

	//read the data in
	sscanf(pData, "%[^',']", &functionName);

	//allocate storage for the name
	storeName(psFunction, functionName);

	return TRUE;
}*/

BOOL loadProduction(SBYTE* pData)
{
  PRODUCTION_FUNCTION* psFunction;
  STRING functionName[MAX_NAME_SIZE], bodySize[MAX_NAME_SIZE];
  UDWORD productionOutput;
  //allocate storage
  psFunction = new (std::nothrow) PRODUCTION_FUNCTION[1];
  if (psFunction == nullptr)
  {
    DBERROR(("Production Function - Out of memory"));
    return FALSE;
  }
  memset(psFunction, 0, sizeof(PRODUCTION_FUNCTION));

  //store the pointer in the Function Array
  *asFunctions = (FUNCTION*)psFunction;
  psFunction->ref = REF_FUNCTION_START + numFunctions;
  numFunctions++;
  asFunctions++;

  //set the type of function
  psFunction->type = PRODUCTION_TYPE;

  //read the data in
  functionName[0] = '\0';
  bodySize[0] = '\0';
  sscanf(pData, "%[^','],%[^','],%d", &functionName, &bodySize, &productionOutput);

  //allocate storage for the name
  storeName((FUNCTION*)psFunction, functionName);

  //get the propulsion stats pointer
  /*	pPropulsionType = asPropulsionTypes;
    psFunction->propulsionType = 0;
  #ifdef HASH_NAMES
    HashedName=HashString(propulsionType);
  #endif
    for (i=0; i < numPropulsionTypes; i++)
    {
      //compare the names
  #ifdef HASH_NAMES
      if (HashedName== pPropulsionType->NameHash)
  #else
      if (!strcmp(propulsionType, pPropulsionType->pName))
  #endif
      {
        psFunction->propulsionType = pPropulsionType;
        break;
      }
      pPropulsionType++;
    }
    //if haven't found the propulsion type, then problem
    if (!psFunction->propulsionType)
    {
      DBERROR(("Unknown Propulsion Type"));
      return FALSE;
    }
  */

  if (!getBodySize(bodySize, (UBYTE*)&psFunction->capacity))
  {
    ASSERT((FALSE, "loadProduction: unknown body size for %s",psFunction->pName));
    return FALSE;
  }

  //check prod output < UWORD_MAX
  if (productionOutput < UWORD_MAX)
    psFunction->productionOutput = static_cast<UWORD>(productionOutput);
  else
  {
    ASSERT((FALSE, "loadProduction: production Output too big for %s",psFunction->pName));
    psFunction->productionOutput = 0;
  }

  return TRUE;
}

BOOL loadProductionUpgradeFunction(SBYTE* pData)
{
  PRODUCTION_UPGRADE_FUNCTION* psFunction;
  STRING functionName[MAX_NAME_SIZE];
  UDWORD factory, cyborg, vtol;
  UDWORD outputModifier;

  //allocate storage
  psFunction = new (std::nothrow) PRODUCTION_UPGRADE_FUNCTION[1];
  if (psFunction == nullptr)
  {
    DBERROR(("Production Upgrade Function - Out of memory"));
    return FALSE;
  }
  memset(psFunction, 0, sizeof(PRODUCTION_UPGRADE_FUNCTION));

  //store the pointer in the Function Array
  *asFunctions = (FUNCTION*)psFunction;
  psFunction->ref = REF_FUNCTION_START + numFunctions;
  numFunctions++;
  asFunctions++;

  //set the type of function
  psFunction->type = PRODUCTION_UPGRADE_TYPE;

  //read the data in
  functionName[0] = '\0';
  sscanf(pData, "%[^','],%d,%d,%d,%d", &functionName, &factory, &cyborg, &vtol, &outputModifier);

  psFunction->outputModifier = static_cast<UBYTE>(outputModifier);
  //allocate storage for the name
  storeName((FUNCTION*)psFunction, functionName);

  //set the factory flags
  if (factory)
    psFunction->factory = TRUE;
  else
    psFunction->factory = FALSE;
  if (cyborg)
    psFunction->cyborgFactory = TRUE;
  else
    psFunction->cyborgFactory = FALSE;
  if (vtol)
    psFunction->vtolFactory = TRUE;
  else
    psFunction->vtolFactory = FALSE;

  //increment the number of upgrades
  return TRUE;
}

BOOL loadResearchFunction(SBYTE* pData)
{
  RESEARCH_FUNCTION* psFunction;
  STRING functionName[MAX_NAME_SIZE];

  //allocate storage
  psFunction = new (std::nothrow) RESEARCH_FUNCTION[1];
  if (psFunction == nullptr)
  {
    DBERROR(("Research Function - Out of memory"));
    return FALSE;
  }
  memset(psFunction, 0, sizeof(RESEARCH_FUNCTION));

  //store the pointer in the Function Array
  *asFunctions = (FUNCTION*)psFunction;
  psFunction->ref = REF_FUNCTION_START + numFunctions;
  numFunctions++;
  asFunctions++;

  //set the type of function
  psFunction->type = RESEARCH_TYPE;

  //read the data in
  functionName[0] = '\0';
  sscanf(pData, "%[^','],%d", &functionName, &psFunction->researchPoints);

  //allocate storage for the name
  storeName((FUNCTION*)psFunction, functionName);

  return TRUE;
}

BOOL loadReArmFunction(SBYTE* pData)
{
  REARM_FUNCTION* psFunction;
  STRING functionName[MAX_NAME_SIZE];

  //allocate storage
  psFunction = new (std::nothrow) REARM_FUNCTION[1];
  if (psFunction == nullptr)
  {
    DBERROR(("ReArm Function - Out of memory"));
    return FALSE;
  }
  memset(psFunction, 0, sizeof(REARM_FUNCTION));

  //store the pointer in the Function Array
  *asFunctions = (FUNCTION*)psFunction;
  psFunction->ref = REF_FUNCTION_START + numFunctions;
  numFunctions++;
  asFunctions++;

  //set the type of function
  psFunction->type = REARM_TYPE;

  //read the data in
  functionName[0] = '\0';
  sscanf(pData, "%[^','],%d", &functionName, &psFunction->reArmPoints);

  //allocate storage for the name
  storeName((FUNCTION*)psFunction, functionName);

  return TRUE;
}

BOOL loadResearchUpgradeFunction(SBYTE* pData)
{
  if (!loadUpgradeFunction(pData, RESEARCH_UPGRADE_TYPE))
    return FALSE;

  return TRUE;
}

BOOL loadPowerUpgradeFunction(SBYTE* pData)
{
  if (!loadUpgradeFunction(pData, POWER_UPGRADE_TYPE))
    return FALSE;

  return TRUE;
}

BOOL loadRepairUpgradeFunction(SBYTE* pData)
{
  if (!loadUpgradeFunction(pData, REPAIR_UPGRADE_TYPE))
    return FALSE;

  return TRUE;
}

BOOL loadDroidRepairUpgradeFunction(SBYTE* pData)
{
  if (!loadUpgradeFunction(pData, DROIDREPAIR_UPGRADE_TYPE))
    return FALSE;

  return TRUE;
}

BOOL loadDroidECMUpgradeFunction(SBYTE* pData)
{
  if (!loadUpgradeFunction(pData, DROIDECM_UPGRADE_TYPE))
    return FALSE;

  return TRUE;
}

BOOL loadDroidConstUpgradeFunction(SBYTE* pData)
{
  if (!loadUpgradeFunction(pData, DROIDCONST_UPGRADE_TYPE))
    return FALSE;

  return TRUE;
}

BOOL loadReArmUpgradeFunction(SBYTE* pData)
{
  if (!loadUpgradeFunction(pData, REARM_UPGRADE_TYPE))
    return FALSE;

  return TRUE;
}

//generic load function for upgrade type
BOOL loadUpgradeFunction(SBYTE* pData, UBYTE type)
{
  STRING functionName[MAX_NAME_SIZE];
  UDWORD modifier;
  UPGRADE_FUNCTION* psFunction;

  //allocate storage
  psFunction = new (std::nothrow) UPGRADE_FUNCTION[1];
  if (psFunction == nullptr)
  {
    DBERROR(("Upgrade Function - Out of memory"));
    return FALSE;
  }
  memset(psFunction, 0, sizeof(UPGRADE_FUNCTION));

  //store the pointer in the Function Array
  *asFunctions = (FUNCTION*)psFunction;
  psFunction->ref = REF_FUNCTION_START + numFunctions;
  numFunctions++;
  asFunctions++;

  //set the type of function
  psFunction->type = type;

  //read the data in
  functionName[0] = '\0';
  sscanf(pData, "%[^','],%d", &functionName, &modifier);

  //allocate storage for the name
  storeName((FUNCTION*)psFunction, functionName);

  if (modifier > UWORD_MAX)
  {
    ASSERT((FALSE, "loadUpgradeFunction: modifier too great for %s", functionName));
    return FALSE;
  }

  //store the % upgrade
  psFunction->upgradePoints = static_cast<UWORD>(modifier);

  return TRUE;
}

BOOL loadDroidBodyUpgradeFunction(SBYTE* pData)
{
  DROIDBODY_UPGRADE_FUNCTION* psFunction;
  STRING functionName[MAX_NAME_SIZE];
  UDWORD modifier, armourKinetic, armourHeat, body, droid, cyborg;

  //allocate storage
  psFunction = new (std::nothrow) DROIDBODY_UPGRADE_FUNCTION[1];
  if (psFunction == nullptr)
  {
    DBERROR(("UnitBody Upgrade Function - Out of memory"));
    return FALSE;
  }
  memset(psFunction, 0, sizeof(DROIDBODY_UPGRADE_FUNCTION));

  //store the pointer in the Function Array
  *asFunctions = (FUNCTION*)psFunction;
  psFunction->ref = REF_FUNCTION_START + numFunctions;
  numFunctions++;
  asFunctions++;

  //set the type of function
  psFunction->type = DROIDBODY_UPGRADE_TYPE;

  //read the data in
  functionName[0] = '\0';
  sscanf(pData, "%[^','],%d,%d,%d,%d,%d,%d", &functionName, &modifier, &body, &armourKinetic, &armourHeat, &droid, &cyborg);

  //allocate storage for the name
  storeName((FUNCTION*)psFunction, functionName);

  if (modifier > UWORD_MAX OR armourKinetic > UWORD_MAX OR armourHeat > UWORD_MAX OR body > UWORD_MAX)
  {
    ASSERT((FALSE, "loadUnitBodyUpgradeFunction: one or more modifiers too great"));
    return FALSE;
  }

  //store the % upgrades
  psFunction->upgradePoints = static_cast<UWORD>(modifier);
  psFunction->body = static_cast<UWORD>(body);
  psFunction->armourValue[WC_KINETIC] = static_cast<UWORD>(armourKinetic);
  psFunction->armourValue[WC_HEAT] = static_cast<UWORD>(armourHeat);
  if (droid)
    psFunction->droid = TRUE;
  else
    psFunction->droid = FALSE;
  if (cyborg)
    psFunction->cyborg = TRUE;
  else
    psFunction->cyborg = FALSE;

  return TRUE;
}

BOOL loadDroidSensorUpgradeFunction(SBYTE* pData)
{
  DROIDSENSOR_UPGRADE_FUNCTION* psFunction;
  STRING functionName[MAX_NAME_SIZE];
  UDWORD modifier, range;

  //allocate storage
  psFunction = new (std::nothrow) DROIDSENSOR_UPGRADE_FUNCTION[1];
  if (psFunction == nullptr)
  {
    DBERROR(("UnitSensor Upgrade Function - Out of memory"));
    return FALSE;
  }
  memset(psFunction, 0, sizeof(DROIDSENSOR_UPGRADE_FUNCTION));

  //store the pointer in the Function Array
  *asFunctions = (FUNCTION*)psFunction;
  psFunction->ref = REF_FUNCTION_START + numFunctions;
  numFunctions++;
  asFunctions++;

  //set the type of function
  psFunction->type = DROIDSENSOR_UPGRADE_TYPE;

  //read the data in
  functionName[0] = '\0';
  sscanf(pData, "%[^','],%d,%d", &functionName, &modifier, &range);

  //allocate storage for the name
  storeName((FUNCTION*)psFunction, functionName);

  if (modifier > UWORD_MAX OR range > UWORD_MAX)
  {
    ASSERT((FALSE, "loadUnitSensorUpgradeFunction: one or more modifiers too great"));
    return FALSE;
  }

  //store the % upgrades
  psFunction->upgradePoints = static_cast<UWORD>(modifier);
  psFunction->range = static_cast<UWORD>(range);

  return TRUE;
}

BOOL loadWeaponUpgradeFunction(SBYTE* pData)
{
  WEAPON_UPGRADE_FUNCTION* psFunction;
  STRING functionName[MAX_NAME_SIZE], weaponSubClass[MAX_NAME_SIZE];
  UDWORD firePause, shortHit, longHit, damage, radiusDamage, incenDamage, radiusHit;

  //allocate storage
  psFunction = new (std::nothrow) WEAPON_UPGRADE_FUNCTION[1];
  if (psFunction == nullptr)
  {
    DBERROR(("Weapon Upgrade Function - Out of memory"));
    return FALSE;
  }
  memset(psFunction, 0, sizeof(WEAPON_UPGRADE_FUNCTION));

  //store the pointer in the Function Array
  *asFunctions = (FUNCTION*)psFunction;
  psFunction->ref = REF_FUNCTION_START + numFunctions;
  numFunctions++;
  asFunctions++;

  //set the type of function
  psFunction->type = WEAPON_UPGRADE_TYPE;

  //read the data in
  functionName[0] = '\0';
  weaponSubClass[0] = '\0';
  sscanf(pData, "%[^','],%[^','],%d,%d,%d,%d,%d,%d,%d", &functionName, &weaponSubClass, &firePause, &shortHit, &longHit, &damage,
         &radiusDamage, &incenDamage, &radiusHit);

  //allocate storage for the name
  storeName((FUNCTION*)psFunction, functionName);

  psFunction->subClass = static_cast<WEAPON_CLASS>(getWeaponSubClass(weaponSubClass));
  if (psFunction->subClass == INVALID_SUBCLASS)
    return FALSE;

  //check none of the %increases are over UBYTE max
  if (firePause > UBYTE_MAX OR shortHit > UWORD_MAX OR longHit > UWORD_MAX OR damage > UWORD_MAX OR radiusDamage > UWORD_MAX OR incenDamage
    > UWORD_MAX OR radiusHit > UWORD_MAX)
  {
    DBERROR(("A percentage increase for Weapon Upgrade function is too large"));
    return FALSE;
  }

  //copy the data across
  psFunction->firePause = static_cast<UBYTE>(firePause);
  psFunction->shortHit = static_cast<UWORD>(shortHit);
  psFunction->longHit = static_cast<UWORD>(longHit);
  psFunction->damage = static_cast<UWORD>(damage);
  psFunction->radiusDamage = static_cast<UWORD>(radiusDamage);
  psFunction->incenDamage = static_cast<UWORD>(incenDamage);
  psFunction->radiusHit = static_cast<UWORD>(radiusHit);

  //increment the number of upgrades

  return TRUE;
}

BOOL loadStructureUpgradeFunction(SBYTE* pData)
{
  STRUCTURE_UPGRADE_FUNCTION* psFunction;
  STRING functionName[MAX_NAME_SIZE];
  UDWORD armour, body, resistance;

  //allocate storage
  psFunction = new (std::nothrow) STRUCTURE_UPGRADE_FUNCTION[1];
  if (psFunction == nullptr)
  {
    DBERROR(("Structure Upgrade Function - Out of memory"));
    return FALSE;
  }
  memset(psFunction, 0, sizeof(STRUCTURE_UPGRADE_FUNCTION));

  //store the pointer in the Function Array
  *asFunctions = (FUNCTION*)psFunction;
  psFunction->ref = REF_FUNCTION_START + numFunctions;
  numFunctions++;
  asFunctions++;

  //set the type of function
  psFunction->type = STRUCTURE_UPGRADE_TYPE;

  //read the data in
  functionName[0] = '\0';
  sscanf(pData, "%[^','],%d,%d,%d", &functionName, &armour, &body, &resistance);

  //allocate storage for the name
  storeName((FUNCTION*)psFunction, functionName);

  //check none of the %increases are over UWORD max
  if (armour > UWORD_MAX OR body > UWORD_MAX OR resistance > UWORD_MAX)
  {
    DBERROR(("A percentage increase for Structure Upgrade function is too large"));
    return FALSE;
  }

  //copy the data across
  psFunction->armour = static_cast<UWORD>(armour);
  psFunction->body = static_cast<UWORD>(body);
  psFunction->resistance = static_cast<UWORD>(resistance);

  return TRUE;
}

BOOL loadWallDefenceUpgradeFunction(SBYTE* pData)
{
  WALLDEFENCE_UPGRADE_FUNCTION* psFunction;
  STRING functionName[MAX_NAME_SIZE];
  UDWORD armour, body;

  //allocate storage
  psFunction = new (std::nothrow) WALLDEFENCE_UPGRADE_FUNCTION[1];
  if (psFunction == nullptr)
  {
    DBERROR(("WallDefence Upgrade Function - Out of memory"));
    return FALSE;
  }
  memset(psFunction, 0, sizeof(WALLDEFENCE_UPGRADE_FUNCTION));

  //store the pointer in the Function Array
  *asFunctions = (FUNCTION*)psFunction;
  psFunction->ref = REF_FUNCTION_START + numFunctions;
  numFunctions++;
  asFunctions++;

  //set the type of function
  psFunction->type = WALLDEFENCE_UPGRADE_TYPE;

  //read the data in
  functionName[0] = '\0';
  sscanf(pData, "%[^','],%d,%d", &functionName, &armour, &body);

  //allocate storage for the name
  storeName((FUNCTION*)psFunction, functionName);

  //check none of the %increases are over UWORD max
  if (armour > UWORD_MAX OR body > UWORD_MAX)
  {
    DBERROR(("A percentage increase for WallDefence Upgrade function is too large"));
    return FALSE;
  }

  //copy the data across
  psFunction->armour = static_cast<UWORD>(armour);
  psFunction->body = static_cast<UWORD>(body);

  return TRUE;
}

/*BOOL loadBodyUpgradeFunction(SBYTE *pData)
{
	BODY_UPGRADE_FUNCTION*		psFunction;
	STRING						functionName[MAX_NAME_SIZE];

	//allocate storage
	psFunction = (BODY_UPGRADE_FUNCTION *)MALLOC(sizeof
		(BODY_UPGRADE_FUNCTION));
	if (psFunction == NULL)
	{
		DBERROR(("Body Upgrade Function - Out of memory"));
		return FALSE;
	}

	//store the pointer in the Function Array
	*asFunctions = (FUNCTION *)psFunction;
	psFunction->ref = REF_FUNCTION_START + numFunctions;
	numFunctions++;
	asFunctions++;

	//set the type of function
	psFunction->type = BODY_UPGRADE_TYPE;
	
	//read the data in
	functionName[0] = '\0';
	sscanf(pData, "%[^','],%d", &functionName, &psFunction->bodyPoints);

	//allocate storage for the name
	storeName((FUNCTION *)psFunction, functionName);

	//increment the number of upgrades
	numBodyUpgrades++;

	return TRUE;
}*/

/*BOOL loadRadarMapFunction(SBYTE *pData)
{
	RADAR_MAP_FUNCTION*			psFunction;
	STRING						functionName[MAX_NAME_SIZE];

	//allocate storage
	psFunction = (RADAR_MAP_FUNCTION *)MALLOC(sizeof
		(RADAR_MAP_FUNCTION));
	if (psFunction == NULL)
	{
		DBERROR(("Radar Map Function - Out of memory"));
		return FALSE;
	}

	//store the pointer in the Function Array
	*asFunctions = (FUNCTION *)psFunction;
	psFunction->ref = REF_FUNCTION_START + numFunctions;
	numFunctions++;
	asFunctions++;

	//set the type of function
	psFunction->type = RADAR_MAP_TYPE;
	
	//read the data in
	functionName[0] = '\0';
	sscanf(pData, "%[^','],%d,%d", &functionName, 
		&psFunction->radarDecayRate, &psFunction->radarRadius);

	//allocate storage for the name
	storeName((FUNCTION *)psFunction, functionName);

	return TRUE;
}*/

BOOL loadPowerGenFunction(SBYTE* pData)
{
  POWER_GEN_FUNCTION* psFunction;
  STRING functionName[MAX_NAME_SIZE];

  //allocate storage
  psFunction = new (std::nothrow) POWER_GEN_FUNCTION[1];
  if (psFunction == nullptr)
  {
    DBERROR(("Power Gen Function - Out of memory"));
    return FALSE;
  }
  memset(psFunction, 0, sizeof(POWER_GEN_FUNCTION));

  //store the pointer in the Function Array
  *asFunctions = (FUNCTION*)psFunction;
  psFunction->ref = REF_FUNCTION_START + numFunctions;
  numFunctions++;
  asFunctions++;

  //set the type of function
  psFunction->type = POWER_GEN_TYPE;

  //read the data in
  functionName[0] = '\0';
  sscanf(pData, "%[^','],%d,%d,%d,%d,%d,%d", &functionName, &psFunction->powerOutput, &psFunction->powerMultiplier,
         &psFunction->criticalMassChance, &psFunction->criticalMassRadius, &psFunction->criticalMassDamage,
         &psFunction->radiationDecayTime);

  if (bMultiPlayer)
    modifyResources(psFunction);

  //allocate storage for the name
  storeName((FUNCTION*)psFunction, functionName);

  return TRUE;
}

BOOL loadResourceFunction(SBYTE* pData)
{
  RESOURCE_FUNCTION* psFunction;
  STRING functionName[MAX_NAME_SIZE];

  //allocate storage
  psFunction = new (std::nothrow) RESOURCE_FUNCTION[1];
  if (psFunction == nullptr)
  {
    DBERROR(("Resource Function - Out of memory"));
    return FALSE;
  }
  memset(psFunction, 0, sizeof(RESOURCE_FUNCTION));

  //store the pointer in the Function Array
  *asFunctions = (FUNCTION*)psFunction;
  psFunction->ref = REF_FUNCTION_START + numFunctions;
  numFunctions++;
  asFunctions++;

  //set the type of function
  psFunction->type = RESOURCE_TYPE;

  //read the data in
  functionName[0] = '\0';
  sscanf(pData, "%[^','],%d", &functionName, &psFunction->maxPower);

  //allocate storage for the name
  storeName((FUNCTION*)psFunction, functionName);

  return TRUE;
}

/*BOOL loadPowerRegFunction(SBYTE *pData)
{
	POWER_REG_FUNCTION*			psFunction;
	STRING						functionName[MAX_NAME_SIZE];

	//allocate storage
	psFunction = (POWER_REG_FUNCTION *)MALLOC(sizeof
		(POWER_REG_FUNCTION));
	if (psFunction == NULL)
	{
		DBERROR(("Power Reg Function - Out of memory"));
		return FALSE;
	}

	//store the pointer in the Function Array
	*asFunctions = (FUNCTION *)psFunction;
	psFunction->ref = REF_FUNCTION_START + numFunctions;
	numFunctions++;
	asFunctions++;

	//set the type of function
 	psFunction->type = POWER_REG_TYPE;
	
	//read the data in
	functionName[0] = '\0';
	sscanf(pData, "%[^','],%d", &functionName, &psFunction->maxPower);

	//allocate storage for the name
	storeName((FUNCTION *)psFunction, functionName);

	return TRUE;
}*/

/*BOOL loadPowerRelayFunction(SBYTE *pData)
{
	POWER_RELAY_FUNCTION*			psFunction;
	STRING						functionName[MAX_NAME_SIZE];

	//allocate storage
	psFunction = (POWER_RELAY_FUNCTION *)MALLOC(sizeof
		(POWER_RELAY_FUNCTION));
	if (psFunction == NULL)
	{
		DBERROR(("Power Relay Function - Out of memory"));
		return FALSE;
	}

	//store the pointer in the Function Array
	*asFunctions = (FUNCTION *)psFunction;
	psFunction->ref = REF_FUNCTION_START + numFunctions;
	numFunctions++;
	asFunctions++;

	//set the type of function
	psFunction->type = POWER_RELAY_TYPE;
	
	//read the data in
	functionName[0] = '\0';
	sscanf(pData, "%[^','],%d,%d", &functionName, 
		&psFunction->powerRelayType, &psFunction->powerRelayRange);

	//allocate storage for the name
	storeName((FUNCTION *)psFunction, functionName);

	return TRUE;
}*/

BOOL loadRepairDroidFunction(SBYTE* pData)
{
  REPAIR_DROID_FUNCTION* psFunction;
  STRING functionName[MAX_NAME_SIZE];

  //allocate storage
  psFunction = new (std::nothrow) REPAIR_DROID_FUNCTION[1];
  if (psFunction == nullptr)
  {
    DBERROR(("Repair Droid Function - Out of memory"));
    return FALSE;
  }
  memset(psFunction, 0, sizeof(REPAIR_DROID_FUNCTION));

  //store the pointer in the Function Array
  *asFunctions = (FUNCTION*)psFunction;
  psFunction->ref = REF_FUNCTION_START + numFunctions;
  numFunctions++;
  asFunctions++;

  //set the type of function
  psFunction->type = REPAIR_DROID_TYPE;

  //read the data in
  functionName[0] = '\0';
  sscanf(pData, "%[^','],%d", &functionName, &psFunction->repairPoints);

  //allocate storage for the name
  storeName((FUNCTION*)psFunction, functionName);

  return TRUE;
}

/*BOOL loadDefensiveStructFunction(SBYTE *pData)
{
	//DBERROR(("Defensive Structure Function is not longer used - \
	DEFENSIVE_STRUCTURE_FUNCTION*	psFunction;
	UDWORD							i;
	STRING							functionName[MAX_NAME_SIZE];
	STRING							sensorType[MAX_NAME_SIZE];
	STRING							ecmType[MAX_NAME_SIZE];
	SENSOR_STATS*					pSensorType;
	ECM_STATS*						pECMType;

	//allocate storage
	psFunction = (DEFENSIVE_STRUCTURE_FUNCTION *)MALLOC(
		sizeof(DEFENSIVE_STRUCTURE_FUNCTION));
	if (psFunction == NULL)
	{
		DBERROR(("Defensive Structure Function - Out of memory"));
		return FALSE;
	}

	//store the pointer in the Function Array
	*asFunctions = (FUNCTION *)psFunction;
	psFunction->ref = REF_FUNCTION_START + numFunctions;
	numFunctions++;
	asFunctions++;

	//set the type of function
	psFunction->type = DEFENSIVE_STRUCTURE_TYPE;
	
	//read the data in
	sscanf(pData, "%[^','],%[^','],%[^','],%d", &functionName, &ecmType, 
		&sensorType, &psFunction->weaponCapacity);

	//allocate storage for the name
	storeName((FUNCTION *)psFunction, functionName);

	//get the sensor stats pointer
	if (!strcmp(sensorType,"0"))
	{
		psFunction->pSensor = NULL;
	}
	else
	{
		pSensorType = asSensorStats;
		for (i=0; i < numSensorStats; i++)
		{
			//compare the names
			if (!strcmp(sensorType, pSensorType->pName))
			{
				psFunction->pSensor = pSensorType;
				break;
			}
			pSensorType++;
		}
	}
	//get the ecm stats pointer
	if (!strcmp(ecmType,"0"))
	{
		psFunction->pECM = NULL;
	}
	else
	{
		pECMType = asECMStats;
		for (i=0; i < numECMStats; i++)
		{
			//compare the names
			if (!strcmp(ecmType, pECMType->pName))
			{
				psFunction->pECM = pECMType;
				break;
			}
			pECMType++;
		}
	}
	return TRUE;
}*/

/*BOOL loadHQFunction(SBYTE *pData)
{
	HQ_FUNCTION*		psFunction;
	STRING				functionName[MAX_NAME_SIZE];

	//allocate storage
	psFunction = (HQ_FUNCTION *)MALLOC(sizeof(HQ_FUNCTION));
	if (psFunction == NULL)
	{
		DBERROR(("HQ Function - Out of memory"));
		return FALSE;
	}

	//store the pointer in the Function Array
	*asFunctions = (FUNCTION *)psFunction;
	psFunction->ref = REF_FUNCTION_START + numFunctions;
	numFunctions++;
	asFunctions++;

	//set the type of function
	psFunction->type = HQ_TYPE;
	
	//read the data in
	functionName[0] = '\0';
	sscanf(pData, "%[^','],%d", &functionName, 
		&psFunction->power);

	//allocate storage for the name
	storeName((FUNCTION *)psFunction, functionName);

	return TRUE;
}*/

/*BOOL loadArmourUpgradeFunction(SBYTE *pData)
{
	ARMOUR_UPGRADE_FUNCTION*	psFunction;
	STRING						functionName[MAX_NAME_SIZE];

	//allocate storage
	psFunction = (ARMOUR_UPGRADE_FUNCTION *)MALLOC(sizeof(ARMOUR_UPGRADE_FUNCTION));
	if (psFunction == NULL)
	{
		DBERROR(("Armour Upgrade Function - Out of memory"));
		return FALSE;
	}

	//store the pointer in the Function Array
	*asFunctions = (FUNCTION *)psFunction;
	psFunction->ref = REF_FUNCTION_START + numFunctions;
	numFunctions++;
	asFunctions++;

	//set the type of function
	psFunction->type = ARMOUR_UPGRADE_TYPE;
	
	//read the data in
	functionName[0] = '\0';
	sscanf(pData, "%[^','],%d,%d,%d", &functionName,
		&psFunction->buildPoints, &psFunction->powerRequired, &psFunction->armourPoints);

	//allocate storage for the name
	storeName((FUNCTION *)psFunction, functionName);

	//get the armour stats pointer
		//compare the names
	//	if (!strcmp(armourType, pArmourType->pName))
	//if not found the armour stat then problem

	//increment the number of upgrades
	numArmourUpgrades++;

	return TRUE;
}*/

/*BOOL loadRepairUpgradeFunction(SBYTE *pData)
{
	REPAIR_UPGRADE_FUNCTION*	psFunction;
	UDWORD						i;
	STRING						functionName[MAX_NAME_SIZE];
	STRING						repairType[MAX_NAME_SIZE];
	REPAIR_STATS*				pRepairType;

#ifdef HASH_NAMES
	UDWORD	HashedName;
#endif
	//allocate storage
	psFunction = (REPAIR_UPGRADE_FUNCTION *)MALLOC(sizeof(REPAIR_UPGRADE_FUNCTION));
	if (psFunction == NULL)
	{
		DBERROR(("Repair Upgrade Function - Out of memory"));
		return FALSE;
	}

	//store the pointer in the Function Array
	*asFunctions = (FUNCTION *)psFunction;
	psFunction->ref = REF_FUNCTION_START + numFunctions;
	numFunctions++;
	asFunctions++;

	//set the type of function
	psFunction->type = REPAIR_UPGRADE_TYPE;
	
	//read the data in
	functionName[0] = '\0';
	repairType[0] = '\0';
	sscanf(pData, "%[^','],%[^','],%d,%d,%d", &functionName,
		&repairType, &psFunction->repairPoints, &psFunction->buildPoints,
		&psFunction->powerRequired);

	//allocate storage for the name
	storeName((FUNCTION *)psFunction, functionName);

	if (!getResourceName(repairType))
	{
		return FALSE;
	}

	//get the repair stats pointer
	pRepairType = asRepairStats;
	psFunction->pRepair = NULL;
#ifdef HASH_NAMES
	HashedName=HashString(repairType);
#endif

	for (i=0; i < numRepairStats; i++)
	{
		//compare the names
#ifdef HASH_NAMES
		if (HashedName== pRepairType->NameHash)
#else
		if (!strcmp(repairType, pRepairType->pName))
#endif
		{
			psFunction->pRepair = pRepairType;
			break;
		}
		pRepairType++;
	}
	//if not found the repair stats then problem
	if (!psFunction->pRepair)
	{
		DBERROR(("Repair Stats Invalid for function - %s", functionName));
		return FALSE;
	}
	//increment the number of upgrades
	numRepairUpgrades++;

	return TRUE;
}*/
/*BOOL loadResistanceUpgradeFunction(SBYTE *pData)
{
	RESISTANCE_UPGRADE_FUNCTION*		psFunction;
	STRING								functionName[MAX_NAME_SIZE];

	//allocate storage
	psFunction = (RESISTANCE_UPGRADE_FUNCTION *)MALLOC(sizeof
		(RESISTANCE_UPGRADE_FUNCTION));
	if (psFunction == NULL)
	{
		DBERROR(("Resistance Upgrade Function - Out of memory"));
		return FALSE;
	}

	//store the pointer in the Function Array
	*asFunctions = (FUNCTION *)psFunction;
	psFunction->ref = REF_FUNCTION_START + numFunctions;
	numFunctions++;
	asFunctions++;

	//set the type of function
	psFunction->type = RESISTANCE_UPGRADE_TYPE;
	
	//read the data in
	functionName[0] = '\0';
	sscanf(pData, "%[^','],%d,%d,%d,%d", &functionName, 
		&psFunction->resistanceUpgrade, &psFunction->buildPoints, 
		&psFunction->powerRequired, &psFunction->resistancePoints);

	//allocate storage for the name
	storeName((FUNCTION *)psFunction, functionName);

	//increment the number of upgrades
	numResistanceUpgrades++;

	return TRUE;
}*/

/*loads the corner stat to use for a particular wall stat */
BOOL loadWallFunction(SBYTE* pData)
{
  WALL_FUNCTION* psFunction;
  STRING functionName[MAX_NAME_SIZE];
  STRING structureName[MAX_NAME_SIZE];

  //allocate storage
  psFunction = new (std::nothrow) WALL_FUNCTION[1];
  if (psFunction == nullptr)
  {
    DBERROR(("Wall Function - Out of memory"));
    return FALSE;
  }
  memset(psFunction, 0, sizeof(WALL_FUNCTION));

  //store the pointer in the Function Array
  *asFunctions = (FUNCTION*)psFunction;
  psFunction->ref = REF_FUNCTION_START + numFunctions;
  numFunctions++;
  asFunctions++;

  //set the type of function
  psFunction->type = WALL_TYPE;

  //read the data in
  functionName[0] = '\0';
  structureName[0] = '\0';
  sscanf(pData, "%[^','],%[^','],%*d", &functionName, &structureName);

  //allocate storage for the name
  storeName((FUNCTION*)psFunction, functionName);

  //store the structure name - cannot set the stat pointer here because structures 
  //haven't been loaded in yet!
#ifdef HASH_NAMES
  psFunction->StructNameHash = HashString(structureName);
#else
  if (!allocateName(&psFunction->pStructName, structureName))
  {
    DBERROR(("Structure Stats Invalid for function - %s", functionName));
    return FALSE;
  }
#endif
  psFunction->pCornerStat = nullptr;

  return TRUE;
}

void productionUpgrade(FUNCTION* pFunction, UBYTE player)
{
  PRODUCTION_UPGRADE_FUNCTION* pUpgrade;

  pUpgrade = (PRODUCTION_UPGRADE_FUNCTION*)pFunction;

  //check upgrades increase all values
  if (pUpgrade->factory)
  {
    if (asProductionUpgrade[player][FACTORY_FLAG].modifier < pUpgrade->outputModifier)
    {
      asProductionUpgrade[player][FACTORY_FLAG].modifier = pUpgrade->outputModifier;
    }
  }
  if (pUpgrade->cyborgFactory)
  {
    if (asProductionUpgrade[player][CYBORG_FLAG].modifier < pUpgrade->outputModifier)
    {
      asProductionUpgrade[player][CYBORG_FLAG].modifier = pUpgrade->outputModifier;
    }
  }
  if (pUpgrade->vtolFactory)
  {
    if (asProductionUpgrade[player][VTOL_FLAG].modifier < pUpgrade->outputModifier)
    {
      asProductionUpgrade[player][VTOL_FLAG].modifier = pUpgrade->outputModifier;
    }
  }
}

void researchUpgrade(FUNCTION* pFunction, UBYTE player)
{
  RESEARCH_UPGRADE_FUNCTION* pUpgrade;

  pUpgrade = (RESEARCH_UPGRADE_FUNCTION*)pFunction;

  //check upgrades increase all values
  if (asResearchUpgrade[player].modifier < pUpgrade->upgradePoints)
    asResearchUpgrade[player].modifier = pUpgrade->upgradePoints;
}

void repairFacUpgrade(FUNCTION* pFunction, UBYTE player)
{
  REPAIR_UPGRADE_FUNCTION* pUpgrade;

  pUpgrade = (REPAIR_UPGRADE_FUNCTION*)pFunction;

  //check upgrades increase all values
  if (asRepairFacUpgrade[player].modifier < pUpgrade->upgradePoints)
    asRepairFacUpgrade[player].modifier = pUpgrade->upgradePoints;
}

void powerUpgrade(FUNCTION* pFunction, UBYTE player)
{
  POWER_UPGRADE_FUNCTION* pUpgrade;

  pUpgrade = (POWER_UPGRADE_FUNCTION*)pFunction;

  //check upgrades increase all values
  if (asPowerUpgrade[player].modifier < pUpgrade->upgradePoints)
    asPowerUpgrade[player].modifier = pUpgrade->upgradePoints;
}

void reArmUpgrade(FUNCTION* pFunction, UBYTE player)
{
  REARM_UPGRADE_FUNCTION* pUpgrade;

  pUpgrade = (REARM_UPGRADE_FUNCTION*)pFunction;

  //check upgrades increase all values
  if (asReArmUpgrade[player].modifier < pUpgrade->upgradePoints)
    asReArmUpgrade[player].modifier = pUpgrade->upgradePoints;
}

void structureBodyUpgrade(FUNCTION* pFunction, STRUCTURE* psBuilding)
{
  UWORD increase, prevBaseBody, newBaseBody;

  switch (psBuilding->pStructureType->type)
  {
  case REF_WALL:
  case REF_WALLCORNER:
  case REF_DEFENSE:
  case REF_BLASTDOOR:
    increase = ((WALLDEFENCE_UPGRADE_FUNCTION*)pFunction)->body;
    break;
  default:
    increase = ((STRUCTURE_UPGRADE_FUNCTION*)pFunction)->body;
    break;
  }

  prevBaseBody = static_cast<UWORD>(structureBody(psBuilding));
  //newBaseBody = (UWORD)(psBuilding->pStructureType->bodyPoints + (psBuilding->
  newBaseBody = static_cast<UWORD>(structureBaseBody(psBuilding) + (structureBaseBody(psBuilding) * increase) / 100);

  if (newBaseBody > prevBaseBody)
    psBuilding->body = static_cast<UWORD>((psBuilding->body * newBaseBody) / prevBaseBody);
}

void structureArmourUpgrade(FUNCTION* pFunction, STRUCTURE* psBuilding)
{
  UWORD increase, prevBaseArmour, newBaseArmour;

  switch (psBuilding->pStructureType->type)
  {
  case REF_WALL:
  case REF_WALLCORNER:
  case REF_DEFENSE:
  case REF_BLASTDOOR:
    increase = ((WALLDEFENCE_UPGRADE_FUNCTION*)pFunction)->armour;
    break;
  default:
    increase = ((STRUCTURE_UPGRADE_FUNCTION*)pFunction)->armour;
    break;
  }

  prevBaseArmour = static_cast<UWORD>(structureArmour(psBuilding->pStructureType, psBuilding->player));
  newBaseArmour = static_cast<UWORD>(psBuilding->pStructureType->armourValue + (psBuilding->pStructureType->armourValue * increase) / 100);

  if (newBaseArmour > prevBaseArmour)
    psBuilding->armour = static_cast<UWORD>((psBuilding->armour * newBaseArmour) / prevBaseArmour);
}

void structureResistanceUpgrade(FUNCTION* pFunction, STRUCTURE* psBuilding)
{
  UWORD increase, prevBaseResistance, newBaseResistance;

  increase = ((STRUCTURE_UPGRADE_FUNCTION*)pFunction)->resistance;

  prevBaseResistance = static_cast<UWORD>(structureResistance(psBuilding->pStructureType, psBuilding->player));
  newBaseResistance = static_cast<UWORD>(psBuilding->pStructureType->resistance + (psBuilding->pStructureType->resistance * increase) /
    100);

  if (newBaseResistance > prevBaseResistance)
  {
    psBuilding->resistance = static_cast<UWORD>((psBuilding->resistance * newBaseResistance) / prevBaseResistance);
  }
}

void structureProductionUpgrade(STRUCTURE* psBuilding)
{
  FACTORY* pFact;
  PRODUCTION_FUNCTION* pFactFunc;
  UDWORD type, baseOutput, i;
  STRUCTURE_STATS* psStat;

  switch (psBuilding->pStructureType->type)
  {
  case REF_FACTORY:
    type = FACTORY_FLAG;
    break;
  case REF_CYBORG_FACTORY:
    type = CYBORG_FLAG;
    break;
  case REF_VTOL_FACTORY:
    type = VTOL_FLAG;
    break;
  default: ASSERT((FALSE, "structureProductionUpgrade: Invalid factory type"));
    return;
  }

  //upgrade the Output
  pFact = (FACTORY*)psBuilding->pFunctionality;

  pFactFunc = (PRODUCTION_FUNCTION*)psBuilding->pStructureType->asFuncList[0];

  //current base value depends on whether there are modules attached to the structure
  baseOutput = pFactFunc->productionOutput;
  psStat = getModuleStat(psBuilding);
  if (psStat)
  {
    for (i = 0; i < pFact->capacity; i++)
      baseOutput += ((PRODUCTION_FUNCTION*)psStat->asFuncList[0])->productionOutput;
  }

  pFact->productionOutput = static_cast<UBYTE>(baseOutput + (pFactFunc->productionOutput * asProductionUpgrade[psBuilding->player][type].
    modifier) / 100);
}

void structureResearchUpgrade(STRUCTURE* psBuilding)
{
  RESEARCH_FACILITY* pRes;
  RESEARCH_FUNCTION* pResFunc;
  UDWORD baseOutput;
  STRUCTURE_STATS* psStat;

  //upgrade the research points
  pRes = (RESEARCH_FACILITY*)psBuilding->pFunctionality;

  pResFunc = (RESEARCH_FUNCTION*)psBuilding->pStructureType->asFuncList[0];

  //current base value depends on whether there are modules attached to the structure
  baseOutput = pResFunc->researchPoints;
  psStat = getModuleStat(psBuilding);
  if (psStat)
  {
    if (pRes->capacity)
      baseOutput += ((RESEARCH_FUNCTION*)psStat->asFuncList[0])->researchPoints;
  }
  pRes->researchPoints = baseOutput + (pResFunc->researchPoints * asResearchUpgrade[psBuilding->player].modifier) / 100;
}

void structureReArmUpgrade(STRUCTURE* psBuilding)
{
  REARM_PAD* pPad;
  REARM_FUNCTION* pPadFunc;

  //upgrade the reArm points
  pPad = (REARM_PAD*)psBuilding->pFunctionality;

  pPadFunc = (REARM_FUNCTION*)psBuilding->pStructureType->asFuncList[0];

  pPad->reArmPoints = pPadFunc->reArmPoints + (pPadFunc->reArmPoints * asReArmUpgrade[psBuilding->player].modifier) / 100;
}

void structurePowerUpgrade(STRUCTURE* psBuilding)
{
  POWER_GEN* pPowerGen;
  POWER_GEN_FUNCTION* pPGFunc;
  UDWORD baseOutput;
  STRUCTURE_STATS* psStat;

  //upgrade the research points
  pPowerGen = (POWER_GEN*)psBuilding->pFunctionality;

  pPGFunc = (POWER_GEN_FUNCTION*)psBuilding->pStructureType->asFuncList[0];

  //current base value depends on whether there are modules attached to the structure
  baseOutput = pPGFunc->powerMultiplier;
  psStat = getModuleStat(psBuilding);
  if (psStat)
  {
    if (pPowerGen->capacity)
      baseOutput += ((POWER_GEN_FUNCTION*)psStat->asFuncList[0])->powerOutput;
  }
  pPowerGen->multiplier = baseOutput + (pPGFunc->powerMultiplier * asPowerUpgrade[psBuilding->player].modifier) / 100;
}

void structureRepairUpgrade(STRUCTURE* psBuilding)
{
  REPAIR_FACILITY* pRepair;
  REPAIR_DROID_FUNCTION* pRepairFunc;

  //upgrade the research points
  pRepair = (REPAIR_FACILITY*)psBuilding->pFunctionality;

  pRepairFunc = (REPAIR_DROID_FUNCTION*)psBuilding->pStructureType->asFuncList[0];

  pRepair->power = pRepairFunc->repairPoints + (pRepairFunc->repairPoints * asRepairFacUpgrade[psBuilding->player].modifier) / 100;
}

void structureSensorUpgrade(STRUCTURE* psBuilding)
{
  //reallocate the sensor range and power since the upgrade
  if (psBuilding->pStructureType->pSensor)
  {
    psBuilding->sensorRange = static_cast<UWORD>(sensorRange(psBuilding->pStructureType->pSensor, psBuilding->player));
    psBuilding->sensorPower = static_cast<UWORD>(sensorPower(psBuilding->pStructureType->pSensor, psBuilding->player));
  }
  else
  {
    //give them the default sensor for droids if not
    psBuilding->sensorRange = static_cast<UWORD>(sensorRange(asSensorStats + aDefaultSensor[psBuilding->player], psBuilding->player));
    psBuilding->sensorPower = static_cast<UWORD>(sensorPower(asSensorStats + aDefaultSensor[psBuilding->player], psBuilding->player));
  }
}

void structureECMUpgrade(STRUCTURE* psBuilding)
{
  //reallocate the sensor range and power since the upgrade
  if (psBuilding->pStructureType->pECM)
  {
    psBuilding->ecmPower = static_cast<UWORD>(ecmPower(psBuilding->pStructureType->pECM, psBuilding->player));
  }
  else
    psBuilding->ecmPower = 0;
}

void droidSensorUpgrade(DROID* psDroid)
{
  //reallocate the sensor range and power since the upgrade
  psDroid->sensorRange = sensorRange((asSensorStats + psDroid->asBits[COMP_SENSOR].nStat), psDroid->player);
  psDroid->sensorPower = sensorPower((asSensorStats + psDroid->asBits[COMP_SENSOR].nStat), psDroid->player);
}

void droidECMUpgrade(DROID* psDroid)
{
  //reallocate the ecm power since the upgrade
  psDroid->ECMMod = ecmPower((asECMStats + psDroid->asBits[COMP_ECM].nStat), psDroid->player);
}

void droidBodyUpgrade(FUNCTION* pFunction, DROID* psDroid)
{
  UDWORD increase, prevBaseBody, newBaseBody, base;
  DROID* psCurr;

  increase = ((DROIDBODY_UPGRADE_FUNCTION*)pFunction)->body;

  prevBaseBody = psDroid->originalBody;
  base = calcDroidBaseBody(psDroid);
  newBaseBody = base + (base * increase) / 100;

  if (newBaseBody > prevBaseBody)
  {
    psDroid->body = (psDroid->body * newBaseBody) / prevBaseBody;
    psDroid->originalBody = newBaseBody;
  }
  //if a transporter droid then need to upgrade the contents
  if (psDroid->droidType == DROID_TRANSPORTER)
  {
    for (psCurr = psDroid->psGroup->psList; psCurr != nullptr; psCurr = psCurr->psGrpNext)
    {
      if (psCurr != psDroid)
        droidBodyUpgrade(pFunction, psCurr);
    }
  }
}

/*void armourUpgrade(FUNCTION *pFunction, STRUCTURE *psBuilding)
{
	psBuilding->armour = psBuilding->pStructureType->armourValue + (psBuilding->
		pStructureType->armourValue *((	ARMOUR_UPGRADE_FUNCTION *)pFunction)->
		armourPoints) / 100;
}*/

/*void repairUpgrade(FUNCTION *pFunction, STRUCTURE *psBuilding)
{
	psBuilding->repair = psBuilding->pStructureType->repairSystem + (psBuilding->
		pStructureType->repairSystem *((REPAIR_UPGRADE_FUNCTION *)pFunction)->
		repairPoints) / 100;
}*/

/*void bodyUpgrade(FUNCTION *pFunction, STRUCTURE *psBuilding)
{
	UDWORD	increment;
	 
	increment = (psBuilding->pStructureType->bodyPoints *((
		BODY_UPGRADE_FUNCTION *)pFunction)->bodyPoints) / 100;

	psBuilding->body += increment;
	//upgrade the base body points
	psBuilding->baseBodyPoints += increment;
}*/

/*void resistanceUpgrade(FUNCTION *pFunction, STRUCTURE *psBuilding)
{
	psBuilding->resistance = psBuilding->pStructureType->resistance + (psBuilding->
		pStructureType->resistance *((RESISTANCE_UPGRADE_FUNCTION *)pFunction)->
		resistancePoints) / 100;

	/*I'm not convinced this function is ever going to get used but if it does then
	we will have to keep a copy of the baseResistancePoints per Structure like we do
	with the body points, but I don't want to increase the size of STRUCTURE if we
	don't need to! AB 01/07/98
	ASSERT((TRUE, 
		"resistanceUpgrade - for this function to work, there has to be a code change"));
}*/

//upgrade the weapon stats for the correct subclass
void weaponUpgrade(FUNCTION* pFunction, UBYTE player)
{
  WEAPON_UPGRADE_FUNCTION* pUpgrade;

  pUpgrade = (WEAPON_UPGRADE_FUNCTION*)pFunction;

  //check upgrades increase all values!
  if (asWeaponUpgrade[player][pUpgrade->subClass].firePause < pUpgrade->firePause)
  {
    //make sure don't go less than 100%
    if (pUpgrade->firePause > 100)
      pUpgrade->firePause = 100;
    asWeaponUpgrade[player][pUpgrade->subClass].firePause = pUpgrade->firePause;
  }
  if (asWeaponUpgrade[player][pUpgrade->subClass].shortHit < pUpgrade->shortHit)
    asWeaponUpgrade[player][pUpgrade->subClass].shortHit = pUpgrade->shortHit;
  if (asWeaponUpgrade[player][pUpgrade->subClass].longHit < pUpgrade->longHit)
    asWeaponUpgrade[player][pUpgrade->subClass].longHit = pUpgrade->longHit;
  if (asWeaponUpgrade[player][pUpgrade->subClass].damage < pUpgrade->damage)
    asWeaponUpgrade[player][pUpgrade->subClass].damage = pUpgrade->damage;
  if (asWeaponUpgrade[player][pUpgrade->subClass].radiusDamage < pUpgrade->radiusDamage)
    asWeaponUpgrade[player][pUpgrade->subClass].radiusDamage = pUpgrade->radiusDamage;
  if (asWeaponUpgrade[player][pUpgrade->subClass].incenDamage < pUpgrade->incenDamage)
    asWeaponUpgrade[player][pUpgrade->subClass].incenDamage = pUpgrade->incenDamage;
  if (asWeaponUpgrade[player][pUpgrade->subClass].radiusHit < pUpgrade->radiusHit)
    asWeaponUpgrade[player][pUpgrade->subClass].radiusHit = pUpgrade->radiusHit;
}

//upgrade the sensor stats 
void sensorUpgrade(FUNCTION* pFunction, UBYTE player)
{
  DROIDSENSOR_UPGRADE_FUNCTION* pUpgrade;

  pUpgrade = (DROIDSENSOR_UPGRADE_FUNCTION*)pFunction;

  //check upgrades increase all values!
  if (asSensorUpgrade[player].range < pUpgrade->range)
    asSensorUpgrade[player].range = pUpgrade->range;
  if (asSensorUpgrade[player].power < pUpgrade->upgradePoints)
    asSensorUpgrade[player].power = pUpgrade->upgradePoints;
}

//upgrade the repair stats 
void repairUpgrade(FUNCTION* pFunction, UBYTE player)
{
  DROIDREPAIR_UPGRADE_FUNCTION* pUpgrade;

  pUpgrade = (DROIDREPAIR_UPGRADE_FUNCTION*)pFunction;

  //check upgrades increase all values!
  if (asRepairUpgrade[player].repairPoints < pUpgrade->upgradePoints)
    asRepairUpgrade[player].repairPoints = pUpgrade->upgradePoints;
}

//upgrade the repair stats 
void ecmUpgrade(FUNCTION* pFunction, UBYTE player)
{
  DROIDECM_UPGRADE_FUNCTION* pUpgrade;

  pUpgrade = (DROIDECM_UPGRADE_FUNCTION*)pFunction;

  //check upgrades increase all values!
  if (asECMUpgrade[player].power < pUpgrade->upgradePoints)
    asECMUpgrade[player].power = pUpgrade->upgradePoints;
}

//upgrade the repair stats 
void constructorUpgrade(FUNCTION* pFunction, UBYTE player)
{
  DROIDCONSTR_UPGRADE_FUNCTION* pUpgrade;

  pUpgrade = (DROIDCONSTR_UPGRADE_FUNCTION*)pFunction;

  //check upgrades increase all values!
  if (asConstUpgrade[player].constructPoints < pUpgrade->upgradePoints)
    asConstUpgrade[player].constructPoints = pUpgrade->upgradePoints;
}

//upgrade the body stats 
void bodyUpgrade(FUNCTION* pFunction, UBYTE player)
{
  DROIDBODY_UPGRADE_FUNCTION* pUpgrade;
  UBYTE inc;

  pUpgrade = (DROIDBODY_UPGRADE_FUNCTION*)pFunction;

  //check upgrades increase all values!
  if (pUpgrade->droid)
  {
    if (asBodyUpgrade[player][DROID_BODY_UPGRADE].powerOutput < pUpgrade->upgradePoints)
    {
      asBodyUpgrade[player][DROID_BODY_UPGRADE].powerOutput = pUpgrade->upgradePoints;
    }
    if (asBodyUpgrade[player][DROID_BODY_UPGRADE].body < pUpgrade->body)
    {
      asBodyUpgrade[player][DROID_BODY_UPGRADE].body = pUpgrade->body;
    }
    for (inc = 0; inc < NUM_WEAPON_CLASS; inc++)
    {
      if (asBodyUpgrade[player][DROID_BODY_UPGRADE].armourValue[inc] < pUpgrade->armourValue[inc])
      {
        asBodyUpgrade[player][DROID_BODY_UPGRADE].armourValue[inc] = pUpgrade->armourValue[inc];
      }
    }
  }
  if (pUpgrade->cyborg)
  {
    if (asBodyUpgrade[player][CYBORG_BODY_UPGRADE].powerOutput < pUpgrade->upgradePoints)
    {
      asBodyUpgrade[player][CYBORG_BODY_UPGRADE].powerOutput = pUpgrade->upgradePoints;
    }
    if (asBodyUpgrade[player][CYBORG_BODY_UPGRADE].body < pUpgrade->body)
    {
      asBodyUpgrade[player][CYBORG_BODY_UPGRADE].body = pUpgrade->body;
    }
    for (inc = 0; inc < NUM_WEAPON_CLASS; inc++)
    {
      if (asBodyUpgrade[player][CYBORG_BODY_UPGRADE].armourValue[inc] < pUpgrade->armourValue[inc])
      {
        asBodyUpgrade[player][CYBORG_BODY_UPGRADE].armourValue[inc] = pUpgrade->armourValue[inc];
      }
    }
  }
}

//upgrade the structure stats for the correct player
void structureUpgrade(FUNCTION* pFunction, UBYTE player)
{
  STRUCTURE_UPGRADE_FUNCTION* pUpgrade;

  pUpgrade = (STRUCTURE_UPGRADE_FUNCTION*)pFunction;

  //check upgrades increase all values!
  if (asStructureUpgrade[player].armour < pUpgrade->armour)
    asStructureUpgrade[player].armour = pUpgrade->armour;
  if (asStructureUpgrade[player].body < pUpgrade->body)
    asStructureUpgrade[player].body = pUpgrade->body;
  if (asStructureUpgrade[player].resistance < pUpgrade->resistance)
    asStructureUpgrade[player].resistance = pUpgrade->resistance;
}

//upgrade the wall/Defence structure stats for the correct player
void wallDefenceUpgrade(FUNCTION* pFunction, UBYTE player)
{
  WALLDEFENCE_UPGRADE_FUNCTION* pUpgrade;

  pUpgrade = (WALLDEFENCE_UPGRADE_FUNCTION*)pFunction;

  //check upgrades increase all values!
  if (asWallDefenceUpgrade[player].armour < pUpgrade->armour)
    asWallDefenceUpgrade[player].armour = pUpgrade->armour;
  if (asWallDefenceUpgrade[player].body < pUpgrade->body)
    asWallDefenceUpgrade[player].body = pUpgrade->body;
}

/*upgrades the droids inside a Transporter uwith the appropriate upgrade function*/
void upgradeTransporterDroids(DROID* psTransporter, void (*pUpgradeFunction)(DROID* psDroid))
{
  DROID* psCurr;

  ASSERT((psTransporter->droidType == DROID_TRANSPORTER, "upgradeTransporterUnits: invalid unit type"));

  //loop thru' each unit in the Transporter
  for (psCurr = psTransporter->psGroup->psList; psCurr != nullptr; psCurr = psCurr->psGrpNext)
  {
    if (psCurr != psTransporter)
    {
      //apply upgrade if not the transporter itself
      pUpgradeFunction(psCurr);
    }
  }
}

BOOL FunctionShutDown()
{
  UDWORD inc; //, player;
  FUNCTION *pFunction, **pStartList = asFunctions;

  for (inc = 0; inc < numFunctions; inc++)
  {
    pFunction = *asFunctions;
#ifndef HASH_NAMES
    delete[] pFunction->pName;
    pFunction->pName = nullptr;
#endif

#if !defined (RESOURCE_NAMES) && !defined(STORE_RESOURCE_ID)
    if (pFunction->type == WALL_TYPE) { delete[] ((WALL_FUNCTION *)pFunction)->pStructName; }
#endif
    delete[] pFunction;
    pFunction = nullptr;
    asFunctions++;
  }
  delete[] pStartList;
  pStartList = nullptr;

  //free the Upgrade lists
  return TRUE;
}

/*Returns the Function type based on the string - used for reading in data */
UDWORD functionType(char* pType)
{
  if (!strcmp(pType, "Production"))
    return PRODUCTION_TYPE;
  if (!strcmp(pType, "Production Upgrade"))
    return PRODUCTION_UPGRADE_TYPE;
  if (!strcmp(pType, "Research"))
    return RESEARCH_TYPE;
  if (!strcmp(pType, "Research Upgrade"))
    return RESEARCH_UPGRADE_TYPE;
  if (!strcmp(pType, "Power Generator"))
    return POWER_GEN_TYPE;
  if (!strcmp(pType, "Resource"))
    return RESOURCE_TYPE;
  if (!strcmp(pType, "Repair Droid"))
    return REPAIR_DROID_TYPE;
  if (!strcmp(pType, "Weapon Upgrade"))
    return WEAPON_UPGRADE_TYPE;
  if (!strcmp(pType, "Wall Function"))
    return WALL_TYPE;
  if (!strcmp(pType, "Structure Upgrade"))
    return STRUCTURE_UPGRADE_TYPE;
  if (!strcmp(pType, "WallDefence Upgrade"))
    return WALLDEFENCE_UPGRADE_TYPE;
  if (!strcmp(pType, "Power Upgrade"))
    return POWER_UPGRADE_TYPE;
  if (!strcmp(pType, "Repair Upgrade"))
    return REPAIR_UPGRADE_TYPE;
  if (!strcmp(pType, "VehicleRepair Upgrade"))
    return DROIDREPAIR_UPGRADE_TYPE;
  if (!strcmp(pType, "VehicleECM Upgrade"))
    return DROIDECM_UPGRADE_TYPE;
  if (!strcmp(pType, "VehicleConst Upgrade"))
    return DROIDCONST_UPGRADE_TYPE;
  if (!strcmp(pType, "VehicleBody Upgrade"))
    return DROIDBODY_UPGRADE_TYPE;
  if (!strcmp(pType, "VehicleSensor Upgrade"))
    return DROIDSENSOR_UPGRADE_TYPE;
  if (!strcmp(pType, "ReArm"))
    return REARM_TYPE;
  if (!strcmp(pType, "ReArm Upgrade"))
    return REARM_UPGRADE_TYPE;

  /*if (!strcmp(pType,"Radar Map"))
	{
		return RADAR_MAP_TYPE;
	}*/
  /*if (!strcmp(pType,"Power Regulator"))
  {
    return POWER_REG_TYPE;
  }*/
  /*if (!strcmp(pType,"Power Relay"))
  {
    return POWER_RELAY_TYPE;
  }*/
  /*if (!strcmp(pType,"Defensive Structure"))
  {
    return DEFENSIVE_STRUCTURE_TYPE;
  }*/
  /*if (!strcmp(pType,"Armour Upgrade"))
  {
    return ARMOUR_UPGRADE_TYPE;
  }*/
  /*if (!strcmp(pType,"Repair Upgrade"))
  {
    return REPAIR_UPGRADE_TYPE;
  }*/
  /*if (!strcmp(pType,"Resistance Upgrade"))
  {
    return RESISTANCE_UPGRADE_TYPE;
  }*/
  /*	if (!strcmp(pType,"Droid Design"))
    {
      return DROID_DESIGN_TYPE;
    }
    if (!strcmp(pType,"Map Marker"))
    {
      return MAP_MARKER_TYPE;
    }
    if (!strcmp(pType,"Sky Dome Map"))
    {
      return SKY_DOME_MAP_TYPE;
    }*/
  /*if (!strcmp(pType,"Body Upgrade"))
  {
    return BODY_UPGRADE_TYPE;
  }*/
  /*if (!strcmp(pType,"HQ"))
  {
    return HQ_TYPE;
  }*/

  ASSERT((FALSE, "Unknown Function Type"));
}
