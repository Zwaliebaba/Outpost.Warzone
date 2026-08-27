#include "pch.h"
/*	
	25th September, 1998. Path Optimisation.
	An attempt to optimise the final path found for routing
	in order to stop units getting snagged on corners.
	Alex McLean. Will only be executed on my machine for now...
*/

// -------------------------------------------------------------------------
#include "Frame.h"
#include "Base.h"
#include "Move.h"
#include "Map.h"
#include "Weapons.h"
#include "StatsDef.h"
#include "DroidDef.h"
#include "OptimisePath.h"
#include "RayCast.h"
#include "FPath.h"
#include "Effects.h"

// -------------------------------------------------------------------------
/* Where to slide a path point according to bisecting angle */
signed char markerSteps[8][2] = {{0, 0}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};

// -------------------------------------------------------------------------
/* Returns the index into the above table for a given angle */
UDWORD getStepIndexFromAngle(UDWORD angle);

/*
	Attempts to make the droid's path more 'followable' by moving
	waypoints away from the nearest blocking tile. The direction
	in which the waypoint is moved is dependant on where the previous
	and next way points lie. Essentially, the angle that bisects the two
	vectors to the previous and next waypoint is found. We then
	calculate which way to make this angle (line) face. It needs to face into
	the larger of the two arcs, thereby moving _away_ from the blocking
	tile 
*/
void optimisePathForDroid(DROID* psDroid) { UNUSEDPARAMETER(psDroid); }

// -------------------------------------------------------------------------
/*
	A hack function - could be done by dividing the angle by 45 
	and establishing the right quadrant 
*/
UDWORD getStepIndexFromAngle(UDWORD angle)
{
  float accA;
  UDWORD retVal;

  accA = static_cast<float>(angle);

  DEBUG_ASSERT_TEXT(angle<360, "Angle's too big!!!");

  if (accA <= 22.5 OR accA > 337.0)
    retVal = 0;
  else if (accA > 22.5 AND accA <= 67.5)
    retVal = 1;
  else if (accA > 67.5 AND accA <= 112.5)
    retVal = 2;
  else if (accA > 112.5 AND accA <= 157.5)
    retVal = 3;
  else if (accA > 157.5 AND accA <= 202.5)
    retVal = 4;
  else if (accA > 202.5 AND accA <= 247.5)
    retVal = 5;
  else if (accA > 247.5 AND accA <= 292.5)
    retVal = 6;

  else if (accA > 292.5 AND accA <= 337.5)
    retVal = 7;

  return (retVal);
}

// -------------------------------------------------------------------------
