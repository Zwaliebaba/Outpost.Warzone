/*
 * LoopbackTransport.h
 *
 * The boundary between the two halves, before there are two machines.
 */

#pragma once

#include "Protocol.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

namespace Neuron
{

/// Carries encoded messages between a client and a server in one process.
///
/// This is rung 1 of the separation ladder in Docs/ServerAuthority.md: the
/// embedded server and its client exchange exactly the bytes they would
/// exchange over a network, through queues instead of sockets. Neither end can
/// tell which it is talking through, and that is the whole point -- the moment
/// either end can reach the other's objects instead of its bytes, the server
/// stops being separable and rung 3 will not work.
///
/// Paying the encode and decode cost in single player is deliberate. It means
/// the protocol is exercised by everyone who boots the campaign rather than
/// only by the rare networked session.
///
/// **What this does not model.** Delivery here is ordered and lossless. QUIC
/// datagrams are neither, so the Snapshot channel's "may be dropped, the next
/// one supersedes it" behaviour is not exercised until rung 3 puts a real
/// network underneath. That is the same gap Phase 5 recorded about NetTest:
/// loss and reordering are not simulated, so what depends on them is not
/// tested here.
class LoopbackTransport
{
public:
  /// Which half of the link a call speaks for.
  enum class End : std::uint8_t
  {
    Client,
    Server,
  };

  /// A message as it waits: the channel it was sent on, and its bytes.
  struct Message
  {
    NetChannel channel = NetChannel::Session;
    std::vector<std::byte> bytes;
  };

  /// Queues _bytes for the other end. The bytes are copied, so the caller's
  /// buffer -- typically a NetWriter's stack buffer -- can be reused at once.
  void Send(End _from, NetChannel _channel, std::span<const std::byte> _bytes);

  /// Takes the next message waiting for _to, or returns FALSE when there is
  /// none.
  ///
  /// Channels are drained in declaration order, so Session traffic is seen
  /// ahead of world data and Bulk last. That ordering is a policy rather than
  /// a guarantee the protocol makes: a saturated Session channel would starve
  /// Bulk, which in-process cannot happen and over a real link is the
  /// transport's problem rather than this one's.
  [[nodiscard]] bool Receive(End _to, Message& _outMessage);

  /// How many messages are waiting for _to, across every channel.
  [[nodiscard]] std::size_t Pending(End _to) const noexcept;

  /// Drops everything queued in both directions, for ending a session.
  void Clear() noexcept;

private:
  /// One queue per channel per direction, so the ordering guarantee is
  /// per-channel -- which is what the protocol promises. A single shared queue
  /// would quietly strengthen that into a global order QUIC will not
  /// reproduce, and code written against the stronger promise would break at
  /// rung 3 rather than here.
  using ChannelQueues = std::array<std::deque<Message>, static_cast<std::size_t>(NetChannel::Count)>;

  [[nodiscard]] static std::size_t Index(NetChannel _channel) noexcept { return static_cast<std::size_t>(_channel); }

  [[nodiscard]] ChannelQueues& QueuesFor(End _to) noexcept { return _to == End::Client ? m_toClient : m_toServer; }
  [[nodiscard]] const ChannelQueues& QueuesFor(End _to) const noexcept { return _to == End::Client ? m_toClient : m_toServer; }

  ChannelQueues m_toClient;
  ChannelQueues m_toServer;
};

} // namespace Neuron
