#include "pch.h"
/*
 * WinMain.c
 *
 */
#include <direct.h>
#include "Frame.h"
#include "Widget.h"
#include "Script.h"
#include "Init.h"
#include "Loop.h"
#include "Objects.h"
#include "Display.h"
#include "PieState.h"
#include "GTime.h"
#include "WinMain.h"
#include "Wrappers.h"
#include "ScriptTabs.h"
#include "Deliverance.h"
#include "FrontEnd.h"
#include "SeqDisp.h"
#include "Audio.h"
#include "Console.h"
#include "RendMode.h"
#include "PieMode.h"
#include "Levels.h"
#include "Research.h"
#include "WarzoneConfig.h"
#include "ClParse.h"
#include "Config.h"
#include "MultiPlay.h"
#include "NetPlay.h"
#include "LoadSave.h"
#include "Render.h"
#include "TexMan.h"
#include "Game.h"
#include "Lighting.h"
#include "WDG.h"
#include "MultiWDG.h"
#include "Palette.h"

// Warzone 2100 . Pumpkin Studios

// Quick Note on defines
//
// covermount		- Single Player Demo
//		noninteract	- incomplete. used with covermount to stop player input
//		multidemo	- used with covermount to make a multiplayer demo.
//

UDWORD gameStatus = GS_TITLE_SCREEN; // Start game in title mode.
UDWORD lastStatus = GS_TITLE_SCREEN;
//flag to indicate when initialisation is complete
BOOL videoInitialised = FALSE;
BOOL gameInitialised = FALSE;
BOOL frontendInitialised = FALSE;
BOOL reInit = FALSE;
BOOL bDisableLobby;

/*
BOOL checkDisableLobby(void)
{
	BOOL	disable;

	disable = FALSE;
	if(!openWarzoneKey())
	{
		return FALSE;
	}

	if (!getWarzoneKeyNumeric("DisableLobby",(DWORD*)&disable))
	{
		return FALSE;
	}

	if (!closeWarzoneKey())
	{
		return FALSE;
	}

	return disable;
}
*/

int WINAPI WinMain(HINSTANCE hInstance, // handle to current instance
                   HINSTANCE hPrevInstance, // handle to previous instance
                   LPSTR lpCmdLine, // pointer to command line
                   int nShowCmd) // show state of window
{
  FRAME_STATUS frameRet;
  BOOL quit = FALSE;
  BOOL Restart = FALSE;
  BOOL paused = FALSE; //, firstTime = TRUE;
  BOOL bVidMem = FALSE;
  SDWORD dispBitDepth = DISP_BITDEPTH;
  SDWORD introVideoControl = 3;
  GAMECODE loopStatus;
  iColour* psPaletteBuffer;
  SDWORD pSize;

  (void)nShowCmd;
  (void)hPrevInstance;

  war_SetDefaultStates();

init: //jump here from the end if re_initialising

  loadRenderMode(); //get the registry entry for clRendMode

  bDisableLobby = FALSE;

  // parse the command line
  if (!reInit)
  {
    if (!ParseCommandLine(lpCmdLine))
      return -1;
  }

  // find out if the lobby stuff has been disabled
  if (!bDisableLobby && !lobbyInitialise()) // ajl. Init net stuff. Lobby can modify startup conditions like commandline.
    return -1;

  reInit = FALSE; //just so we dont restart again

#ifdef USE_FILE_PATH
  _chdir(FILE_PATH);
#endif

  //always start windowed toggle to fullscreen later
  bVidMem = TRUE;
  dispBitDepth = DISP_HARDBITDEPTH;

  if (!frameInitialise(hInstance, "Warzone 2100", DISP_WIDTH,DISP_HEIGHT, dispBitDepth, !clStartWindowed, bVidMem))
    return -1;
  if (!wdgLoadAllWDGCatalogs())
    return -1;

  pie_SetFogStatus(FALSE);
  pie_ScreenFlip(CLEAR_BLACK);
  pie_ScreenFlip(CLEAR_BLACK);

  if (gameStatus == GS_VIDEO_MODE)
  {
    introVideoControl = 0; //play video
    gameStatus = GS_TITLE_SCREEN;
  }

  //load palette
  // palette.bin is 256 entries plus one trailing byte
  psPaletteBuffer = new (std::nothrow) iColour[257];
  if (psPaletteBuffer == nullptr)
  {
    Neuron::Fatal("Out of memory");
    return -1;
  }
  if (!loadFileToBuffer("palette.bin", (UBYTE*)psPaletteBuffer, (256 * sizeof(iColour) + 1), (UDWORD*)&pSize))
  {
    Neuron::Fatal("Couldn't load palette data");
    return -1;
  }
  pal_AddNewPalette(psPaletteBuffer);
  delete[] psPaletteBuffer;
  psPaletteBuffer = nullptr;

#ifdef COVERMOUNT
  pie_LoadBackDrop(SCREEN_COVERMOUNT,FALSE);
#else
  pie_LoadBackDrop(SCREEN_RANDOMBDROP,FALSE);
#endif
  pie_SetFogStatus(FALSE);
  pie_ScreenFlip(CLEAR_BLACK);

  quit = FALSE;

  if (!systemInitialise())
    return -1;

  // If windowed mode not requested then toggle to full screen. Doing
  // it here rather than in the call to frameInitialise fixes a problem
  // where machines with an NVidia and a 3DFX would kill the 3dfx display. (Definitly a HACK, PD)
  //set all the pause states to false
  setAllPauseStates(FALSE);

  while (!quit)
  {
    // Do the game mode specific initialisation.
    switch (gameStatus)
    {
    case GS_TITLE_SCREEN:
      screen_RestartBackDrop();

      if (!frontendInitialise("wrf\\frontend.wrf"))
        goto exit;

      frontendInitialised = TRUE;
      frontendInitVars();
      //if intro required set up the video
      if (introVideoControl <= 1)
      {
        seq_ClearSeqList();
        seq_AddSeqToList("eidos-logo.mp4", nullptr, nullptr, FALSE, 0);
        seq_AddSeqToList("pumpkin.mp4", nullptr, nullptr, FALSE, 0);
        seq_AddSeqToList("titles.mp4", nullptr, nullptr, FALSE, 0);
        seq_AddSeqToList("devastation.mp4", nullptr, "devastation.txa", FALSE, 0);

        seq_StartNextFullScreenVideo();
        introVideoControl = 2;
      }
      break;

    case GS_SAVEGAMELOAD:
      screen_RestartBackDrop();
      gameStatus = GS_NORMAL;
      // load up a save game
      if (!loadGameInit(saveGameName,FALSE))
        goto exit;
      screen_StopBackDrop();
      break;
    case GS_NORMAL:
      if (!levLoadData(pLevelName, nullptr, 0))
        goto exit;
      //after data is loaded check the research stats are valid
      if (!checkResearchStats())
      {
        Neuron::Fatal("Invalid Research Stats");
        goto exit;
      }
      //and check the structure stats are valid
      if (!checkStructureStats())
      {
        Neuron::Fatal("Invalid Structure Stats");
        goto exit;
      }

      //set a flag for the trigger/event system to indicate initialisation is complete
      gameInitialised = TRUE;
      screen_StopBackDrop();
      break;
    case GS_VIDEO_MODE: Neuron::Fatal("Video_mode no longer valid");
      if (introVideoControl == 0)
        videoInitialised = TRUE;
      break;

    default: Neuron::Fatal("Unknown game status on startup!");
    }

    Neuron::DebugTrace("Entering main loop\n");

    Restart = FALSE;

    while (!Restart)
    {
      frameRet = frameUpdate();

      if (frameRet == FRAME_SETFOCUS)
        D3DTestCooperativeLevel(TRUE);
      else
        D3DTestCooperativeLevel(FALSE);

      switch (frameRet)
      {
      case FRAME_KILLFOCUS:
        paused = TRUE;
        gameTimeStop();
        audio_StopAll();
        break;
      case FRAME_SETFOCUS:
        paused = FALSE;
        gameTimeStart();
        if (!dispModeChange())
        {
          quit = TRUE;
          Restart = TRUE;
        }
        dtm_RestoreTextures();
        break;
      case FRAME_QUIT:
        quit = TRUE;
        Restart = TRUE;
        break;
      }

      lastStatus = gameStatus;

      if ((!paused) && (!quit))
      {
        switch (gameStatus)
        {
        case GS_TITLE_SCREEN:
          if (loop_GetVideoStatus())
            videoLoop();
          else
          {
            switch (titleLoop())
            {
            case TITLECODE_QUITGAME: Neuron::DebugTrace("TITLECODE_QUITGAME\n");
              Restart = TRUE;
              quit = TRUE;
              break;

            //						case TITLECODE_ATTRACT:

            case TITLECODE_SAVEGAMELOAD: Neuron::DebugTrace("TITLECODE_SAVEGAMELOAD\n");
              gameStatus = GS_SAVEGAMELOAD;
              Restart = TRUE;
              break;
            case TITLECODE_STARTGAME: Neuron::DebugTrace("TITLECODE_STARTGAME\n");
              gameStatus = GS_NORMAL;
              Restart = TRUE;
              break;

            case TITLECODE_SHOWINTRO: Neuron::DebugTrace("TITLECODE_SHOWINTRO\n");
              seq_ClearSeqList();
              seq_AddSeqToList("eidos-logo.mp4", nullptr, nullptr, FALSE, 0);
              seq_AddSeqToList("pumpkin.mp4", nullptr, nullptr, FALSE, 0);
              seq_AddSeqToList("titles.mp4", nullptr, nullptr, FALSE, 0);
              seq_AddSeqToList("devastation.mp4", nullptr, "devastation.txa", FALSE, 0);
              seq_StartNextFullScreenVideo();
              introVideoControl = 2; //play the video but dont init the sound system
              break;

            case TITLECODE_CONTINUE:
              break;

            default: Neuron::Fatal("Unknown code returned by titleLoop");
            }
          }
          break;

        /*				case GS_SAVEGAMELOAD:
                  if (loopNewLevel)
                  {
                    //the start of a campaign/expand mission
                    DBPRINTF(("GAMECODE_NEWLEVEL\n"));
                    loopNewLevel = FALSE;
                    // gameStatus is unchanged, just loading additional data
                    Restart = TRUE;
                  }
                  break;
        */
        case GS_NORMAL:
          if (loop_GetVideoStatus())
            videoLoop();
          else
          {
            loopStatus = gameLoop();
            switch (loopStatus)
            {
            case GAMECODE_QUITGAME: Neuron::DebugTrace("GAMECODE_QUITGAME\n");
              gameStatus = GS_TITLE_SCREEN;
              Restart = TRUE;
#ifdef NON_INTERACT
              quit = TRUE;
#endif

              if (NetPlay.bLobbyLaunched)
                quit = TRUE;
              break;
            case GAMECODE_FASTEXIT: Neuron::DebugTrace("GAMECODE_FASTEXIT\n");
              Restart = TRUE;
              quit = TRUE;
              break;

            case GAMECODE_LOADGAME: Neuron::DebugTrace("GAMECODE_LOADGAME\n");
              Restart = TRUE;
              gameStatus = GS_SAVEGAMELOAD;
              break;

            case GAMECODE_PLAYVIDEO: Neuron::DebugTrace("GAMECODE_PLAYVIDEO\n");
              Restart = FALSE;
              break;

            case GAMECODE_NEWLEVEL: Neuron::DebugTrace("GAMECODE_NEWLEVEL\n");
              // gameStatus is unchanged, just loading additional data
              Restart = TRUE;
              break;

            case GAMECODE_RESTARTGAME: Neuron::DebugTrace("GAMECODE_RESTARTGAME\n");
              Restart = TRUE;
              break;

            case GAMECODE_CONTINUE:
              break;

            default: Neuron::Fatal("Unknown code returned by gameLoop");
            }
          }
          break;

        case GS_VIDEO_MODE: Neuron::Fatal("Video_mode no longer valid");
          if (loop_GetVideoStatus())
            videoLoop();
          else
          {
            if (introVideoControl <= 1)
            {
              seq_ClearSeqList();

              seq_AddSeqToList("factory.mp4", nullptr, nullptr, FALSE, 0);
              seq_StartNextFullScreenVideo(); //"sequences\\factory.mp4","sequences\\factory.wav");
              introVideoControl = 2;
            }
            else
            {
              Neuron::DebugTrace("VIDEO_QUIT\n");
              if (introVideoControl == 2) //finished playing intro video
              {
                gameStatus = GS_TITLE_SCREEN;
                if (videoInitialised)
                  Restart = TRUE;
                introVideoControl = 3;
              }
              else
                gameStatus = GS_NORMAL;
            }
          }

          break;

        default: Neuron::Fatal("Weirdy game status I'm afraid!!");
          break;
        }

        gameTimeUpdate();
      }
    } // End of !Restart loop.

    // Do game mode specific shutdown.	
    switch (lastStatus)
    {
    case GS_TITLE_SCREEN:
      if (!frontendShutdown())
        goto exit;
      frontendInitialised = FALSE;
      break;

    /*			case GS_SAVEGAMELOAD:
            //get the next level to load up
            gameStatus = GS_NORMAL;
            break;*/
    case GS_NORMAL:
      if (loopStatus != GAMECODE_NEWLEVEL)
      {
        initLoadingScreen(TRUE,FALSE); // returning to f.e. do a loader.render not active
        pie_EnableFog(FALSE); //dont let the normal loop code set status on
        fogStatus = 0;
        if (loopStatus != GAMECODE_LOADGAME)
          levReleaseAll();
      }
      gameInitialised = FALSE;
      break;

    case GS_VIDEO_MODE: Neuron::Fatal("Video_mode no longer valid");
      if (videoInitialised)
        videoInitialised = FALSE;
      break;

    default: Neuron::Fatal("Unknown game status on shutdown!");
      break;
    }
  } // End of !quit loop.

  Neuron::DebugTrace("Shuting down application\n");

  systemShutdown();

  pal_ShutDown();

  frameShutDown();

  if (reInit)
    goto init;

  PostQuitMessage(0);

  return 0;

exit: Neuron::DebugTrace("Shutting down after fail\n");

  systemShutdown();

  pal_ShutDown();

  frameShutDown();

  PostQuitMessage(1);

  return 1;
}

UDWORD GetGameMode(void) { return gameStatus; }

void SetGameMode(UDWORD status)
{
  DEBUG_ASSERT_TEXT(status == GS_TITLE_SCREEN ||
    status == GS_MISSION_SCREEN || status == GS_NORMAL || status == GS_VIDEO_MODE || status == GS_SAVEGAMELOAD, "SetGameMode: invalid game mode");

  gameStatus = status;
}
