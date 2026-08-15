#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "Track.h"

namespace Neuron
{

/// The audio module's public surface: track registry, sample lifecycle, the
/// speech queue and its gates, ducking, music and the volume controls. One
/// implementation, static methods over state internal to the translation
/// unit - the Transport precedent from Phase 5.
///
/// The legacy audio_* free functions in Audio.h are a shim over this class
/// and are deleted by Phase 9 stage F; new code calls the class.
class AudioSystem
{
public:
  /// _enabled FALSE means run silent: every entry point stays callable and
  /// inert, which is what the AUDIO_DISABLED build and a failed device both
  /// get. _stoppedCallback is invoked whenever an object-attached sample
  /// stops, on the game thread.
  static bool Init(bool _enabled, AUDIO_CALLBACK _stoppedCallback);
  static bool Shutdown();
  static bool Update();
  [[nodiscard]] static bool Enabled();

  /// True between a successful Init and Shutdown - the gate the mixer's
  /// completion delivery checks so no callback lands after teardown began.
  [[nodiscard]] static bool Active();

  /* tracks - loaded through the resource system, addressed by small dense
   * IDs that scripts resolve by WAV name and save games by name hash
   */
  [[nodiscard]] static TRACK* LoadTrackFromBuffer(std::span<const std::byte> _riff);
  static bool SetTrackVals(const char* _fileName, bool _loop, std::int32_t& _id, std::int32_t _volume, std::int32_t _priority,
                           std::int32_t _audibleRadius);
  static bool SetTrackValsByHash(std::uint32_t _hash, bool _loop, std::int32_t _id, std::int32_t _volume, std::int32_t _priority,
                                 std::int32_t _audibleRadius);
  static bool RegisterTrack(TRACK& _track, bool _loop, std::int32_t _id, std::int32_t _volume, std::int32_t _priority,
                            std::int32_t _audibleRadius);
  static void ReleaseTrack(TRACK& _track);
  static void CheckAllUnloaded();

  [[nodiscard]] static std::int32_t TrackId(const char* _fileName);
  [[nodiscard]] static std::int32_t TrackIdFromHash(std::uint32_t _hash);
  [[nodiscard]] static std::int32_t AvailableTrackId();
  [[nodiscard]] static std::uint32_t TrackHashName(std::int32_t _id);
  [[nodiscard]] static bool ValidTrack(std::int32_t _id);
  [[nodiscard]] static bool TrackLooped(std::int32_t _id);
  [[nodiscard]] static std::int32_t TrackAudibleRadius(std::int32_t _id);

  /* playback */
  static void PlayTrack(std::int32_t _id);
  [[nodiscard]] static bool PlayStaticTrack(std::int32_t _mapX, std::int32_t _mapY, std::int32_t _id);
  [[nodiscard]] static bool PlayObjectTrack(void* _object, std::int32_t _id, AUDIO_CALLBACK _callback);
  static void StopObjectTrack(void* _object, std::int32_t _id);
  [[nodiscard]] static bool PlayStream(const char* _fileName, std::int32_t _volume, AUDIO_CALLBACK _callback);
  static void StopAll();

  /* the speech queue */
  static void QueueTrack(std::int32_t _id);
  static void QueueTrackMinDelay(std::int32_t _id, std::uint32_t _minDelayMs);
  static void QueueTrackMinDelayPos(std::int32_t _id, std::uint32_t _minDelayMs, std::int32_t _x, std::int32_t _y, std::int32_t _z);
  static void QueueTrackPos(std::int32_t _id, std::int32_t _x, std::int32_t _y, std::int32_t _z);
  static bool PreviousQueueTrackPos(std::int32_t& _x, std::int32_t& _y, std::int32_t& _z);

  /* music - its own backend slot, no sample bookkeeping */
  static bool PlayMusic(const char* _fileName, std::int32_t _volume, bool _loop);
  static void StopMusic();
  static void PauseMusic();
  static void ResumeMusic();

  /* the two sliders, and the duck applied to 3D sounds while speech plays */
  [[nodiscard]] static std::int32_t FxVolume();
  static void SetFxVolume(std::int32_t _volume);
  [[nodiscard]] static std::int32_t MusicVolume();
  static void SetMusicVolume(std::int32_t _volume);
  [[nodiscard]] static std::int32_t Volume3D();
  static void SetVolume3D(std::int32_t _volume);

  /// Composes the requested volume, the track's authored volume and (for 3D
  /// sounds) the duck factor onto the mixer's 0..MaxVolume scale.
  [[nodiscard]] static std::int32_t SampleMixVolume(AUDIO_SAMPLE& _sample, std::int32_t _volume, bool _scale3d);

  /// The mixer reports a finished sample here, on the game thread, at most
  /// one frame after the voice ended.
  static void FinishedCallback(AUDIO_SAMPLE& _sample);
};

} // namespace Neuron
