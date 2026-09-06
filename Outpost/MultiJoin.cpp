#include "pch.h"
/* 
 * Multijoin.c
 *
 * Alex Lee, pumpkin studios, bath,  
 *
 * Stuff to handle the comings and goings of players.
 */

#include <stdio.h>					// for sprintf

#include "Frame.h"
#include "StrRes.h"

#include "ObjMem.h"
#include "StatsDef.h"
#include "DroidDef.h"
#include "TextDraw.h"
#include "GTime.h"
#include "Game.h"
#include "Projectile.h"
#include "Droid.h"
#include "Map.h"
#include "Power.h"
#include "Game.h"					// for loading maps
#include "Player.h"
#include "Message.h"				// for clearing game messages
#include "Text.h"					// for string resources.
#include "Order.h"
#include "Console.h"
#include "OrderDef.h"				// for droid_order_data
#include "HCI.h"
#include "Component.h"
#include "Research.h"
#include "AudioSystem.h"
#include "AudioID.h"
#include "Wrappers.h"
#include "IntImage.h"
#include "Data.h"
#include "Script.h"
#include "ScriptTabs.h"

#include "NetPlay.h"
#include "MultiPlay.h"
#include "MultiJoin.h"
#include "MultiRecv.h"
#include "MultiInt.h"
#include "MultiStat.h"
#include "MultiGifts.h"
#include "RenderClip.h"
#include "Widget.h"

// ////////////////////////////////////////////////////////////////////////////
// External Variables
extern BOOL bHosted;
extern BOOL multiRequestUp;
// ////////////////////////////////////////////////////////////////////////////
//external functions

// ////////////////////////////////////////////////////////////////////////////
// Local Functions
BOOL sendVersionCheck();
BOOL recvVersionCheck(NETMSG* pMsg);
BOOL intDisplayMultiJoiningStatus(UBYTE joinCount);
VOID clearPlayer(UDWORD player, BOOL quietly, BOOL removeOil); // what to do when a arena player leaves.
BOOL MultiPlayerLeave(NETPLAYERID dp); // remote player has left.
BOOL MultiPlayerJoin(NETPLAYERID dp); // remote player has just joined.
VOID setupNewPlayer(NETPLAYERID dpid, UDWORD player); // stuff to do when player joins.	
//BOOL multiPlayerRequest	(NETMSG *pMsg);							// remote player has requested info 
//BOOL UpdateClient		(NETPLAYERID dest, UDWORD playerToSend);		// send information to a remote player
//BOOL ProcessDroidOrders	(VOID);									// ince setup, this player issues each droid order.
VOID resetMultiVisibility(UDWORD player);

// ////////////////////////////////////////////////////////////////////////////
// Version Check

BOOL sendVersionCheck()
{
  NETMSG msg;

  msg.size = 0;

  NetAdd(msg, 0, selectedPlayer);
  NetAdd(msg, 1, cheatHash);
  msg.size = sizeof(cheatHash) + 1;
  msg.type = NET_VERSION;

  return NETbcast(&msg,TRUE);
}

BOOL recvVersionCheck(NETMSG* pMsg)
{
  UDWORD extCheat[CHEAT_MAXCHEAT];
  UBYTE pl;
  CHAR sTmp[128];
  NetGet(pMsg, 1, extCheat);

  if (memcmp(extCheat, cheatHash, CHEAT_MAXCHEAT * 4) != 0)
    goto FAILURE;
  return TRUE;

FAILURE:

  // what should we do now ?
  NetGet(pMsg, 0, pl);

  sprintf(sTmp, "%s has different data. CHEATING or wrong version", getPlayerName(pl));
  addConsoleMessage(sTmp, DEFAULT_JUSTIFY);
  sendTextMessage(sTmp,TRUE);

  if (NetPlay.bHost)
    kickPlayer(player2dpid[pl]);
  return TRUE;
}

// ////////////////////////////////////////////////////////////////////////////
// Wait For Players

BOOL intDisplayMultiJoiningStatus(UBYTE joinCount)
{
  UDWORD x, y, w, h;
  CHAR sTmp[6];

  w = RET_FORMWIDTH;
  h = RET_FORMHEIGHT;
  x = RET_X;
  y = RET_Y;

  //	cameraToHome(selectedPlayer);				// home the camera to the player.
  RenderWindowFrame(&FrameNormal, x, y, w, h); // draw a wee blu box.

  // display how far done..
  pie_DrawText((UCHAR*)strresGetString(psStringRes, STR_GAM_JOINING),
               x + (w / 2) - (Neuron::GetTextWidth((unsigned char*)strresGetString(psStringRes, STR_GAM_JOINING)) / 2), y + (h / 2) - 8);
  sprintf(sTmp, "%d%%", PERCENT((NetPlay.playercount-joinCount), NetPlay.playercount));
  pie_DrawText((UCHAR*)sTmp, x + (w / 2) - 10, y + (h / 2) + 10);

  return TRUE;
}

// ////////////////////////////////////////////////////////////////////////////
// when a remote player leaves an arena game do this!
VOID clearPlayer(UDWORD player, BOOL quietly, BOOL removeOil)
{
  UDWORD i;
  BOOL bTemp;
  STRUCTURE *psStruct, *psNext;

  player2dpid[player] = 0; // remove player, make computer controlled
  ingame.JoiningInProgress[player] = FALSE; // if they never joined, reset the flag

  for (i = 0; i < MAX_PLAYERS; i++) // remove alliances
  {
    alliances[player][i] = ALLIANCE_BROKEN;
    alliances[i][player] = ALLIANCE_BROKEN;
  }

  while (apsDroidLists[player]) // delete all droids
  {
    if (quietly)
      killDroid(apsDroidLists[player]);
    else
      destroyDroid(apsDroidLists[player]);
  }

  psStruct = apsStructLists[player];
  while (psStruct) // delete all structs
  {
    psNext = psStruct->psNext;
    bTemp = FALSE;

    if (removeOil)
    {
      if (psStruct->pStructureType->type == REF_RESOURCE_EXTRACTOR)
        bTemp = TRUE;
    }

    if (quietly)
      removeStruct(psStruct, TRUE);
    else
    {
      if ((psStruct->pStructureType->type != REF_WALL && psStruct->pStructureType->type != REF_WALLCORNER))
        destroyStruct(psStruct);
    }

    if (bTemp)
    {
      if (apsFeatureLists[0]->psStats->subType == FEAT_OIL_RESOURCE)
        removeFeature(apsFeatureLists[0]);
    }
    psStruct = psNext;
  }
}

// Reset visibilty, so a new player can't see the old stuff!!
VOID resetMultiVisibility(UDWORD player)
{
  UDWORD owned;
  DROID* pDroid;
  STRUCTURE* pStruct;

  for (owned = 0; owned < MAX_PLAYERS; owned++) // for each player
  {
    if (owned != player) // done reset own stuff..
    {
      //droids
      for (pDroid = apsDroidLists[owned]; pDroid; pDroid = pDroid->psNext)
        pDroid->visible[player] = FALSE;

      //structures
      for (pStruct = apsStructLists[owned]; pStruct; pStruct = pStruct->psNext)
        pStruct->visible[player] = FALSE;
    }
  }
}

// ////////////////////////////////////////////////////////////////////////////
// A remote player has left the game
BOOL MultiPlayerLeave(NETPLAYERID dp)
{
  UDWORD i = 0;
  char buf[255];

  while ((player2dpid[i] != dp) && (i < MAX_PLAYERS))
  {
    i++; // find out which!
  }

  if (i != MAX_PLAYERS) // player not already removed
  {
    NETlogEntry("Player Unexpectedly leaving, came from directplay...", 0, dp);

    sprintf(buf, strresGetString(psStringRes, STR_MUL_LEAVE), getPlayerName(i));

    turnOffMultiMsg(TRUE);
    clearPlayer(i,FALSE,FALSE);
    game.skDiff[i] = 10;

    turnOffMultiMsg(FALSE);

    addConsoleMessage(buf, DEFAULT_JUSTIFY);

    if (widgGetFromID(psWScreen,IDRET_FORM))
      AudioSystem::QueueTrack(ID_CLAN_EXIT);
  }

  NETplayerInfo(); // update the player info stuff		

  // fire script callback to reassign skirmish players.
  eventFireCallbackTrigger(CALL_PLAYERLEFT);

  return TRUE;
}

// ////////////////////////////////////////////////////////////////////////////
// A Remote Player has joined the game.
BOOL MultiPlayerJoin(NETPLAYERID dpid)
{
  UDWORD i;

  if (widgGetFromID(psWScreen,IDRET_FORM)) // if ingame.
    AudioSystem::QueueTrack(ID_CLAN_ENTER);

  /* The new machine has nobody's stats. DirectPlay kept its replicated
   * per-player data available to whoever joined later and the transport does
   * not, so everybody already here says theirs again.
   */
  if (dpid != NetPlay.dpidPlayer)
    setMultiStats(NetPlay.dpidPlayer, getMultiStats(selectedPlayer,TRUE),FALSE);

  if (widgGetFromID(psWScreen,MULTIOP_PLAYERS)) // if in multimenu.
  {
    if (!multiRequestUp && (bHosted || ingame.localJoiningInProgress))
      addPlayerBox(TRUE); // update the player box.
  }

  if (NetPlay.bHost) // host responsible for welcoming this player.
  {
    // if we've already received a request from this player don't reallocate.
    for (i = 0; (i < MAX_PLAYERS); i++)
    {
      if ((player2dpid[i] == dpid) && ingame.JoiningInProgress[i])
        return TRUE;
    }
    DEBUG_ASSERT_TEXT(NetPlay.playercount<=MAX_PLAYERS, "Too many players!");

    // setup data for this player, then broadcast it to the other players.
#if 0
    for (i = 0; player2dpid[i] != 0; i++); // find a zero entry, for a new player. MAKE RANDOM!!!
#else
    do
    {
      // randomly allocate a player to this new machine.
      i = rand() % game.maxPlayers;
    }
    while (player2dpid[i] != 0);
#endif

    setPlayerColour(i,MAX_PLAYERS); // force a colourchoice
    chooseColour(i); // pick an unused colour.

    setupNewPlayer(dpid, i); // setup all the guff for that player.
    sendOptions(dpid, i);

    // if skirmish and game full, then kick... 
    if (game.type == SKIRMISH && NetPlay.playercount >= game.maxPlayers)
      kickPlayer(dpid);
  }
  return TRUE;
}

// ////////////////////////////////////////////////////////////////////////////
// Setup Stuff for a new player.
void setupNewPlayer(NETPLAYERID dpid, UDWORD player)
{
  UDWORD i; //,col;
  char buf[255];

  player2dpid[player] = dpid; // assign them a player too.
  ingame.PingTimes[player] = 0; // reset ping time
  ingame.JoiningInProgress[player] = TRUE; // note that player is now joining.

  //		// set the power level for that player..	

  for (i = 0; i < MAX_PLAYERS; i++) // set all alliances to broken.
  {
    alliances[selectedPlayer][i] = ALLIANCE_BROKEN;
    alliances[i][selectedPlayer] = ALLIANCE_BROKEN;
  }

  resetMultiVisibility(player); // set visibility flags.
  NETplayerInfo(); // update the net info stuff

  setMultiStats(player2dpid[player], getMultiStats(player,FALSE),TRUE); // get the players score from the ether.

  sprintf(buf, strresGetString(psStringRes, STR_MUL_JOINING), getPlayerName(player));
  addConsoleMessage(buf, DEFAULT_JUSTIFY);
}

// ////////////////////////////////////////////////////////////////////////////
// CAMPAIGN STUFF

// ////////////////////////////////////////////////////////////////////////////
// ARENA STUFF

// ////////////////////////////////////////////////////////////////////////////
// reduce the amount of oil that can be extracted.
void modifyResources(POWER_GEN_FUNCTION* psFunction)
{
  switch (game.power)
  {
  case LEV_LOW:
    psFunction->powerMultiplier = psFunction->powerMultiplier * 3 / 4; // careful with the brackets! (do mul before div)
    break;
  case LEV_MED:
    psFunction->powerMultiplier = psFunction->powerMultiplier * 1;
    break;
  case LEV_HI:
    psFunction->powerMultiplier = psFunction->powerMultiplier * 5 / 4;
    break;
  default:
    break;
  }
}
