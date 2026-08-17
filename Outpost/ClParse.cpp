#include "pch.h"
/*
 * clParse.c
 *
 * Parse command line arguments
 *
 */

#include "Direct.h"

#include "Frame.h"
#include "FrameResource.h"
#include "Widget.h"

#include "WinMain.h"
#include "FrontEnd.h"

#include "RenderClip.h"
#include "WarzoneConfig.h"

#include "ClParse.h"
#include "PieState.h"
#include "Objects.h"
#include "AdvVis.h"
#include "MultiPlay.h"
#include "MultiInt.h"
#include "NetPlay.h"
#include "Wrappers.h"
#include "Cheat.h"

BOOL scanGameSpyFlags(LPSTR gflag, LPSTR value);

// let the end user into debug mode....
BOOL bAllowDebugMode = FALSE;

// note that render mode must come before resolution flag.
BOOL ParseCommandLine(LPSTR psCmdLine)
{
  char seps[] = " ,\t\n";
  char* tokenType;
  char* token;
  char seps2[] = "\"";
  char cl[255];
  char cl2[255];
  unsigned char* pXor;

  /* get first token: */
  tokenType = strtok(psCmdLine, seps);
  // for cheating
  sprintf(cl, "%s", "VR^\\WZ^KVQXL\\^SSFH^XXZM");
  pXor = xorString((unsigned char*)cl);
  sprintf(cl2, "%s%s", "-", cl);

  /* loop through command line */
  while (tokenType != nullptr)
  {
    if (stricmp(tokenType, "-window") == 0)
    {
      /* Accepted and ignored: the display is always a borderless window
       * covering the desktop now. */
    }
    else if (stricmp(tokenType, "-intro") == 0)
      SetGameMode(GS_VIDEO_MODE);
    else if (stricmp(tokenType, "-title") == 0)
      SetGameMode(GS_TITLE_SCREEN);
    else if (stricmp(tokenType, "-game") == 0)
    {
      // find the game name
      token = strtok(nullptr, seps);
      if (token == nullptr)
      {
        Neuron::Fatal("Unrecognised -game name\n");
        return FALSE;
      }
      strncpy(pLevelName, token, 254);
      SetGameMode(GS_NORMAL);
    }
    else if (stricmp(tokenType, "-datapath") == 0)
    {
      // find the quoted path name
      token = strtok(nullptr, seps);
      if (token == nullptr)
      {
        Neuron::Fatal("Unrecognised datapath\n");
        return FALSE;
      }
      resSetBaseDir(token);

      if (_chdir(token) != 0)
        Neuron::Fatal("Path not found: {}\n", token);
    }

    else if (stricmp(tokenType, cl2) == 0)
      bAllowDebugMode = TRUE;
    /* The -640 .. -1280 resolution switches are gone: the display is the
     * desktop's own resolution, decided by the framework at start up. */
    else if (stricmp(tokenType, "-noTranslucent") == 0)
      war_SetTranslucent(FALSE);
    else if (stricmp(tokenType, "-noAdditive") == 0)
      war_SetAdditive(FALSE);
    else if (stricmp(tokenType, "-noFog") == 0)
      pie_SetFogCap(FOG_CAP_NO);
    else if (stricmp(tokenType, "-greyFog") == 0)
      pie_SetFogCap(FOG_CAP_GREY);
    else if (stricmp(tokenType, "-2meg") == 0)
      pie_SetTexCap(TEX_CAP_2M);
    else if (stricmp(tokenType, "-seqSmall") == 0)
      war_SetSeqMode(SEQ_SMALL);
    else if (stricmp(tokenType, "-seqSkip") == 0)
      war_SetSeqMode(SEQ_SKIP);
    else if (stricmp(tokenType, "-disableLobby") == 0)
      bDisableLobby = TRUE;
      /*		else if ( stricmp( tokenType,"-routeLimit") == 0)
		{
			// find the actual maximum routing limit
			token = strtok( NULL, seps );
			if (token == NULL)
			{
				DBERROR( ("Unrecognised -routeLimit value\n") );
				return FALSE;
			}

			if (!openWarzoneKey())
			{
				DBERROR(("Couldn't open registry for -routeLimit"));
				return FALSE;
			}
			fpathSetMaxRoute(atoi(token));
			setWarzoneKeyNumeric("maxRoute",(DWORD)(fpathGetMaxRoute()));
			closeWarzoneKey();
		}*/

      // gamespy flags
    else if (stricmp(tokenType, "+host") == 0 // host a multiplayer.
      || stricmp(tokenType, "+connect") == 0 || stricmp(tokenType, "+name") == 0 || stricmp(tokenType, "+ip") == 0 ||
      stricmp(tokenType, "+maxplayers") == 0 || stricmp(tokenType, "+hostname") == 0)
    {
      token = strtok(nullptr, seps2);
      scanGameSpyFlags(tokenType, token);
    }
    // end of gamespy

    else {}

    /* Get next token: */
    tokenType = strtok(nullptr, seps);
  }

  // look for any gamespy flags in the command line.

  return TRUE;
}

// ////////////////////////////////////////////////////////
// gamespy flags
BOOL scanGameSpyFlags(LPSTR gflag, LPSTR value)
{
  static UBYTE count = 0;

  count++;
  if (count == 1)
  {
    lobbyInitialise();
    bDisableLobby = TRUE; // dont reset everything on boot!
    gameSpy.bGameSpy = TRUE;
  }

  if (stricmp(gflag, "+host") == 0) // host a multiplayer.
  {
    NetPlay.bHost = 1;
    game.bytesPerSec = INETBYTESPERSEC;
    game.packetsPerSec = INETPACKETS;
    /* Was a DirectPlay compound address with an empty host, built and then
     * initialised. A host binds when it hosts; there is nothing to set up
     * here any more.
     */
    NETjoinAddress[0] = '\0';
  }
  else if (stricmp(gflag, "+connect") == 0) // join a multiplayer.
  {
    NetPlay.bHost = 0;
    game.bytesPerSec = INETBYTESPERSEC;
    game.packetsPerSec = INETPACKETS;
    /* The address to join, kept for NETjoinGame the same way the connection
     * screen keeps what the player typed.
     */
    strncpy(NETjoinAddress, value, Transport::AddressSize - 1);
    NETjoinAddress[Transport::AddressSize - 1] = '\0';
  }
  else if (stricmp(gflag, "+name") == 0) // player name.
    strcpy((char*)sPlayer, value);
  else if (stricmp(gflag, "+hostname") == 0) // game name.
    strcpy(game.name, value);

  /*not used from here on..
+ip
+maxplayers
+game
+team
+skin
+playerid
+password
tokenType = strtok( NULL, seps );
*/
  return TRUE;
}
