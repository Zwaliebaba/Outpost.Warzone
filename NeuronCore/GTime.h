/*
 * GTime.h
 *
 * Interface to the game clock.
 *
 */
#ifndef _gtime_h
#define _gtime_h

/* The number of ticks per second for the game clock */
#define GAME_TICKS_PER_SEC		1000

// changed to /6 by ajl. if this needs to go back to ticks/10 then tell me. 
#define GTIME_MAXFRAME	(GAME_TICKS_PER_SEC/6)

/* The current time in the game world */
extern UDWORD gameTime;

/* The time for the last frame */
extern UDWORD frameTime;

/* The current time in the game world */
extern UDWORD gameTime2; // Never stops.

/* The time for the last frame */
extern UDWORD frameTime2; // Never stops.

/* Initialise the game clock */
extern BOOL gameTimeInit(void);

/* Call this each loop to update the game timer.
 *
 * This no longer advances gameTime. It advances the *target* the simulation is
 * owed - see Neuron::ConsumeSimulationTick below - along with gameTime2, which
 * is the client's own clock and still moves once per frame.
 */
extern void gameTimeUpdate(void);

/***************************************************************************/
/* The fixed simulation tick.
 *
 * The world used to advance by however long the last frame took, so how far a
 * droid moved in one step depended on the frame rate; Move.cpp carries a
 * ten-frame rolling average of frameTime for no other reason than to smooth
 * that out. It advances by a fixed quantum now, as many whole quanta per frame
 * as the wall clock has paid for, which is what an authoritative server needs
 * (Docs/ServerAuthority.md stage A): a headless one has no frame to hang a
 * timestep off.
 */
/***************************************************************************/

namespace Neuron
{

/* Game time one simulation tick advances the world by, in game ticks
 * (milliseconds).
 *
 * 25 ticks a second is BASE_DEF_RATE in Move.cpp - the rate the movement system
 * was written around, and the value moveInitialise seeds its whole frame-time
 * history with. So a fixed tick of this length is the one length at which
 * baseSpeed settles on exactly the BASE_SPEED_INIT the movement code starts
 * from, rather than on whatever the frame rate happened to average.
 */
inline constexpr UDWORD SimulationTickMs = GAME_TICKS_PER_SEC / 25;

/* How many ticks one frame may run before the rest of what is owed is
 * discarded. Past this a machine that cannot keep up runs the world slowly
 * instead of fast-forwarding it, which is the policy GTIME_MAXFRAME used to
 * enforce on a single variable-length frame - and four ticks is 160ms, near
 * enough the same bound it enforced.
 */
inline constexpr UDWORD MaxSimulationTicksPerFrame = 4;

/* Takes one tick's worth of game time off what the clock has accumulated,
 * advancing gameTime by SimulationTickMs and setting frameTime to it. Returns
 * FALSE once what is left no longer fills a whole tick, so the caller is
 *
 *     while (Neuron::ConsumeSimulationTick())
 *       SimulateTick();
 *
 * and the remainder stays owed rather than being rounded away.
 */
BOOL ConsumeSimulationTick();

} // namespace Neuron

/* Returns TRUE if gameTime is stopped. */
extern BOOL gameTimeIsStopped(void);

/* Call this to stop the game timer */
extern void gameTimeStop(void);

/* Call this to restart the game timer after a call to gameTimeStop */
extern void gameTimeStart(void);

/*Call this to reset the game timer*/
extern void gameTimeReset(UDWORD time);

// reset the game time modifiers
void gameTimeResetMod(void);
// set the time modifier
void gameTimeSetMod(float mod);
// get the current time modifier
void gameTimeGetMod(float* pMod);

// get the current time modifier
void gameTimeGetModifier(UDWORD* pMod, UDWORD* pFactor);

/* Useful for periodical stuff */
/* Will return a number that climbs over tickFrequency game ticks and ends up in the required range. */
/*	
	For instance getTimeValueRange(4096,256) will return a number that cycles through
	the values 0..256 every 4.096 seconds...
	Ensure that the first is an integer multiple of the second 
*/
extern UDWORD getTimeValueRange(UDWORD tickFrequency, UDWORD requiredRange);
extern UDWORD getStaticTimeValueRange(UDWORD tickFrequency, UDWORD requiredRange);

extern void getTimeComponents(UDWORD time, UDWORD* hours, UDWORD* minutes, UDWORD* seconds);

/* The frame counters. These were Frame.h's, beside the window and the message
 * pump; they are here because core code reads them - NetSupp stamps every
 * network log entry with the frame number - and a headless build has a frame
 * number without having a window. Window.cpp advances them once per frame.
 */

/* Returns the current frame we're on - used to establish whats on screen */
extern UDWORD frameGetFrameNumber(void);

/* Return the current frame rate */
extern UDWORD frameGetFrameRate(void);

/* Return the overall frame rate */
extern UDWORD frameGetOverallRate(void);

/* Return the frame rate for the last second */
extern UDWORD frameGetRecentRate(void);

/* Called by the frame loop: once at start up, then once per frame */
extern void gtimeFrameCountInit(void);
extern void gtimeFrameCountUpdate(void);

#endif
