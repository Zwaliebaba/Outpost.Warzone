#include "pch.h"
/* WarCAM - Handles tracking/following of in game objects */
/* Alex McLean, Pumpkin Studios, EIDOS Interactive, 1998 */
/*	23rd September, 1998 - This code is now so hideously complex
	and unreadable that's it's inadvisable to attempt changing
	how the camera works, since I'm not sure that I'll be able to even
	get it working the way it used to, should anything get broken. 
	I really hope that no further changes are needed here...:-(
	Alex M. */
#include "stdio.h"
#include <cmath>
#include <directxmath.h>
#include "Frame.h"
#include "Input.h"
#include "RenderMatrix.h"
#include "RenderTypes.h"
#include "Objects.h"
#include "WarCAM.h"
#include "Display.h"
#include "Display3D.h"
#include "HCI.h"
#include "Console.h"
#include "GTime.h"
#include "Effects.h"
#include "Map.h"
#include "Geometry.h"
#include "OPrint.h"
#include "MiscIMD.h"
#include "Loop.h"
#include "Drive.h"
#include "Move.h"
#include "Order.h"
#include "Action.h"
#include "IntDisplay.h"
#include "RayCast.h"
#include "Display3D.h"
#include "RenderClip.h"
#ifndef PAUL
#include "Selection.h"
#endif

#define MIN_TRACK_HEIGHT 16

/* The radar-track stop test: the squared frame-to-frame rotation, under the
   old threshold of 10000 square 16-bit binary angles. */
constexpr float ROTATION_SETTLED = 10000.0f * (DirectX::XM_2PI / 65536.0f) * (DirectX::XM_2PI / 65536.0f);

extern BOOL bTrackingTransporter;

/* Holds all the details of our camera */
static WARCAM trackingCamera;

/* The fake target that we track when jumping to a new location on the radar */
static BASE_OBJECT radarTarget;

/* Do we trun to face when doing a radar jump? */
static BOOL bRadarAllign;

/* How far we track relative to the droids location - direction matters */
SDWORD camDroidXOffset;
SDWORD camDroidYOffset;

/*	These are the DEFAULT offsets that make us track _behind_ a droid and allow
	it to be pretty far _down_ the screen, so we can see more 
*/
#define	CAM_DEFAULT_X_OFFSET	-400
#define CAM_DEFAULT_Y_OFFSET	-400
#define	MINCAMROTX	-20

/* Function Prototypes... */
/* Firstly for tracking position */
void updateCameraAcceleration(UBYTE update);
void updateCameraVelocity(UBYTE update);
void updateCameraPosition(UBYTE update);

/* And now, rotation */
void updateCameraRotationAcceleration(UBYTE update);
void updateCameraRotationVelocity(UBYTE update);
void updateCameraRotationPosition(UBYTE update);

void initWarCam(void);
BOOL processWarCam(void);
void setWarCamActive(BOOL status);
BASE_OBJECT* camFindTarget(void);
void camAllignWithTarget(BASE_OBJECT* psTarget);
BOOL camTrackCamera(void);
void camSwitchOff(void);
BOOL getWarCamStatus(void);
void camToggleInfo(void);
void setUpRadarTarget(SDWORD x, SDWORD y);
void requestRadarTrack(SDWORD x, SDWORD y);
BOOL getRadarTrackingStatus(void);
void dispWarCamLogo(void);
UDWORD getPositionMagnitude(void);
float getRotationMagnitude(void);
void toggleRadarAllignment(void);
void camInformOfRotation(const DirectX::XMFLOAT3* rotation);
void processLeaderSelection(void);
float getAverageTrackAngle(BOOL bCheckOnScreen);
float getGroupAverageTrackAngle(UDWORD groupNumber, BOOL bCheckOnScreen);
void getTrackingConcerns(SDWORD* x, SDWORD* y, SDWORD* z);
void getGroupTrackingConcerns(SDWORD* x, SDWORD* y, SDWORD* z, UDWORD groupNumber, BOOL bOnScreen);

UDWORD getNumDroidsSelected(void);

/*	These used to be #defines but they're variable now as it may be necessary
	to allow the player	to customise tracking speed? Jim? 
*/
float accelConstant, velocityConstant, rotAccelConstant, rotVelocityConstant;

/* How much info do you want when tracking a droid - this toggles full stat info */
static BOOL bFullInfo = FALSE;

/* Are we requesting a new track to start that is a radar (location) track? */
static BOOL bRadarTrackingRequested = FALSE;

/* World coordinates for a radar track/jump */
static float radarX, radarY;

/*	Where we were up to (pos and rot) last update - allows us to see whether
	we are sufficently near our target to disable further tracking */
static iVector oldPosition;
static DirectX::XMFLOAT3 oldRotation;

/* The fraction of a second that the last game frame took */
static float fraction;

//-----------------------------------------------------------------------------------
/* Sets the camera to inactive to begin with */
void initWarCam(void)
{
  /* We're not intitially following anything */
  trackingCamera.status = CAM_INACTIVE;

  /* Set up the default tracking variables */
  accelConstant = ACCEL_CONSTANT;
  velocityConstant = VELOCITY_CONSTANT;
  rotAccelConstant = ROT_ACCEL_CONSTANT;
  rotVelocityConstant = ROT_VELOCITY_CONSTANT;

  /* Offset from droid's world coords */
  camDroidXOffset = CAM_DEFAULT_X_OFFSET;
  camDroidYOffset = CAM_DEFAULT_Y_OFFSET;
}

//-----------------------------------------------------------------------------------

// Just turn it off.
//
void CancelWarCam(void)
{
  if (trackingCamera.target->type == OBJ_DROID)
  {
    if (bTrackingTransporter && (((DROID*)trackingCamera.target)->droidType == DROID_TRANSPORTER))
      return;
  }

  trackingCamera.status = CAM_INACTIVE;
}

/* Updates the camera position/angle along with the object movement */
BOOL processWarCam(void)
{
  BASE_OBJECT* foundTarget;
  BOOL Status = TRUE;

  /* Get out if the camera isn't active */
  if (trackingCamera.status == CAM_INACTIVE)
    return (TRUE);

  /* Calculate fraction of a second for last game frame */
  fraction = (static_cast<float>(frameTime2) / static_cast<float>(GAME_TICKS_PER_SEC));

  /* Ensure that the camera only ever flips state within this routine! */
  switch (trackingCamera.status)
  {
  case CAM_REQUEST:

    /* See if we can find the target to follow */
    foundTarget = camFindTarget();

    if (foundTarget AND !foundTarget->died)
    {
      /* We've got one, so store away info */
      camAllignWithTarget(foundTarget);
      /* We're now into tracking status */
      trackingCamera.status = CAM_TRACKING;
      /* Inform via console */
      if (foundTarget->type == OBJ_DROID)
      {
        if (!getWarCamStatus()) { CONPRINTF(ConsoleString, (ConsoleString,"WZ/CAM  - %s",droidGetName((DROID*)foundTarget))); }
      }
      else {}
    }
    else
    {
      /* We've requested a track with no droid selected */
      trackingCamera.status = CAM_INACTIVE;
    }
    break;

  case CAM_TRACKING:
    /* Track the droid unless routine comes back false */
    if (!camTrackCamera())
    {
      /*
        Camera track came back false, either because droid died or is
        no longer selected, so reset to old values 
      */
      foundTarget = camFindTarget();
      if (foundTarget AND !foundTarget->died)
        trackingCamera.status = CAM_REQUEST;
      else
        trackingCamera.status = CAM_RESET;
    }
    processLeaderSelection();
    break;
  case CAM_RESET:
    /* Reset camera to pre-droid tracking status */
    if ((trackingCamera.target == nullptr) || (trackingCamera.target->type != OBJ_TARGET))
      camSwitchOff();
    /* Switch to inactive mode */
    trackingCamera.status = CAM_INACTIVE;
    Status = FALSE;
    break;
  default: Neuron::Fatal("Weirdy status for tracking Camera");
    break;
  }
  /* TBR
  flushConsoleMessages();
  CONPRINTF(ConsoleString,(ConsoleString,"Acceleration of movement constant : %.2f",accelConstant));
  CONPRINTF(ConsoleString,(ConsoleString,"Velocity of movement constant : %.2f",velocityConstant));
  CONPRINTF(ConsoleString,(ConsoleString,"Acceleration of rotation constant : %.2f",rotAccelConstant));
  CONPRINTF(ConsoleString,(ConsoleString,"Velocity of rotation constant : %.2f",rotVelocityConstant));
  CONPRINTF(ConsoleString,(ConsoleString,"Tracking droid direction : %.2f",trackingCamera.droid->direction));
  CONPRINTF(ConsoleString,(ConsoleString,"Tracking droid pitch : %d",trackingCamera.droid->pitch));
  CONPRINTF(ConsoleString,(ConsoleString,"Tracking droid roll : %d",trackingCamera.droid->roll));
  CONPRINTF(ConsoleString,(ConsoleString,"Tracking droid height (z) : %d",trackingCamera.droid->z));
  CONPRINTF(ConsoleString,(ConsoleString,"position.p.y : %d",player.p.y));
  */

  return Status;
}

//-----------------------------------------------------------------------------------

/* Flips states for camera active */
void setWarCamActive(BOOL status)
{
  Neuron::DebugTrace("setWarCamActive({})\n",status);

  /* We're trying to switch it on */
  if (status == TRUE)
  {
    /* If it's not inactive then it's already in use - so return */
    /* We're tracking a droid */
    if (trackingCamera.status != CAM_INACTIVE)
    {
      if (bRadarTrackingRequested)
        trackingCamera.status = CAM_REQUEST;
      else
        return;
    }
    else
    {
      /* Otherwise request the camera to track */
      trackingCamera.status = CAM_REQUEST;
    }
  }
  else
  /* We trying to switch off */
  {
    /* Is it already off? */
    if (trackingCamera.status == CAM_INACTIVE)
      return;
    /* Attempt to set to normal */
    trackingCamera.status = CAM_RESET;
  }
}

//-----------------------------------------------------------------------------------

BASE_OBJECT* camFindDroidTarget(void)
{
  DROID* psDroid;

  for (psDroid = apsDroidLists[selectedPlayer]; psDroid; psDroid = psDroid->psNext)
  {
    if (psDroid->selected)
    {
      /* Return the first one found */
      return ((BASE_OBJECT*)psDroid);
    }
  }

  /* We didn't find one */
  return (nullptr);
}

/* Attempts to find the target for the camera to track */
BASE_OBJECT* camFindTarget(void)
{
  /*	See if we can find a selected droid. If there's more than one
    droid selected for the present player, then we track the oldest
    one. */

  if (bRadarTrackingRequested)
  {
    setUpRadarTarget(radarX, radarY);
    bRadarTrackingRequested = FALSE;
    return (&radarTarget);
  }

  return camFindDroidTarget();
}

//-----------------------------------------------------------------------------------

/* Stores away old viewangle info and sets up new distance and angles */
void camAllignWithTarget(BASE_OBJECT* psTarget)
{
  /* Store away the target */
  trackingCamera.target = psTarget;

  /* Save away all the view angles */
  trackingCamera.oldView.r.x = trackingCamera.rotation.x = player.r.x;
  trackingCamera.oldView.r.y = trackingCamera.rotation.y = player.r.y;
  trackingCamera.oldView.r.z = trackingCamera.rotation.z = player.r.z;

  /* Store away the old positions and set the start position too */
  trackingCamera.oldView.p.x = trackingCamera.position.x = static_cast<float>(player.p.x);
  trackingCamera.oldView.p.y = trackingCamera.position.y = static_cast<float>(player.p.y);
  trackingCamera.oldView.p.z = trackingCamera.position.z = static_cast<float>(player.p.z);

  /* No initial velocity for moving */
  trackingCamera.velocity.x = trackingCamera.velocity.y = trackingCamera.velocity.z = 0.0f;
  /* Nor for rotation */
  trackingCamera.rotVel.x = trackingCamera.rotVel.y = trackingCamera.rotVel.z = 0.0f;
  /* No initial acceleration for moving */
  trackingCamera.acceleration.x = trackingCamera.acceleration.y = trackingCamera.acceleration.z = 0.0f;
  /* Nor for rotation */
  trackingCamera.rotAccel.x = trackingCamera.rotAccel.y = trackingCamera.rotAccel.z = 0.0f;

  /* Sote the old distance */
  trackingCamera.oldDistance = getViewDistance(); //distance;

  /* Store away when we started */
  trackingCamera.lastUpdate = gameTime2;
}

//-----------------------------------------------------------------------------------

//-----------------------------------------------------------------------------------
/* How this all works */
/*
Each frame we calculate the new acceleration, velocity and positions for the location
and rotation of the camera. The velocity is obviously based on the acceleration and this
in turn is based on the separation between the two objects. This separation is distance
in the case of location and degrees of arc in the case of rotation.

  Each frame:-

  ACCELERATION	-	A
  VELOCITY		-	V
  POSITION		-	P
  Location of camera	(x1,y1)
  Location of droid		(x2,y2)
  Separation(distance) = D. This is the distance between (x1,y1) and (x2,y2)

  A = c1D - c2V		Where c1 and c2 are two constants to be found (by experiment)
  V = V + A(frameTime/GAME_TICKS_PER_SEC)
  P = P + V(frameTime/GAME_TICKS_PER_SEC)

  Things are the same for the rotation except that D is then the difference in angles 
  between the way the camera and droid being tracked are facing. AND.... the two
  constants c1 and c2 will be different as we're dealing with entirely different scales
  and units. Separation in terms of distance could be in the thousands whereas degrees
  cannot exceed 180.

  This all works because acceleration is based on how far apart they are minus some factor
  times the camera's present velocity. This minus factor is what slows it down when the 
  separation gets very small. Without this, it would continually oscillate about it's target
  point. The four constants (two each for rotation and position) need to be found 
  by trial and error since the metrics of time,space and rotation are entirely warzone
  specific.

  And that's all folks.
*/

//-----------------------------------------------------------------------------------

//-----------------------------------------------------------------------------------
/*	N.B. the code from here on in is not very PSX friendly as there's lots of 
	unfriendly little floats - essentially the next 6 functions */

void updateCameraAcceleration(UBYTE update)
{
  float separation;
  SDWORD realPos;
  SDWORD xConcern, yConcern, zConcern;
  SDWORD xBehind, yBehind;
  BOOL bFlying;
  DROID* psDroid;
  float multiAngle;
  PROPULSION_STATS* psPropStats;
  SDWORD angle;

  /* The magnitude of the camera pitch, in whole degrees */
  angle = abs(static_cast<SDWORD>(std::lround(DirectX::XMConvertToDegrees(player.r.x)))) % 90;

  bFlying = FALSE;
  if (trackingCamera.target->type == OBJ_DROID)
  {
    psDroid = (DROID*)trackingCamera.target;
    psPropStats = asPropulsionStats + psDroid->asBits[COMP_PROPULSION].nStat;
    if (psPropStats->propulsionType == LIFT)
      bFlying = TRUE;
  }
  /*	This is where we check what it is we're tracking. 
    Were we to track a building or location - this is
    where it'd be set up */

  /*	If we're tracking a droid, then we nned to track slightly in front
    of it in order that the droid appears down the screen a bit. This means
    that we need to find an offset point from it relative to it's present 
    direction 
  */
  if (trackingCamera.target->type == OBJ_DROID)
  {
    /* Present direction is important */
    if (getNumDroidsSelected() > 2)
    {
      if (trackingCamera.target->selected)
        multiAngle = getAverageTrackAngle(TRUE);
      else
        multiAngle = getGroupAverageTrackAngle(trackingCamera.target->group,TRUE);
      float trackSin, trackCos;
      DirectX::XMScalarSinCos(&trackSin, &trackCos, multiAngle);
      xBehind = static_cast<SDWORD>(std::lrintf(camDroidYOffset * trackSin));
      yBehind = static_cast<SDWORD>(std::lrintf(camDroidXOffset * trackCos));
    }
    else
    {
      float trackSin, trackCos;
      DirectX::XMScalarSinCos(&trackSin, &trackCos, trackingCamera.target->direction);
      xBehind = static_cast<SDWORD>(std::lrintf(camDroidYOffset * trackSin));
      yBehind = static_cast<SDWORD>(std::lrintf(camDroidXOffset * trackCos));
    }
  }
  else
  {
    /* Irrelevant for normal radar tracking */
    xBehind = 0;
    yBehind = 0;
  }

  /*	Get these new coordinates */
  if (getNumDroidsSelected() > 2 AND trackingCamera.target->type == OBJ_DROID)
  {
    xConcern = trackingCamera.target->x; // nb - still NEED to be set
    yConcern = trackingCamera.target->z;
    zConcern = trackingCamera.target->y;
    if (trackingCamera.target->selected)
      getTrackingConcerns(&xConcern, &yConcern, &zConcern);
    else
      getGroupTrackingConcerns(&xConcern, &yConcern, &zConcern, trackingCamera.target->group,TRUE);
    yConcern += angle * 5;
  }
  else
  {
    xConcern = trackingCamera.target->x;
    yConcern = trackingCamera.target->z;
    zConcern = trackingCamera.target->y;
  }

  if (trackingCamera.target->type == OBJ_DROID AND getNumDroidsSelected() <= 2)
  {
    //		getBestPitchToEdgeOfGrid(trackingCamera.target->x,trackingCamera.target->z,
    yConcern += angle * 5;
  }

  if (update & X_UPDATE)
  {
    /* Need to update acceleration along x axis */
    realPos = xConcern - (CAM_X_SHIFT) - xBehind;
    separation = realPos - trackingCamera.position.x;
    if (!bFlying) { trackingCamera.acceleration.x = (accelConstant * separation - velocityConstant * trackingCamera.velocity.x); }
    else { trackingCamera.acceleration.x = ((accelConstant * separation * 4) - (velocityConstant * 2 * trackingCamera.velocity.x)); }
  }

  if (update & Y_UPDATE)
  {
    /* Need to update acceleration along y axis */
    realPos = (yConcern);
    separation = realPos - trackingCamera.position.y;
    if (bFlying)
      separation = separation / 2;
    if (!bFlying) { trackingCamera.acceleration.y = ((accelConstant) * separation - (velocityConstant) * trackingCamera.velocity.y); }
    else { trackingCamera.acceleration.y = (((accelConstant) * separation * 4) - ((velocityConstant) * 2 * trackingCamera.velocity.y)); }
  }

  if (update & Z_UPDATE)
  {
    /* Need to update acceleration along z axis */
    realPos = zConcern - (CAM_Z_SHIFT) - yBehind;
    separation = realPos - trackingCamera.position.z;
    if (!bFlying) { trackingCamera.acceleration.z = (accelConstant * separation - velocityConstant * trackingCamera.velocity.z); }
    else { trackingCamera.acceleration.z = ((accelConstant * separation * 4) - (velocityConstant * 2 * trackingCamera.velocity.z)); }
  }
}

//-----------------------------------------------------------------------------------

void updateCameraVelocity(UBYTE update)
{
  float fraction;

  /*	Get the time fraction of a second - the next two lines are present in 4
    of the next six functions. All 4 of these functions are called every frame, so
    it may be an idea to calculate these higher up and store them in a static but 
    I've left them in for clarity for now */

  fraction = (static_cast<float>(frameTime2) / static_cast<float>(GAME_TICKS_PER_SEC));

  if (update & X_UPDATE)
    trackingCamera.velocity.x += (trackingCamera.acceleration.x * fraction);

  if (update & Y_UPDATE)
    trackingCamera.velocity.y += (trackingCamera.acceleration.y * fraction);

  if (update & Z_UPDATE)
    trackingCamera.velocity.z += (trackingCamera.acceleration.z * fraction);
}

//-----------------------------------------------------------------------------------

void updateCameraPosition(UBYTE update)
{
  BOOL bFlying;
  float fraction;
  DROID* psDroid;
  PROPULSION_STATS* psPropStats;

  bFlying = FALSE;
  if (trackingCamera.target->type == OBJ_DROID)
  {
    psDroid = (DROID*)trackingCamera.target;
    psPropStats = asPropulsionStats + psDroid->asBits[COMP_PROPULSION].nStat;
    if (psPropStats->propulsionType == LIFT)
      bFlying = TRUE;
  }
  /* See above */
  fraction = (static_cast<float>(frameTime2) / static_cast<float>(GAME_TICKS_PER_SEC));

  if (update & X_UPDATE)
  {
    /* Need to update position along x axis */
    trackingCamera.position.x += (trackingCamera.velocity.x * fraction);
  }

  if (update & Y_UPDATE)
  {
    /* Need to update position along y axis */
    trackingCamera.position.y += (trackingCamera.velocity.y * fraction);
    //		else
  }

  if (update & Z_UPDATE)
  {
    /* Need to update position along z axis */
    trackingCamera.position.z += (trackingCamera.velocity.z * fraction);
  }
}

//-----------------------------------------------------------------------------------
/* Calculate the acceleration that the camera spins around at */
void updateCameraRotationAcceleration(UBYTE update)
{
  float separation;
  float xConcern, yConcern, zConcern;
  BOOL bTooLow;
  DROID* psDroid;
  UDWORD droidHeight, mapHeight, difHeight;
  PROPULSION_STATS* psPropStats;
  float pitch;
  BOOL bGotFlying = FALSE;
  SDWORD xPos, yPos, zPos;

  bTooLow = FALSE;
  if (trackingCamera.target->type == OBJ_DROID)
  {
    psDroid = (DROID*)trackingCamera.target;
    psPropStats = asPropulsionStats + psDroid->asBits[COMP_PROPULSION].nStat;
    if (psPropStats->propulsionType == LIFT)
    {
      bGotFlying = TRUE;
      droidHeight = psDroid->z;
      mapHeight = map_Height(psDroid->x, psDroid->y);
      difHeight = abs(static_cast<SDWORD>(droidHeight - mapHeight));
      if (difHeight < MIN_TRACK_HEIGHT)
        bTooLow = TRUE;
    }
  }

  if (update & Y_UPDATE)
  {
    /* Presently only y rotation being calculated - but same idea for other axes */
    /* Check what we're tracking */
    if (getNumDroidsSelected() > 2 AND trackingCamera.target->type == OBJ_DROID)
    {
      if (trackingCamera.target->selected)
        yConcern = getAverageTrackAngle(FALSE);
      else
        yConcern = getGroupAverageTrackAngle(trackingCamera.target->group,FALSE);
    }
    else
      yConcern = trackingCamera.target->direction;
    yConcern += DirectX::XM_PI;

    /* Which way are we facing? */
    separation = DirectX::XMScalarModAngle(yConcern - trackingCamera.rotation.y);

    /* Make new acceleration */
    trackingCamera.rotAccel.y = (rotAccelConstant * separation - rotVelocityConstant * trackingCamera.rotVel.y);
  }

  if (update & X_UPDATE)
  {
    if (trackingCamera.target->type == OBJ_DROID AND !bGotFlying)
    {
      getTrackingConcerns(&xPos, &yPos, &zPos);
      if (trackingCamera.target->selected)
        getBestPitchToEdgeOfGrid(xPos, zPos, -(getAverageTrackAngle(TRUE) + DirectX::XM_PI), &pitch);
      else
        getBestPitchToEdgeOfGrid(xPos, zPos, -(getGroupAverageTrackAngle(trackingCamera.target->group,TRUE) + DirectX::XM_PI), &pitch);
      if (pitch < DirectX::XMConvertToRadians(14.0f))
        pitch = DirectX::XMConvertToRadians(14.0f);
      xConcern = -pitch;
    }
    else
      xConcern = trackingCamera.target->pitch - DirectX::XMConvertToRadians(16.0f);

    separation = DirectX::XMScalarModAngle(xConcern - trackingCamera.rotation.x);

    /* Make new acceleration */
    trackingCamera.rotAccel.x =
      /* Make this really slow */
      ((rotAccelConstant) * separation - rotVelocityConstant * trackingCamera.rotVel.x);
  }

  /* This looks a bit arse - looks like a flight sim */
  if (update & Z_UPDATE)
  {
    if (bTooLow)
      zConcern = 0.0f;
    else
      zConcern = trackingCamera.target->roll;
    separation = DirectX::XMScalarModAngle(zConcern - trackingCamera.rotation.z);

    /* Make new acceleration */
    trackingCamera.rotAccel.z =
      /* Make this really slow */
      ((rotAccelConstant / 1) * separation - rotVelocityConstant * trackingCamera.rotVel.z);
  }
}

//-----------------------------------------------------------------------------------
/*	Calculate the velocity that the camera spins around at - just add previously
	calculated acceleration */
void updateCameraRotationVelocity(UBYTE update)
{
  float fraction;

  fraction = (static_cast<float>(frameTime2) / static_cast<float>(GAME_TICKS_PER_SEC));

  if (update & Y_UPDATE)
    trackingCamera.rotVel.y += (trackingCamera.rotAccel.y * fraction);
  if (update & X_UPDATE)
    trackingCamera.rotVel.x += (trackingCamera.rotAccel.x * fraction);
  if (update & Z_UPDATE)
    trackingCamera.rotVel.z += (trackingCamera.rotAccel.z * fraction);
}

//-----------------------------------------------------------------------------------
/* Move the camera around by adding the velocity */
void updateCameraRotationPosition(UBYTE update)
{
  float fraction;

  fraction = (static_cast<float>(frameTime2) / static_cast<float>(GAME_TICKS_PER_SEC));

  if (update & Y_UPDATE)
    trackingCamera.rotation.y = DirectX::XMScalarModAngle(trackingCamera.rotation.y + trackingCamera.rotVel.y * fraction);
  if (update & X_UPDATE)
    trackingCamera.rotation.x = DirectX::XMScalarModAngle(trackingCamera.rotation.x + trackingCamera.rotVel.x * fraction);
  if (update & Z_UPDATE)
    trackingCamera.rotation.z = DirectX::XMScalarModAngle(trackingCamera.rotation.z + trackingCamera.rotVel.z * fraction);
}

BOOL nearEnough(void)
{
  BOOL retVal = FALSE;
  SDWORD xPos;
  SDWORD yPos;

  xPos = player.p.x + (VISIBLE_XTILES * TILE_UNITS) / 2;
  yPos = player.p.z + (VISIBLE_YTILES * TILE_UNITS) / 2;

  if ((abs(xPos - trackingCamera.target->x) <= 256) AND (abs(yPos - trackingCamera.target->y) <= 256))
    retVal = TRUE;
  return (retVal);
}

/* Updates the viewpoint according to the object being tracked */
BOOL camTrackCamera(void)
{
  PROPULSION_STATS* psPropStats;
  DROID* psDroid;
  BOOL bFlying;

  bFlying = FALSE;

  /* Most importantly - see if the target we're tracking is dead! */
  if (trackingCamera.target->died)
    return (FALSE);

  /*	Cancel tracking if it's no longer selected.
    This may not be desirable? 	*/
  if (trackingCamera.target->type == OBJ_DROID) {}

  /* Update the acceleration,velocity and position of the camera for movement */
  updateCameraAcceleration(CAM_ALL);
  updateCameraVelocity(CAM_ALL);
  updateCameraPosition(CAM_ALL);

  /* Update the acceleration,velocity and rotation of the camera for rotation */
  /*	You can track roll as well (z axis) but it makes you ill and looks 
    like a flight sim, so for now just pitch and orientation */

  if (trackingCamera.target->type == OBJ_DROID)
  {
    psDroid = (DROID*)trackingCamera.target;
    psPropStats = asPropulsionStats + psDroid->asBits[COMP_PROPULSION].nStat;
    if (psPropStats->propulsionType == LIFT)
      bFlying = TRUE;
  }
  /*	
    bIsBuilding = FALSE;
    if(trackingCamera.target->type == OBJ_DROID)
    {
      psDroid= (DROID*)trackingCamera.target;
      if(DroidIsBuilding(psDroid))
      {
        bIsBuilding = TRUE;
      }
    }
  */

  if (bRadarAllign OR trackingCamera.target->type == OBJ_DROID)
  {
    if (bFlying)
      updateCameraRotationAcceleration(CAM_ALL);
    else
      updateCameraRotationAcceleration(CAM_X_AND_Y);
  }
  if (bFlying)
  {
    updateCameraRotationVelocity(CAM_ALL);
    updateCameraRotationPosition(CAM_ALL);
  }
  else
  {
    updateCameraRotationVelocity(CAM_X_AND_Y);
    updateCameraRotationPosition(CAM_X_AND_Y);
  }

  /* Record the old positions for comparison */
  oldPosition.x = player.p.x;
  oldPosition.y = player.p.y;
  oldPosition.z = player.p.z;

  /* Update the position that's now stored in trackingCamera.position (iVector) */
  player.p.x = trackingCamera.position.x;
  player.p.y = trackingCamera.position.y;
  player.p.z = trackingCamera.position.z;

  /* Record the old positions for comparison */
  oldRotation.x = player.r.x;
  oldRotation.y = player.r.y;
  oldRotation.z = player.r.z;

  /* Update the rotations that're now stored in trackingCamera.rotation */
  player.r.x = trackingCamera.rotation.x;
  /*if(!bIsBuilding)*/
  player.r.y = trackingCamera.rotation.y;
  player.r.z = trackingCamera.rotation.z;

  /* There's a minimum for this - especially when John's VTOL code lets them land vertically on cliffs */
  if (player.r.x > DirectX::XMConvertToRadians(MAX_PLAYER_X_ANGLE))
    player.r.x = DirectX::XMConvertToRadians(MAX_PLAYER_X_ANGLE);

  /* Clip the position to the edge of the map */
  CheckScrollLimits();

  /* Store away our last update as acceleration and velocity are all fn()/dt */
  trackingCamera.lastUpdate = gameTime2;
  if (bFullInfo)
  {
    flushConsoleMessages();
    if (trackingCamera.target->type == OBJ_DROID)
      printDroidInfo((DROID*)trackingCamera.target);
  }

  /* Switch off if we're jumping to a new location and we've got there */
  if (getRadarTrackingStatus())
  {
    /*	This will ensure we come to a rest and terminate the tracking
      routine once we're close enough
    */
    if (getRotationMagnitude() < ROTATION_SETTLED)
    {
      if (nearEnough() AND getPositionMagnitude() < 60)
        camToggleStatus();
    }
  }
  return (TRUE);
}

//-----------------------------------------------------------------------------------
#define	LEADER_LEFT			1
#define	LEADER_RIGHT		2
#define	LEADER_UP			3
#define	LEADER_DOWN			4
#define LEADER_STATIC		5

void processLeaderSelection(void)
{
  DROID* psDroid;
  DROID* psPresent;
  DROID* psNew = nullptr;
  UDWORD leaderClass;
  BOOL bSuccess;
  UDWORD dif;
  UDWORD bestSoFar;

  if (getWarCamStatus())
  {
    /* Only do if we're tracking a droid */
    if (trackingCamera.target->type != OBJ_DROID)
      return;
  }
  else
    return;

  /* Don't do if we're driving?! */
  if (getDrivingStatus())
    return;

  psPresent = (DROID*)trackingCamera.target;

  if (keyPressed(KEY_LEFTARROW))
    leaderClass = LEADER_LEFT;

  else if (keyPressed(KEY_RIGHTARROW))
    leaderClass = LEADER_RIGHT;

  else if (keyPressed(KEY_UPARROW))
    leaderClass = LEADER_UP;

  else if (keyPressed(KEY_DOWNARROW))
    leaderClass = LEADER_DOWN;
  else
    leaderClass = LEADER_STATIC;

  bSuccess = FALSE;
  bestSoFar = UDWORD_MAX;
  switch (leaderClass)
  {
  case LEADER_LEFT:
    for (psDroid = apsDroidLists[selectedPlayer]; psDroid; psDroid = psDroid->psNext)
    {
      /* Is it even on the sscreen? */
      if (DrawnInLastFrame(psDroid->sDisplay.frameNumber) AND psDroid->selected AND psDroid != psPresent)
      {
        if (psDroid->sDisplay.screenX < psPresent->sDisplay.screenX)
        {
          dif = psPresent->sDisplay.screenX - psDroid->sDisplay.screenX;
          if (dif < bestSoFar)
          {
            bestSoFar = dif;
            bSuccess = TRUE;
            psNew = psDroid;
          }
        }
      }
    }
    break;
  case LEADER_RIGHT:
    for (psDroid = apsDroidLists[selectedPlayer]; psDroid; psDroid = psDroid->psNext)
    {
      /* Is it even on the sscreen? */
      if (DrawnInLastFrame(psDroid->sDisplay.frameNumber) AND psDroid->selected AND psDroid != psPresent)
      {
        if (psDroid->sDisplay.screenX > psPresent->sDisplay.screenX)
        {
          dif = psDroid->sDisplay.screenX - psPresent->sDisplay.screenX;
          if (dif < bestSoFar)
          {
            bestSoFar = dif;
            bSuccess = TRUE;
            psNew = psDroid;
          }
        }
      }
    }
    break;
  case LEADER_UP:
    for (psDroid = apsDroidLists[selectedPlayer]; psDroid; psDroid = psDroid->psNext)
    {
      /* Is it even on the sscreen? */
      if (DrawnInLastFrame(psDroid->sDisplay.frameNumber) AND psDroid->selected AND psDroid != psPresent)
      {
        if (psDroid->sDisplay.screenY < psPresent->sDisplay.screenY)
        {
          dif = psPresent->sDisplay.screenY - psDroid->sDisplay.screenY;
          if (dif < bestSoFar)
          {
            bestSoFar = dif;
            bSuccess = TRUE;
            psNew = psDroid;
          }
        }
      }
    }
    break;
  case LEADER_DOWN:
    for (psDroid = apsDroidLists[selectedPlayer]; psDroid; psDroid = psDroid->psNext)
    {
      /* Is it even on the sscreen? */
      if (DrawnInLastFrame(psDroid->sDisplay.frameNumber) AND psDroid->selected AND psDroid != psPresent)
      {
        if (psDroid->sDisplay.screenY > psPresent->sDisplay.screenY)
        {
          dif = psDroid->sDisplay.screenY - psPresent->sDisplay.screenY;
          if (dif < bestSoFar)
          {
            bestSoFar = dif;
            bSuccess = TRUE;
            psNew = psDroid;
          }
        }
      }
    }
    break;
  case LEADER_STATIC:
    break;
  }
  if (bSuccess)
    camAllignWithTarget((BASE_OBJECT*)psNew);
}

//-----------------------------------------------------------------------------------
DROID* getTrackingDroid(void)
{
  if (!getWarCamStatus())
    return (nullptr);
  if (trackingCamera.status != CAM_TRACKING)
    return (nullptr);
  if (trackingCamera.target->type != OBJ_DROID)
    return (nullptr);
  return ((DROID*)trackingCamera.target);
}

//-----------------------------------------------------------------------------------
float getGroupAverageTrackAngle(UDWORD groupNumber, BOOL bCheckOnScreen)
{
  DROID* psDroid;
  float xTotal, yTotal;
  SDWORD droidCount;

  /* Initialise all the stuff */
  droidCount = 0;

  /* Set totals to zero */
  xTotal = yTotal = 0.0f;

  /* Got thru' all droids */
  for (psDroid = apsDroidLists[selectedPlayer]; psDroid; psDroid = psDroid->psNext)
  {
    /* Is he worth considering? */
    if (psDroid->group == groupNumber)
    {
      if (bCheckOnScreen ? droidOnScreen(psDroid,DISP_WIDTH / 6) : TRUE)
      {
        droidCount++;
        xTotal += sinf(psDroid->direction);
        yTotal += cosf(psDroid->direction);
      }
    }
  }
  if (droidCount == 0)
    return 0.0f;
  return atan2f(xTotal, yTotal);
}

//-----------------------------------------------------------------------------------
float getAverageTrackAngle(BOOL bCheckOnScreen)
{
  DROID* psDroid;
  float xTotal, yTotal;
  SDWORD droidCount;

  /* Initialise all the stuff */
  droidCount = 0;

  /* Set totals to zero */
  xTotal = yTotal = 0.0f;

  /* Got thru' all droids */
  for (psDroid = apsDroidLists[selectedPlayer]; psDroid; psDroid = psDroid->psNext)
  {
    /* Is he worth selecting? */
    if (psDroid->selected)
    {
      if (bCheckOnScreen ? droidOnScreen(psDroid,DISP_WIDTH / 6) : TRUE)
      {
        droidCount++;
        xTotal += sinf(psDroid->direction);
        yTotal += cosf(psDroid->direction);
      }
    }
  }
  if (droidCount == 0)
    return 0.0f;
  return atan2f(xTotal, yTotal);
}

//-----------------------------------------------------------------------------------

//-----------------------------------------------------------------------------------
UDWORD getNumDroidsSelected(void)
{
  return (selNumSelected(selectedPlayer));
  /*
  DROID	*psDroid;
  UDWORD	count;
  
    for(psDroid = apsDroidLists[selectedPlayer],count = 0;
      psDroid; psDroid = psDroid->psNext)
    {
      if(psDroid->selected)
      {
        count++;
      }
    }
    return(count);
  */
}

//-----------------------------------------------------------------------------------
void getTrackingConcerns(SDWORD* x, SDWORD* y, SDWORD* z)
{
  SDWORD xTotals, yTotals, zTotals;
  DROID* psDroid;
  UDWORD count;

  xTotals = yTotals = zTotals = 0;
  for (count = 0, psDroid = apsDroidLists[selectedPlayer]; psDroid; psDroid = psDroid->psNext)
  {
    if (psDroid->selected)
    {
      if (droidOnScreen(psDroid,DISP_WIDTH / 4))
      {
        count++;
        xTotals += psDroid->x;
        yTotals += psDroid->z; // note the flip
        zTotals += psDroid->y;
      }
    }
  }

  if (count) // necessary!!!!!!!
  {
    *x = xTotals / count;
    *y = yTotals / count;
    *z = zTotals / count;
  }
}

//-----------------------------------------------------------------------------------
void getGroupTrackingConcerns(SDWORD* x, SDWORD* y, SDWORD* z, UDWORD groupNumber, BOOL bOnScreen)
{
  SDWORD xTotals, yTotals, zTotals;
  DROID* psDroid;
  UDWORD count;

  xTotals = yTotals = zTotals = 0;
  for (count = 0, psDroid = apsDroidLists[selectedPlayer]; psDroid; psDroid = psDroid->psNext)
  {
    if (psDroid->group == groupNumber)
    {
      if (bOnScreen ? droidOnScreen(psDroid,DISP_WIDTH / 4) : TRUE)
      {
        //					if(droidOnScreen(psDroid,DISP_WIDTH/4))
        count++;
        xTotals += psDroid->x;
        yTotals += psDroid->z; // note the flip
        zTotals += psDroid->y;
      }
      //				else
      //						yTotals+=psDroid->z;	// note the flip
    }
  }

  if (count) // necessary!!!!!!!
  {
    *x = xTotals / count;
    *y = yTotals / count;
    *z = zTotals / count;
  }
}

//-----------------------------------------------------------------------------------

/* Static function that switches off tracking - and might not be desirable? - Jim?*/
void camSwitchOff(void)
{
  /* Restore the angles */
  player.r.z = trackingCamera.oldView.r.z;

  /* And height */
  /* Is this desirable??? */

  /* Restore distance */
  setViewDistance(trackingCamera.oldDistance);
}

//-----------------------------------------------------------------------------------

/* Returns whether or not the tracking camera is active */
BOOL getWarCamStatus(void)
{
  /* Is it switched off? */
  if (trackingCamera.status == CAM_INACTIVE)
    return (FALSE);
  /* Tracking is ON */
  return (TRUE);
}

//-----------------------------------------------------------------------------------

/* Flips the status of tracking to the opposite of what it presently is */
void camToggleStatus(void)
{
  /* If it's off */
  if (trackingCamera.status == CAM_INACTIVE)
  {
    /* Switch it on */
    setWarCamActive(TRUE);
  }
  else
  {
    /* Otherwise, switch it off */
    setWarCamActive(FALSE);
    //		if(getDrivingStatus())
  }
}

/*	Flips on/off whether we print out full info about the droid being tracked.
	If ON then this info is permanent on screen and realtime updating */
void camToggleInfo(void) { bFullInfo = !bFullInfo; }

/* Sets up the dummy target for the camera */
void setUpRadarTarget(SDWORD x, SDWORD y)
{
  radarTarget.x = x;
  radarTarget.y = y;
  if ((x < 0) OR (y < 0) OR (x > static_cast<SDWORD>((mapWidth - 1) * TILE_UNITS)) OR (y > static_cast<SDWORD>((mapHeight - 1) *
    TILE_UNITS)))
    radarTarget.z = 128 * ELEVATION_SCALE;
  else
    radarTarget.z = map_Height(x, y);
  radarTarget.direction = calcDirection(player.p.x, player.p.z, x, y);
  radarTarget.pitch = 0;
  radarTarget.roll = 0;
  radarTarget.type = OBJ_TARGET;
  radarTarget.died = 0;
}

/* Informs the tracking camera that we want to start tracking to a new radar target */
void requestRadarTrack(SDWORD x, SDWORD y)
{
  radarX = static_cast<SWORD>(x);
  radarY = static_cast<SWORD>(y);
  bRadarTrackingRequested = TRUE;
  trackingCamera.status = CAM_REQUEST;
  processWarCam();
}

/* Returns whether we're presently tracking to a new _location_ */
BOOL getRadarTrackingStatus(void)
{
  BOOL retVal;

  if (trackingCamera.status == CAM_INACTIVE)
    retVal = FALSE;
  else
  {
    //if you know why the above check was commented out please tell me AB 19/11/98
    if (trackingCamera.target && trackingCamera.target->type == OBJ_TARGET)
      retVal = TRUE;
    else
      retVal = FALSE;
  }
  return (retVal);
}

/* Displays a spinning MTV style logo in the top right of the screen */
void dispWarCamLogo(void)
{
  //
  //	if(gamePaused())
  //		/* get out if we're paused */
  //
  //	pie_MatBegin();							/* Push the indentity matrix */
  //
}

void toggleRadarAllignment(void) { bRadarAllign = !bRadarAllign; }

/* Returns how far away we are from our goal in a radar track */
UDWORD getPositionMagnitude(void)
{
  iVector dif;
  UDWORD val;

  dif.x = abs(player.p.x - oldPosition.x);
  dif.y = abs(player.p.y - oldPosition.y);
  dif.z = abs(player.p.z - oldPosition.z);
  val = (dif.x * dif.x) + (dif.y * dif.y) + (dif.z * dif.z);
  return (val);
}

/* Rteurns how far away we are from our goal in rotation */
float getRotationMagnitude(void)
{
  const float difX = DirectX::XMScalarModAngle(player.r.x - oldRotation.x);
  const float difY = DirectX::XMScalarModAngle(player.r.y - oldRotation.y);
  const float difZ = DirectX::XMScalarModAngle(player.r.z - oldRotation.z);
  return (difX * difX) + (difY * difY) + (difZ * difZ);
}

void camInformOfRotation(const DirectX::XMFLOAT3* rotation)
{
  trackingCamera.rotation = *rotation;
}
