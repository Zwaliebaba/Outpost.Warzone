#include "pch.h"
#include "NetReader.h"

#include <bit>

namespace Neuron
{

std::uint32_t NetReader::Get(std::size_t _bytes) noexcept
{
  /* Written as a subtraction against what is left rather than as m_at +
     _bytes > size(), which can wrap on a length a peer chose. */
  if (m_truncated || _bytes > m_buffer.size() - m_at)
  {
    m_truncated = true;
    return 0;
  }

  std::uint32_t value = 0;
  for (std::size_t i = 0; i < _bytes; i++)
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(m_buffer[m_at + i])) << (i * 8);

  m_at += _bytes;
  return value;
}

float NetReader::F32() noexcept
{
  return std::bit_cast<float>(Get(4));
}

void NetReader::Bytes(std::span<std::byte> _out) noexcept
{
  if (m_truncated || _out.size() > m_buffer.size() - m_at)
  {
    m_truncated = true;
    return;
  }

  for (std::size_t i = 0; i < _out.size(); i++)
    _out[i] = m_buffer[m_at + i];

  m_at += _out.size();
}

void NetReader::Text(std::span<char> _out) noexcept
{
  const std::size_t declared = U16();

  if (m_truncated || declared > m_buffer.size() - m_at)
  {
    m_truncated = true;
    if (!_out.empty())
      _out[0] = '\0';
    return;
  }

  /* Copy what fits, keeping room for the terminator, then step over the whole
     declared length. Consuming the declared length rather than the copied one
     is what keeps the following fields aligned when a name is too long. */
  const std::size_t room = _out.empty() ? 0 : _out.size() - 1;
  const std::size_t copied = declared < room ? declared : room;

  for (std::size_t i = 0; i < copied; i++)
    _out[i] = static_cast<char>(std::to_integer<std::uint8_t>(m_buffer[m_at + i]));

  if (!_out.empty())
    _out[copied] = '\0';

  m_at += declared;
}

} // namespace Neuron
