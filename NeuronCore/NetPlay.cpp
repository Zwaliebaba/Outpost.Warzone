#include "pch.h"

// ////////////////////////////////////////////////////////////////////////
// includes
#include "Frame.h"
#include "NetPlay.h"
#include "time.h"			//for stats

// ////////////////////////////////////////////////////////////////////////
// Variables
NETPLAY NetPlay;

/* Where the joiner's typed address goes, since nothing discovers hosts. */
char NETjoinAddress[NETTRANS_ADDRESS_SIZE] = "";

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

// ////////////////////////////////////////////////////////////////////////
// Prototypes
BOOL NETinit(BOOL bFirstCall);
HRESULT NETshutdown(VOID);
UDWORD NETgetBytesSent(VOID);
UDWORD NETgetPacketsSent(VOID);
UDWORD NETgetBytesRecvd(VOID);
UDWORD NETgetPacketsRecvd(VOID);
UDWORD NETgetRecentBytesSent(VOID);
UDWORD NETgetRecentPacketsSent(VOID);
UDWORD NETgetRecentBytesRecvd(VOID);
UDWORD NETgetRecentPacketsRecvd(VOID);
BOOL NETsend(NETMSG* msg, NETPLAYERID player, BOOL guarantee);
BOOL NETbcast(NETMSG* msg, BOOL guarantee);
BOOL NETrecv(NETMSG* msg);
UBYTE NETsendFile(BOOL newFile, CHAR* fileName, NETPLAYERID player);
UBYTE NETrecvFile(NETMSG* pMsg);

// ////////////////////////////////////////////////////////////////////////
// setup stuff
BOOL NETinit(BOOL bFirstCall)
{
  UDWORD i;

  if (bFirstCall)
  {
    Neuron::DebugTrace("NETPLAY: Init called, MORNIN' \n ");

    NetPlay.dpidPlayer = 0;
    NetPlay.bHost = 0;
    NetPlay.bComms = TRUE;

    /* The key no longer has any packets to encrypt -- QUIC does that -- but
     * NETmangleData still obfuscates the stats file with it and NEThashVal
     * still derives the executable check from it, so it is still seeded.
     */
    NETsetKey(0x2fe8f810, 0xb72a5, 0x114d0, 0x2a7); // j-random key to get us started

    for (i = 0; i < MaxNumberOfPlayers; i++)
      ZeroMemory(&NetPlay.players[i], sizeof(PLAYER));
    for (i = 0; i < MaxGames; i++)
      ZeroMemory(&NetPlay.games[i], sizeof(GAMESTRUCT));

    NETuseNetwork(TRUE);
    NETstartLogging();
  }

  /* Brings MsQuic up. There is nothing per-connection here: that happens at
   * NEThostGame or NETjoinGame, where DirectPlay used to need an interface
   * created and a service provider chosen first.
   */
  if (!nettrans_Startup())
  {
    Neuron::DebugTrace("NETPLAY: transport would not start\n");
    return FALSE;
  }

  Neuron::DebugTrace("NETPLAY: init success\n ");
  return TRUE;
}

// ////////////////////////////////////////////////////////////////////////
// SHUTDOWN THE CONNECTION.
HRESULT NETshutdown(VOID)
{
  nettrans_Shutdown();
  NetPlay.dpidPlayer = 0;

  NETstopLogging();
  Neuron::DebugTrace("NETPLAY: shutdown success\n");
  return S_OK;
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
// How many bytes of a NETMSG actually travel: the header plus the body it
// says it is carrying. Was written out longhand at each of the four call
// sites, twice per send, and got it right every time -- but it is the one
// number the whole wire format depends on.
static UDWORD netMsgBytes(const NETMSG* msg)
{
  return msg->size + sizeof(msg->size) + sizeof(msg->type) + sizeof(msg->paddedBytes);
}

// ////////////////////////////////////////////////////////////////////////
// Send a message to a player, option to guarantee message
BOOL NETsend(NETMSG* msg, NETPLAYERID player, BOOL guarantee)
{
  UDWORD size;

  NETlogEntry("send", msg->type, msg->size);

  if (!NetPlay.bComms)
    return TRUE;

  if (msg->size > MaxMsgSize)
    Neuron::Fatal("NETPLAY: Message too large passed to NETsend");

  size = netMsgBytes(msg);

  if (!nettrans_Send(player, msg, size, guarantee))
    return FALSE;

  nStats.bytesSent += size;
  nStats.packetsSent += 1;
  return TRUE;
}

// ////////////////////////////////////////////////////////////////////////
// broadcast a message to all players.
BOOL NETbcast(NETMSG* msg, BOOL guarantee)
{
  UDWORD size;

  NETlogEntry("bcst", msg->type, msg->size);

  if (!NetPlay.bComms)
    return TRUE;

  if (msg->size > MaxMsgSize)
    Neuron::Fatal("NETPLAY: Message Too large passed to NETbcast");

  size = netMsgBytes(msg);

  /* As with DPID_ALLPLAYERS, this does not come back to the sender. The game
   * depends on it: removeFeature broadcasts the destruction and then removes
   * the feature locally, so a loopback copy would be applied twice.
   */
  if (!nettrans_Broadcast(msg, size, guarantee))
    return FALSE;

  nStats.bytesSent += size;
  nStats.packetsSent += 1;
  return TRUE;
}

// ////////////////////////////////////////////////////////////////////////
// receive a message over the current connection
//
// Returns FALSE on a transport event as well as on an empty queue, which is
// what the DPID_SYSMSG branch did: recvMessage loops on this, and a join or a
// leave stops the loop for that frame rather than being processed alongside
// ordinary traffic.
BOOL NETrecv(NETMSG* pMsg)
{
  NETTRANS_EVENT sEvent;
  NETPLAYERID from;
  UDWORD size = 0;

  if (!NetPlay.bComms)
    return FALSE;

  /* Once per call rather than once per frame. The transport needs a pump for
   * the work that wants a clock rather than a callback, and this is the one
   * function the game calls unconditionally while a game is running.
   */
  nettrans_Update();

  if (nettrans_NextEvent(&sEvent))
  {
    NETlogEntry("SYSM", 0, 0);
    NETeventHandler(&sEvent);
    return FALSE;
  }

  if (!nettrans_Receive(pMsg, &size, &from))
    return FALSE;

  nStats.bytesRecvd += size;
  nStats.packetsRecvd += 1;

  NETlogEntry("recv", pMsg->type, pMsg->size);

  return TRUE;
}

// ////////////////////////////////////////////////////////////////////////
// File Transfer programs.
// uses guaranteed messages to send files between clients.

// send file. it returns % of file sent. when 100 it's complete. call until it returns 100.

UBYTE NETsendFile(BOOL newFile, CHAR* fileName, NETPLAYERID player)
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
