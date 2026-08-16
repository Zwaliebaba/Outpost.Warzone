#include "pch.h"

#include "Frame.h"
#include "GTime.h"

#define TIME_FIX

#define GTIME_MINFRAME	(GAME_TICKS_PER_SEC/80)

/* The current time in the game world */
UDWORD gameTime;

/* The time for the last frame */
UDWORD frameTime;

/* The current time in the game world ( never stops )*/
UDWORD gameTime2;

/* The time for the last frame (never stops)*/
UDWORD frameTime2;

// the current clock modifier
static float modifier;

// the amount of game time before the last time clock speed was set
static UDWORD timeOffset;
static UDWORD timeOffset2;

// the tick count the last time the clock speed was set
static UDWORD baseTime;
static UDWORD baseTime2;

/* When the game paused so that gameTime can be adjusted when the game restarts */
static SDWORD pauseStart;

/* Count how many times gameTimeStop has been called without a game time start */
static UDWORD stopCount;

/* Initialise the game clock */
BOOL gameTimeInit(void)
{
  /*start the timer off at 2 so that when the scripts strip the map of objects 
  for multiPlayer they will be processed as if they died*/
  gameTime = 2;
  timeOffset = 0;
  baseTime = GetTickCount();

  gameTime2 = 0;
  timeOffset2 = 0;
  baseTime2 = baseTime;

  modifier = 1.0f;

  stopCount = 0;

  return TRUE;
}

UDWORD getTimeValueRange(UDWORD tickFrequency, UDWORD requiredRange)
{
  UDWORD div1, div2;

  div1 = gameTime2 % tickFrequency;
  div2 = tickFrequency / requiredRange;
  return (div1 / div2);
}

UDWORD getStaticTimeValueRange(UDWORD tickFrequency, UDWORD requiredRange)
{
  UDWORD div1, div2;

  div1 = gameTime % tickFrequency;
  div2 = tickFrequency / requiredRange;
  return (div1 / div2);
}

/* Call this each loop to update the game timer */
void gameTimeUpdate(void)
{
  UDWORD currTime, fpMod;
  unsigned __int64 newTime;
  unsigned __int64 extraTime;

  currTime = GetTickCount();

  if (currTime < baseTime)
  {
#ifdef RATE_LIMIT
    // Limit the frame time
#endif

    // ooops the clock has wrapped round -
    // someone actually managed to keep windows running for 50 days !!!!
    // ........
  }

  //don't update the game time if gameTimeStop has been called
  if (stopCount == 0)
  {
    // Calculate the new game time
    newTime = currTime - baseTime;

    // convert the modifier to fixed point cos we loose accuracy
    fpMod = std::lrintf(modifier * 1000.0f);

    newTime = newTime * fpMod / 1000;

    newTime += timeOffset;

    // Calculate the time for this frame
    frameTime = static_cast<UDWORD>(newTime - gameTime);

    // Limit the frame time
    if (frameTime > GTIME_MAXFRAME)
    {
#ifdef TIME_FIX
      extraTime = frameTime - GTIME_MAXFRAME;
      extraTime = extraTime * 1000 / fpMod; //adjust the addition to base time
      baseTime += static_cast<UDWORD>(extraTime);
#else
      baseTime += frameTime - GTIME_MAXFRAME;
#endif

      newTime = gameTime + GTIME_MAXFRAME;
      frameTime = GTIME_MAXFRAME;
    }

    // Store the game time
    gameTime = static_cast<UDWORD>(newTime);
  }

  // now update gameTime2 which does not pause
  newTime = currTime - baseTime2;

  newTime += timeOffset;

  // Calculate the time for this frame
  frameTime2 = static_cast<UDWORD>(newTime) - gameTime2;

  // Limit the frame time
  if (frameTime2 > GTIME_MAXFRAME)
  {
    baseTime2 += frameTime2 - GTIME_MAXFRAME;
    newTime = gameTime2 + GTIME_MAXFRAME;
    frameTime2 = GTIME_MAXFRAME;
  }

  // Store the game time
  gameTime2 = static_cast<UDWORD>(newTime);
}

// reset the game time modifiers
void gameTimeResetMod(void)
{
  timeOffset = gameTime;
  timeOffset2 = gameTime2;

  baseTime = GetTickCount();
  baseTime2 = GetTickCount();

  modifier = 1.0f;
}

// set the time modifier
void gameTimeSetMod(float mod)
{
  gameTimeResetMod();
  modifier = mod;
}

// get the current time modifier
void gameTimeGetMod(float* pMod) { *pMod = modifier; }

BOOL gameTimeIsStopped(void) { return (stopCount != 0); }

/* Call this to stop the game timer */
void gameTimeStop(void)
{
  if (stopCount == 0)
  {
    pauseStart = GetTickCount();
  }
  stopCount += 1;
}

/* Call this to restart the game timer after a call to gameTimeStop */
void gameTimeStart(void)
{
  if (stopCount == 1)
  {
    // shift the base time to now
    timeOffset = gameTime;
    baseTime = GetTickCount();
  }

  if (stopCount > 0)
    stopCount--;
}

/*Call this to reset the game timer*/
void gameTimeReset(UDWORD time)
{
  // reset the game timers
  gameTime = time;
  timeOffset = time;

  gameTime2 = time;
  timeOffset2 = time;

  baseTime = GetTickCount(); //used from save game only so GetTickCount is as valid as anything
  baseTime2 = GetTickCount();

  modifier = 1.0f;
}

void getTimeComponents(UDWORD time, UDWORD* hours, UDWORD* minutes, UDWORD* seconds)
{
  UDWORD h, m, s;
  UDWORD tph, tpm;

  /* Ticks in a minute */
  tpm = GAME_TICKS_PER_SEC * 60;

  /* Ticks in an hour */
  tph = tpm * 60;

  h = time / tph;

  m = (time - (h * tph)) / tpm;

  s = (time - ((h * tph) + (m * tpm))) / GAME_TICKS_PER_SEC;

  *hours = h;
  *minutes = m;
  *seconds = s;
}

/************************************************************************************
 *
 * The frame counters.
 *
 * These were Frame.cpp's, but the window and message pump they sat beside are
 * NeuronClient's now and the counters are not: NetSupp stamps every network log
 * entry with frameGetFrameNumber, and a dedicated server has a frame number
 * without having a window. The values and the point they advance are unchanged
 * - Window.cpp's frameUpdate calls gtimeFrameCountUpdate where it used to call
 * the static MaintainFrameStuff.
 */
/* Over how many seconds is the average required? */
#define	TIMESPAN	5

/* Initial filler value for the averages - arbitrary */
#define  IN_A_FRAME 70

/* Global variables for the frame rate stuff */
static SDWORD FrameCounts[TIMESPAN];
static SDWORD Frames; // Number of frames elapsed since start
static SDWORD LastFrames;
static SDWORD RecentFrames; // Number of frames in last second
static SDWORD PresSeconds; // Number of seconds since execution started
static SDWORD LastSeconds;
static SDWORD FrameRate; // Average frame rate since start
static SDWORD FrameIndex;
static SDWORD Total;
static SDWORD RecentAverage; // Average frame rate over last TIMSPAN seconds

/* InitFrameStuff - needs to be called once before frame loop commences */
void gtimeFrameCountInit(void)
{
  for (SDWORD i = 0; i < TIMESPAN; i++)
    FrameCounts[i] = IN_A_FRAME;

  Frames = 0;
  LastFrames = 0;
  RecentFrames = 0;
  RecentAverage = 0;
  PresSeconds = 0;
  LastSeconds = 0;
  LastSeconds = PresSeconds;
  FrameIndex = 0;
}

/* MaintainFrameStuff - call this during completion of each frame loop */
void gtimeFrameCountUpdate(void)
{
  PresSeconds = clock() / CLOCKS_PER_SEC;
  if (PresSeconds != LastSeconds)
  {
    LastSeconds = PresSeconds;
    RecentFrames = Frames - LastFrames;
    LastFrames = Frames;
    FrameCounts[FrameIndex++] = RecentFrames;
    if (FrameIndex >= TIMESPAN)
      FrameIndex = 0;

    Total = 0;
    for (SDWORD i = 0; i < TIMESPAN; i++)
      Total += FrameCounts[i];
    RecentAverage = Total / TIMESPAN;

    FrameRate = Frames / PresSeconds;
  }
  Frames++;
}

/* Return the current frame rate */
UDWORD frameGetFrameRate(void) { return RecentAverage; }

/* Return the overall frame rate */
UDWORD frameGetOverallRate(void) { return FrameRate; }

/* Return the frame rate for the last second */
UDWORD frameGetRecentRate(void) { return RecentFrames; }

UDWORD frameGetFrameNumber(void) { return Frames; }
