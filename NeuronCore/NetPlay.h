/*
 * Netplay.h
 *
 * Alex Lee sep97.
 */

#ifndef _netplay_h
#define _netplay_h

// ////////////////////////////////////////////////////////////////////////
// Include this file in your game to add multiplayer facilities.
//
// This used to open with <dplay.h> and <dplobby.h>, and the comment on the
// constants below used to explain that they were here so the game did not have
// to include them -- while including them. The transport is QUIC now and lives
// behind Transport.h; nothing above this line names it either.

#include "Transport.h"	// the seam, and through it NetTypes.h

// ////////////////////////////////////////////////////////////////////////
// A game as the browser sees it, which is to say before joining one. What
// DirectPlay carried in a DPSESSIONDESC2, and what Transport::FindSessions
// fills in.
using GAMESTRUCT = struct
{
  char name[StringSize];
  char address[Transport::AddressSize];
  UDWORD currentPlayers;
  UDWORD maxPlayers;
  BOOL bJoinDisabled;
  DWORD flags[Transport::GameFlagCount];
};

// ////////////////////////////////////////////////////////////////////////
// Message information. ie. the packets sent between machines.
typedef struct
{
  unsigned short size;
  unsigned char paddedBytes; // numberofbytes appended for encryption
  unsigned char type;
  char body[MaxMsgSize];
} NETMSG, *LPNETMSG;

// ENCRYPTFLAG was here, added to a message type to mark the packet encrypted.
// QUIC encrypts every byte, so packet encryption went with the swap and the
// type field is a plain type again. paddedBytes above is what is left of it:
// still on the wire, always zero, and not worth a format change to remove.
#define		FILEMSG			254			// a file packet

// ////////////////////////////////////////////////////////////////////////
// Player information. Update using NETplayerinfo
using PLAYER = struct
{
  NETPLAYERID dpid;
  char name[StringSize];
  BOOL bHost; // a bool.
};

// ////////////////////////////////////////////////////////////////////////
// all the luvly Netplay info....
typedef struct
{
  GAMESTRUCT games[MaxGames]; // the collection of games
  PLAYER players[MaxNumberOfPlayers]; // the array of players.
  UDWORD playercount; // number of players in game.

  NETPLAYERID dpidPlayer; // ID of the local player

  BOOL bComms; // actually do the comms?
  BOOL bHost; // TRUE if we are hosting the session

  UDWORD cryptKey[4]; // 4*32 bit key, now only for the stats file and hashes
} NETPLAY, *LPNETPLAY;

/* Gone with DirectPlay: the protocol list (there is one transport now), the
 * IDirectPlay4A pointer and its player event, bSpectator (NETspectate had no
 * callers and neither did NETisSpectator), and bEncryptAllPackets.
 */

// ////////////////////////////////////////////////////////////////////////
// variables

extern NETPLAY NetPlay;
extern LPNETPLAY lpNetPlay;

/* Where the joiner's typed address goes. There is no discovery in this build
 * -- see Transport.h -- so this is how the game reaches a host, and it is
 * what Transport::Join is given.
 */
extern char NETjoinAddress[Transport::AddressSize];

// ////////////////////////////////////////////////////////////////////////
// functions available to you.
/* The GUID is gone with the argument: it identified the application to
 * DirectPlay's session enumeration, and QUIC does the same job with an ALPN
 * the transport chooses for itself.
 */
extern BOOL NETinit(BOOL bFirstCall);
extern BOOL NETsend(NETMSG* msg, NETPLAYERID player, BOOL guarantee); // send to player, possibly guaranteed
extern BOOL NETbcast(NETMSG* msg, BOOL guarantee); // broadcast to everyone, possibly guaranteed
extern BOOL NETrecv(NETMSG* msg); // recv a message if possible

extern UCHAR NETsendFile(BOOL newFile, CHAR* fileName, NETPLAYERID player); // send file chunk.
extern UCHAR NETrecvFile(NETMSG* pMsg); // recv file chunk

extern HRESULT NETclose(VOID); // close current game
extern HRESULT NETshutdown(VOID); // leave the game in play.

extern UDWORD NETgetBytesSent(VOID); // return bytes sent/recv.  call regularly for good results
extern UDWORD NETgetPacketsSent(VOID); // return packets sent/recv.  call regularly for good results
extern UDWORD NETgetBytesRecvd(VOID); // return bytes sent/recv.  call regularly for good results
extern UDWORD NETgetPacketsRecvd(VOID); // return packets sent/recv.  call regularly for good results
extern UDWORD NETgetRecentBytesSent(VOID); // more immediate functions.
extern UDWORD NETgetRecentPacketsSent(VOID);
extern UDWORD NETgetRecentBytesRecvd(VOID);
extern UDWORD NETgetRecentPacketsRecvd(VOID);

// from netjoin.c
extern DWORD NETgetGameFlagsUnjoined(UDWORD gameid, UDWORD flag); // return one of the four flags(dword) about the game.
extern BOOL NETsetGameFlags(UDWORD flag, DWORD value); // set game flag(1-4) to value.
extern BOOL NEThaltJoining(VOID); // stop new players joining this game

/* Fills NetPlay.games. Answers nothing until there is a server to ask -- the
 * asynchronous flag went with DirectPlay's enumeration, which every caller
 * passed the same value anyway.
 */
extern BOOL NETfindGame(VOID);
extern BOOL NETjoinGame(const char* address, LPSTR playername); // join the game at an address
extern BOOL NEThostGame(LPSTR SessionName, LPSTR PlayerName, // host a game
                        DWORD one, DWORD two, DWORD three, DWORD four, UDWORD plyrs);

//from netusers.c
extern BOOL NETuseNetwork(BOOL val); // TURN on/off networking.
extern UDWORD NETplayerInfo(VOID); // count players in this game.
extern BOOL NETchangePlayerName(NETPLAYERID dpid, char* newName); // change a players name.

//from netsupp
extern BOOL NETlogEntry(CHAR* str, UDWORD a, UDWORD b);
extern BOOL NETstopLogging(VOID);
extern BOOL NETstartLogging(VOID);

/* What is left of NetCrypt.cpp. NETmanglePacket and NETunmanglePacket went
 * with the swap -- QUIC encrypts every byte, so the packet half had nothing
 * left to do. The other three are not networking and never were: the first
 * pair obfuscates the player-stats file on disk, NEThashFile and NEThashVal
 * catch a mismatched executable at join, and NEThashBuffer backs Data.cpp's
 * cheat hashing.
 */
extern BOOL NETsetKey(UDWORD c1, UDWORD c2, UDWORD c3, UDWORD c4);
extern BOOL NETmangleData(long* input, long* result, UDWORD dataSize);
extern BOOL NETunmangleData(long* input, long* result, UDWORD dataSize);
extern UDWORD NEThashFile(char* pFileName);
extern UCHAR NEThashVal(UDWORD value);
extern UDWORD NEThashBuffer(unsigned char* pData, UDWORD size);

/* YOU MUST PROVIDE THIS FUNCTION -- as the game always has, under its old name
 * DirectPlaySystemMessageHandler. Join, leave and the host going away used to
 * arrive as DirectPlay system messages and now arrive as transport events.
 */
extern BOOL NETeventHandler(const Transport::Event* psEvent);

// Some shortcuts to help you along!
#define NetAdd(m,pos,thing) \
	memcpy(&(m.body[pos]),&(thing),sizeof(thing))

#define NetAdd2(m,pos,thing) \
	memcpy( &((*m).body[pos]), &(thing), sizeof(thing))

/* Width-explicit variants.  Callers that need to put a value on the wire as a
   narrower type used to write NetAdd(m,pos,((UBYTE)value)); taking the address
   of a cast is an MSVC-only extension and is invalid C++.  Naming the type
   materialises a temporary instead, so the number of bytes copied is still
   sizeof(type) and the message layout is unchanged. */
#define NetAddType(m,pos,type,value) \
	do { type netAddTmp_ = (type)(value); \
		 memcpy(&(m.body[pos]), &netAddTmp_, sizeof(netAddTmp_)); } while (0)

#define NetAdd2Type(m,pos,type,value) \
	do { type netAddTmp_ = (type)(value); \
		 memcpy(&((*m).body[pos]), &netAddTmp_, sizeof(netAddTmp_)); } while (0)

#define NetGet(m,pos,thing) \
	memcpy(&(thing),&(m->body[pos]),sizeof(thing))

#define NetAddSt(m,pos,stri) \
	strcpy(&(m.body[pos]),stri)

#define NetGetSt(m,pos,stri) \
	strcpy(stri,&(m->body[pos]))

#endif
