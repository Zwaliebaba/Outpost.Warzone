#include "pch.h"
#include <directxmath.h>
/*	
	Component.c
	Draws component objects - oh yes indeed.
	Alex M, Pumpkin Studios, EIDOS Interactive, 1997. 
	
*/

#include "Frame.h"
#include "Base.h"
#include "Droid.h"
#include "Action.h"
#include "Order.h"
#include "Component.h"
#include "Model.h"
#include "RenderMatrix.h" //ivis matrix code
#include "RenderTypes.h"
#include "RenderModel.h"
#include "PieState.h" //ivis render code
#include "Lighting.h"

#define TESTLOWPOLY

#define GAME2PSX(x) (((x)*4096)/360)

#define GetRadius(x) ((x)->sradius)

#include "Stats.h"
#include "PieMode.h"
#include "Objects.h"
#include "Display.h"
#include "Geometry.h"
#include "Display3D.h"
#include "Map.h"
#include "GTime.h"
#include "IntDisplay.h"
#include "MiscIMD.h"
#include "Effects.h"
#include "Transporter.h"
#include "Projectile.h"
#include "Findpath.h"
#include "Loop.h"                          // gamePaused

void unsetMatrix(void);
void setMatrix(iVector* Position, iVector* Rotation, iVector* CameraPos, BOOL RotXYZ);

#define MAX_GROUP_SIZE	10

#define NUM_GIBLETS	5
#define BLIP_ANIM_DURATION			200

#define	DEFAULT_COMPONENT_TRANSLUCENCY	128

#define	DROID_EMP_SPREAD	(20 - rand()%40)

UDWORD droidScale = 100;
void displayComponentTemplate(DROID_TEMPLATE* psTemplate);
static void displayCompObj(BASE_OBJECT* psObj, DirectX::XMFLOAT3* mountRotation, BOOL bButton);
static iIMDShape* getLeftPropulsionIMD(DROID* psDroid);
static iIMDShape* getRightPropulsionIMD(DROID* psDroid);

static UDWORD getStructureHeight(STRUCTURE* psStructure);

BOOL leftFirst;

SDWORD droidLightLevel = 224;
UDWORD lightInterval = 15;
UDWORD lightLastChanged;
SDWORD lightSpeed = 2;
extern UDWORD selectedPlayer;

UBYTE PlayerColour[MAX_PLAYERS]; // = {0,1,2,3,4,5,6,7}

// Colour Lookups
// use col = MAX_PLAYERS for anycolour (see multiint.c)
BOOL setPlayerColour(UDWORD player, UDWORD col)
{
  if (player > MAX_PLAYERS || col > MAX_PLAYERS)
  {
    Neuron::Fatal("setplayercolour: wrong values");
    return FALSE;
  }
  PlayerColour[static_cast<UBYTE>(player)] = static_cast<UBYTE>(col);
  return TRUE;
}

UBYTE getPlayerColour(UDWORD pl) { return PlayerColour[pl]; }

void initPlayerColours(void)
{
  UBYTE i;
  for (i = 0; i < MAX_PLAYERS; i++)
    PlayerColour[i] = i;
}

void updateLightLevels(void)
{
  if (gameTime > (lightLastChanged + lightInterval))
  {
    droidLightLevel += lightSpeed;
    lightLastChanged = gameTime;
    if (droidLightLevel > 255 OR droidLightLevel < 128)
    {
      if (lightSpeed > 0)
        lightSpeed = -lightSpeed;
      else
        lightSpeed = -lightSpeed;
    }
  }
}

void setMatrix(iVector* Position, iVector* Rotation, iVector* CameraPos, BOOL RotXYZ)
{
  Neuron::MatrixPush();

  Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(static_cast<float>(Position->x), static_cast<float>(Position->y), static_cast<float>(Position->z)) * Neuron::WorldMatrix();

  if (RotXYZ)
  {
    Neuron::WorldMatrix() = DirectX::XMMatrixRotationX(DirectX::XMConvertToRadians(static_cast<float>(Rotation->x))) * Neuron::WorldMatrix();
    Neuron::WorldMatrix() = DirectX::XMMatrixRotationY(DirectX::XMConvertToRadians(static_cast<float>(Rotation->y))) * Neuron::WorldMatrix();
    Neuron::WorldMatrix() = DirectX::XMMatrixRotationZ(DirectX::XMConvertToRadians(static_cast<float>(Rotation->z))) * Neuron::WorldMatrix();
  }
  else
  {
    Neuron::WorldMatrix() = DirectX::XMMatrixRotationY(DirectX::XMConvertToRadians(static_cast<float>(Rotation->y))) * Neuron::WorldMatrix();
    Neuron::WorldMatrix() = DirectX::XMMatrixRotationX(DirectX::XMConvertToRadians(static_cast<float>(Rotation->x))) * Neuron::WorldMatrix();
    Neuron::WorldMatrix() = DirectX::XMMatrixRotationZ(DirectX::XMConvertToRadians(static_cast<float>(Rotation->z))) * Neuron::WorldMatrix();
  }
}

void unsetMatrix(void) { Neuron::MatrixPop(); }

UDWORD getComponentDroidRadius(DROID* psDroid)
{
  UNUSEDPARAMETER(psDroid);

  return 100;
}

UDWORD getComponentDroidTemplateRadius(DROID_TEMPLATE* psDroid)
{
  UNUSEDPARAMETER(psDroid);

  return 100;
}

UDWORD getComponentRadius(BASE_STATS* psComponent)
{
  iIMDShape* ComponentIMD = nullptr;
  iIMDShape* MountIMD = nullptr;
  SDWORD compID;

  compID = StatIsComponent(psComponent);
  if (compID > 0)
  {
    StatGetComponentIMD(psComponent, compID, &ComponentIMD, &MountIMD);
    if (ComponentIMD)
      return GetRadius(ComponentIMD); // NULL component,should this be an error?
  }

  /* VTOL bombs are only stats allowed to have NULL ComponentIMD */
  if ((StatIsComponent(psComponent) != COMP_WEAPON) || ((WEAPON_STATS*)psComponent)->weaponSubClass != WSC_BOMB)
    Neuron::DebugTrace("ComponentPIE == NULL : File : {} Line : {}\n",__FILE__,__LINE__);

  return COMPONENT_RADIUS;
}

UDWORD getResearchRadius(BASE_STATS* Stat)
{
  iIMDShape* ResearchIMD = ((RESEARCH*)Stat)->pIMD;

  if (ResearchIMD)
    return GetRadius(ResearchIMD);

  Neuron::DebugTrace("ResearchPIE == NULL : File : {} Line : {}\n",__FILE__,__LINE__);

  return 100;
}

UDWORD getStructureSize(STRUCTURE* psStructure)
{
  UDWORD size;
  //radius based on base plate size

  size = psStructure->pStructureType->baseWidth;
  if (psStructure->pStructureType->baseBreadth > size)
    size = psStructure->pStructureType->baseBreadth;
  return (size);
}

UDWORD getStructureRadius(STRUCTURE* psStructure)
{
  iIMDShape* baseImd;
  SDWORD Radius = GetRadius(psStructure->sDisplay.imd);

  baseImd = psStructure->pStructureType->pBaseIMD;
  if (baseImd != nullptr)
  {
    if (GetRadius(baseImd) > Radius)
      Radius = GetRadius(baseImd);
  }

  return Radius;
}

UDWORD getStructureStatSize(STRUCTURE_STATS* Stats)
{
  UDWORD size;
  //radius based on base plate size

  size = Stats->baseWidth;
  if (Stats->baseBreadth > size)
    size = Stats->baseBreadth;
  return (size);
}

UDWORD getStructureStatRadius(STRUCTURE_STATS* Stats, UDWORD Player)
{
  iIMDShape* baseImd;
  SDWORD Radius = GetRadius(Stats->pIMD);

  UNUSEDPARAMETER(Player);

  baseImd = Stats->pBaseIMD;
  if (baseImd != nullptr)
  {
    if (GetRadius(baseImd) > Radius)
      Radius = GetRadius(baseImd);
  }

  return Radius;
}

UDWORD getStructureHeight(STRUCTURE* psStructure) { return (getStructureStatHeight(psStructure->pStructureType)); }

UDWORD getStructureStatHeight(STRUCTURE_STATS* psStat)
{
  if (psStat->pIMD)
    return (psStat->pIMD->ymax - psStat->pIMD->ymin);
  return 0;
}

void displayIMDButton(iIMDShape* IMDShape, iVector* Rotation, iVector* Position, BOOL RotXYZ, SDWORD scale)
{
  iVector TmpCamPos = {0, 0, 0};

  setMatrix(Position, Rotation, &TmpCamPos, RotXYZ);
  scaleMatrix(scale);

  pie_SetFogStatus(FALSE);
  pie_Draw3DShape(IMDShape, 0, getPlayerColour(selectedPlayer), pie_MAX_BRIGHT_LEVEL, 0, pie_BUTTON, 0); // ajl changed 0 to selectedPlayer
  unsetMatrix();
}

void displayStructureButton(STRUCTURE* psStructure, iVector* Rotation, iVector* Position, BOOL RotXYZ, SDWORD scale)
{
  iIMDShape *baseImd, *strImd, *mountImd, *weaponImd;
  UDWORD nWeaponStat;
  iVector TmpCamPos = {0, 0, 0};

  /*HACK HACK HACK! 
  if its a 'tall thin (ie tower)' structure with something on the top - offset the 
  position to show the object on top*/
  if (psStructure->pStructureType->pIMD->nconnectors AND scale == SMALL_STRUCT_SCALE AND getStructureHeight(psStructure) > TOWER_HEIGHT)
    Position->y -= 20;

  setMatrix(Position, Rotation, &TmpCamPos, RotXYZ);
  scaleMatrix(scale);

  /* Draw the building's base first */
  baseImd = psStructure->pStructureType->pBaseIMD;
  if (baseImd != nullptr)
    pie_Draw3DShape(baseImd, 0, getPlayerColour(selectedPlayer), pie_MAX_BRIGHT_LEVEL, 0, pie_BUTTON, 0); // ajl changed 0 to selectedPlayer
  pie_Draw3DShape(psStructure->sDisplay.imd, 0, getPlayerColour(selectedPlayer), pie_MAX_BRIGHT_LEVEL, 0, pie_BUTTON, 0);
  // ajl changed 0 to selectedPlayer
  //and draw the turret
  if (psStructure->sDisplay.imd->nconnectors)
  {
    weaponImd = nullptr; //weapon is gun ecm or sensor
    mountImd = nullptr;
    strImd = psStructure->sDisplay.imd;
    //get an imd to draw on the connector priority is weapon, ECM, sensor
    //check for weapon
    if (psStructure->asWeaps[0].nStat > 0)
    {
      nWeaponStat = psStructure->asWeaps[0].nStat;
      weaponImd = asWeaponStats[nWeaponStat].pIMD;
      mountImd = asWeaponStats[nWeaponStat].pMountGraphic;
    }

    if (weaponImd == nullptr)
    {
      //check for ECM
      if (psStructure->pStructureType->pECM != nullptr)
      {
        weaponImd = psStructure->pStructureType->pECM->pIMD;
        mountImd = psStructure->pStructureType->pECM->pMountGraphic;
      }
    }

    if (weaponImd == nullptr)
    {
      //check for sensor
      if (psStructure->pStructureType->pSensor != nullptr)
      {
        weaponImd = psStructure->pStructureType->pSensor->pIMD;
        mountImd = psStructure->pStructureType->pSensor->pMountGraphic;
      }
    }

    //draw Weapon/ECM/Sensor for structure
    if (weaponImd != nullptr)
    {
      Neuron::MatrixPush();
      Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(static_cast<float>(strImd->connectors->x), static_cast<float>(strImd->connectors->z), static_cast<float>(strImd->connectors->y)) * Neuron::WorldMatrix();
      Neuron::WorldMatrix() = DirectX::XMMatrixRotationY(-psStructure->turretRotation) * Neuron::WorldMatrix();
      if (mountImd != nullptr)
      {
        pie_Draw3DShape(mountImd, 0, getPlayerColour(selectedPlayer), pie_MAX_BRIGHT_LEVEL, 0, pie_BUTTON, 0);
        if (mountImd->nconnectors) { Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(static_cast<float>(mountImd->connectors->x), static_cast<float>(mountImd->connectors->z), static_cast<float>(mountImd->connectors->y)) * Neuron::WorldMatrix(); }
      }
      Neuron::WorldMatrix() = DirectX::XMMatrixRotationX(psStructure->turretPitch) * Neuron::WorldMatrix();
      pie_Draw3DShape(weaponImd, 0, getPlayerColour(selectedPlayer), pie_MAX_BRIGHT_LEVEL, 0, pie_BUTTON, 0);
      //we have a droid weapon so do we draw a muzzle flash
      Neuron::MatrixPop();
    }
  }
  unsetMatrix();
}

void displayStructureStatButton(STRUCTURE_STATS* Stats, UDWORD Player, iVector* Rotation, iVector* Position, BOOL RotXYZ, SDWORD scale)
{
  iIMDShape *baseImd, *strImd, *mountImd, *weaponImd;
  iVector TmpCamPos = {0, 0, 0};

  UNUSEDPARAMETER(Player);

  /*HACK HACK HACK! 
  if its a 'tall thin (ie tower)' structure stat with something on the top - offset the 
  position to show the object on top*/
  if (Stats->pIMD->nconnectors AND scale == SMALL_STRUCT_SCALE AND getStructureStatHeight(Stats) > TOWER_HEIGHT)
    Position->y -= 20;

  setMatrix(Position, Rotation, &TmpCamPos, RotXYZ);
  scaleMatrix(scale);

  /* Draw the building's base first */
  baseImd = Stats->pBaseIMD;

  if (baseImd != nullptr)
    pie_Draw3DShape(baseImd, 0, getPlayerColour(selectedPlayer), pie_MAX_BRIGHT_LEVEL, 0, pie_BUTTON, 0); // ajl changed 0 to selectedPlayer
  pie_Draw3DShape(Stats->pIMD, 0, getPlayerColour(selectedPlayer), pie_MAX_BRIGHT_LEVEL, 0, pie_BUTTON, 0);
  // ajl changed 0 to selectedPlayer

  //and draw the turret
  if (Stats->pIMD->nconnectors)
  {
    weaponImd = nullptr; //weapon is gun ecm or sensor
    mountImd = nullptr;
    strImd = Stats->pIMD;
    //get an imd to draw on the connector priority is weapon, ECM, sensor
    //check for weapon
    //can only have the one
    if (Stats->psWeapStat != nullptr)
    {
      weaponImd = Stats->psWeapStat->pIMD;
      mountImd = Stats->psWeapStat->pMountGraphic;
    }

    if (weaponImd == nullptr)
    {
      //check for ECM
      if (Stats->pECM != nullptr)
      {
        weaponImd = Stats->pECM->pIMD;
        mountImd = Stats->pECM->pMountGraphic;
      }
    }

    if (weaponImd == nullptr)
    {
      //check for sensor
      if (Stats->pSensor != nullptr)
      {
        weaponImd = Stats->pSensor->pIMD;
        mountImd = Stats->pSensor->pMountGraphic;
      }
    }

    //draw Weapon/ECM/Sensor for structure
    if (weaponImd != nullptr)
    {
      Neuron::MatrixPush();
      Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(static_cast<float>(strImd->connectors->x), static_cast<float>(strImd->connectors->z), static_cast<float>(strImd->connectors->y)) * Neuron::WorldMatrix();
      if (mountImd != nullptr)
      {
        pie_Draw3DShape(mountImd, 0, getPlayerColour(selectedPlayer), pie_MAX_BRIGHT_LEVEL, 0, pie_BUTTON, 0);
        if (mountImd->nconnectors) { Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(static_cast<float>(mountImd->connectors->x), static_cast<float>(mountImd->connectors->z), static_cast<float>(mountImd->connectors->y)) * Neuron::WorldMatrix(); }
      }
      pie_Draw3DShape(weaponImd, 0, getPlayerColour(selectedPlayer), pie_MAX_BRIGHT_LEVEL, 0, pie_BUTTON, 0);
      //we have a droid weapon so do we draw a muzzle flash
      Neuron::MatrixPop();
    }
  }

  unsetMatrix();
}

// Render a component given a BASE_STATS structure.
//
void displayComponentButton(BASE_STATS* Stat, iVector* Rotation, iVector* Position, BOOL RotXYZ, SDWORD scale)
{
  iIMDShape* ComponentIMD = nullptr;
  iIMDShape* MountIMD = nullptr;
  SDWORD compID;
  iVector TmpCamPos = {0, 0, 0};

  setMatrix(Position, Rotation, &TmpCamPos, RotXYZ);
  scaleMatrix(scale);

  compID = StatIsComponent(Stat);
  if (compID > 0)
    StatGetComponentIMD(Stat, compID, &ComponentIMD, &MountIMD);
  else
  {
    unsetMatrix();
    return;
  }

  /* VTOL bombs are only stats allowed to have NULL ComponentIMD */
  if ((ComponentIMD == nullptr) && ((StatIsComponent(Stat) != COMP_WEAPON) || ((WEAPON_STATS*)Stat)->weaponSubClass != WSC_BOMB))
    Neuron::DebugTrace("ComponentPIE == NULL : File : {} Line : {}\n",__FILE__,__LINE__);

  if (MountIMD)
    pie_Draw3DShape(MountIMD, 0, getPlayerColour(selectedPlayer), pie_MAX_BRIGHT_LEVEL, 0, pie_BUTTON, 0);
  // ajl changed 0 to selectedPlayer
  if (ComponentIMD)
    pie_Draw3DShape(ComponentIMD, 0, getPlayerColour(selectedPlayer), pie_MAX_BRIGHT_LEVEL, 0, pie_BUTTON, 0);
  // ajl changed 0 to selectedPlayer

  unsetMatrix();
}

// Render a research item given a BASE_STATS structure.
//
void displayResearchButton(BASE_STATS* Stat, iVector* Rotation, iVector* Position, BOOL RotXYZ, SDWORD scale)
{
  iIMDShape* ResearchIMD = ((RESEARCH*)Stat)->pIMD;
  iIMDShape* MountIMD = ((RESEARCH*)Stat)->pIMD2;
  iVector TmpCamPos = {0, 0, 0};

  if (ResearchIMD)
  {
    setMatrix(Position, Rotation, &TmpCamPos, RotXYZ);
    scaleMatrix(scale);

    if (MountIMD)
      pie_Draw3DShape(MountIMD, 0, getPlayerColour(selectedPlayer), pie_MAX_BRIGHT_LEVEL, 0, pie_BUTTON, 0);
    // ajl, added colourthing using selectedPlayer
    pie_Draw3DShape(ResearchIMD, 0, getPlayerColour(selectedPlayer), pie_MAX_BRIGHT_LEVEL, 0, pie_BUTTON, 0);
    //ajl, added colourthing using selectedPlayer

    unsetMatrix();
  }
}

// Render a composite droid given a DROID_TEMPLATE structure.
//
void displayComponentButtonTemplate(DROID_TEMPLATE* psTemplate, iVector* Rotation, iVector* Position, BOOL RotXYZ, SDWORD scale)
{
  static DROID Droid; // Made static to reduce stack usage.
  SDWORD difference;
  DirectX::XMFLOAT3 mountRotation = {0.0f, 0.0f, 0.0f};
  iVector TmpCamPos = {0, 0, 0};

  /* init to NULL */
  memset(&Droid, 0, sizeof(DROID));

  setMatrix(Position, Rotation, &TmpCamPos, RotXYZ);
  scaleMatrix(scale);

  // Decide how to sort it.

  difference = Rotation->y % 360;

  if ((difference > 0 AND difference < 180) OR difference < -180)
    leftFirst = FALSE;
  else
    leftFirst = TRUE;

  droidSetBits(psTemplate, &Droid);
  Droid.player = static_cast<UBYTE>(selectedPlayer);

  Droid.x = Droid.y = Droid.z = 0; // important for psx stuff ...

  //draw multi component object as a button object
  displayCompObj((BASE_OBJECT*)&Droid, &mountRotation,TRUE);

  unsetMatrix();
}

// Render a composite droid given a DROID structure.
//
void displayComponentButtonObject(DROID* psDroid, iVector* Rotation, iVector* Position, BOOL RotXYZ, SDWORD scale)
{
  SDWORD difference;
  DirectX::XMFLOAT3 mountRotation = {0.0f, 0.0f, 0.0f};
  iVector TmpCamPos = {0, 0, 0};

  setMatrix(Position, Rotation, &TmpCamPos, RotXYZ);
  scaleMatrix(scale);

  // Decide how to sort it.
  difference = Rotation->y % 360;

  if ((difference > 0 AND difference < 180) OR difference < -180)
    leftFirst = FALSE;
  else
    leftFirst = TRUE;

  // And render the composite object.
  //draw multi component object as a button object
  displayCompObj((BASE_OBJECT*)psDroid, &mountRotation,TRUE);

  unsetMatrix();
}

/* Assumes matrix context is already set */
void displayComponentObject(BASE_OBJECT* psObj)
{
  DROID* psDroid;
  iVector position; //,null;
  DirectX::XMFLOAT3 rotation, mountRotation;
  int32 xShift, zShift;
  UDWORD worldAngle;
  SDWORD difference;
  SDWORD frame;
  PROPULSION_STATS* psPropStats;
  UDWORD tileX, tileY;
  MAPTILE* psTile;

  psDroid = (DROID*)psObj;
  psPropStats = asPropulsionStats + psDroid->asBits[COMP_PROPULSION].nStat;

  worldAngle = static_cast<UDWORD>(player.r.y) / DEG_1 % 360;
  difference = (worldAngle - psObj->direction);

  if ((difference > 0 AND difference < 180) OR difference < -180)
    leftFirst = FALSE;
  else
    leftFirst = TRUE;

  /* Push the matrix */
  Neuron::MatrixPush();

  /* Get internal tile units coordinates */
  xShift = player.p.x & (TILE_UNITS - 1);
  zShift = player.p.z & (TILE_UNITS - 1);

  /* Mask out to tile_units resolution */
  Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(static_cast<float>(xShift), 0.0f, static_cast<float>(-zShift)) * Neuron::WorldMatrix();

  /* Get the real position */
  position.x = (psDroid->x - player.p.x) - terrainMidX * TILE_UNITS;
  position.z = terrainMidY * TILE_UNITS - (psDroid->y - player.p.z);
  position.y = psDroid->z;

  if (psDroid->droidType == DROID_TRANSPORTER)
    position.y += bobTransporterHeight();

  /* Get all the pitch,roll,yaw info */
  rotation.y = -psDroid->direction;
  rotation.x = psDroid->pitch;
  rotation.z = psDroid->roll;

  /* Get rotation info for the mounting too (holds the gun */
  mountRotation.y = -psDroid->turretRotation;
  mountRotation.x = psDroid->turretPitch;
  mountRotation.z = 0.0f;

  /* Translate origin */
  Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(static_cast<float>(position.x), static_cast<float>(position.y), static_cast<float>(position.z)) * Neuron::WorldMatrix();

  /* Rotate for droid */
  Neuron::WorldMatrix() = DirectX::XMMatrixRotationY(rotation.y) * Neuron::WorldMatrix();
  Neuron::WorldMatrix() = DirectX::XMMatrixRotationX(rotation.x) * Neuron::WorldMatrix();
  Neuron::WorldMatrix() = DirectX::XMMatrixRotationZ(rotation.z) * Neuron::WorldMatrix();

  if ((gameTime - psDroid->timeLastHit < GAME_TICKS_PER_SEC) AND psDroid->lastHitWeapon == WSC_ELECTRONIC)
    objectShimmy((BASE_OBJECT*)psDroid);

  if (psDroid->lastHitWeapon == WSC_EMP AND (gameTime - psDroid->timeLastHit < EMP_DISABLE_TIME))
  {
    iVector position;

    //add an effect on the droid
    position.x = psDroid->x + DROID_EMP_SPREAD;
    position.y = psDroid->z + rand() % 8;
    position.z = psDroid->y + DROID_EMP_SPREAD;
    effectGiveAuxVar(90 + rand() % 20);
    addEffect(&position, EFFECT_EXPLOSION, EXPLOSION_TYPE_PLASMA,FALSE, nullptr, 0);
  }

  if (godMode || (psDroid->visible[selectedPlayer] == UBYTE_MAX))
  {
    //ingame not button object
    displayCompObj(psObj, &mountRotation,FALSE);
  }
  else
  {
    // make sure it's not over water.
    tileX = psDroid->x / TILE_UNITS;
    tileY = psDroid->y / TILE_UNITS;
    // double check it's on map
    if ((tileX >= 0) AND (tileY >= 0) AND (tileX < mapWidth) AND (tileY < mapHeight))
    {
      psTile = mapTile(tileX, tileY);
      if (TERRAIN_TYPE(psTile) != TER_WATER)
      {
        frame = gameTime / BLIP_ANIM_DURATION + psDroid->id; //visible[selectedPlayer];
        pie_Draw3DShape(getImdFromIndex(MI_BLIP), frame, 0, pie_MAX_BRIGHT_LEVEL, 0, pie_ADDITIVE, (psDroid->visible[selectedPlayer] / 2));
        /* set up all the screen coords stuff - need to REMOVE FROM THIS LOOP */
      }
    }
  }
  Neuron::MatrixPop();
}

/* Assumes matrix context is already set */
void displayCompObj(BASE_OBJECT* psObj, DirectX::XMFLOAT3* mountRotation, BOOL bButton)
{
  DROID* psDroid;
  iIMDShape *psShape, *psJet = nullptr;
  iVector null;
  iPoint screenCoords;
  SDWORD dummyZ, iConnector;
  PROPULSION_STATS* psPropStats;
  SDWORD frame;
  SDWORD pieFlag, iPieData;
  UDWORD brightness, specular;
  UDWORD colour;
  UDWORD bDarkSide = FALSE;

  /* Cast the droid pointer */
  psDroid = (DROID*)psObj;
  if ((gameTime - psDroid->timeLastHit < GAME_TICKS_PER_SEC / 4) AND psDroid->lastHitWeapon == WSC_ELECTRONIC AND !gamePaused())
  {
    colour = getPlayerColour(rand() % MAX_PLAYERS);
    bDarkSide = TRUE;
  }
  else
    colour = getPlayerColour(psDroid->player);

  //set pieflag for button object or ingame object
  if (bButton)
    pieFlag = pie_BUTTON;
  else
    pieFlag = 0;

  if (!pieFlag)
    brightness = lightDoFogAndIllumination(psDroid->illumination, getCentreX() - psDroid->x, getCentreZ() - psDroid->y, &specular);
  else
  {
    brightness = pie_MAX_BRIGHT_LEVEL;
    specular = 0;
  }

  //	/* No auxilliary rotation */
  null.x = null.y = null.z = 0;

  /* We've got a z value here _and_ screen coords of origin */
  dummyZ = Neuron::ProjectToScreen(null.x, null.y, null.z, &screenCoords.x, &screenCoords.y);

  /* Draw the propulsion and body imds here */
  /* Establish the propulsion - this is more complex if two parts */
  /*
    if(leftFirst)
    {
      psShape = getLeftPropulsionIMD(psDroid);
      if(psShape!=NULL)
      {
        Neuron::PIEDraw(psShape,psDroid->player);
      }
    }
    else
    {	
      psShape = getRightPropulsionIMD(psDroid);
      if(psShape!=NULL)
      {
        Neuron::PIEDraw(psShape,psDroid->player);
      }
    }
  */

  /* set default components transparent */
  if (psDroid->asBits[COMP_PROPULSION].nStat == 0)
  {
    pieFlag |= pie_TRANSLUCENT;
    iPieData = DEFAULT_COMPONENT_TRANSLUCENCY;
  }
  else
    iPieData = 0;

  psShape = (leftFirst ? getLeftPropulsionIMD(psDroid) : getRightPropulsionIMD(psDroid));
  if (psShape != nullptr)
    pie_Draw3DShape(psShape, 0, colour/*getPlayerColour(psDroid->player)*/, brightness, specular, pieFlag, iPieData);

  /* set default components transparent */
  if (psDroid->asBits[COMP_BODY].nStat == 0)
  {
    pieFlag |= pie_TRANSLUCENT;
    iPieData = DEFAULT_COMPONENT_TRANSLUCENCY;
  }
  else
  {
    pieFlag &= ~pie_TRANSLUCENT;
    iPieData = 0;
  }

  /* Get the body graphic now*/
  psShape = BODY_IMD(psDroid, psDroid->player);
  if (psShape != nullptr)
  {
    // FIXME
    if (psDroid->droidType == DROID_PERSON)
    {
      /* draw body if not animating */
      if (psDroid->psCurAnim == nullptr || psDroid->psCurAnim->bVisible == FALSE)
      {
        // FIXME - hideous....!!!!
        scaleMatrix(75);
        pie_Draw3DShape(psShape, 0, psDroid->player - 6, brightness, specular, pieFlag, iPieData);
      }
    }
    else if (cyborgDroid(psDroid))
    {
      /* draw body if cyborg not animating */
      if (psDroid->psCurAnim == nullptr || psDroid->psCurAnim->bVisible == FALSE)
        pie_Draw3DShape(psShape, 0, colour/*getPlayerColour(psDroid->player)*/, brightness, specular, pieFlag, iPieData);
    }
    else
      pie_Draw3DShape(psShape, 0, colour/*getPlayerColour(psDroid->player)*/, brightness, specular, pieFlag, iPieData);
  }

  /* get propulsion stats */
  psPropStats = asPropulsionStats + psDroid->asBits[COMP_PROPULSION].nStat;

  /* render vtol jet if flying - horrible hack - GJ */
  if (((psPropStats->propulsionType == LIFT) &&
    //(psDroid->droidType != DROID_CYBORG)) && (!bButton))
    (!cyborgDroid(psDroid))) && (!bButton))
  {
    /* show flame if above ground */
    if (psDroid->sMove.Status != MOVEINACTIVE)
    {
      /* draw flame if found  */

      /* GJ TODO: add flame-finding code here */
      psJet = asBodyStats[psDroid->asBits[COMP_BODY].nStat].pFlameIMD;

      if (psJet != nullptr)
        pie_Draw3DShape(psJet, getStaticTimeValueRange(100, psJet->numFrames), colour /*getPlayerColour(psDroid->player)*/, brightness,
                        specular, pie_ADDITIVE, 200);
    }
  }

  //don't change the screen coords of an object if drawing it in a button
  if (!bButton)
  {
    /* set up all the screen coords stuff - need to REMOVE FROM THIS LOOP */
    calcScreenCoords(psDroid);
  }

  /* set default components transparent */
  if (psDroid->asWeaps[0].nStat == 0 && psDroid->asBits[COMP_SENSOR].nStat == 0 && psDroid->asBits[COMP_ECM].nStat == 0 && psDroid->asBits[
    COMP_BRAIN].nStat == 0 && psDroid->asBits[COMP_REPAIRUNIT].nStat == 0 && psDroid->asBits[COMP_CONSTRUCT].nStat == 0)
  {
    pieFlag |= pie_TRANSLUCENT;
    iPieData = DEFAULT_COMPONENT_TRANSLUCENCY;
  }
  else
  {
    pieFlag &= ~pie_TRANSLUCENT;
    iPieData = 0;
  }

  /* Indenting here is only to show new matrix context */
  {
    psShape = BODY_IMD(psDroid, psDroid->player);
    Neuron::MatrixPush();
    if (psShape->nconnectors)
    {
      /* vtol weapons attach to connector 2 (underneath);
       * all others to connector 1 */
      if ((psPropStats->propulsionType == LIFT) && psDroid->droidType == DROID_WEAPON)
        iConnector = 1;
      else
        iConnector = 0;

      /* Now we need to move for the mount point */
      Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(static_cast<float>(psShape->connectors[iConnector].x), static_cast<float>(psShape->connectors[iConnector].z), static_cast<float>(psShape->connectors[iConnector].y)) * Neuron::WorldMatrix();

      /* Rotate the turret */
      Neuron::WorldMatrix() = DirectX::XMMatrixRotationY(mountRotation->y) * Neuron::WorldMatrix();
      //dont pitch the turret

      /* vtol weapons inverted */
      if (iConnector == 1)
        Neuron::WorldMatrix() = DirectX::XMMatrixRotationZ((DEG_360 / 2) * Neuron::RadiansPerWorldAngle) * Neuron::WorldMatrix(); //this might affect gun rotation

      //SEPERATE Mount IMDs now...
      /*	Get the mounting graphic - we've already moved to the right position 
        Allegedly - all droids will have a mount graphic so this shouldn't
        fall on it's arse......*/
      /* Draw it */
      switch (psDroid->droidType)
      {
      case DROID_DEFAULT:
      case DROID_TRANSPORTER:
      case DROID_CYBORG:
      case DROID_CYBORG_SUPER:
      case DROID_WEAPON:
      case DROID_COMMAND: // command droids have a weapon to store all the graphics
        /*	Get the mounting graphic - we've already moved to the right position 
        Allegedly - all droids will have a mount graphic so this shouldn't
        fall on it's arse......*/
        /* Double check that the weapon droid actually has any */
        if (psDroid->asWeaps[0].nStat > 0 OR psDroid->droidType == DROID_DEFAULT)
        {
          psShape = WEAPON_MOUNT_IMD(psDroid, psDroid->player);
          /* Draw it */
          //if(psDroid->numWeaps) already done this check above?!
          {
            Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(0.0f, 0.0f, static_cast<float>(psDroid->asWeaps[0].recoilValue / 3)) * Neuron::WorldMatrix();
          }
          if (psShape)
            pie_Draw3DShape(psShape, 0, colour/*getPlayerColour(psDroid->player)*/, brightness, specular, pieFlag, iPieData);

          //if(psDroid->numWeaps) already done this check above?!
          {
            Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(0.0f, 0.0f, static_cast<float>(psDroid->asWeaps[0].recoilValue)) * Neuron::WorldMatrix();
          }

          /* translate for weapon mount point if cyborg */
          //if( psDroid->droidType == DROID_CYBORG &&
          if (cyborgDroid(psDroid) && psShape && psShape->nconnectors)
          {
            Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(static_cast<float>(psShape->connectors[0].x), static_cast<float>(psShape->connectors[0].z), static_cast<float>(psShape->connectors[0].y)) * Neuron::WorldMatrix();
          }

          /* vtol weapons inverted */
          if (iConnector == 1)
          {
            //pitch the barrel down
            Neuron::WorldMatrix() = DirectX::XMMatrixRotationX(-mountRotation->x) * Neuron::WorldMatrix();
          }
          else
          {
            //pitch the barrel up
            Neuron::WorldMatrix() = DirectX::XMMatrixRotationX(mountRotation->x) * Neuron::WorldMatrix();
          }

          /* Get the weapon (gun?) graphic */
          psShape = WEAPON_IMD(psDroid, psDroid->player);
          /* Draw it */
          if (psShape)
            pie_Draw3DShape(psShape, 0, colour/*getPlayerColour(psDroid->player)*/, brightness, specular, pieFlag, iPieData);
          //we have a droid weapon so do we draw a muzzle flash
          if (psShape && psShape->nconnectors)
          {
            /* Now we need to move to the end fo the barrel */
            Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(static_cast<float>(psShape->connectors[0].x), static_cast<float>(psShape->connectors[0].z), static_cast<float>(psShape->connectors[0].y)) * Neuron::WorldMatrix();
            //and draw the muzzle flash
            //animate for the duration of the flash only
            psShape = MUZZLE_FLASH_PIE(psDroid, psDroid->player);
            if (psShape)
            {
              //assume no clan colours formuzzle effects
              if ((psShape->numFrames == 0) || (psShape->animInterval <= 0)) //no anim so display one frame for a fixed time
              {
                if (gameTime < (psDroid->asWeaps->lastFired + BASE_MUZZLE_FLASH_DURATION))
                {
                  pie_Draw3DShape(psShape, 0, 0, brightness, 0, pieFlag | pie_ADDITIVE, EFFECT_MUZZLE_ADDITIVE); //muzzle flash
                  //								pie_Draw3DShape(psShape, 0, 0, brightness, 0, pieFlag, 0);//muzzle flash
                }
              }
              else
              {
                frame = (gameTime - psDroid->asWeaps->lastFired) / psShape->animInterval;
                if (frame < psShape->numFrames)
                {
                  pie_Draw3DShape(psShape, frame, 0, brightness, 0, pieFlag | pie_ADDITIVE, EFFECT_MUZZLE_ADDITIVE); //muzzle flash
                  //								pie_Draw3DShape(psShape, frame, 0, brightness, 0, pieFlag, 0);//muzzle flash
                }
              }
            }
          }
        }
        break;

      case DROID_SENSOR:
        /*	Get the mounting graphic - we've already moved to the right position 
        Allegedly - all droids will have a mount graphic so this shouldn't
        fall on it's arse......*/
        psShape = SENSOR_MOUNT_IMD(psDroid, psDroid->player);
        /* Draw it */
        if (psShape)
          pie_Draw3DShape(psShape, 0, colour/*getPlayerColour( psDroid->player)*/, brightness, specular, pieFlag, iPieData);

        /* Get the sensor graphic, assuming it's there */
        psShape = SENSOR_IMD(psDroid, psDroid->player);
        /* Draw it */
        if (psShape)
          pie_Draw3DShape(psShape, 0, colour/*getPlayerColour( psDroid->player)*/, brightness, specular, pieFlag, iPieData);
        break;

      case DROID_CONSTRUCT:
      case DROID_CYBORG_CONSTRUCT:
        /*	Get the mounting graphic - we've already moved to the right position 
        Allegedly - all droids will have a mount graphic so this shouldn't
        fall on it's arse......*/
        psShape = CONSTRUCT_MOUNT_IMD(psDroid, psDroid->player);
        /* Draw it */
        if (psShape)
          pie_Draw3DShape(psShape, 0, colour/*getPlayerColour( psDroid->player)*/, brightness, specular, pieFlag, iPieData);

        /* translate for construct mount point if cyborg */
        if (cyborgDroid(psDroid) && psShape && psShape->nconnectors)
        {
          Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(static_cast<float>(psShape->connectors[0].x), static_cast<float>(psShape->connectors[0].z), static_cast<float>(psShape->connectors[0].y)) * Neuron::WorldMatrix();
        }

        /* Get the construct graphic assuming it's there */
        psShape = CONSTRUCT_IMD(psDroid, psDroid->player);

        /* Draw it */
        if (psShape)
          pie_Draw3DShape(psShape, 0, colour/*getPlayerColour( psDroid->player)*/, brightness, specular, pieFlag, iPieData);

        break;
      case DROID_ECM:
        /*	Get the mounting graphic - we've already moved to the right position 
        Allegedly - all droids will have a mount graphic so this shouldn't
        fall on it's arse......*/
        psShape = ECM_MOUNT_IMD(psDroid, psDroid->player);
        /* Draw it */
        if (psShape)
          pie_Draw3DShape(psShape, 0, colour/*getPlayerColour(psDroid->player)*/, brightness, specular, pieFlag, iPieData);

        /* Get the ECM graphic assuming it's there.... */
        psShape = ECM_IMD(psDroid, psDroid->player);
        /* Draw it */
        if (psShape)
          pie_Draw3DShape(psShape, 0, colour/*getPlayerColour(psDroid->player)*/, brightness, specular, pieFlag, iPieData);
        break;
      case DROID_REPAIR:
      case DROID_CYBORG_REPAIR:
        /*	Get the mounting graphic - we've already moved to the right position 
        Allegedly - all droids will have a mount graphic so this shouldn't
        fall on it's arse......*/
        psShape = REPAIR_MOUNT_IMD(psDroid, psDroid->player);
        /* Draw it */
        if (psShape)
          pie_Draw3DShape(psShape, 0, colour /*getPlayerColour( psDroid->player)*/, brightness, specular, pieFlag, iPieData);

        /* translate for construct mount point if cyborg */
        if (cyborgDroid(psDroid) && psShape && psShape->nconnectors)
        {
          Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(static_cast<float>(psShape->connectors[0].x), static_cast<float>(psShape->connectors[0].z), static_cast<float>(psShape->connectors[0].y)) * Neuron::WorldMatrix();
        }

        /* Get the Repair graphic assuming it's there.... */
        psShape = REPAIR_IMD(psDroid, psDroid->player);

        /* Draw it */
        if (psShape)
        {
          pie_Draw3DShape(psShape, 0, colour /*getPlayerColour( psDroid->player)*/, brightness, specular, pieFlag, iPieData);
          if (psShape->nconnectors AND psDroid->action == DACTION_DROIDREPAIR)
          {
            Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(static_cast<float>(psShape->connectors[0].x), static_cast<float>(psShape->connectors[0].z), static_cast<float>(psShape->connectors[0].y)) * Neuron::WorldMatrix();
            Neuron::WorldMatrix() = DirectX::XMMatrixTranslation(0.0f, -20.0f, 0.0f) * Neuron::WorldMatrix();

            psShape = getImdFromIndex(MI_FLAME);

            /* Rotate for droid */
            Neuron::WorldMatrix() = DirectX::XMMatrixRotationY(psDroid->direction) * Neuron::WorldMatrix();
            Neuron::WorldMatrix() = DirectX::XMMatrixRotationX(-psDroid->pitch) * Neuron::WorldMatrix();
            Neuron::WorldMatrix() = DirectX::XMMatrixRotationZ(-psDroid->roll) * Neuron::WorldMatrix();
            Neuron::WorldMatrix() = DirectX::XMMatrixRotationY(-mountRotation->y) * Neuron::WorldMatrix();

            Neuron::WorldMatrix() = DirectX::XMMatrixRotationY(-player.r.y * Neuron::RadiansPerWorldAngle) * Neuron::WorldMatrix();
            Neuron::WorldMatrix() = DirectX::XMMatrixRotationX(-player.r.x * Neuron::RadiansPerWorldAngle) * Neuron::WorldMatrix();
            pie_Draw3DShape(psShape, getStaticTimeValueRange(100, psShape->numFrames), 0, brightness, 0, pie_ADDITIVE, 140);

            Neuron::WorldMatrix() = DirectX::XMMatrixRotationX(player.r.x * Neuron::RadiansPerWorldAngle) * Neuron::WorldMatrix();
            Neuron::WorldMatrix() = DirectX::XMMatrixRotationY(player.r.y * Neuron::RadiansPerWorldAngle) * Neuron::WorldMatrix();
          }
        }
        break;
      case DROID_PERSON:
        // no extra mounts for people
        break;
      default: DEBUG_ASSERT_TEXT(FALSE, "Whoa! Weirdy type of droid found in drawComponentObject!!!");
        break;
      }
    }
    /*	We've also got a handle on the psShape here for the weapon which has a connector to point to
      muzzle flash attachment points - just grab it from psShape->connectors->[x|y|z] */

    /* Pop Matrix */
    Neuron::MatrixPop();
  } // end of illustrative indentation - see above

  /* set default components transparent */
  if (psDroid->asBits[COMP_PROPULSION].nStat == 0)
  {
    pieFlag |= pie_TRANSLUCENT;
    iPieData = DEFAULT_COMPONENT_TRANSLUCENCY;
  }
  else
  {
    pieFlag &= ~pie_TRANSLUCENT;
    iPieData = 0;
  }

  psShape = (leftFirst ? getRightPropulsionIMD(psDroid) : getLeftPropulsionIMD(psDroid));
  if (psShape != nullptr)
    pie_Draw3DShape(psShape, 0, colour /*getPlayerColour( psDroid->player)*/, brightness, specular, pieFlag, iPieData);

  /*
	if(leftFirst)
	{
		psShape = getRightPropulsionIMD(psDroid);
		if(psShape!=NULL)
		{
			Neuron::PIEDraw(psShape,psDroid->player);
		}
	}
	else
	{	
		psShape = getLeftPropulsionIMD(psDroid);
		if(psShape!=NULL)
		{
			Neuron::PIEDraw(psShape,psDroid->player);
		}
	}
	*/
}

void destroyFXDroid(DROID* psDroid)
{
  UDWORD i;
  iIMDShape* psImd;
  SDWORD widthScatter, breadthScatter, heightScatter;
  iVector pos;

  widthScatter = TILE_UNITS / 4;
  breadthScatter = TILE_UNITS / 4;
  heightScatter = TILE_UNITS / 5;
  for (i = 0; i < 5; i++)
  {
    pos.x = psDroid->x + widthScatter - rand() % (2 * widthScatter);
    pos.z = psDroid->y + breadthScatter - rand() % (2 * breadthScatter);
    pos.y = psDroid->z + 16 + heightScatter;
    switch (i)
    {
    case 0:
      switch (psDroid->droidType)
      {
      case DROID_DEFAULT:
      case DROID_CYBORG:
      case DROID_CYBORG_SUPER:
      case DROID_CYBORG_CONSTRUCT:
      case DROID_CYBORG_REPAIR:
      case DROID_WEAPON:
      case DROID_COMMAND:
        if (psDroid->asWeaps[0].nStat > 0)
          psImd = WEAPON_MOUNT_IMD(psDroid, psDroid->player);
        else
          psImd = getRandomDebrisImd();
        break;
      default:
        psImd = getRandomDebrisImd();
        break;
      }
      break;
    case 1:
      switch (psDroid->droidType)
      {
      case DROID_DEFAULT:
      case DROID_CYBORG:
      case DROID_CYBORG_SUPER:
      case DROID_CYBORG_CONSTRUCT:
      case DROID_CYBORG_REPAIR:
      case DROID_WEAPON:
      case DROID_COMMAND:
        if (psDroid->asWeaps[0].nStat > 0)
          psImd = WEAPON_IMD(psDroid, psDroid->player);
        else
          psImd = getRandomDebrisImd();
        break;
      default:
        psImd = getRandomDebrisImd();
        break;
      }
      break;
    case 2:
    case 3:
    case 4:
      psImd = getRandomDebrisImd();
      break;
    }
    if (psImd)
      addEffect(&pos, EFFECT_GRAVITON, GRAVITON_TYPE_EMITTING_DR,TRUE, psImd, getPlayerColour(psDroid->player));
    else
      addEffect(&pos, EFFECT_GRAVITON, GRAVITON_TYPE_EMITTING_DR,TRUE, getRandomDebrisImd(), 0);
  }
}

void compPersonToBits(DROID* psDroid)
{
  iVector position; //,rotation,velocity;
  iIMDShape *headImd, *legsImd, *armImd, *bodyImd;
  UDWORD groundHeight;
  UDWORD col;

  if (!psDroid->visible[selectedPlayer])
  {
    /* We can't see the person or cyborg - so get out */
    return;
  }
  /* get bits pointers according to whether baba or cyborg*/
  if (cyborgDroid(psDroid))
  {
    headImd = getImdFromIndex(MI_CYBORG_HEAD);
    legsImd = getImdFromIndex(MI_CYBORG_LEGS);
    armImd = getImdFromIndex(MI_CYBORG_ARM);
    bodyImd = getImdFromIndex(MI_CYBORG_BODY);
  }
  else
  {
    headImd = getImdFromIndex(MI_BABA_HEAD);
    legsImd = getImdFromIndex(MI_BABA_LEGS);
    armImd = getImdFromIndex(MI_BABA_ARM);
    bodyImd = getImdFromIndex(MI_BABA_BODY);
  }

  /* Get where he's at */
  position.x = psDroid->x;
  position.y = psDroid->z + 1;
  groundHeight = psDroid->z;
  position.z = psDroid->y;

  /* Tell about player colour */
  col = getPlayerColour(psDroid->player);

  addEffect(&position, EFFECT_GRAVITON, GRAVITON_TYPE_GIBLET,TRUE, headImd, col);
  addEffect(&position, EFFECT_GRAVITON, GRAVITON_TYPE_GIBLET,TRUE, legsImd, col);
  addEffect(&position, EFFECT_GRAVITON, GRAVITON_TYPE_GIBLET,TRUE, armImd, col);
  addEffect(&position, EFFECT_GRAVITON, GRAVITON_TYPE_GIBLET,TRUE, bodyImd, col);
}

iIMDShape* getLeftPropulsionIMD(DROID* psDroid)
{
  UDWORD bodyStat, propStat;
  iIMDShape** imd;

  bodyStat = psDroid->asBits[COMP_BODY].nStat;
  propStat = psDroid->asBits[COMP_PROPULSION].nStat;

  imd = asBodyStats[bodyStat].ppIMDList;
  imd += (propStat * NUM_PROP_SIDES + LEFT_PROP);

  return *imd;
}

iIMDShape* getRightPropulsionIMD(DROID* psDroid)
{
  UDWORD bodyStat, propStat;
  iIMDShape** imd;

  bodyStat = psDroid->asBits[COMP_BODY].nStat;
  propStat = psDroid->asBits[COMP_PROPULSION].nStat;

  imd = asBodyStats[bodyStat].ppIMDList;
  imd += (propStat * NUM_PROP_SIDES + RIGHT_PROP);

  return *imd;
}

SDWORD rescaleButtonObject(SDWORD radius, SDWORD baseScale, SDWORD baseRadius)
{
  SDWORD newScale;
  newScale = 100 * baseRadius;
  newScale /= radius;
  if (baseScale > 0)
  {
    newScale += baseScale;
    newScale /= 2;
  }
  return newScale;
}
