#include "pch.h"

// ////////////////////////////////////////////////////////////////////////
// includes
#include "Frame.h"
#include "NetPlay.h"
#include "NetSupp.h"
#include "time.h"			//for stats

// ////////////////////////////////////////////////////////////////////////
// Variables 
GUID GAME_GUID;

LPDIRECTPLAY4 glpDP = nullptr; // Directplay object pointer.
LPDIRECTPLAYLOBBY3 glpDPL3 = nullptr; // Lobby pointer
LPDIRECTPLAYLOBBYA glpDPL = nullptr; // Lobby pointer

NETPLAY NetPlay;

// ////////////////////////////////////////////////////////////////////////
using NETSTATS = struct
{
  // data regarding the last one second or so.
  UDWORD bytesRecvd;
  UDWORD bytesSent; // number of bytes sent in about 1 sec.
  UDWORD packetsSent;
  UDWORD packetsRecvd;
};

static NETSTATS nStats;
static UDWORD protocount = 0;

extern VOID* pSingleUserData; // single player mode. a local copy...
extern DWORD userDataSize;

extern VOID* lpPermDescription; // description data store.
extern UDWORD descriptionSize;

// ////////////////////////////////////////////////////////////////////////
// Prototypes
BOOL NETinit(GUID g, BOOL bFirstCall);
HRESULT NETshutdown(VOID);
UDWORD NETgetBytesSent(VOID);
UDWORD NETgetPacketsSent(VOID);
UDWORD NETgetBytesRecvd(VOID);
UDWORD NETgetPacketsRecvd(VOID);
UDWORD NETgetRecentBytesSent(VOID);
UDWORD NETgetRecentPacketsSent(VOID);
UDWORD NETgetRecentBytesRecvd(VOID);
UDWORD NETgetRecentPacketsRecvd(VOID);
BOOL NETsend(NETMSG* msg, DPID player, BOOL guarantee);
BOOL NETbcast(NETMSG* msg, BOOL guarantee);
BOOL NETrecv(NETMSG* msg);
BOOL NETselectProtocol(LPVOID lpConnection);
BOOL FAR PASCAL NETfindProtocolCallback(LPCGUID lpguidSP, LPVOID lpConnection, DWORD dwConnectionSize, LPCDPNAME lpName, DWORD dwFlags,
                                        LPVOID lpContext);
BOOL NETfindProtocol(BOOL Lob);
UBYTE NETsendFile(BOOL newFile, CHAR* fileName, DPID player);
UBYTE NETrecvFile(NETMSG* pMsg);

// ////////////////////////////////////////////////////////////////////////
// setup stuff
BOOL NETinit(GUID g, BOOL bFirstCall)
{
  HRESULT hr;
  LPDIRECTPLAYLOBBY glpTEMP; // for setting overrides.
  UDWORD i;

  if (bFirstCall)
  {
    Neuron::DebugTrace("NETPLAY: Init called, MORNIN' \n ");

    NetPlay.bLobbyLaunched = FALSE; // clean up 
    NetPlay.lpDirectPlay4A = nullptr;
    NetPlay.hPlayerEvent = nullptr;
    NetPlay.dpidPlayer = 0;
    NetPlay.bHost = 0;
    NetPlay.bComms = TRUE;

    NetPlay.bEncryptAllPackets = FALSE;
    NETsetKey(0x2fe8f810, 0xb72a5, 0x114d0, 0x2a7); // j-random key to get us started

    for (i = 0; i < MaxNumberOfPlayers; i++)
    {
      ZeroMemory(&NetPlay.protocols[i], sizeof(PROTO));
      ZeroMemory(&NetPlay.players[i], sizeof(PLAYER));
      ZeroMemory(&NetPlay.games[i], sizeof(GAMESTRUCT));
    }
    GAME_GUID = g;
    CoInitialize(nullptr);

    NETuseNetwork(TRUE);
    NETstartLogging();
  }

  if (NETconnectToLobby(&NetPlay)) // try to connect to lobby
  {
    // we were lobby launched.	
    // dont need to do anymore, we are connected.
    Neuron::DebugTrace("NETPLAY: LobbyLaunched\n");
  }
  else
  {
    // not lobby launched.
    hr = CreateDirectPlayInterface(&glpDP); // Create an IDirectPlay4 interface
    if (FAILED(hr))
      return FALSE;

    hr = DirectPlayLobbyCreate(nullptr, &glpTEMP, nullptr, nullptr, 0); // create lobinterface
    hr = IDirectPlayLobby_QueryInterface(glpTEMP, // set this up to allowfor service dialog
                                         IID_IDirectPlayLobby3, // box overrides.
                                         (LPVOID*)&glpDPL3); // GLPDPL nalso gets set here.
  }

  if (FAILED(hr))
    return FALSE;

  Neuron::DebugTrace("NETPLAY: init success\n ");
  return TRUE;
}

// ////////////////////////////////////////////////////////////////////////
// SHUTDOWN THE CONNECTION.
HRESULT NETshutdown(VOID)
{
  if (NetPlay.dpidPlayer || NetPlay.lpDirectPlay4A)
  {
    IDirectPlay_DestroyPlayer(glpDP, NetPlay.dpidPlayer);
    NetPlay.dpidPlayer = 0;
  }

  if (userDataSize) // free player data store.
  {
    delete[] static_cast<UBYTE*>(pSingleUserData);
    pSingleUserData = nullptr;
    userDataSize = 0;
  }

  if (descriptionSize) // free connection info malloc area
    freePermMalloc();

  if (glpDPL)
    IDirectPlayLobby_Release(glpDPL);

  if (glpDP)
  {
    IDirectPlayX_Close(glpDP);
    IDirectPlayX_Release(glpDP);
  }

  NETstopLogging();
  Neuron::DebugTrace("NETPLAY: shutdown success\n");
  return (DP_OK);
}

// ////////////////////////////////////////////////////////////////////////
// ////////////////////////////////////////////////////////////////////////
// Send and Recv functions

// ////////////////////////////////////////////////////////////////////////
// return bytes of data sent recently.
UDWORD NETgetBytesSent(VOID)
{
  static UDWORD lastsec = 0;
  static UDWORD timy = 0;

  if (static_cast<UDWORD>(clock()) > (timy + CLOCKS_PER_SEC))
  {
    timy = clock();
    lastsec = nStats.bytesSent;
    nStats.bytesSent = 0;
  }

  return lastsec;
}

UDWORD NETgetRecentBytesSent(VOID) { return nStats.bytesSent; }

UDWORD NETgetBytesRecvd(VOID)
{
  static UDWORD lastsec = 0;
  static UDWORD timy = 0;
  if (static_cast<UDWORD>(clock()) > (timy + CLOCKS_PER_SEC))
  {
    timy = clock();
    lastsec = nStats.bytesRecvd;
    nStats.bytesRecvd = 0;
  }
  return lastsec;
}

UDWORD NETgetRecentBytesRecvd(VOID) { return nStats.bytesRecvd; }

//return number of packets sent last sec.
UDWORD NETgetPacketsSent(VOID)
{
  static UDWORD lastsec = 0;
  static UDWORD timy = 0;

  if (static_cast<UDWORD>(clock()) > (timy + CLOCKS_PER_SEC))
  {
    timy = clock();
    lastsec = nStats.packetsSent;
    nStats.packetsSent = 0;
  }

  return lastsec;
}

UDWORD NETgetRecentPacketsSent(VOID) { return nStats.packetsSent; }

UDWORD NETgetPacketsRecvd(VOID)
{
  static UDWORD lastsec = 0;
  static UDWORD timy = 0;
  if (static_cast<UDWORD>(clock()) > (timy + CLOCKS_PER_SEC))
  {
    timy = clock();
    lastsec = nStats.packetsRecvd;
    nStats.packetsRecvd = 0;
  }
  return lastsec;
}

UDWORD NETgetRecentPacketsRecvd(VOID) { return nStats.packetsRecvd; }

// ////////////////////////////////////////////////////////////////////////
// Send a message to a player, option to guarantee message
BOOL NETsend(NETMSG* msg, DPID player, BOOL guarantee)
{
  HRESULT hr;

  NETlogEntry("send", msg->type, msg->size);

  if (!NetPlay.bComms)
    return TRUE;

  if (msg->size > MaxMsgSize)
    Neuron::Fatal("NETPLAY: Message too large passed to NETsend");

  if (NetPlay.bEncryptAllPackets && (msg->type != FILEMSG)) // optionally encrypt all packets.
    NETmanglePacket(msg);

  // send it.			
  if (guarantee == TRUE)
  {
    hr = IDirectPlayX_Send(glpDP, NetPlay.dpidPlayer, player, DPSEND_GUARANTEED, msg,
                           (msg->size + sizeof(msg->size) + sizeof(msg->type)+ sizeof(msg->paddedBytes) ));
  }
  else
  {
    hr = IDirectPlayX_Send(glpDP, NetPlay.dpidPlayer, player, 0, msg,
                           (msg->size + sizeof(msg->size) + sizeof(msg->type)+sizeof(msg->paddedBytes) ));
  }

  if (hr != DP_OK) //error!
    return (FALSE);

  nStats.bytesSent += (msg->size + sizeof(msg->size) + sizeof(msg->type) + sizeof(msg->paddedBytes));
  nStats.packetsSent += 1;
  return (TRUE);
}

// ////////////////////////////////////////////////////////////////////////
// broadcast a message to all players.
BOOL NETbcast(NETMSG* msg, BOOL guarantee)
{
  HRESULT hr;

  NETlogEntry("bcst", msg->type, msg->size);

  if (!NetPlay.bComms)
    return TRUE;

  if (msg->size > MaxMsgSize)
    Neuron::Fatal("NETPLAY: Message Too large passed to NETbcast");

  if (NetPlay.bEncryptAllPackets && (msg->type != FILEMSG)) // optionally encrypt all packets.
    NETmanglePacket(msg);

  // send it.			
  if (guarantee == TRUE)
  {
    hr = IDirectPlayX_Send(glpDP, NetPlay.dpidPlayer, DPID_ALLPLAYERS, DPSEND_GUARANTEED, msg,
                           (msg->size + sizeof(msg->size) + sizeof(msg->type) + sizeof(msg->paddedBytes) ));
  }
  else
  {
    hr = IDirectPlayX_Send(glpDP, NetPlay.dpidPlayer, DPID_ALLPLAYERS, 0, msg,
                           (msg->size + sizeof(msg->size)+ sizeof(msg->type) +sizeof(msg->paddedBytes)));
  }

  if (hr != DP_OK) //error!
    return (FALSE);

  nStats.bytesSent += (msg->size + sizeof(msg->size) + sizeof(msg->type) + sizeof(msg->paddedBytes));
  nStats.packetsSent += 1;
  return (TRUE);
}

// ////////////////////////////////////////////////////////////////////////
// receive a message over the current connection
BOOL NETrecv(NETMSG* pMsg)
{
  DPID idTo, idFrom;
  HRESULT hr;

  DWORD bufsize = sizeof(NETMSG); // can only be as big as a NETMSG.

  if (!NetPlay.bComms)
    return FALSE;

  idFrom = 0;
  idTo = 0;
  hr = IDirectPlayX_Receive(glpDP, &idFrom, &idTo, DPRECEIVE_ALL, pMsg, &bufsize);

  if (hr == DPERR_NOMESSAGES)
    return (FALSE); // no more messages waiting.

  if (hr != DP_OK)
  {
    Neuron::DebugTrace("NETPLAY: failed to recv on NETrecv\n");
    return (FALSE);
  }

  nStats.bytesRecvd += bufsize; // note how much came in.
  nStats.packetsRecvd += 1;

  if (idFrom == DPID_SYSMSG)
  {
    NETlogEntry("SYSM", 0, pMsg->size);

    DirectPlaySystemMessageHandler(pMsg); //it's a system message. argh!
    return FALSE; // return false since it could be important not to keep going..
  }

  if ((pMsg->type >= ENCRYPTFLAG) && (pMsg->type != FILEMSG)) // decrypt if required..
    NETunmanglePacket(pMsg);
  NETlogEntry("recv", pMsg->type, pMsg->size);

  return (TRUE);
}

// ////////////////////////////////////////////////////////////////////////
// ////////////////////////////////////////////////////////////////////////
// Protocol functions

// select a current protocol. Call with a connection returned by findprotocol.
BOOL NETselectProtocol(LPVOID lpConnection)
{
  HRESULT hr;

  hr = IDirectPlayX_InitializeConnection(glpDP, lpConnection, 0); // initialize the connection
  if (hr == DP_OK)
  {
    Neuron::DebugTrace("NETPLAY: Protocol initialised\n");
    return TRUE;
  }
  if (hr == DPERR_ALREADYINITIALIZED)
  {
    if (NetPlay.bLobbyLaunched) // lobby shouldn't disconnect in this way.
    {
      Neuron::DebugTrace("NETPLAY: attempted a reconnect in a lobby, ignoring since already connected.\n");
      return TRUE;
    }

    Neuron::DebugTrace("NETPLAY: Protocol Already Initialised trying restart\n");

    NETshutdown(); // try restart
    NETinit(GAME_GUID,FALSE);
    hr = IDirectPlayX_InitializeConnection(glpDP, lpConnection, 0); // initialize the connection
    if (hr == DP_OK)
    {
      Neuron::DebugTrace("NETPLAY: protocol restart OK\n");
      return TRUE;
    }
    Neuron::DebugTrace("NETPLAY: failed to restart protocol \n");
    return FALSE;
  }
  if (hr == DPERR_INVALIDFLAGS)
    Neuron::DebugTrace("NETPLAY: Bad Flags.\n");
  else if (hr == DPERR_INVALIDPARAMS)
    Neuron::DebugTrace("NETPLAY: Invalid Parameters. for initconnection\n");

  else if (hr == DPERR_UNAVAILABLE)
    Neuron::DebugTrace("NETPLAY: protocol Unavailable.\n");
  else
    Neuron::DebugTrace("NETPLAY:Can't select desired protocol. UNKNOWN ERROR\n");
  // Couldn't init this connection.

  return (FALSE);
}

BOOL FAR PASCAL NETfindProtocolCallback(LPCGUID lpguidSP, LPVOID lpConnection, DWORD dwConnectionSize, LPCDPNAME lpName, DWORD dwFlags,
                                        LPVOID lpContext)
{
  if (protocount >= MaxProtocols)
  {
    Neuron::DebugTrace("NETPLAY:Maximum number of protocols exceeded.terminating search\n");
    return (FALSE);
  }

  NetPlay.protocols[protocount].guid = *lpguidSP;
  NetPlay.protocols[protocount].size = dwConnectionSize;

  strcpy(NetPlay.protocols[protocount].name, (char*)(lpName->lpszShortName));
  memcpy(&NetPlay.protocols[protocount].connection, lpConnection, dwConnectionSize);
  protocount = protocount + 1;

  return (TRUE);
}

// ////////////////////////////////////////////////////////////////////////
// call with true to enumerate available protocols.
BOOL NETfindProtocol(BOOL Lob)
{
  HRESULT hr;

  protocount = 0;
  ZeroMemory(NetPlay.protocols, (MaxProtocols*sizeof(PROTO))); // clear the proto list.

  if (Lob == TRUE)
    hr = IDirectPlayX_EnumConnections(glpDP, &GAME_GUID, NETfindProtocolCallback, NULL, DPCONNECTION_DIRECTPLAYLOBBY);
  else
    hr = IDirectPlayX_EnumConnections(glpDP, &GAME_GUID, NETfindProtocolCallback, NULL, 0);

  if (hr != DP_OK)
  {
    Neuron::DebugTrace("NETPLAY: Find Protocol failed\n");
    return (FALSE);
  }
  return (TRUE);
}

// ////////////////////////////////////////////////////////////////////////
// File Transfer programs.
// uses guaranteed messages to send files between clients.

// send file. it returns % of file sent. when 100 it's complete. call until it returns 100.

UBYTE NETsendFile(BOOL newFile, CHAR* fileName, DPID player)
{
  static UDWORD fileSize, currPos;
  static FILE* pFileHandle;
  UDWORD bytesRead;
  UBYTE inBuff[2048];
  NETMSG msg;

  if (newFile)
  {
    // open the file.
    pFileHandle = fopen(fileName, "rb"); // check file exists
    if (pFileHandle == nullptr)
    {
      Neuron::DebugTrace("NETsendFile: Failed\n");
      return 0; // failed
    }
    // get the file's size.
    fileSize = 0;
    currPos = 0;
    do
    {
      bytesRead = fread(&inBuff, 1, sizeof(inBuff), pFileHandle);
      fileSize += bytesRead;
    }
    while (bytesRead != 0);
    fclose(pFileHandle); // close 
    pFileHandle = fopen(fileName, "rb"); // reopen
  }
  // read some bytes.
  bytesRead = fread(&inBuff, 1, sizeof(inBuff), pFileHandle);

  // form a message
  NetAdd(msg, 0, fileSize); // total bytes in this file.
  NetAdd(msg, 4, bytesRead); // bytes in this packet	
  NetAdd(msg, 8, currPos); // start byte
  msg.body[12] = strlen(fileName);
  msg.size = 13;
  NetAddSt(msg, msg.size, fileName);
  msg.size += strlen(fileName);
  memcpy(&(msg.body[msg.size]), &inBuff, bytesRead);
  msg.size += bytesRead;
  msg.type = FILEMSG;
  msg.paddedBytes = 0;
  if (player == 0)
    NETbcast(&msg,TRUE); // send it.
  else
    NETsend(&msg, player,TRUE);

  currPos += bytesRead; // update position!
  if (currPos == fileSize)
    fclose(pFileHandle);

  return (currPos * 100) / fileSize;
}

// recv file. it returns % of the file so far recvd.
UBYTE NETrecvFile(NETMSG* pMsg)
{
  UDWORD pos, fileSize, currPos, bytesRead;
  CHAR fileName[128], len;
  static FILE* pFileHandle;

  //read incoming bytes.
  NetGet(pMsg, 0, fileSize);
  NetGet(pMsg, 4, bytesRead);
  NetGet(pMsg, 8, currPos);

  // read filename
  len = pMsg->body[12];
  memcpy(fileName, &(pMsg->body[13]), len);
  fileName[len] = '\0'; // terminate string.
  pos = 13 + len;

  if (currPos == 0) // first packet!
    pFileHandle = fopen(fileName, "wb"); // create a new file.

  //write packet to the file.
  fwrite(&(pMsg->body[pos]), 1, bytesRead, pFileHandle);

  if (currPos + bytesRead == fileSize) // last packet
    fclose(pFileHandle);

  //return the percent count.
  return ((currPos + bytesRead) * 100) / fileSize;
}
