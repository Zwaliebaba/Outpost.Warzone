/*
 * NetReader.h
 *
 * Reading the wire format NetWriter produces, from bytes nobody trusts.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace Neuron
{

/// Takes a message body apart, field by named field, in the little-endian
/// layout NetWriter lays down.
///
/// Every byte this sees was chosen by a peer, so nothing here trusts a length,
/// throws, or reads outside the buffer. Truncation is sticky: a read that would
/// run past the end yields zero and sets Truncated, and every later read yields
/// zero too, so a caller decodes a whole message and asks once at the end
/// rather than testing thirty return values. A decoder that forgets to ask gets
/// a zeroed record, which is inert, rather than a partly-populated one.
///
/// A decoder may also mark the read invalid itself, for bytes that were all
/// present and still meant nothing -- an enum value this build has no name for
/// is the usual case. That folds into the same Ok(), so a caller still asks one
/// question however the record failed.
class NetReader
{
public:
  /// Reads from _buffer, which the caller owns and must outlive the reader.
  explicit NetReader(std::span<const std::byte> _buffer) noexcept : m_buffer(_buffer) {}

  [[nodiscard]] std::uint8_t U8() noexcept { return static_cast<std::uint8_t>(Get(1)); }
  [[nodiscard]] std::uint16_t U16() noexcept { return static_cast<std::uint16_t>(Get(2)); }
  [[nodiscard]] std::uint32_t U32() noexcept { return Get(4); }
  [[nodiscard]] std::int32_t S32() noexcept { return static_cast<std::int32_t>(Get(4)); }
  [[nodiscard]] float F32() noexcept;
  [[nodiscard]] bool Bool() noexcept { return Get(1) != 0; }

  /// Copies _out.size() bytes. Short input leaves _out untouched and sets
  /// Truncated rather than copying a partial record.
  void Bytes(std::span<std::byte> _out) noexcept;

  /// Reads a length-prefixed string into _out and null-terminates it.
  ///
  /// A string longer than _out is truncated into it rather than refused --
  /// _out is a fixed record field and dropping a message over a long name
  /// helps nobody -- but the full declared length is always consumed from the
  /// stream, so the fields after it still line up. That is the property worth
  /// stating: truncating the copy must not desynchronise the read.
  void Text(std::span<char> _out) noexcept;

  /// TRUE once any read has run past the end. Everything decoded after that
  /// point is zero.
  [[nodiscard]] bool Truncated() const noexcept { return m_truncated; }

  /// Says the bytes were all there but did not mean anything -- an enum value
  /// this build has no name for, a field a decoder can see is impossible.
  ///
  /// Sticky, and folded into Ok() alongside truncation, so a decoder that
  /// finds a field it cannot use says so once and the caller still asks a
  /// single question about the whole record. Kept apart from Truncated()
  /// because the two are different facts: one says the message was cut short,
  /// the other says it arrived whole and was nonsense.
  void Invalidate() noexcept { m_invalid = true; }

  /// Whether the record means anything: nothing ran off the end, and no
  /// decoder found a field it could not use. This is the one test worth
  /// making, and making it once at the end is the point of both flags being
  /// sticky.
  [[nodiscard]] bool Ok() const noexcept { return !m_truncated && !m_invalid; }

  [[nodiscard]] std::size_t Remaining() const noexcept { return m_buffer.size() - m_at; }

private:
  /// The one place bytes are taken up, so little-endian is stated once and the
  /// bounds test lives in one line rather than in every accessor.
  [[nodiscard]] std::uint32_t Get(std::size_t _bytes) noexcept;

  std::span<const std::byte> m_buffer;
  std::size_t m_at = 0;
  bool m_truncated = false;
  bool m_invalid = false;
};

} // namespace Neuron
