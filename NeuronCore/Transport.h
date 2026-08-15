/*
 * Transport.h
 *
 * The seam between the game's networking and whatever moves the bytes.
 *
 * DirectPlay was on one side of it and MsQuic is on the other. Nothing above
 * this header names either, which is the point: when the implementation was
 * replaced, NetPlay.cpp, NetJoin.cpp and NetUsers.cpp were the only files that
 * had to notice.
 *
 * There is one implementation and therefore no hierarchy -- a base class for a
 * single derived class is ceremony. If a second transport ever arrives, this
 * becomes the base and the QUIC one becomes QuicTransport; until then the
 * concept and the implementation are the same type.
 *
 * This header deliberately includes no networking header of its own. If it ever
 * needs one, the seam has leaked.
 */

#pragma once

/***************************************************************************/

/* NetTypes.h rather than NetPlay.h, which is the other way round from how this
 * started: NetPlay.h now includes this header, so taking anything from it
 * would be a cycle.
 *
 * BOOL, DWORD and UDWORD are assumed to be visible already, as every header in
 * this tree assumes -- Windows.h and Types.h both arrive through pch.h. So a
 * unit that wants this header includes Frame.h first, like every other unit.
 */
#include "NetTypes.h"	// NETPLAYERID, StringSize, MaxGames, MaxMsgSize

namespace Neuron
{

/***************************************************************************/
/* What the transport reports upwards, replacing DirectPlay's system messages.
 *
 * NETrecv used to notice DPID_SYSMSG and hand the message to
 * DirectPlaySystemMessageHandler, which switched on DPSYS_CREATEPLAYERORGROUP,
 * DPSYS_DESTROYPLAYERORGROUP and DPSYS_HOST. Those three become the three
 * below, except that the last one is not a promotion: the session ends when
 * the host goes, so HostLost is terminal.
 */
/***************************************************************************/

enum class TransportEventType : std::uint8_t
{
  PlayerJoined,
  PlayerLeft,
  HostLost
};

/***************************************************************************/
/* Moving the bytes.
 *
 * Every member is static: there is one transport per process, as there was one
 * DirectPlay interface, and the session it holds is process-wide state rather
 * than something a caller could sensibly own two of.
 */
/***************************************************************************/

class Transport
{
public:
  static constexpr UDWORD GameFlagCount = 4;
  static constexpr UDWORD AddressSize = 64;

  /* The largest message this seam will carry, and therefore the smallest
   * buffer Receive may be given. It is the size of NETMSG -- size, paddedBytes
   * and type, then the body -- because NETMSG is the only thing the game
   * sends, and a transport that would accept more than the receiver can hold
   * is a buffer overrun waiting for a peer to ask for one.
   */
  static constexpr UDWORD MaxMessageBytes = MaxMsgSize + 4;

  /* A session as a browser sees it, which is to say before joining one.
   *
   * The four flags are the part that is easy to miss. DirectPlay carried them
   * in DPSESSIONDESC2's dwUser1..4, and NETgetGameFlagsUnjoined reads them out
   * of a session the player has not joined yet -- the multiplayer browser
   * shows map, version and player count from them. Whatever lists sessions has
   * to carry them, or the browser has nothing to display.
   */
  struct SessionInfo
  {
    char name[StringSize];
    char address[AddressSize];		/* how to reach the host                 */
    UDWORD currentPlayers;
    UDWORD maxPlayers;
    DWORD gameFlags[GameFlagCount];
  };

  struct Event
  {
    TransportEventType type;
    NETPLAYERID player;
    char name[StringSize];
  };

  /* ---- Lifecycle ------------------------------------------------------- */

  static BOOL Startup();
  static void Shutdown();

  /* Called once a frame. Connection maintenance and the queues behind Receive
   * and NextEvent are serviced here, so that nothing above this line has to
   * think about which thread it is on.
   */
  static void Update();

  /* ---- Sessions -------------------------------------------------------- */

  static BOOL Host(const char _sessionName[], const char _playerName[], UDWORD _maxPlayers, const DWORD _gameFlags[]);

  /* Fills the array with what is reachable, and returns how many.
   * Synchronous: DirectPlay offered an asynchronous enumeration and
   * NETfindGame took a flag for it, but every caller passed the same value.
   *
   * Answers nothing today, and the join screen takes a typed address instead.
   * LAN broadcast discovery was deliberately not built: the destination is a
   * relay server that owns the connections, where listing sessions is a query
   * to a known server. That query goes here, behind this signature, and the
   * game above the seam does not learn about it.
   */
  static UDWORD FindSessions(SessionInfo _sessions[], UDWORD _max);

  static BOOL Join(const char _address[], const char _playerName[]);
  static void Leave();

  /* The four flags again, on a session this machine is hosting. Reading them
   * back for a session not joined is what FindSessions is for.
   */
  static BOOL SetGameFlags(const DWORD _gameFlags[]);
  static BOOL GetGameFlags(DWORD _gameFlags[]);

  /* ---- Players --------------------------------------------------------- */

  static NETPLAYERID LocalPlayer();
  static BOOL IsHost();
  static UDWORD PlayerCount();
  static BOOL PlayerName(NETPLAYERID _player, char _name[], UDWORD _size);

  /* Fills the array with everybody in the session and returns how many, so the
   * game can rebuild its own roster. DirectPlay offered this as an enumeration
   * with a callback; a list is the same thing without the inversion.
   */
  static UDWORD PlayerList(NETPLAYERID _players[], UDWORD _max);

  /* Whether a given player is the one hosting, which is not the same question
   * as IsHost -- that one asks about this machine.
   */
  static BOOL IsHostPlayer(NETPLAYERID _player);

  /* Renames the local player everywhere. Only the local player: DirectPlay
   * would let a machine rename anyone and nothing ever did.
   */
  static BOOL SetLocalName(const char _name[]);

  /* Stops anyone else joining a session this machine hosts. */
  static BOOL CloseToJoiners();

  /* ---- Data ------------------------------------------------------------
   *
   * Reliable sends are ordered with respect to each other, which is what the
   * lockstep command traffic depends on and what DPSEND_GUARANTEED gave it.
   * Unreliable sends have no ordering and may be dropped -- and above roughly
   * 1440 bytes there is no datagram big enough, so they are sent reliably
   * instead rather than dropped.
   *
   * A client sending to another client is relayed by the host, because the
   * host is the only machine everyone is connected to. A broadcast never
   * returns to its sender, as DPID_ALLPLAYERS did not: the game applies the
   * effect of its own broadcasts locally as it sends them.
   */

  static BOOL Send(NETPLAYERID _to, const void* _data, UDWORD _size, BOOL _reliable);
  static BOOL Broadcast(const void* _data, UDWORD _size, BOOL _reliable);

  /* Returns FALSE when nothing is waiting. On TRUE, *_size is how much was
   * written and *_from is who sent it. _data must have room for
   * MaxMessageBytes bytes.
   */
  static BOOL Receive(void* _data, UDWORD* _size, NETPLAYERID* _from);

  /* Drained in the same loop as Receive. Returns FALSE when empty. */
  static BOOL NextEvent(Event* _event);

  /* ---- Statistics, which the game shows on its multiplayer screens ------ */

  static UDWORD BytesSent();
  static UDWORD BytesReceived();
  static UDWORD PacketsSent();
  static UDWORD PacketsReceived();
};

/***************************************************************************/

} // namespace Neuron

/***************************************************************************/
