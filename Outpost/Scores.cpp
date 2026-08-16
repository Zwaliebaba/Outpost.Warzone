#include "pch.h"
/* 
	Scores.c Deals with all the mission results gubbins.
	Alex W. McLean
*/

// --------------------------------------------------------------------
#include "Frame.h"
#include "GTime.h"
#include "Console.h"
#include "Scores.h"
#include "PieFunc.h"
#include "PieMode.h"
#include "PieState.h"
#include "RendMode.h"
#include "Objects.h"
#include "DroidDef.h"
#include "Base.h"
#include "StatsDef.h"
#include "HCI.h"
#include "Text.h"
#include "MiscIMD.h"
#include "Geo.h"
#include "Display3D.h"
#include "Mission.h"
#include "Game.h"
#include "AudioSystem.h"
#include "AudioID.h"
#include "IntImage.h"

#define	BAR_CRAWL_TIME	(GAME_TICKS_PER_SEC*3)

#define	MT_X_POS	(MISSIONRES_TITLE_X  + D_W + 140)
#define MT_Y_POS	(MISSIONRES_TITLE_Y  + D_H + 80)

#define DROID_LEVELS	9
#define MAX_BAR_LENGTH	100
#define LC_UPPER	100

#define LC_X	32
#define RC_X	320+32
#define	RANK_BAR_WIDTH	100
#define STAT_BAR_WIDTH	100
STAT_BAR infoBars[] = {
  {LC_X, 100,STAT_BAR_WIDTH, 16, 10, STR_MR_UNITS_LOST, 0,FALSE,TRUE, 0, 165}, // left column		STAT_UNIT_LOST       
  {LC_X, 120,STAT_BAR_WIDTH, 16, 20, STR_MR_UNITS_KILLED, 0,FALSE,TRUE, 0, 81}, //	STAT_UNIT_KILLED         
  {LC_X, 160,STAT_BAR_WIDTH, 16, 30, STR_MR_STR_LOST, 0,FALSE,TRUE, 0, 165}, //	STAT_STR_LOST            
  {LC_X, 180,STAT_BAR_WIDTH, 16, 40, STR_MR_STR_BLOWN_UP, 0,FALSE,TRUE, 0, 81}, //	STAT_STR_BLOWN_UP        
  {LC_X, 220,STAT_BAR_WIDTH, 16, 50, STR_MR_UNITS_BUILT, 0,FALSE,TRUE, 0, 185}, //	STAT_UNITS_BUILT         
  {LC_X, 240,STAT_BAR_WIDTH, 16, 60, STR_MR_UNITS_NOW, 0,FALSE,TRUE, 0, 185}, //	STAT_UNITS_NOW           
  {LC_X, 260,STAT_BAR_WIDTH, 16, 70, STR_MR_STR_BUILT, 0,FALSE,TRUE, 0, 185}, //	STAT_STR_BUILT           
  {LC_X, 280,STAT_BAR_WIDTH, 16, 80, STR_MR_STR_NOW, 0,FALSE,FALSE, 0, 185}, //	STAT_STR_NOW             

  {RC_X, 100,RANK_BAR_WIDTH, 16, 10, STR_MR_LEVEL_ROOKIE, 0,FALSE,TRUE, 0, 117}, // right column	//	STAT_ROOKIE      
  {RC_X, 120,RANK_BAR_WIDTH, 16, 20, STR_MR_LEVEL_GREEN, 0,FALSE,TRUE, 0, 117}, //	STAT_GREEN       
  {RC_X, 140,RANK_BAR_WIDTH, 16, 30, STR_MR_LEVEL_TRAINED, 0,FALSE,TRUE, 0, 117}, //	STAT_TRAINED 
  {RC_X, 160,RANK_BAR_WIDTH, 16, 40, STR_MR_LEVEL_REGULAR, 0,FALSE,TRUE, 0, 117}, //	STAT_REGULAR 
  {RC_X, 180,RANK_BAR_WIDTH, 16, 50, STR_MR_LEVEL_VETERAN, 0,FALSE,TRUE, 0, 117}, //	STAT_VETERAN 
  {RC_X, 200,RANK_BAR_WIDTH, 16, 60, STR_MR_LEVEL_CRACK, 0,FALSE,TRUE, 0, 117}, //	STAT_CRACK       
  {RC_X, 220,RANK_BAR_WIDTH, 16, 70, STR_MR_LEVEL_ELITE, 0,FALSE,TRUE, 0, 117}, //	STAT_ELITE       
  {RC_X, 240,RANK_BAR_WIDTH, 16, 80, STR_MR_LEVEL_SPECIAL, 0,FALSE,TRUE, 0, 117}, //	STAT_SPECIAL 
  {RC_X, 260,RANK_BAR_WIDTH, 16, 90, STR_MR_LEVEL_ACE, 0,FALSE,TRUE, 0, 117}, //	STAT_ACE     

  {0, 0, 0, 0, 0, 0, 0}
};

// --------------------------------------------------------------------
void constructTime(STRING* psText, UDWORD hours, UDWORD minutes, UDWORD seconds);
void drawDroidBars(void);
void drawUnitBars(void);
void drawStatBars(void);
void fillUpStats(void);
void dispAdditionalInfo(void);
// --------------------------------------------------------------------

/* The present mission data */
static MISSION_DATA missionData;
static UDWORD numUnits;
static UDWORD numStrs;
static UDWORD dispST;
static BOOL bDispStarted = FALSE;
static char text[255];
static char text2[255];

// --------------------------------------------------------------------
/* Initialise the mission data info - done before each mission */
BOOL scoreInitSystem(void)
{
  missionData.unitsBuilt = 0;
  missionData.unitsKilled = 0;
  missionData.unitsLost = 0;

  missionData.strBuilt = 0;
  missionData.strKilled = 0;
  missionData.strLost = 0;

  missionData.artefactsFound = 0;
  missionData.missionStarted = gameTime; // total game time is just gameTime
  missionData.shotsOnTarget = 0;
  missionData.shotsOffTarget = 0;
  missionData.babasMowedDown = 0;
  bDispStarted = FALSE;
  return (TRUE);
}

// --------------------------------------------------------------------
// Updates a game statistic - more can be added if we need 'em
void scoreUpdateVar(DATA_INDEX var)
{
  switch (var)
  {
  case WD_UNITS_BUILT:
    missionData.unitsBuilt++; // We've built another unit
    break;
  case WD_UNITS_KILLED:
    missionData.unitsKilled++; // We've destroyed an enemy unit
    break;
  case WD_UNITS_LOST:
    missionData.unitsLost++; // We've lost a unit
    break;
  case WD_STR_BUILT:
    missionData.strBuilt++; // Built a structure
    break;
  case WD_STR_KILLED:
    missionData.strKilled++; // Destroyed an enemy structure
    break;
  case WD_STR_LOST:
    missionData.strLost++; // Lost a structure
    break;
  case WD_ARTEFACTS_FOUND:
    missionData.artefactsFound++; // Got an artefact
    break;
  case WD_MISSION_STARTED:
    missionData.missionStarted = gameTime; // Init the mission start time
    break; // Should be called once per mission
  case WD_SHOTS_ON_TARGET:
    missionData.shotsOnTarget++; // We hit something
    break;
  case WD_SHOTS_OFF_TARGET:
    missionData.shotsOffTarget++; // Missed something
    break;
  case WD_BARBARIANS_MOWED_DOWN:
    missionData.babasMowedDown++; // Ran over a barbarian
    break;
  default: Neuron::Fatal("Weirdy variable request from scoreUpdateVar");
    break;
  }
}

// --------------------------------------------------------------------
void scoreDataToScreen(void) { drawStatBars(); }

// --------------------------------------------------------------------
/* Builds an ascii string for the passed in components 04:02:23 for example */
void constructTime(STRING* psText, UDWORD hours, UDWORD minutes, UDWORD seconds)
{
  UDWORD index = 0;
  // Hours do not have trailing zeros
  if (hours != 0)
  {
    if (hours < 10)
    {
      // Less than 10 hours
      psText[index++] = static_cast<UBYTE>('0' + hours % 10);
    }
    else if (hours < 100)
    {
      // Over ten hours
      psText[index++] = static_cast<UBYTE>('0' + hours / 10);
      psText[index++] = static_cast<UBYTE>('0' + hours % 10);
    }
    else
    {
      // Over 100 hours - go outside people!!!!
      // build hours
      psText[index++] = static_cast<UBYTE>('0' + (hours / 100)); // hmmmmmm....
      UDWORD div = hours / 100;
      psText[index++] = static_cast<UBYTE>('0' + (hours - (div * 100)) / 10); // nice
      psText[index++] = static_cast<UBYTE>('0' + hours % 10);
    }
    // Put in the hrs/mins separator - only for non-zero hours
    psText[index++] = static_cast<UBYTE>(':');
  }

  // put in the minutes
  psText[index++] = static_cast<UBYTE>('0' + minutes / 10);
  psText[index++] = static_cast<UBYTE>('0' + minutes % 10);

  // mins/secs separator
  psText[index++] = static_cast<UBYTE>(':');

  // Put in the seconds
  psText[index++] = static_cast<UBYTE>('0' + seconds / 10);
  psText[index++] = static_cast<UBYTE>('0' + seconds % 10);

  // terminate the string
  psText[index] = '\0';
}

// --------------------------------------------------------------------
/* Builds an ascii string for the passed in time */
void getAsciiTime(STRING* psText, UDWORD time)
{
  UDWORD hours, minutes, seconds;

  getTimeComponents(time, &hours, &minutes, &seconds);
  constructTime(psText, hours, minutes, seconds);
}

// -----------------------------------------------------------------------------------
void drawStatBars(void)
{
  if (!bDispStarted)
  {
    bDispStarted = TRUE;
    dispST = gameTime2;
    AudioSystem::PlayTrack(ID_SOUND_BUTTON_CLICK_5);
  }

  fillUpStats();

  pie_UniTransBoxFill(16 + D_W,MT_Y_POS - 16,DISP_WIDTH - D_W - 16,MT_Y_POS + 256, 0x00000088, 128);
  pie_Box(16 + D_W,MT_Y_POS - 16,DISP_WIDTH - D_W - 16,MT_Y_POS + 256, 1);

  pie_DrawText((unsigned char*)strresGetString(psStringRes, STR_MR_UNIT_LOSSES),LC_X + D_W, 80 + 16 + D_H);
  pie_DrawText((unsigned char*)strresGetString(psStringRes, STR_MR_STRUCTURE_LOSSES),LC_X + D_W, 140 + 16 + D_H);
  pie_DrawText((unsigned char*)strresGetString(psStringRes, STR_MR_FORCE_INFO),LC_X + D_W, 200 + 16 + D_H);

  UDWORD index = 0;
  BOOL bMoreBars = TRUE;
  while (bMoreBars)
  {
    /* Is it time to display this bar? */
    if (infoBars[index].bActive)
    {
      /* Has it been queued before? */
      if (infoBars[index].bQueued == FALSE)
      {
        /* Don't do this next time...! */
        infoBars[index].bQueued = TRUE;

        /* Play a sound */
      }
      UDWORD x = infoBars[index].topX + D_W;
      UDWORD y = infoBars[index].topY + D_H;
      UDWORD width = infoBars[index].width;
      UDWORD height = infoBars[index].height;

      pie_Box(x, y, x + width, y + height, 0);

      /* Draw the background border box */
      pie_BoxFillIndex(x - 1, y - 1, x + width + 1, y + height + 1, 1);

      /* Draw the interior grey */
      pie_BoxFillIndex(x, y, x + width, y + height, 222);

      if (((gameTime2 - dispST) > infoBars[index].queTime))
      {
        /* Now draw amount filled */
        float length = static_cast<float>(infoBars[index].percent) / 100.0f;
        length = length * static_cast<float>(infoBars[index].width);
        UDWORD div = PERCENT(gameTime2-dispST, BAR_CRAWL_TIME);
        if (div > 100)
          div = 100;
        float mul = static_cast<float>(div) / 100;
        length = length * mul;
        if (std::lrintf(length) > 4)
        {
          /* Black shadow */
          pie_BoxFillIndex(x + 1, y + 3, x + std::lrintf(length) - 1, y + height - 1, 1);
          /* Solid coloured bit */
          pie_BoxFillIndex(x + 1, y + 2, x + std::lrintf(length) - 4, y + height - 4, static_cast<UBYTE>(infoBars[index].colour));
        }
      }
      /* Now render the text by the bar */
      sprintf(text, strresGetString(psStringRes, infoBars[index].stringID), infoBars[index].number);
      pie_DrawText((unsigned char*)text, x + width + 16, y + 12);

      /* If we're beyond STAT_ROOKIE, then we're on rankings */
      if (index >= STAT_GREEN AND index <= STAT_ACE)
        pie_ImageFileID(IntImages, static_cast<UWORD>(IMAGE_LEV_0 + (index - STAT_GREEN)), x - 8, y + 2);
    }
    /* Move onto the next bar */
    index++;
    if (infoBars[index].topX == 0 AND infoBars[index].topY == 0)
      bMoreBars = FALSE;
  }
  dispAdditionalInfo();
}

// -----------------------------------------------------------------------------------
void dispAdditionalInfo(void)
{
  /* We now need to dsiplay the mission time, game time, 
    average unit experience level an number of artefacts found */

  /* Firstly, top of the screen, number of artefacts found */
  sprintf(text, strresGetString(psStringRes, STR_MR_ARTEFACTS_FOUND), missionData.artefactsFound);
  pie_DrawText((unsigned char*)text, (DISP_WIDTH - Neuron::GetTextWidth((unsigned char*)text)) / 2, 300 + D_H);

  /* Get the mission result time in a string - and write it out */
  getAsciiTime((char*)&text2, gameTime - missionData.missionStarted);
  sprintf(text, strresGetString(psStringRes, STR_MR_MISSION_TIME), text2);
  pie_DrawText((unsigned char*)text, (DISP_WIDTH - Neuron::GetTextWidth((unsigned char*)text)) / 2, 320 + D_H);

  /* Write out total game time so far */
  getAsciiTime((char*)&text2, gameTime);
  sprintf(text, strresGetString(psStringRes, STR_MR_GAME_TIME), text2);
  pie_DrawText((unsigned char*)text, (DISP_WIDTH - Neuron::GetTextWidth((unsigned char*)text)) / 2, 340 + D_H);
}

// -----------------------------------------------------------------------------------
void fillUpStats(void)
{
  UDWORD i;
  UDWORD maxi;
  float scaleFactor;
  UDWORD length;
  UDWORD numUnits;
  DROID* psDroid;

  /* Do rankings first cos they're easier */
  for (i = 0, maxi = 0; i < DROID_LEVELS; i++)
  {
    UDWORD num = getNumDroidsForLevel(i);
    if (num > maxi)
      maxi = num;
  }

  /* Make sure we got something */
  if (maxi == 0)
    scaleFactor = 0.0f;
  else
    scaleFactor = (static_cast<float>(RANK_BAR_WIDTH) / maxi);

  /* Scale for percent */
  for (i = 0; i < DROID_LEVELS; i++)
  {
    length = std::lrintf(scaleFactor * getNumDroidsForLevel(i));
    infoBars[STAT_ROOKIE + i].percent = PERCENT(length, RANK_BAR_WIDTH);
    infoBars[STAT_ROOKIE + i].number = getNumDroidsForLevel(i);
  }

  /* Now do the other stuff... */
  /* Units killed and lost... */
  maxi = std::max(missionData.unitsLost, missionData.unitsKilled);
  if (maxi == 0)
    scaleFactor = 0;
  else
    scaleFactor = (static_cast<float>(STAT_BAR_WIDTH) / maxi);

  length = std::lrintf(scaleFactor * missionData.unitsLost);
  infoBars[STAT_UNIT_LOST].percent = PERCENT(length, STAT_BAR_WIDTH);
  length = std::lrintf(scaleFactor * missionData.unitsKilled);
  infoBars[STAT_UNIT_KILLED].percent = PERCENT(length, STAT_BAR_WIDTH);

  /* Now do the structure losses */
  maxi = std::max(missionData.strLost, missionData.strKilled);
  if (maxi == 0)
    scaleFactor = 0;
  else
    scaleFactor = (static_cast<float>(STAT_BAR_WIDTH) / maxi);

  length = std::lrintf(scaleFactor * missionData.strLost);
  infoBars[STAT_STR_LOST].percent = PERCENT(length, STAT_BAR_WIDTH);
  length = std::lrintf(scaleFactor * missionData.strKilled);
  infoBars[STAT_STR_BLOWN_UP].percent = PERCENT(length, STAT_BAR_WIDTH);

  /* Finally the force information - need amount of droids as well*/
  for (psDroid = apsDroidLists[selectedPlayer], numUnits = 0; psDroid; psDroid = psDroid->psNext, numUnits++);

  for (psDroid = mission.apsDroidLists[selectedPlayer]; psDroid; psDroid = psDroid->psNext, numUnits++);

  maxi = std::max(missionData.unitsBuilt, missionData.strBuilt);
  maxi = std::max(maxi, numUnits);

  if (maxi == 0)
    scaleFactor = 0;
  else
    scaleFactor = (static_cast<float>(STAT_BAR_WIDTH) / maxi);

  length = std::lrintf(scaleFactor * missionData.unitsBuilt);
  infoBars[STAT_UNITS_BUILT].percent = PERCENT(length, STAT_BAR_WIDTH);
  length = std::lrintf(scaleFactor * numUnits);
  infoBars[STAT_UNITS_NOW].percent = PERCENT(length, STAT_BAR_WIDTH);
  length = std::lrintf(scaleFactor * missionData.strBuilt);
  infoBars[STAT_STR_BUILT].percent = PERCENT(length, STAT_BAR_WIDTH);

  /* Finally the numbers themselves */
  infoBars[STAT_UNIT_LOST].number = missionData.unitsLost;
  infoBars[STAT_UNIT_KILLED].number = missionData.unitsKilled;
  infoBars[STAT_STR_LOST].number = missionData.strLost;
  infoBars[STAT_STR_BLOWN_UP].number = missionData.strKilled;
  infoBars[STAT_UNITS_BUILT].number = missionData.unitsBuilt;
  infoBars[STAT_UNITS_NOW].number = numUnits;
  infoBars[STAT_STR_BUILT].number = missionData.strBuilt;
}

// -----------------------------------------------------------------------------------
