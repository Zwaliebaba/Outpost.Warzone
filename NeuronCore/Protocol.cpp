#include "pch.h"
#include "Protocol.h"

namespace Neuron
{

void ClientHello::Encode(NetWriter& _writer) const
{
  _writer.U16(protocolVersion);
  _writer.U32(buildHash);
}

ClientHello ClientHello::Decode(NetReader& _reader)
{
  ClientHello hello;
  hello.protocolVersion = _reader.U16();
  hello.buildHash = _reader.U32();
  return hello;
}

void ServerHello::Encode(NetWriter& _writer) const
{
  _writer.U8(static_cast<std::uint8_t>(result));
  _writer.U16(tickMs);
  _writer.U32(connectionId);
}

ServerHello ServerHello::Decode(NetReader& _reader)
{
  ServerHello hello;

  /* A result byte off the wire is a byte a peer chose, so an unknown one is
     folded to ProtocolMismatch rather than cast into the enum and switched on.
     Refusing for a reason we cannot name is still refusing. */
  const std::uint8_t raw = _reader.U8();
  hello.result = raw < static_cast<std::uint8_t>(HandshakeResult::Count)
                   ? static_cast<HandshakeResult>(raw)
                   : HandshakeResult::ProtocolMismatch;

  hello.tickMs = _reader.U16();
  hello.connectionId = _reader.U32();
  return hello;
}

void ServerStart::Encode(NetWriter& _writer) const
{
  _writer.U32(startTick);
  _writer.U16(countdownMs);
  _writer.U32(mapHash);
}

ServerStart ServerStart::Decode(NetReader& _reader)
{
  ServerStart start;
  start.startTick = _reader.U32();
  start.countdownMs = _reader.U16();
  start.mapHash = _reader.U32();
  return start;
}

void ServerTick::Encode(NetWriter& _writer) const
{
  _writer.U32(tick);
}

ServerTick ServerTick::Decode(NetReader& _reader)
{
  ServerTick value;
  value.tick = _reader.U32();
  return value;
}

HandshakeResult Consider(const ClientHello& _hello, std::uint32_t _serverBuildHash) noexcept
{
  if (_hello.protocolVersion != ProtocolVersion)
    return HandshakeResult::ProtocolMismatch;

  /* A server that does not know its own build hash cannot check anyone's, which
     is the case in a solo session where both halves are the same binary. */
  if (_serverBuildHash != 0 && _hello.buildHash != _serverBuildHash)
    return HandshakeResult::BuildMismatch;

  return HandshakeResult::Accepted;
}

} // namespace Neuron
