#include "pch.h"
/***************************************************************************/
/*
 * WavData.cpp
 *
 * RIFF WAVE parsing and normalisation to 16-bit signed PCM. Replaces the
 * mmio-based reader that lived in the XAudio2 backend - and with it the last
 * winmm API use in the tree.
 *
 * The subset accepted is the subset the game ships: integer PCM, one or two
 * channels, 8 or 16 bits (all 551 WAVs measured in Phase4Plan.md). 8-bit
 * unsigned widens to 16-bit signed; stereo folds to mono when the caller
 * asks, because the pooled voices share one channel layout.
 */
/***************************************************************************/

#include <cstring>

#include "WavData.h"

namespace Neuron
{

namespace
{

constexpr std::uint32_t FourCc(char _a, char _b, char _c, char _d)
{
  return static_cast<std::uint32_t>(static_cast<unsigned char>(_a)) |
         static_cast<std::uint32_t>(static_cast<unsigned char>(_b)) << 8 |
         static_cast<std::uint32_t>(static_cast<unsigned char>(_c)) << 16 |
         static_cast<std::uint32_t>(static_cast<unsigned char>(_d)) << 24;
}

constexpr std::uint32_t RiffTag = FourCc('R', 'I', 'F', 'F');
constexpr std::uint32_t WaveTag = FourCc('W', 'A', 'V', 'E');
constexpr std::uint32_t FormatTag = FourCc('f', 'm', 't', ' ');
constexpr std::uint32_t DataTag = FourCc('d', 'a', 't', 'a');
constexpr std::uint32_t FactTag = FourCc('f', 'a', 'c', 't');

constexpr std::uint16_t WaveFormatPcm = 1;

/* Little-endian scalar reads. The buffer comes from disk, so alignment is
 * whatever it is - memcpy is the defined way in.
 */
template <typename T> T ReadScalar(std::span<const std::byte> _bytes, std::size_t _offset)
{
  T value{};
  std::memcpy(&value, _bytes.data() + _offset, sizeof(T));
  return value;
}

struct FormatChunk
{
  std::uint16_t formatTag;
  std::uint16_t channels;
  std::uint32_t samplesPerSecond;
  std::uint16_t bitsPerSample;
};

} // namespace

/***************************************************************************/

std::expected<WavData, WavError> WavData::FromBytes(std::span<const std::byte> _riff, bool _forceMono)
{
  if (_riff.size() < 12 || ReadScalar<std::uint32_t>(_riff, 0) != RiffTag || ReadScalar<std::uint32_t>(_riff, 8) != WaveTag)
    return std::unexpected(WavError::NotRiff);

  FormatChunk format{};
  bool haveFormat = false;
  bool haveFact = false;
  std::span<const std::byte> data;

  /* Walk the chunk list. Chunk payloads are padded to even offsets; the pad
   * byte is not counted in the chunk size.
   */
  std::size_t offset = 12;
  while (offset + 8 <= _riff.size())
  {
    const auto id = ReadScalar<std::uint32_t>(_riff, offset);
    const auto size = ReadScalar<std::uint32_t>(_riff, offset + 4);
    offset += 8;

    if (size > _riff.size() - offset)
      return std::unexpected(WavError::Truncated);

    if (id == FormatTag)
    {
      /* PCMWAVEFORMAT is the 16-byte floor; the tail of a longer chunk
       * carries nothing this decoder reads.
       */
      if (size < 16)
        return std::unexpected(WavError::NoFormat);

      format.formatTag = ReadScalar<std::uint16_t>(_riff, offset);
      format.channels = ReadScalar<std::uint16_t>(_riff, offset + 2);
      format.samplesPerSecond = ReadScalar<std::uint32_t>(_riff, offset + 4);
      format.bitsPerSample = ReadScalar<std::uint16_t>(_riff, offset + 14);
      haveFormat = true;
    }
    else if (id == DataTag)
      data = _riff.subspan(offset, size);
    else if (id == FactTag)
      haveFact = true;

    offset += size + (size & 1);
  }

  if (haveFormat == false)
    return std::unexpected(WavError::NoFormat);
  if (data.empty())
    return std::unexpected(WavError::NoData);

  if (format.formatTag != WaveFormatPcm || (format.channels != 1 && format.channels != 2) ||
      (format.bitsPerSample != 8 && format.bitsPerSample != 16) || format.samplesPerSecond == 0)
    return std::unexpected(WavError::Unsupported);

  const std::size_t bytesPerSample = format.bitsPerSample / 8;
  const std::size_t frames = data.size() / bytesPerSample / format.channels;
  if (frames == 0)
    return std::unexpected(WavError::NoData);

  const std::uint16_t outChannels = _forceMono ? std::uint16_t{1} : format.channels;

  WavData wav;
  wav.m_sampleRate = format.samplesPerSecond;
  wav.m_channels = outChannels;
  wav.m_hasFactChunk = haveFact;
  wav.m_samples.resize(frames * outChannels);

  for (std::size_t frame = 0; frame < frames; frame++)
  {
    std::int32_t left, right;

    if (format.bitsPerSample == 8)
    {
      /* 8-bit wave data is unsigned, centred on 128 */
      left = (static_cast<std::int32_t>(std::to_integer<std::uint8_t>(data[frame * format.channels])) - 128) << 8;
      right = format.channels == 2
                ? (static_cast<std::int32_t>(std::to_integer<std::uint8_t>(data[frame * format.channels + 1])) - 128) << 8
                : left;
    }
    else
    {
      left = ReadScalar<std::int16_t>(data, frame * format.channels * 2);
      right = format.channels == 2 ? ReadScalar<std::int16_t>(data, (frame * format.channels + 1) * 2) : left;
    }

    if (outChannels == 1)
      wav.m_samples[frame] = static_cast<std::int16_t>(format.channels == 2 ? (left + right) / 2 : left);
    else
    {
      wav.m_samples[frame * 2] = static_cast<std::int16_t>(left);
      wav.m_samples[frame * 2 + 1] = static_cast<std::int16_t>(right);
    }
  }

  return wav;
}

/***************************************************************************/

std::uint32_t WavData::DurationMs() const noexcept
{
  if (m_sampleRate == 0 || m_channels == 0)
    return 0;

  const auto frames = static_cast<std::uint64_t>(m_samples.size()) / m_channels;
  return static_cast<std::uint32_t>(frames * 1000 / m_sampleRate);
}

} // namespace Neuron
