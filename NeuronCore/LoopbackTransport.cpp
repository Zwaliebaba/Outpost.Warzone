#include "pch.h"
#include "LoopbackTransport.h"

namespace Neuron
{

void LoopbackTransport::Send(End _from, NetChannel _channel, std::span<const std::byte> _bytes)
{
  if (_channel >= NetChannel::Count)
    return;

  /* Addressed by where it is going rather than where it came from: a client
     send is a server receive. */
  ChannelQueues& queues = QueuesFor(_from == End::Client ? End::Server : End::Client);

  Message message;
  message.channel = _channel;
  message.bytes.assign(_bytes.begin(), _bytes.end());
  queues[Index(_channel)].push_back(std::move(message));
}

bool LoopbackTransport::Receive(End _to, Message& _outMessage)
{
  ChannelQueues& queues = QueuesFor(_to);

  for (std::deque<Message>& queue : queues)
  {
    if (queue.empty())
      continue;

    _outMessage = std::move(queue.front());
    queue.pop_front();
    return true;
  }

  return false;
}

std::size_t LoopbackTransport::Pending(End _to) const noexcept
{
  std::size_t total = 0;
  for (const std::deque<Message>& queue : QueuesFor(_to))
    total += queue.size();

  return total;
}

void LoopbackTransport::Clear() noexcept
{
  for (std::deque<Message>& queue : m_toClient)
    queue.clear();
  for (std::deque<Message>& queue : m_toServer)
    queue.clear();
}

} // namespace Neuron
