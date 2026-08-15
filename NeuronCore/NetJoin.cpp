#include "pch.h"
/*
 * NetJoin.cpp
 *
 * Opening, finding, joining and closing a session.
 *
 * Every function here used to be DirectPlay: an EnumSessions with a callback,
 * a DPSESSIONDESC2 fetched twice to learn its own size, and four game flags
 * living in that description's dwUser1..4. All of it is now one call each into
 * Transport.h, and the permanent malloc that existed to keep the
 * variable-sized description out of the frame loop went with it.
 */

#include "Frame.h"
#include "NetPlay.h"

// ////////////////////////////////////////////////////////////////////////
// Stop the transport from accepting more players.
BOOL NEThaltJoining(VOID)
{
  if (!NetPlay.bComms)
    return TRUE;

  return Transport::CloseToJoiners();
}

// ////////////////////////////////////////////////////////////////////////
// find games on the current connection.
//
// There is no discovery in this build -- the destination is a relay server
// that owns the connections, where listing games is a query to a known server
// -- so Transport::FindSessions answers nothing until that server exists.
//
// Which would leave the browser empty and joining unreachable, since the only
// way into joinCampaign is clicking a game in it. So when nothing answers and
// the player has typed an address, the list is that address: one entry, which
// behaves like any other and needs no special case anywhere downstream. When
// the server arrives it fills the list properly and this branch stops firing.
BOOL NETfindGame(VOID)
{
  Transport::SessionInfo aSessions[MaxGames];
  UDWORD udwCount;
  UDWORD i;

  ZeroMemory(NetPlay.games, sizeof(NetPlay.games));

  if (!NetPlay.bComms)
    return TRUE;

  udwCount = Transport::FindSessions(aSessions, MaxGames);

  for (i = 0; i < udwCount && i < MaxGames; i++)
  {
    strncpy(NetPlay.games[i].name, aSessions[i].name, StringSize - 1);
    strncpy(NetPlay.games[i].address, aSessions[i].address, Transport::AddressSize - 1);
    NetPlay.games[i].currentPlayers = aSessions[i].currentPlayers;
    NetPlay.games[i].maxPlayers = aSessions[i].maxPlayers;
    NetPlay.games[i].bJoinDisabled = FALSE;
    memcpy(NetPlay.games[i].flags, aSessions[i].gameFlags, sizeof(NetPlay.games[i].flags));
  }

  if (udwCount == 0 && NETjoinAddress[0] != '\0')
  {
    /* Named for the address because that is genuinely all that is known about
     * it: nothing has been asked and nothing has answered. The player count
     * says one of MaxNumberOfPlayers so the browser draws it as joinable --
     * whether it really is, only the join attempt can say.
     */
    strncpy(NetPlay.games[0].name, NETjoinAddress, StringSize - 1);
    strncpy(NetPlay.games[0].address, NETjoinAddress, Transport::AddressSize - 1);
    NetPlay.games[0].currentPlayers = 1;
    NetPlay.games[0].maxPlayers = MaxNumberOfPlayers;
    NetPlay.games[0].bJoinDisabled = FALSE;
  }

  return TRUE;
}

// ////////////////////////////////////////////////////////////////////////
// ////////////////////////////////////////////////////////////////////////
// Functions used to setup and join games.

BOOL NETjoinGame(const char* address, LPSTR playername)
{
  if (!NetPlay.bComms)
    return TRUE;

  if (address == nullptr || address[0] == '\0')
  {
    Neuron::DebugTrace("NETPLAY: no address to join\n");
    return FALSE;
  }

  if (!Transport::Join(address, playername))
  {
    Neuron::DebugTrace("NETPLAY: Failed to Join Game at {}\n", address);
    return FALSE;
  }

  /* Not known yet. The handshake is still running and the host assigns the id,
   * which arrives a frame or two later; NETplayerInfo picks it up.
   */
  NetPlay.dpidPlayer = 0;
  NetPlay.bHost = FALSE;

  return TRUE;
}

// ////////////////////////////////////////////////////////////////////////
// Host a game with a given name and player name. & 4 user game flags
BOOL NEThostGame(LPSTR SessionName, LPSTR PlayerName, DWORD one, // flags.
                 DWORD two, DWORD three, DWORD four, UDWORD plyrs) // # of players.
{
  DWORD gameFlags[Transport::GameFlagCount];

  if (!NetPlay.bComms)
  {
    NetPlay.dpidPlayer = 1;
    NetPlay.bHost = TRUE;
    return TRUE;
  }

  gameFlags[0] = one;
  gameFlags[1] = two;
  gameFlags[2] = three;
  gameFlags[3] = four;

  if (!Transport::Host(SessionName, PlayerName, plyrs, gameFlags))
  {
    Neuron::Fatal("failed to host game");
    return FALSE;
  }

  NetPlay.dpidPlayer = Transport::LocalPlayer();
  NetPlay.bHost = TRUE;

  return TRUE;
}

// ////////////////////////////////////////////////////////////////////////
//close the open game..
HRESULT NETclose(VOID)
{
  Transport::Leave();
  NetPlay.dpidPlayer = 0;
  NetPlay.bHost = FALSE;
  NetPlay.playercount = 0;

  return S_OK;
}

// ////////////////////////////////////////////////////////////////////////
// return one of the four user flags for a game in the browser list.
//
// The flag numbers are 1 to 4 because DirectPlay called the fields dwUser1 to
// dwUser4 and the game says NETgetGameFlagsUnjoined(n, 1) all over MultiInt.
DWORD NETgetGameFlagsUnjoined(UDWORD gameid, UDWORD flag)
{
  if (gameid >= MaxGames || flag < 1 || flag > Transport::GameFlagCount)
  {
    Neuron::Fatal("Invalid flag for getgameflagsunjoined in netplay lib");
    return 0;
  }

  return NetPlay.games[gameid].flags[flag - 1];
}

// ////////////////////////////////////////////////////////////////////////
// Set a game flag on the session this machine is hosting.
BOOL NETsetGameFlags(UDWORD flag, DWORD value)
{
  DWORD gameFlags[Transport::GameFlagCount];

  if (!NetPlay.bComms)
    return TRUE;

  if (flag < 1 || flag > Transport::GameFlagCount)
  {
    Neuron::Fatal("Invalid flag for setgameflags in netplay lib");
    return FALSE;
  }

  if (!Transport::GetGameFlags(gameFlags))
    return FALSE;

  gameFlags[flag - 1] = value;

  return Transport::SetGameFlags(gameFlags);
}
