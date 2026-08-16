#include "pch.h"
#include <directxmath.h>
//
// Drive.c
//
// Routines for player driving units about the map.
//							   

#define DEFINE_DRIVE_INLINE

#include <stdio.h>
#include <Math.h>
#include "IMD.h"
#include "RendMode.h"
#include "Frame.h"
#include "Input.h"
#include "Objects.h"
#include "Move.h"
#include "Findpath.h"
#include "Visibility.h"
#include "Map.h"
#include "FPath.h"
#include "Loop.h"
#include "GTime.h"
#include "AudioSystem.h"
#include "AudioID.h"
#include "Geometry.h"
#include "AnimObj.h"
#include "AnimID.h"
#include "FormationDef.h"
#include "Formation.h"
#include "Action.h"
#include "Order.h"
#include "AStar.h"
#include "Combat.h"
#include "MapGrid.h"
#include "Display.h"	// needed for widgetsOn flag.
#include "Effects.h"
#include "HCI.h"
#include "WarCAM.h"
#include "Radar.h"
#include "Display3Ddef.h"
#include "Display3D.h"
#include "Console.h"
#include "Text.h"

// all the bollox needed for script callbacks
#include "Interp.h"				// needed to define types in scripttabs.h
#include "Parse.h"				// needed to define types in scripttabs.h (Arse!)
#include "ScriptTabs.h"			// needed to define the callback
#include "ScriptExtern.h"		// needed to include the GLOBAL for checking bInTutorial
#include "Group.h"

#define DEBUG_DRIVE_SPEED

#define DRIVEFIXED
#define DRIVENOISE		// Enable driving noises.
#define ENGINEVOL (0xfff)
#define MINPITCH (768)
#define PITCHCHANGE (512)

#include "MultiPlay.h"

#include "Target.h"
#include "IntDisplay.h"
#include "Drive.h"

static SWORD DrivingAudioTrack = -1; // Which hardware channel are we using for the car driving noise

extern UDWORD selectedPlayer;
extern BOOL DirectControl;

// Driving characteristics.
#define DRIVE_TURNSPEED	(4)
#define DRIVE_ACCELERATE (16)
#define DRIVE_BRAKE (32)
#define DRIVE_DECELERATE (16)

#define	FOLLOW_STOP_RANGE (256)		// How close followers need to get to the driver before they can stop.

#define DRIVE_TURNDAMP	(32)	// Damping value for analogue turning.

#define MAX_IDLE	(GAME_TICKS_PER_SEC*60)	// Start to orbit if idle for 60 seconds.

DROID* psDrivenDroid = nullptr; // The droid that's being driven.
static BOOL bDriveMode = FALSE;
static float driveDir; // Driven droid's direction, radians in (-pi, pi].
static SDWORD driveSpeed; // Driven droid's speed.
static int driveBumpTime; // Time that followers get a kick up the ass.
static BOOL DoFollowRangeCheck = TRUE;
static BOOL AllInRange = TRUE;
static BOOL ClearFollowRangeCheck = FALSE;
static BOOL DriveControlEnabled = FALSE;
static BOOL DriveInterfaceEnabled = FALSE;
static UDWORD IdleTime;
static BOOL TacticalActive = FALSE;
static BOOL WasDriving = FALSE;

enum
{
  CONTROLMODE_POINTNCLICK,
  CONTROLMODE_DRIVE,
};

UWORD ControlMode = CONTROLMODE_DRIVE;
BOOL TargetFeatures = FALSE;

// Intialise drive statics, call with TRUE if coming from frontend, FALSE if
// coming from a mission.
//
void driveInitVars(BOOL Restart)
{
  if (WasDriving && !Restart)
  {
    Neuron::DebugTrace("driveInitVars: WasDriving\n");
    DrivingAudioTrack = -1;
    psDrivenDroid = nullptr;
    DoFollowRangeCheck = TRUE;
    ClearFollowRangeCheck = FALSE;
    bDriveMode = FALSE;
    DriveControlEnabled = TRUE; //FALSE;
    DriveInterfaceEnabled = FALSE; //TRUE;
    TacticalActive = FALSE;
    ControlMode = CONTROLMODE_DRIVE;
    TargetFeatures = FALSE;
  }
  else
  {
    Neuron::DebugTrace("driveInitVars: Driving\n");
    DrivingAudioTrack = -1;
    psDrivenDroid = nullptr;
    DoFollowRangeCheck = TRUE;
    ClearFollowRangeCheck = FALSE;
    bDriveMode = FALSE;
    DriveControlEnabled = TRUE; //FALSE;
    DriveInterfaceEnabled = FALSE;
    TacticalActive = FALSE;
    ControlMode = CONTROLMODE_DRIVE;
    TargetFeatures = FALSE;
    WasDriving = FALSE;
  }
}

UWORD controlModeGet(void) { return ControlMode; }

void controlModeSet(UWORD Mode) { ControlMode = Mode; }

void setDrivingStatus(BOOL val) { bDriveMode = val; }

BOOL getDrivingStatus(void) { return (bDriveMode); }

// Start droid driving mode.
//
BOOL StartDriverMode(DROID* psOldDroid)
{
  DROID* psDroid;
  DROID* psLastDriven;

  IdleTime = gameTime;

  psLastDriven = psDrivenDroid;
  psDrivenDroid = nullptr;

  // Find a selected droid and make that the driven one.
  for (psDroid = apsDroidLists[selectedPlayer]; psDroid; psDroid = psDroid->psNext)
  {
    if (psDroid->selected)
    {
      if ((psDrivenDroid == nullptr) && (psDroid != psOldDroid))
      {
        // The first droid found becomes the driven droid.
        if (!(DroidIsBuilding(psDroid) || DroidGoingToBuild(psDroid))) {}
        psDrivenDroid = psDroid;
        Neuron::DebugTrace("New driven droid\n");
      }
      else if (psDroid != psDrivenDroid)
      {
        // All the others become followers of the driven droid.
      }
    }
  }

  // If that failed then find any droid and make it the driven one.
  if (psDrivenDroid == nullptr)
  {
    psLastDriven = nullptr;
    psDrivenDroid = intGotoNextDroidType(nullptr, DROID_ANY,TRUE);

    // If it's the same droid then try again
    if (psDrivenDroid == psOldDroid)
      psDrivenDroid = intGotoNextDroidType(nullptr, DROID_ANY,TRUE);

    if (psDrivenDroid == psOldDroid)
      psDrivenDroid = nullptr;

    // If it failed then try for a transporter.
    if (psDrivenDroid == nullptr)
      psDrivenDroid = intGotoNextDroidType(nullptr, DROID_TRANSPORTER,TRUE);
  }

  if (psDrivenDroid)
  {
    driveDir = psDrivenDroid->direction;
    driveSpeed = 0;
    driveBumpTime = gameTime;
    setDrivingStatus(TRUE);

    if (DriveInterfaceEnabled)
    {
      Neuron::DebugTrace("Interface enabled1 ! Disabling drive control\n");
      DriveControlEnabled = FALSE;
    }
    else
      DriveControlEnabled = TRUE;

    if (psLastDriven != psDrivenDroid)
    {
      Neuron::DebugTrace("camAllignWithTarget\n");
      camAllignWithTarget((BASE_OBJECT*)psDrivenDroid);
    }

    return TRUE;
  }

  return FALSE;
}

void StopEngineNoise(void) { DrivingAudioTrack = -1; }

void ChangeDriver(void)
{
  DROID* psDroid;

  if (psDrivenDroid != nullptr)
  {
    Neuron::DebugTrace("Driver Changed\n");

    for (psDroid = apsDroidLists[selectedPlayer]; psDroid; psDroid = psDroid->psNext)
    {
      if ((psDroid->sMove.Status == MOVEDRIVE))
      {
        DEBUG_ASSERT_TEXT((psDroid->droidType != DROID_TRANSPORTER), "Tried to control a transporter");
        secondarySetState(psDroid, DSO_HALTTYPE, DSS_HALT_GUARD);
        psDroid->sMove.Status = MOVEINACTIVE;
      }
    }
  }
}

// Stop droid driving mode.
//
void StopDriverMode(void)
{
  DROID* psDroid;

  if (psDrivenDroid != nullptr)
  {
    Neuron::DebugTrace("Drive mode canceled\n");

    psDrivenDroid = nullptr;

    for (psDroid = apsDroidLists[selectedPlayer]; psDroid; psDroid = psDroid->psNext)
    {
      if ((psDroid->sMove.Status == MOVEDRIVE))
      {
        DEBUG_ASSERT_TEXT((psDroid->droidType != DROID_TRANSPORTER), "Tried to control a transporter");
        secondarySetState(psDroid, DSO_HALTTYPE, DSS_HALT_GUARD);
        psDroid->sMove.Status = MOVEINACTIVE;
      }
    }
  }

  setDrivingStatus(FALSE);
  DriveControlEnabled = FALSE;
}

// Returns TRUE if we have a driven droid.
//

// Returns TRUE if drive mode is active.
//
//INLINED #ifdef DRIVEFIXED
//INLINED #else
//INLINED #endif

// Return TRUE if the specified droid is the driven droid.
//
//INLINED #ifdef DRIVEFIXED
//INLINED #else
//INLINED #endif

//INLINED #ifdef DRIVEFIXED
//INLINED #else
//INLINED #endif

// Call this whenever a droid gets killed or removed.
// returns TRUE if ok, returns FALSE if resulted in driving mode being stopped, ie could'nt find
// a selected droid to drive.
//
BOOL driveDroidKilled(DROID* psDroid)
{
  if (driveModeActive())
  {
    if (psDroid == psDrivenDroid)
    {
      ChangeDriver();

      psDrivenDroid = nullptr;

      DeSelectDroid(psDroid);

      if (!StartDriverMode(psDroid))
      {
        //
        //
        //
        //				// Failed to find a droid to track!
        //
        //		DBPRINTF(("no structures or droids, should be game over!\n");
        return FALSE;
      }
    }
  }

  return TRUE;
}

BOOL driveDroidIsBusy(DROID* psDroid)
{
  if (DroidIsBuilding(psDroid) || DroidGoingToBuild(psDroid) || DroidIsDemolishing(psDroid))
    return TRUE;

  return FALSE;
}

// Call whenever the selections change to re initialise drive mode.
//
void driveSelectionChanged(void)
{
  if (driveModeActive())
  {
    if (psDrivenDroid)
    {
      ChangeDriver();
      StartDriverMode(nullptr);
      driveTacticalSelectionChanged();
    }
  }
}

// Cycle to next droid in group and make it the driver.
//
void driveNextDriver(void)
{
  DROID* psDroid;

  // Start from the current driven droid.
  for (psDroid = psDrivenDroid; psDroid; psDroid = psDroid->psNext)
  {
    if ((psDroid->selected) && (psDroid != psDrivenDroid))
    {
      // Found one so make it the driven droid.
      psDrivenDroid = psDroid;
      camAllignWithTarget((BASE_OBJECT*)psDroid);
      driveTacticalSelectionChanged();
      return;
    }
  }

  // Not found so start at the begining.
  for (psDroid = apsDroidLists[selectedPlayer]; psDroid && (psDroid != psDrivenDroid); psDroid = psDroid->psNext)
  {
    if (psDroid->selected)
    {
      // Found one so make it the driven droid.
      psDrivenDroid = psDroid;
      camAllignWithTarget((BASE_OBJECT*)psDroid);
      driveTacticalSelectionChanged();
      return;
    }
  }
  // No other selected droids found. Oh well...
}

static BOOL driveControl(DROID* psDroid)
{
  BOOL Input = FALSE;
  SDWORD MaxSpeed = moveCalcDroidSpeed(psDroid);

  if (!DriveControlEnabled)
    return FALSE;

  if (keyPressed(KEY_N))
    driveNextDriver();

  if (keyDown(KEY_LEFTARROW))
  {
    driveDir += DirectX::XMConvertToRadians(static_cast<float>(DRIVE_TURNSPEED));
    Input = TRUE;
  }
  else if (keyDown(KEY_RIGHTARROW))
  {
    driveDir -= DirectX::XMConvertToRadians(static_cast<float>(DRIVE_TURNSPEED));
    Input = TRUE;
  }

  driveDir = DirectX::XMScalarModAngle(driveDir);

  if (keyDown(KEY_UPARROW))
  {
    if (driveSpeed >= 0)
    {
      driveSpeed += DRIVE_ACCELERATE;
      if (driveSpeed > MaxSpeed)
        driveSpeed = MaxSpeed;
    }
    else
    {
      driveSpeed += DRIVE_BRAKE;
      if (driveSpeed > 0)
        driveSpeed = 0;
    }
    Input = TRUE;
  }
  else if (keyDown(KEY_DOWNARROW))
  {
    if (driveSpeed <= 0)
    {
      driveSpeed -= DRIVE_ACCELERATE;
      if (driveSpeed < -MaxSpeed)
        driveSpeed = -MaxSpeed;
    }
    else
    {
      driveSpeed -= DRIVE_BRAKE;
      if (driveSpeed < 0)
        driveSpeed = 0;
    }
    Input = TRUE;
  }
  else
  {
    if (driveSpeed > 0)
    {
      driveSpeed -= DRIVE_DECELERATE;
      if (driveSpeed < 0)
        driveSpeed = 0;
    }
    else
    {
      driveSpeed += DRIVE_DECELERATE;
      if (driveSpeed > 0)
        driveSpeed = 0;
    }
  }

  return Input;
}

static BOOL driveInDriverRange(DROID* psDroid)
{
  if ((abs(psDroid->x - psDrivenDroid->x) < FOLLOW_STOP_RANGE) && (abs(psDroid->y - psDrivenDroid->y) < FOLLOW_STOP_RANGE))
    return TRUE;

  return FALSE;
}

static void driveMoveFollower(DROID* psDroid)
{
  if (driveBumpTime < gameTime)
  {
    // Update the driven droid's followers.
    if (!driveInDriverRange(psDroid))
    {
      //psDroid->secondaryOrder&=~DSS_MOVEHOLD_SET;		// Remove secondary order ... this stops the droid from jumping back to GUARD mode ... see order.c #111 - tjc
      secondarySetState(psDroid, DSO_HALTTYPE, DSS_HALT_GUARD);
      // if the droid is currently guarding we need to change the order to a move
      if (psDroid->order == DORDER_GUARD)
        orderDroidLoc(psDroid, DORDER_MOVE, psDrivenDroid->x, psDrivenDroid->y);
      else
      {
        // otherwise we just adjust its move-to location
        moveDroidTo(psDroid, psDrivenDroid->x, psDrivenDroid->y);
      }
    }
  }

  // Stop it when it gets within range of the driver.
  if (DoFollowRangeCheck)
  {
    if (driveInDriverRange(psDroid))
      psDroid->sMove.Status = MOVEINACTIVE;
    else
      AllInRange = FALSE;
  }
}

static void driveMoveCommandFollowers(DROID* psDroid)
{
  DROID* psCurr;
  DROID_GROUP* psGroup = psDroid->psGroup;

  for (psCurr = psGroup->psList; psCurr != nullptr; psCurr = psCurr->psGrpNext)
    driveMoveFollower(psCurr);
}

// Call once per frame.
//
void driveUpdate(void)
{
  DROID* psDroid;
  PROPULSION_STATS* psPropStats;

  AllInRange = TRUE;

  if (DirectControl)
  {
    if (psDrivenDroid != nullptr)
    {
      if (bMultiPlayer && (driveBumpTime < gameTime)) // send latest info about driven droid.
        SendDroidInfo(psDrivenDroid, DORDER_MOVE, psDrivenDroid->x, psDrivenDroid->y, nullptr);

      //TO BE DONE:
      //clear the order on taking over the droid, to stop attacks..
      //send some sort of message when droids stopo and get inrange.

      // Check the driven droid is still selected
      if (psDrivenDroid->selected == FALSE)
      {
        // if it's not then reset the driving system.
        driveSelectionChanged();
        return;
      }

      // Update the driven droid.
      if (driveControl(psDrivenDroid))
      {
        // If control did something then force the droid's move status.
        if (psDrivenDroid->sMove.Status != MOVEDRIVE)
        {
          psDrivenDroid->sMove.Status = MOVEDRIVE;
          DEBUG_ASSERT_TEXT((psDrivenDroid->droidType != DROID_TRANSPORTER), "Tried to control a transporter");
          driveDir = psDrivenDroid->direction;
        }

        DoFollowRangeCheck = TRUE;
      }

      // Is the driven droid under user control?
      if (psDrivenDroid->sMove.Status == MOVEDRIVE)
      {
        // Is it a command droid
        if ((psDrivenDroid->droidType == DROID_COMMAND) && (psDrivenDroid->psGroup != nullptr))
          driveMoveCommandFollowers(psDrivenDroid);

        for (psDroid = apsDroidLists[selectedPlayer]; psDroid; psDroid = psDroid->psNext)
        {
          psPropStats = asPropulsionStats + psDroid->asBits[COMP_PROPULSION].nStat;

          if ((psDroid->selected) && (psDroid != psDrivenDroid) && (psDroid->droidType != DROID_TRANSPORTER) && ((psPropStats->
            propulsionType != LIFT) || cyborgDroid(psDroid)))
          {
            // Send new orders to it's followers.
            driveMoveFollower(psDroid);
          }
        }
      }

      if (AllInRange)
        DoFollowRangeCheck = FALSE;

      if (driveBumpTime < gameTime)
      {
        // Send next order in 1 second.
        driveBumpTime = gameTime + GAME_TICKS_PER_SEC;
      }
    }
    else
    {
#ifdef DRIVEFIXED
      // Start driving
      if (StartDriverMode(nullptr) == FALSE)
      {
        //				// No droids to drive so start orbiting a structure.
      }
#endif
    }
  }
}

SDWORD driveGetMoveSpeed(void) { return driveSpeed; }

float driveGetMoveDir(void) { return driveDir; }

void driveSetDroidMove(DROID* psDroid) { psDroid->direction = driveDir; }

// Dissable user control of droid, ie when interface is up.

void driveDisableControl(void) { DriveControlEnabled = FALSE; }

// Enable user control of droid, ie when interface is down.
//
void driveEnableControl(void) { DriveControlEnabled = TRUE; }

// Return TRUE if drive control is enabled.
//
BOOL driveControlEnabled(void) { return DriveControlEnabled; }

// Bring up the reticule.
//
void driveEnableInterface(BOOL AddReticule)
{
  if (AddReticule)
    intAddReticule();
  DriveInterfaceEnabled = TRUE;
}

// Get rid of the reticule.
//
void driveDisableInterface(void)
{
  intResetScreen(FALSE);
  intRemoveReticule();
  DriveInterfaceEnabled = FALSE;
}

// Get rid of the reticule.
//
void driveDisableInterface2(void) { DriveInterfaceEnabled = FALSE; }

// Return TRUE if the reticule is up.
//
BOOL driveInterfaceEnabled(void) { return DriveInterfaceEnabled; }

// Check for and process a user request for a new target.
//
void driveProcessAquireButton(void)
{
  if (mouseReleased(MOUSE_RMB) || keyPressed(KEY_S))
  {
    BASE_OBJECT* psObj;
    psObj = targetAquireNearestObjView((BASE_OBJECT*)psDrivenDroid,TARGET_TYPE_ANY);
  }
}

// Start structure placement for drive mode.
//
void driveStartBuild(void)
{
  intRemoveReticule();
  DriveInterfaceEnabled = FALSE;
  driveEnableControl();
}

void driveStartDemolish(void)
{
  intRemoveReticule();
  DriveInterfaceEnabled = FALSE;
  driveEnableControl();
}

// Stop structure placement for drive mode.
//
void driveStopBuild(void) {}

// Return TRUE if all the conditions for allowing user control of the droid are met.
//
BOOL driveAllowControl(void)
{
  if (TacticalActive || DriveInterfaceEnabled || (!DriveControlEnabled))
    return FALSE;

  return TRUE;
}

// Enable Tactical order mode.
//
void driveEnableTactical(void)
{
  StartTacticalScroll(TRUE);
  TacticalActive = TRUE;
  Neuron::DebugTrace("Tactical Mode Activated\n");
}

// Disable Tactical order mode.
//
void driveDisableTactical(void)
{
  if (driveModeActive() && TacticalActive)
  {
    CancelTacticalScroll();
    TacticalActive = FALSE;
    Neuron::DebugTrace("Tactical Mode Canceled\n");
  }
}

// Return TRUE if Tactical order mode is active.
//
BOOL driveTacticalActive(void) { return TacticalActive; }

void driveTacticalSelectionChanged(void)
{
  if (TacticalActive && psDrivenDroid)
  {
    StartTacticalScrollObj(TRUE, (BASE_OBJECT*)psDrivenDroid);
    Neuron::DebugTrace("driveTacticalSelectionChanged\n");
  }
}

// Player clicked in the radar window.
//
void driveProcessRadarInput(int x, int y)
{
  SDWORD PosX, PosY;

  // when drive mode is active, clicking on the radar orders all selected droids
  // to move to this position.
  CalcRadarPosition(x, y, (UDWORD*)&PosX, (UDWORD*)&PosY);
  orderSelectedLoc(selectedPlayer, PosX * TILE_UNITS, PosY * TILE_UNITS);
}
