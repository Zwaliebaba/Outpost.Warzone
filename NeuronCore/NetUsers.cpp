#include "pch.h"
/*
 * NetUsers.cpp
 *
 * Who is in the game.
 *
 * DirectPlay kept the roster and this file asked it, through an EnumPlayers
 * with a callback per player. The transport keeps it now, so NETplayerInfo is
 * a loop over ids rather than a callback, and it no longer takes the session
 * GUID that told DirectPlay which session to enumerate: there is only one.
 *
 * Two things that were here are gone rather than ported. NETspectate and
 * NETisSpectator had no callers anywhere in the tree -- spectating was never
 * finished -- so PLAYER lost its bSpectator with them. And the four
 * player-data functions wrapped DirectPlay's replicated per-player blobs,
 * whose only consumer was MultiStat.cpp replicating a PLAYERSTATS; that now
 * broadcasts an ordinary message, which is the whole of what the feature was
 * being used for.
 */

#include "Frame.h"
#include "NetPlay.h"

BOOL NETuseNetwork(BOOL val);
UDWORD NETplayerInfo(VOID);
BOOL NETchangePlayerName(NETPLAYERID dpid, char* newName);

// call to disable/enable ALL comms. Absolute arse of a thing, be very careful!
BOOL NETuseNetwork(BOOL val)
{
  if (val)
    NetPlay.bComms = TRUE;
  else
    NetPlay.bComms = FALSE;
  return TRUE;
}

// ////////////////////////////////////////////////////////////////////////
// count players in the game currently joined.
UDWORD NETplayerInfo(VOID)
{
  NETPLAYERID aPlayers[MaxNumberOfPlayers];
  UDWORD udwCount;
  UDWORD i;

  NetPlay.playercount = 0; // reset player counter

  if (!NetPlay.bComms)
  {
    NetPlay.playercount = 1;
    NetPlay.players[0].bHost = TRUE;
    NetPlay.players[0].dpid = 1;
    return 1;
  }

  ZeroMemory(NetPlay.players, sizeof(NetPlay.players)); // reset player info

  udwCount = nettrans_PlayerList(aPlayers, MaxNumberOfPlayers);

  for (i = 0; i < udwCount; i++)
  {
    NetPlay.players[i].dpid = aPlayers[i];
    nettrans_PlayerName(aPlayers[i], NetPlay.players[i].name, StringSize);

    /* The host is the first player and always has been -- the transport
     * assigns ids in join order starting from itself.
     */
    NetPlay.players[i].bHost = nettrans_IsHostPlayer(aPlayers[i]);
  }

  NetPlay.playercount = udwCount;

  /* The local id is not known until the host has answered a join, so this is
   * also where a joiner finds out what it is.
   */
  if (NetPlay.dpidPlayer == 0)
    NetPlay.dpidPlayer = nettrans_LocalPlayer();

  return NetPlay.playercount;
}

// ////////////////////////////////////////////////////////////////////////
// rename a player
//
// Only the local player can be renamed -- DirectPlay would let you set any
// player's name and nothing ever did. Still a guaranteed message underneath,
// so still not something to call every frame.
BOOL NETchangePlayerName(NETPLAYERID dpid, char* newName)
{
  if (!NetPlay.bComms)
  {
    strcpy(NetPlay.players[0].name, newName);
    return TRUE;
  }

  if (dpid != nettrans_LocalPlayer())
  {
    Neuron::DebugTrace("NETPLAY: refusing to rename somebody else\n");
    return FALSE;
  }

  return nettrans_SetLocalName(newName);
}
