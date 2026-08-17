#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "Track.h"

struct IXAudio2;

namespace Neuron
{

/// The XAudio2 half of the audio module: the engine, the mastering voice, the
/// voice slots and X3DAudio. Everything above it (tracks, samples, queueing,
/// gates) is AudioSystem's; this class owns exactly what talks to XAudio2.
///
/// The slot layout is the Phase 4 contract: four fixed slots - the speech
/// queue, the single stream, serialised 2D FX, and music, which is
/// independent of the stream so a briefing neither stops nor un-pauses it -
/// and a pool for 3D sounds, stolen from by greatest listener distance.
class AudioMixer
{
public:
  [[nodiscard]] static bool Init();
  static void Shutdown();

  /// Once per frame on the game thread. Drains the completion bits XAudio2's
  /// thread has raised, pumps the fixed-slot queues, and refreshes the
  /// spatialisation of everything positional.
  static void Update();

  /// The engine, for code that needs a voice of its own - the FMV player
  /// builds its soundtrack voice on the game's graph rather than standing up
  /// a second audio engine. Null before Init or after Shutdown; a caller
  /// that gets null plays silently rather than failing.
  [[nodiscard]] static IXAudio2* Engine();

  /// Decodes a RIFF WAVE into _track.pMem and fills in its duration.
  [[nodiscard]] static bool LoadTrack(TRACK& _track, std::span<const std::byte> _riff);

  /// Reads _path and decodes it into _track, for a track whose WAV has not
  /// been needed until now. Traces and fails rather than fataling: a sound
  /// that cannot be read should cost its own effect, not the mission.
  [[nodiscard]] static bool LoadTrackFile(TRACK& _track, std::string_view _path);
  static void FreeTrack(TRACK& _track);

  [[nodiscard]] static bool Play2D(TRACK& _track, AUDIO_SAMPLE& _sample, bool _queued);
  [[nodiscard]] static bool Play3D(TRACK& _track, AUDIO_SAMPLE& _sample);
  [[nodiscard]] static bool PlayStream(AUDIO_SAMPLE& _sample, std::string_view _fileName, std::int32_t _volume);
  static void StopSample(std::int32_t _handle);
  static void StopAllVoices();

  [[nodiscard]] static bool PlayMusic(std::string_view _fileName, std::int32_t _volume, bool _loop);
  static void StopMusic();
  static void PauseMusic();
  static void ResumeMusic();

  [[nodiscard]] static bool QueueSlotBusy();

  /// The scale sample mix volumes are expressed in (32767 is unity).
  [[nodiscard]] static std::int32_t MaxVolume();

  [[nodiscard]] static std::int32_t GlobalVolume();
  static void SetGlobalVolume(std::int32_t _volume);
  [[nodiscard]] static std::int32_t MusicVolume();
  static void SetMusicVolume(std::int32_t _volume);

  static void SetListenerPosition(std::int32_t _x, std::int32_t _y, std::int32_t _z);
  static void SetListenerOrientation(std::int32_t _degrees);
  static void SetEmitterPosition(std::int32_t _handle, std::int32_t _x, std::int32_t _y, std::int32_t _z);
};

} // namespace Neuron
