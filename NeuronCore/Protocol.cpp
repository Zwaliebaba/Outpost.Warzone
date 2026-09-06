#include "pch.h"
#include "Protocol.h"

#include <algorithm>
#include <cstring>
#include <string_view>

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

void ServerEnter::Encode(NetWriter& _writer) const
{
  _writer.U32(entityId);
  _writer.U8(static_cast<std::uint8_t>(kind));
  _writer.U8(player);
  _writer.U16(x);
  _writer.U16(y);
  _writer.U16(z);
  _writer.F32(direction);
}

ServerEnter ServerEnter::Decode(NetReader& _reader)
{
  ServerEnter enter;
  enter.entityId = _reader.U32();

  /* A kind byte off the wire is a byte a peer chose, and there is no sensible
     default entity, so an unknown one truncates the read instead. The record
     comes back inert and the caller's Ok() check drops it, which is what it
     would do with any other unreadable message. */
  const std::uint8_t raw = _reader.U8();
  if (raw < static_cast<std::uint8_t>(EntityKind::Count))
    enter.kind = static_cast<EntityKind>(raw);
  else
    _reader.Invalidate();

  enter.player = _reader.U8();
  enter.x = _reader.U16();
  enter.y = _reader.U16();
  enter.z = _reader.U16();
  enter.direction = _reader.F32();
  return enter;
}

void ServerUpdate::Encode(NetWriter& _writer) const
{
  _writer.U32(entityId);
  _writer.U16(x);
  _writer.U16(y);
  _writer.U16(z);
  _writer.F32(direction);
}

ServerUpdate ServerUpdate::Decode(NetReader& _reader)
{
  ServerUpdate update;
  update.entityId = _reader.U32();
  update.x = _reader.U16();
  update.y = _reader.U16();
  update.z = _reader.U16();
  update.direction = _reader.F32();
  return update;
}

void ServerLeave::Encode(NetWriter& _writer) const
{
  _writer.U32(entityId);
}

ServerLeave ServerLeave::Decode(NetReader& _reader)
{
  ServerLeave leave;
  leave.entityId = _reader.U32();
  return leave;
}

void ServerDestroy::Encode(NetWriter& _writer) const
{
  _writer.U32(entityId);
}

ServerDestroy ServerDestroy::Decode(NetReader& _reader)
{
  ServerDestroy destroy;
  destroy.entityId = _reader.U32();
  return destroy;
}

void ClientReady::Encode(NetWriter& _writer) const
{
  _writer.U32(mapHash);
}

ClientReady ClientReady::Decode(NetReader& _reader)
{
  ClientReady ready;
  ready.mapHash = _reader.U32();
  return ready;
}

void ServerKick::Encode(NetWriter& _writer) const
{
  _writer.U8(static_cast<std::uint8_t>(reason));
}

ServerKick ServerKick::Decode(NetReader& _reader)
{
  ServerKick kick;

  /* A reason this build has no name for folds to Unstated rather than into
     the enum: the session is over either way, and a wild value is not a reason
     anyone can act on. */
  const std::uint8_t raw = _reader.U8();
  kick.reason = raw < static_cast<std::uint8_t>(KickReason::Count) ? static_cast<KickReason>(raw) : KickReason::Unstated;
  return kick;
}

void ClientSessionControl::Encode(NetWriter& _writer) const
{
  _writer.U32(sequence);
  _writer.U8(static_cast<std::uint8_t>(kind));
  _writer.F32(value);
}

ClientSessionControl ClientSessionControl::Decode(NetReader& _reader)
{
  ClientSessionControl control;
  control.sequence = _reader.U32();

  /* A kind this build has no name for is not a control it can carry out, and
     there is no harmless default to fold it to, so the record is invalidated
     and the caller's Ok() drops it. */
  const std::uint8_t raw = _reader.U8();
  if (raw < static_cast<std::uint8_t>(SessionControlKind::Count))
    control.kind = static_cast<SessionControlKind>(raw);
  else
    _reader.Invalidate();

  control.value = _reader.F32();
  return control;
}

void ServerCommandReject::Encode(NetWriter& _writer) const
{
  _writer.U32(sequence);
  _writer.U8(static_cast<std::uint8_t>(command));
  _writer.U8(static_cast<std::uint8_t>(reason));
}

ServerCommandReject ServerCommandReject::Decode(NetReader& _reader)
{
  ServerCommandReject reject;
  reject.sequence = _reader.U32();

  const std::uint8_t command = _reader.U8();
  if (IsKnown(static_cast<ClientMessage>(command)))
    reject.command = static_cast<ClientMessage>(command);
  else
    _reader.Invalidate();

  /* A reason this build has no name for is still a refusal. NotPermitted is
     the one that says least, which is the honest reading of a code we cannot
     read. */
  const std::uint8_t reason = _reader.U8();
  reject.reason = reason < static_cast<std::uint8_t>(RejectReason::Count) ? static_cast<RejectReason>(reason)
                                                                          : RejectReason::NotPermitted;
  return reject;
}

namespace
{
/// Copies _from into a fixed field, truncating to fit and always terminating.
template <std::size_t Chars>
void CopyField(char (&_field)[Chars], const char* _from) noexcept
{
  if (_from == nullptr)
  {
    _field[0] = '\0';
    return;
  }

  const std::size_t length = std::min(std::strlen(_from), Chars - 1);
  std::memcpy(_field, _from, length);
  _field[length] = '\0';
}
} // namespace

void ServerUiEvent::SetName(const char* _name) noexcept
{
  CopyField(name, _name);
}

void ServerUiEvent::SetText(const char* _text) noexcept
{
  CopyField(text, _text);
}

void ServerUiEvent::Encode(NetWriter& _writer) const
{
  _writer.U8(static_cast<std::uint8_t>(kind));
  _writer.U8(player);
  _writer.U32(a);
  _writer.U32(b);
  _writer.Text(std::string_view{name, std::strlen(name)});
  _writer.Text(std::string_view{text, std::strlen(text)});
}

ServerUiEvent ServerUiEvent::Decode(NetReader& _reader)
{
  ServerUiEvent event;

  /* A kind this build has no name for is nothing it can show. There is no
     harmless default, so the record is invalidated and dropped whole. */
  const std::uint8_t raw = _reader.U8();
  if (raw < static_cast<std::uint8_t>(UiEventKind::Count))
    event.kind = static_cast<UiEventKind>(raw);
  else
    _reader.Invalidate();

  event.player = _reader.U8();
  event.a = _reader.U32();
  event.b = _reader.U32();
  _reader.Text(event.name);
  _reader.Text(event.text);
  return event;
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
