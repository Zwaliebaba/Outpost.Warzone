#include "pch.h"
/*
 * Wrappers.c   
 * frontend loop & also loading screen & game over screen. 
 * AlexL. Pumpkin Studios, EIDOS Interactive, 1997 
 */

#include <stdio.h>

#include "Frame.h"
#include "Window.h"
#include "FrameResource.h"
#include "Screen.h"
#include "Input.h"
#include "RendMode.h"
#include "BitImage.h"
#include "PieState.h"
#include "TextDraw.h" //ivis text code

#include "PieMode.h"
#include "RenderMatrix.h"
#include "PieFunc.h"

#include "HCI.h"		// access to widget screen.
#include "Widget.h"
#include "Wrappers.h"
#include "WinMain.h"
#include "Objects.h"
#include "Display.h"
#include "Display3D.h"
#include "FrontEnd.h"
#include "Frend.h"		// display logo.
#include "Console.h"
#include "IntImage.h"
#include "Text.h"
#include "IntDisplay.h"	//for shutdown
#include "AudioSystem.h"
#include "AudioID.h"
#include "GTime.h"
#include "InGameOp.h"
#include "KeyMap.h"
#include "Mission.h"

#include "KeyEdit.h"
#include "SeqDisp.h"
#include "Config.h"
#include "resource.h"
#include "NetPlay.h"	// multiplayer 
#include "MultiPlay.h"
#include "MultiInt.h"
#include "MultiStat.h"
#include "MultiLimit.h"

extern void frontEndCheckCD(tMode tModeNext);

using STAR = struct _star
{
  UWORD xPos;
  SDWORD speed;
};

STAR stars[30]; // quick hack for loading stuff

#define LOADBARY	460		// position of load bar.
#define LOADBARY2	470
#define LOAD_BOX_SHADES	6

extern IMAGEFILE* FrontImages;
extern int WFont;
extern BOOL bRequesterUp;

static BOOL firstcall;
static UDWORD loadScreenCallNo = 0;
static BOOL bPlayerHasLost = FALSE;
static BOOL bPlayerHasWon = FALSE;
static UBYTE scriptWinLoseVideo = PLAY_NONE;

void startCreditsScreen(BOOL bRenderActive);
void runCreditsScreen(void);
UDWORD lastTick = 0;

void initStars(void)
{
  UDWORD i;
  for (i = 0; i < 20; i++)
  {
    stars[i].xPos = static_cast<UWORD>(rand() % 598); // scatter them
    stars[i].speed = rand() % 10 + 2; // always move
  }
}

// //////////////////////////////////////////////////////////////////
// Initialise frontend globals and statics.
//
BOOL frontendInitVars(void)
{
  firstcall = TRUE;
  initStars();

  return TRUE;
}

// //////////////////////////////////////////////////////////////////
// play the intro if first EVER boot.
BOOL playIntroOnInstall(VOID)
{
  DWORD result;

  if (!openWarzoneKey())
    return FALSE;

  if (!getWarzoneKeyNumeric("nointro", &result))
  {
    setWarzoneKeyNumeric("nointro", 1);
    closeWarzoneKey();

    frontEndCheckCD(SHOWINTRO);
    return TRUE;
  }
  closeWarzoneKey();
  return FALSE;
}

// ///////////////// /////////////////////////////////////////////////
// Main Front end game loop.
TITLECODE titleLoop(void)
{
  TITLECODE RetCode = TITLECODE_CONTINUE;

  pie_GlobalRenderBegin();
  pie_SetDepthBufferStatus(DEPTH_CMP_ALWAYS_WRT_ON);
  pie_SetFogStatus(FALSE);
  screen_RestartBackDrop();

  if (firstcall)
  {
    if (playIntroOnInstall() == FALSE)
    {
      startTitleMenu();
      titleMode = TITLE;
    }

    firstcall = FALSE;


    frameSetCursorFromRes(IDC_DEFAULT); // reset cursor	(sw)

    if (gameSpy.bGameSpy)
    {
      // set host
      if (NetPlay.bHost)
        ingame.bHostSetup = TRUE;
      else
        ingame.bHostSetup = FALSE;
      // set protocol
      // set address
      // if host goto options.
      // if client goto game find.
      if (NetPlay.bHost)
        changeTitleMode(MULTIOPTION);
      else
        changeTitleMode(GAMEFIND);
    }
  }

  switch (titleMode) // run relevant title screen code.
  {
  case PROTOCOL:
    runConnectionScreen(); // multiplayer connection screen.
    break;
  case MULTIOPTION:
    runMultiOptions();
    break;
  case GAMEFIND:
    runGameFind();
    break;
  case FORCESELECT:
    runForceSelect();
    break;
  case MULTI:
    runMultiPlayerMenu();
    break;
  case MULTILIMIT:
    runLimitScreen();
    break;
  case KEYMAP:
    runKeyMapEditor();
    break;

  case TITLE:
    runTitleMenu();
    break;

  case SINGLE:
    runSinglePlayerMenu();
    break;

  case TUTORIAL:
    runTutorialMenu();
    break;

  //		case GRAPHICS:

  case CREDITS:
    runCreditsScreen();
    break;
  //	case VIDEO:
  case OPTIONS:
    runOptionsMenu();
    break;

  case GAME:
    runGameOptionsMenu();
    break;

  case GAME2:
    runGameOptions2Menu();
    break;

  case QUIT:
    RetCode = TITLECODE_QUITGAME;
    break;

  case STARTGAME:
    initLoadingScreen(TRUE,TRUE); //render active
    RetCode = TITLECODE_STARTGAME;
    pie_GlobalRenderEnd(TRUE); //force to black
    return RetCode; // don't flip!
    break;

  case SHOWINTRO:
    pie_SetFogStatus(FALSE);
    pie_ScreenFlip(CLEAR_BLACK); //flip to clear screen but not here//reshow intro video.
    pie_ScreenFlip(CLEAR_BLACK); //flip to clear screen but not here
    changeTitleMode(TITLE);
    RetCode = TITLECODE_SHOWINTRO;
    break;

  default: Neuron::Fatal("unknown title screen mode ");
  }

  AudioSystem::Update();

  pie_GlobalRenderEnd(TRUE); //force to black
  pie_SetFogStatus(FALSE);
  pie_ScreenFlip(CLEAR_BLACK); //title loop

  if ((keyDown(KEY_LALT) || keyDown(KEY_RALT)) && /* Check for toggling display mode */
    keyPressed(KEY_RETURN))
    screenToggleMode();

  return RetCode;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// Loading Screen.

//loadbar update
void loadingScreenCallback(void)
{
  UDWORD i;
  UDWORD topX, topY, botX, botY;
  UDWORD currTick;

  if (GetTickCount() - lastTick < 16) // cos 1000/60Hz gives roughly 16...:-)
    return;
  currTick = GetTickCount();
  if ((currTick - lastTick) > 500)
  {
    currTick -= lastTick;
    Neuron::DebugTrace("loadingScreenCallback: pause {}\n", currTick);
  }
  lastTick = GetTickCount();
  pie_GlobalRenderBegin();
  DrawBegin();
  pie_UniTransBoxFill(1, 1, 2, 2, 0x00010101, 32);
  /* Draw the black rectangle at the bottom */

  topX = 10 + D_W;
  topY = 450 + D_H - 1;
  botX = 630 + D_W;
  botY = 470 + D_H + 1;
  pie_UniTransBoxFill(topX, topY, botX, botY, 0x00010101, 24);

  for (i = 1; i < 19; i++)
  {
    if (stars[i].xPos + stars[i].speed >= 598)
      stars[i].xPos = 1;
    else
      stars[i].xPos = static_cast<UWORD>(stars[i].xPos + stars[i].speed);
    pie_UniTransBoxFill(10 + stars[i].xPos + D_W, 450 + i + D_H, 10 + stars[i].xPos + (2 * stars[i].speed) + D_W, 450 + i + 2 + D_H,
                        0x00ffffff, 255);
  }

  DrawEnd();
  pie_GlobalRenderEnd(TRUE); //force to black
  pie_ScreenFlip(CLEAR_OFF_AND_NO_BUFFER_DOWNLOAD); //loading callback		// dont clear.
}

// fill buffers with the static screen
void initLoadingScreen(BOOL drawbdrop, BOOL bRenderActive)
{
  if (!drawbdrop) // fill buffers
  {
    //just init the load bar with the current screen
    // setup the callback....
    pie_SetFogStatus(FALSE);
    pie_ScreenFlip(CLEAR_BLACK); //init loading
    pie_ScreenFlip(CLEAR_BLACK); //init loading
    resSetLoadCallback(loadingScreenCallback);
    loadScreenCallNo = 0;
    return;
  }
  if (bRenderActive)
    pie_GlobalRenderEnd(TRUE); //force to black

  pie_ResetBackDrop();

  pie_SetFogStatus(FALSE);
  pie_ScreenFlip(CLEAR_BLACK); //init loading
  pie_ScreenFlip(CLEAR_BLACK); //init loading

  // setup the callback....
  resSetLoadCallback(loadingScreenCallback);
  loadScreenCallNo = 0;

  screen_StopBackDrop();

  if (bRenderActive)
    pie_GlobalRenderBegin();
}

UDWORD lastChange = 0;

// fill buffers with the static screen
void startCreditsScreen(BOOL bRenderActive)
{
  SCREENTYPE screen = SCREEN_CREDITS;

  lastChange = gameTime;
  // fill buffers
  pie_LoadBackDrop(screen,FALSE);

  if (bRenderActive)
    pie_GlobalRenderEnd(TRUE); //force to black

  pie_SetFogStatus(FALSE);
  pie_ScreenFlip(CLEAR_BLACK); //flip to set back buffer
  pie_ScreenFlip(CLEAR_BLACK); //init loading

  if (bRenderActive)
    pie_GlobalRenderBegin();
}

/* This function does nothing - since it's already been drawn */
void runCreditsScreen(void)
{
  static UBYTE quitstage = 0;
  // Check for key presses now.

  if (keyReleased(KEY_ESC) || keyReleased(KEY_SPACE) || mouseReleased(MOUSE_LMB) || (gameTime - lastChange > 4000))
  {
    lastChange = gameTime;
    changeTitleMode(QUIT);
  }
}

// shut down the loading screen
void closeLoadingScreen(void)
{
  resSetLoadCallback(nullptr);
  loadScreenCallNo = 0;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// Gameover screen.

BOOL displayGameOver(BOOL bDidit)
{
  // AlexL says take this out......

  //set this for debug mode too - what gets displayed then depends on whether we 
  //have won or lost and if we are in debug mode

  // this bit decides whether to auto quit to front end.
  //if(!getDebugMappingStatus())
  {
    if (bDidit)
      setPlayerHasWon(TRUE); // quit to frontend..
    else
      setPlayerHasLost(TRUE);
  }

  if (bMultiPlayer)
  {
    if (bDidit)
    {
      updateMultiStatsWins();
      multiplayerWinSequence(TRUE);
    }
    else
      updateMultiStatsLoses();
  }

  //	if(getDebugMappingStatus()) 
  else
  {
    //clear out any mission widgets - timers etc that may be on the screen
    clearMissionWidgets();
    intAddMissionResult(bDidit, TRUE);
  }

  return TRUE;
}

////////////////////////////////////////////////////////////////////////////////
BOOL testPlayerHasLost(void) { return (bPlayerHasLost); }

void setPlayerHasLost(BOOL val) { bPlayerHasLost = val; }

////////////////////////////////////////////////////////////////////////////////
BOOL testPlayerHasWon(void) { return (bPlayerHasWon); }

void setPlayerHasWon(BOOL val) { bPlayerHasWon = val; }

/*access functions for scriptWinLoseVideo - used to indicate when the script is playing the win/lose video*/
void setScriptWinLoseVideo(UBYTE val) { scriptWinLoseVideo = val; }

UBYTE getScriptWinLoseVideo(void) { return scriptWinLoseVideo; }
