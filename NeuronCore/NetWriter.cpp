#include "pch.h"
#include "NetWriter.h"

#include <bit>

namespace Neuron
{

void NetWriter::Put(std::uint32_t _value, std::size_t _bytes) noexcept
{
  /* Once overflowed the writer stays overflowed: a message with a hole in the
     middle is not a shorter message, and letting later fields land would make
     the prefix look well formed. */
  if (m_overflowed || _bytes > m_buffer.size() - m_at)
  {
    m_overflowed = true;
    return;
  }

  for (std::size_t i = 0; i < _bytes; i++)
    m_buffer[m_at + i] = static_cast<std::byte>((_value >> (i * 8)) & 0xffu);

  m_at += _bytes;
}

void NetWriter::F32(float _value) noexcept
{
  Put(std::bit_cast<std::uint32_t>(_value), 4);
}

void NetWriter::Bytes(std::span<const std::byte> _bytes) noexcept
{
  if (m_overflowed || _bytes.size() > m_buffer.size() - m_at)
  {
    m_overflowed = true;
    return;
  }

  for (std::size_t i = 0; i < _bytes.size(); i++)
    m_buffer[m_at + i] = _bytes[i];

  m_at += _bytes.size();
}

void NetWriter::Text(std::string_view _text) noexcept
{
  if (_text.size() > 0xffffu)
  {
    m_overflowed = true;
    return;
  }

  U16(static_cast<std::uint16_t>(_text.size()));
  Bytes(std::as_bytes(std::span(_text.data(), _text.size())));
}

} // namespace Neuron
