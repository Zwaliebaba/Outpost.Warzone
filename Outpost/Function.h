/*
 * Function.h
 *
 * Definitions for the Structure Functions.
 *
 */
#ifndef _function_h
#define _function_h

#include "ObjectDef.h"

class StatsTable;

//holder for all functions
extern FUNCTION** asFunctions;
extern UDWORD numFunctions;

//lists the current Upgrade level that can be applied to a structure through research

extern BOOL loadFunctionStats(SBYTE* pFunctionData, UDWORD bufferSize);

//load the specific stats for each function
extern BOOL loadRepairDroidFunction(const StatsTable& _table, UDWORD _row); //, UDWORD functionType);
extern BOOL loadPowerGenFunction(const StatsTable& _table, UDWORD _row); //, UDWORD functionType);
extern BOOL loadResourceFunction(const StatsTable& _table, UDWORD _row);
extern BOOL loadProduction(const StatsTable& _table, UDWORD _row); //, UDWORD functionType);
extern BOOL loadProductionUpgradeFunction(const StatsTable& _table, UDWORD _row); //, UDWORD functionType);
extern BOOL loadResearchFunction(const StatsTable& _table, UDWORD _row);
extern BOOL loadResearchUpgradeFunction(const StatsTable& _table, UDWORD _row); //, UDWORD functionType);
extern BOOL loadHQFunction(SBYTE* pData);
extern BOOL loadWeaponUpgradeFunction(const StatsTable& _table, UDWORD _row);
extern BOOL loadWallFunction(const StatsTable& _table, UDWORD _row);
extern BOOL loadStructureUpgradeFunction(const StatsTable& _table, UDWORD _row);
extern BOOL loadWallDefenceUpgradeFunction(const StatsTable& _table, UDWORD _row);
extern BOOL loadRepairUpgradeFunction(const StatsTable& _table, UDWORD _row);
extern BOOL loadPowerUpgradeFunction(const StatsTable& _table, UDWORD _row);
extern BOOL loadDroidRepairUpgradeFunction(const StatsTable& _table, UDWORD _row);
extern BOOL loadDroidECMUpgradeFunction(const StatsTable& _table, UDWORD _row);
extern BOOL loadDroidBodyUpgradeFunction(const StatsTable& _table, UDWORD _row);
extern BOOL loadDroidSensorUpgradeFunction(const StatsTable& _table, UDWORD _row);
extern BOOL loadDroidConstUpgradeFunction(const StatsTable& _table, UDWORD _row);
extern BOOL loadReArmFunction(const StatsTable& _table, UDWORD _row);
extern BOOL loadReArmUpgradeFunction(const StatsTable& _table, UDWORD _row);

extern void productionUpgrade(FUNCTION* pFunction, UBYTE player);
extern void researchUpgrade(FUNCTION* pFunction, UBYTE player);
extern void powerUpgrade(FUNCTION* pFunction, UBYTE player);
extern void reArmUpgrade(FUNCTION* pFunction, UBYTE player);
extern void repairFacUpgrade(FUNCTION* pFunction, UBYTE player);
extern void weaponUpgrade(FUNCTION* pFunction, UBYTE player);
extern void structureUpgrade(FUNCTION* pFunction, UBYTE player);
extern void wallDefenceUpgrade(FUNCTION* pFunction, UBYTE player);
extern void structureBodyUpgrade(FUNCTION* pFunction, STRUCTURE* psBuilding);
extern void structureArmourUpgrade(FUNCTION* pFunction, STRUCTURE* psBuilding);
extern void structureResistanceUpgrade(FUNCTION* pFunction, STRUCTURE* psBuilding);
extern void structureProductionUpgrade(STRUCTURE* psBuilding);
extern void structureResearchUpgrade(STRUCTURE* psBuilding);
extern void structurePowerUpgrade(STRUCTURE* psBuilding);
extern void structureRepairUpgrade(STRUCTURE* psBuilding);
extern void structureSensorUpgrade(STRUCTURE* psBuilding);
extern void structureReArmUpgrade(STRUCTURE* psBuilding);
extern void structureECMUpgrade(STRUCTURE* psBuilding);
extern void sensorUpgrade(FUNCTION* pFunction, UBYTE player);
extern void repairUpgrade(FUNCTION* pFunction, UBYTE player);
extern void ecmUpgrade(FUNCTION* pFunction, UBYTE player);
extern void constructorUpgrade(FUNCTION* pFunction, UBYTE player);
extern void bodyUpgrade(FUNCTION* pFunction, UBYTE player);
extern void droidSensorUpgrade(DROID* psDroid);
extern void droidECMUpgrade(DROID* psDroid);
extern void droidBodyUpgrade(FUNCTION* pFunction, DROID* psDroid);
extern void upgradeTransporterDroids(DROID* psTransporter, void (*pUpgradeFunction)(DROID* psDroid));

extern BOOL FunctionShutDown();

#endif //_function_h
