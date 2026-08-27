/*
 * NetWriter.h
 *
 * Writing the wire format, one named width at a time.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Neuron
{

/// Assembles a message body: every field little-endian, byte by byte, with no
/// padding and no struct layout on the wire.
///
/// This replaces the idiom the 1998 protocol used. NetAdd was a memcpy of
/// whatever the caller handed it, so the bytes carried the host's padding and
/// its pointer width -- which is how a whole DROID_TEMPLATE went out with its
/// pointers inside it (Docs/X64Readiness.md), and why the same message could
/// not be read by a differently built peer. Nothing here can do that: a field
/// is named by the width it occupies and the bytes are composed rather than
/// copied, so what a 64-bit server writes is what a 32-bit client reads.
///
/// Bounds are sticky rather than fatal. A write that would run past the end of
/// the buffer stores nothing and sets Overflowed, and every later write is a
/// no-op, so a caller encodes a whole message and asks once at the end. No
/// member throws: this is the encoding half of a seam whose decoding half
/// (NetReader) is handed bytes a hostile peer chose.
class NetWriter
{
public:
  /// Writes into _buffer, which the caller owns and must outlive the writer.
  /// Borrowed rather than owned so a per-tick encode can use a stack buffer.
  explicit NetWriter(std::span<std::byte> _buffer) noexcept : m_buffer(_buffer) {}

  void U8(std::uint8_t _value) noexcept { Put(_value, 1); }
  void U16(std::uint16_t _value) noexcept { Put(_value, 2); }
  void U32(std::uint32_t _value) noexcept { Put(_value, 4); }

  /// Two's complement, which C++20 guarantees, so the reader's cast back is
  /// exact for every value including the negative ones.
  void S32(std::int32_t _value) noexcept { Put(static_cast<std::uint32_t>(_value), 4); }

  /// The IEEE-754 bit pattern. Both ends agree because the format is the
  /// pattern rather than the compiler's idea of a float.
  void F32(float _value) noexcept;

  void Bool(bool _value) noexcept { Put(_value ? 1u : 0u, 1); }

  void Bytes(std::span<const std::byte> _bytes) noexcept;

  /// A U16 byte count followed by the bytes. Not null-terminated on the wire:
  /// the length is the length. A string too long to describe in a U16 sets
  /// Overflowed rather than wrapping, because a silently truncated name is a
  /// worse outcome than a refused message.
  void Text(std::string_view _text) noexcept;

  /// TRUE once any write has been refused. The buffer holds a prefix of the
  /// message when this is set, so it must be checked before sending.
  [[nodiscard]] bool Overflowed() const noexcept { return m_overflowed; }

  [[nodiscard]] std::size_t Size() const noexcept { return m_at; }

  /// What has been written so far, for handing to the transport.
  [[nodiscard]] std::span<const std::byte> Written() const noexcept { return m_buffer.first(m_at); }

private:
  /// The one place bytes are laid down, so little-endian is stated once.
  void Put(std::uint32_t _value, std::size_t _bytes) noexcept;

  std::span<std::byte> m_buffer;
  std::size_t m_at = 0;
  bool m_overflowed = false;
};

} // namespace Neuron
