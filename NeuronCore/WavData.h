#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace Neuron
{

enum class WavError : std::uint8_t
{
  NotRiff, // no RIFF/WAVE header
  NoFormat, // fmt chunk missing or short
  NoData, // data chunk missing or empty
  Truncated, // a chunk runs past the end of the buffer
  Unsupported, // not integer PCM in 1-2 channels at 8 or 16 bits
};

/// A WAV decoded to the one format the mixer plays: 16-bit signed PCM.
/// This is the module's single decode seam - a future codec (Media
/// Foundation, per Phase9Plan.md) adds a sibling factory here and nothing
/// downstream changes.
class WavData
{
public:
  /// Parses a RIFF WAVE from memory. _forceMono folds stereo down, which is
  /// what the pooled voices need - they share one channel layout so that
  /// SetSourceSampleRate can retune any of them to any track.
  [[nodiscard]] static std::expected<WavData, WavError> FromBytes(std::span<const std::byte> _riff, bool _forceMono);

  [[nodiscard]] std::span<const std::int16_t> Samples() const noexcept { return m_samples; }
  [[nodiscard]] std::uint32_t SampleRate() const noexcept { return m_sampleRate; }
  [[nodiscard]] std::uint16_t Channels() const noexcept { return m_channels; }

  /// A fact chunk is how a compressed WAV declares itself; the track table
  /// refuses to play tracks that carried one, as it always has.
  [[nodiscard]] bool HasFactChunk() const noexcept { return m_hasFactChunk; }

  [[nodiscard]] std::uint32_t DurationMs() const noexcept;

private:
  std::vector<std::int16_t> m_samples;
  std::uint32_t m_sampleRate = 0;
  std::uint16_t m_channels = 1;
  bool m_hasFactChunk = false;
};

} // namespace Neuron
