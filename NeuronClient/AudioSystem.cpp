#include "pch.h"
/***************************************************************************/
/*
 * AudioSystem.cpp
 *
 * The audio module's public surface (was Audio.cpp plus Track.cpp): the
 * track registry, the sample lifecycle, the speech queue and its gates,
 * ducking, music and the volume controls. The XAudio2 half is AudioMixer's.
 *
 * Everything here runs on the game thread. The behaviour is the Phase 4
 * contract unchanged; Phase 9 changed the shape - owned containers instead
 * of hand-spliced intrusive lists, and a class instead of two layers of free
 * functions whose split existed for a backend swap that is finished.
 */
/***************************************************************************/

#include <algorithm>
#include <cstring>
#include <deque>
#include <functional>
#include <list>
#include <memory>

#include "Frame.h"
#include "Priority.h"
#include "AudioSystem.h"
#include "AudioMixer.h"

namespace Neuron
{

namespace
{

constexpr std::int32_t MaxSameSamples = 2;
constexpr std::int32_t LoweredVolume = AUDIO_VOL_MAX / 4;
constexpr std::int32_t MaxTracks = 600;

bool g_enabled = false;
bool g_stopAll = false;

/* Samples the mixer is (or is about to be) playing, newest first - the
 * search order StopObjectTrack has always used. Removal is deferred to
 * Update's sweep so completion callbacks always run before the memory goes.
 */
std::list<std::unique_ptr<AUDIO_SAMPLE>> g_samples;

/* Speech waiting for the queue slot, oldest first. */
std::deque<std::unique_ptr<AUDIO_SAMPLE>> g_queue;

/* Where the last positioned queue sample played, for the jump-to-event key. */
std::int32_t g_previousX = SAMPLE_COORD_INVALID;
std::int32_t g_previousY = SAMPLE_COORD_INVALID;
std::int32_t g_previousZ = SAMPLE_COORD_INVALID;

/* The duck applied to 3D sounds while queued speech plays. */
std::int32_t g_volume3d = AUDIO_VOL_MAX;

/* The track registry: small dense IDs that scripts resolve by WAV name and
 * save games by name hash. TRACKs are owned by the resource system; these
 * are borrowed pointers.
 */
TRACK* g_tracks[MaxTracks] = {};
std::int32_t g_trackCount = 0;

bool g_active = false;

/* Invoked whenever an object-attached sample stops. Stored as
 * move_only_function so a future registration can capture state; the game
 * passes a plain function pointer today.
 */
std::move_only_function<BOOL(AUDIO_SAMPLE*)> g_stoppedCallback;

/* The game's view of the world - object positions and liveness, the
 * listener, name-to-ID resolution and the game clock. Populated by Init.
 */
AudioWorld g_world;

/***************************************************************************/

/* The bounds-and-liveness gate every by-ID lookup runs. A failure traces
 * and returns false; the callers that ignore it are preserving the old
 * code's exact recklessness.
 */
bool CheckTrackId(std::int32_t _id)
{
  if (_id < 0 || _id > g_trackCount - 1)
  {
    Neuron::DebugTrace("AudioSystem: track number {} outside max {}\n", _id, g_trackCount);
    return false;
  }

  if (g_tracks[_id] == nullptr)
  {
    Neuron::DebugTrace("AudioSystem: track {} NULL\n", _id);
    return false;
  }

  return true;
}

/***************************************************************************/

void CheckSampleHandle(AUDIO_SAMPLE& _sample)
{
  DEBUG_ASSERT_TEXT(_sample.iSample >= 0 || _sample.iSample == SAMPLE_NOT_ALLOCATED,
                    "AudioSystem: sample handle {} out of range\n", _sample.iSample);
}

/***************************************************************************/

/* Stops a sample's voice and runs the stopped callback for object-attached
 * sounds (was sound_StopTrack). The sample itself stays on the list until
 * its completion arrives and the sweep removes it.
 */
void StopSampleVoice(AUDIO_SAMPLE& _sample)
{
  CheckSampleHandle(_sample);

  if (_sample.iSample != SAMPLE_NOT_ALLOCATED)
    AudioMixer::StopSample(_sample.iSample);

  if (g_stoppedCallback && _sample.psObj != nullptr)
    g_stoppedCallback(&_sample);
}

/***************************************************************************/

bool PlayTrackOnSample(AUDIO_SAMPLE& _sample, bool _positional, bool _queued)
{
  TRACK* track = g_tracks[_sample.iTrack];

  if (track->bCompressed)
  {
    Neuron::DebugTrace("AudioSystem: trying to play compressed track {}!\n", track->pName);
    return false;
  }

  return _positional ? AudioMixer::Play3D(*track, _sample) : AudioMixer::Play2D(*track, _sample, _queued);
}

/***************************************************************************/

/* Reject a queued track when too many copies of it are already waiting. */
bool SameQueueTracksOk(std::int32_t _id)
{
  std::int32_t count = 0;

  for (const auto& sample : g_queue)
  {
    if (sample->iTrack == _id && ++count > MaxSameSamples)
      return false;
  }

  return true;
}

/***************************************************************************/

/* Reject a 3D track when too many copies of it are already audible in the
 * same area.
 */
bool Same3DTracksOk(std::int32_t _id, std::int32_t _x, std::int32_t _y, std::int32_t _z)
{
  const std::int32_t radius = AudioSystem::TrackAudibleRadius(_id);
  const std::int32_t maxDistanceSq = radius * radius;
  std::int32_t count = 0;

  for (const auto& sample : g_samples)
  {
    if (sample->iTrack != _id)
      continue;

    const std::int32_t dx = _x - sample->x;
    const std::int32_t dy = _y - sample->y;
    const std::int32_t dz = _z - sample->z;

    if (dx * dx + dy * dy + dz * dz < maxDistanceSq && ++count > MaxSameSamples)
      return false;
  }

  return true;
}

/***************************************************************************/

AUDIO_SAMPLE* QueueSample(std::int32_t _id)
{
  if (g_enabled == false || g_stopAll)
    return nullptr;

  DEBUG_ASSERT_TEXT(CheckTrackId(_id), "AudioSystem: queued track {} outside limits\n", _id);

  if (SameQueueTracksOk(_id) == false)
    return nullptr;

  auto sample = std::make_unique<AUDIO_SAMPLE>();
  sample->iTrack = _id;
  sample->x = SAMPLE_COORD_INVALID;
  sample->y = SAMPLE_COORD_INVALID;
  sample->z = SAMPLE_COORD_INVALID;

  g_queue.push_back(std::move(sample));

  return g_queue.back().get();
}

/***************************************************************************/

void UpdateQueue()
{
  if (AudioMixer::QueueSlotBusy())
  {
    /* lower volume whilst playing queue audio */
    AudioSystem::SetVolume3D(LoweredVolume);
    return;
  }

  /* set full global volume */
  AudioSystem::SetVolume3D(AUDIO_VOL_MAX);

  if (g_queue.empty())
    return;

  auto sample = std::move(g_queue.front());
  g_queue.pop_front();

  if (PlayTrackOnSample(*sample, false, true) == false)
  {
    Neuron::DebugTrace("AudioSystem: couldn't play queued sample\n");
    return;
  }

  /* update last queue sound coords */
  if (sample->x != SAMPLE_COORD_INVALID && sample->y != SAMPLE_COORD_INVALID && sample->z != SAMPLE_COORD_INVALID)
  {
    g_previousX = sample->x;
    g_previousY = sample->y;
    g_previousZ = sample->z;
  }

  g_samples.push_front(std::move(sample));
}

/***************************************************************************/

bool Play3DTrack(std::int32_t _x, std::int32_t _y, std::int32_t _z, std::int32_t _id, void* _object, AUDIO_CALLBACK _callback)
{
  if (g_enabled == false || g_stopAll)
    return false;

  if (Same3DTracksOk(_id, _x, _y, _z) == false)
    return false;

  auto sample = std::make_unique<AUDIO_SAMPLE>();
  sample->iTrack = _id;
  sample->x = _x;
  sample->y = _y;
  sample->z = _z;
  sample->psObj = _object;
  sample->pCallback = _callback;

  if (PlayTrackOnSample(*sample, true, false) == false)
  {
    Neuron::DebugTrace("AudioSystem: couldn't play 3D sample\n");
    return false;
  }

  g_samples.push_front(std::move(sample));
  return true;
}

} // namespace

/***************************************************************************/
/* lifecycle */
/***************************************************************************/

bool AudioSystem::Init(bool _enabled, AUDIO_CALLBACK _stoppedCallback, AudioWorld _world)
{
  /* if audio not enabled return true to carry on game without audio */
  if (_enabled == false)
  {
    g_enabled = false;
    return true;
  }

  DEBUG_ASSERT_TEXT(_world.objectDead && _world.objectPosition && _world.staticPosition && _world.listenerPose &&
                      _world.trackIdForName && _world.gameTimeMs,
                    "AudioSystem::Init: the AudioWorld provider is incomplete\n");

  g_trackCount = 0;
  std::fill(std::begin(g_tracks), std::end(g_tracks), nullptr);

  if (AudioMixer::Init() == false)
  {
    Neuron::DebugTrace("Cannot init sound library\n");
    g_enabled = false;
    return false;
  }

  g_active = true;
  g_enabled = true;
  g_stoppedCallback = _stoppedCallback;
  g_world = std::move(_world);

  return true;
}

/***************************************************************************/

bool AudioSystem::Shutdown()
{
  /* if audio not enabled return true to carry on game without audio */
  if (g_enabled == false)
    return true;

  AudioMixer::StopAllVoices();

  /* set inactive flag to prevent callbacks coming after shutdown */
  g_active = false;
  std::fill(std::begin(g_tracks), std::end(g_tracks), nullptr);

  AudioMixer::Shutdown();

  g_samples.clear();
  g_queue.clear();

  return true;
}

/***************************************************************************/

bool AudioSystem::Enabled() { return g_enabled; }

/***************************************************************************/

bool AudioSystem::Active() { return g_active; }

/***************************************************************************/

bool AudioSystem::Update()
{
  /* if audio not enabled return true to carry on game without audio */
  if (g_enabled == false)
    return true;

  UpdateQueue();

  /* the listener follows the player: the camera in 3D, the map position in
   * the 2D map view
   */
  std::int32_t x, y, z, angle;
  g_world.listenerPose(x, y, z, angle);

  AudioMixer::SetListenerPosition(x, y, z);
  AudioMixer::SetListenerOrientation(angle);

  /* sweep finished samples out, and keep the live ones honest: stop a sound
   * whose object died or whose callback says it is done, and move the rest
   * with their objects
   */
  for (auto it = g_samples.begin(); it != g_samples.end();)
  {
    AUDIO_SAMPLE& sample = **it;

    if (sample.bRemove == TRUE)
    {
      it = g_samples.erase(it);
      continue;
    }

    if (sample.psObj != nullptr)
    {
      if (g_world.objectDead(sample.psObj) || (sample.pCallback != nullptr && sample.pCallback(&sample) == FALSE))
      {
        StopSampleVoice(sample);
        sample.psObj = nullptr;
      }
      else
      {
        g_world.objectPosition(sample.psObj, sample.x, sample.y, sample.z);
        AudioMixer::SetEmitterPosition(sample.iSample, sample.x, sample.y, sample.z);
      }
    }

    ++it;
  }

  AudioMixer::Update();

  return true;
}

/***************************************************************************/
/* tracks */
/***************************************************************************/

TRACK* AudioSystem::LoadTrackFromBuffer(std::span<const std::byte> _riff)
{
  if (g_enabled == false)
    return nullptr;

  auto* track = new (std::nothrow) TRACK{};
  if (track == nullptr)
  {
    Neuron::Fatal("AudioSystem::LoadTrackFromBuffer: couldn't allocate memory\n");
    return nullptr;
  }

  const char* resourceName = GetLastResourceFilename();
  track->pName = new (std::nothrow) STRING[strlen(resourceName) + 1];
  if (track->pName == nullptr)
  {
    Neuron::Fatal("AudioSystem::LoadTrackFromBuffer: couldn't allocate memory\n");
    delete track;
    return nullptr;
  }
  strcpy(track->pName, resourceName);
  track->resID = GetLastHashName();

  if (AudioMixer::LoadTrack(*track, _riff) == false)
  {
    delete[] track->pName;
    delete track;
    return nullptr;
  }

  if (track->bCompressed == TRUE)
    Neuron::DebugTrace("AudioSystem::LoadTrackFromBuffer: {} is compressed!\n", track->pName);

  return track;
}

/***************************************************************************/

bool AudioSystem::SetTrackVals(const char* _fileName, bool _loop, std::int32_t& _id, std::int32_t _volume, std::int32_t _priority,
                               std::int32_t _audibleRadius)
{
  /* if audio not enabled return true to carry on game without audio */
  if (g_enabled == false)
    return true;

  /* the resource layer takes mutable char* and never writes through it */
  auto* track = static_cast<TRACK*>(resGetData("WAV", const_cast<char*>(_fileName)));
  if (track == nullptr)
  {
    Neuron::DebugTrace("AudioSystem::SetTrackVals: track {} resource not found\n", _fileName);
    return false;
  }

  /* get current ID or spare one */
  if (g_world.trackIdForName(_fileName, _id) == false)
    _id = AvailableTrackId();

  if (_id == SAMPLE_NOT_ALLOCATED)
  {
    Neuron::DebugTrace("AudioSystem::SetTrackVals: couldn't get spare track ID\n");
    return false;
  }

  return RegisterTrack(*track, _loop, _id, _volume, _priority, _audibleRadius);
}

/***************************************************************************/

bool AudioSystem::SetTrackValsByHash(std::uint32_t _hash, bool _loop, std::int32_t _id, std::int32_t _volume,
                                     std::int32_t _priority, std::int32_t _audibleRadius)
{
  /* if audio not enabled return true to carry on game without audio */
  if (g_enabled == false)
    return true;

  auto* track = static_cast<TRACK*>(resGetDataFromHash("WAV", _hash));
  if (track == nullptr)
    return false;

  return RegisterTrack(*track, _loop, _id, _volume, _priority, _audibleRadius);
}

/***************************************************************************/

bool AudioSystem::RegisterTrack(TRACK& _track, bool _loop, std::int32_t _id, std::int32_t _volume, std::int32_t _priority,
                                std::int32_t _audibleRadius)
{
  DEBUG_ASSERT_TEXT(_priority >= LOW_PRIORITY && _priority <= HIGH_PRIORITY, "AudioSystem: priority {} out of bounds\n",
                    _priority);

  if (_id >= MaxTracks)
    return false;

  if (g_tracks[_id] != nullptr)
  {
    Neuron::Fatal("AudioSystem::RegisterTrack: track {} already set\n", _id);
    return false;
  }

  _track.bLoop = _loop ? TRUE : FALSE;
  _track.iVol = _volume;
  _track.iPriority = _priority;
  _track.iAudibleRadius = _audibleRadius;
  _track.iTimeLastFinished = 0;

  g_tracks[_id] = &_track;
  g_trackCount++;

  return true;
}

/***************************************************************************/

void AudioSystem::ReleaseTrack(TRACK& _track)
{
  if (g_enabled == false)
    return;

  delete[] _track.pName;
  _track.pName = nullptr;

  for (std::int32_t id = 0; id < g_trackCount; id++)
  {
    if (g_tracks[id] == &_track)
      g_tracks[id] = nullptr;
  }

  AudioMixer::FreeTrack(_track);
}

/***************************************************************************/

void AudioSystem::CheckAllUnloaded()
{
  for (std::int32_t id = 0; id < MaxTracks; id++)
  {
    DEBUG_ASSERT_TEXT(g_tracks[id] == nullptr, "AudioSystem: track {} still loaded - check audio.cfg for duplicate IDs\n", id);
  }
}

/***************************************************************************/

std::int32_t AudioSystem::TrackId(const char* _fileName)
{
  if (g_enabled == false)
    return SAMPLE_NOT_FOUND;

  auto* track = static_cast<TRACK*>(resGetData("WAV", const_cast<char*>(_fileName)));
  if (track == nullptr)
    return SAMPLE_NOT_FOUND;

  for (std::int32_t id = 0; id < MaxTracks; id++)
  {
    if (g_tracks[id] != nullptr && g_tracks[id] == track)
      return id;
  }

  return SAMPLE_NOT_FOUND;
}

/***************************************************************************/

std::int32_t AudioSystem::AvailableTrackId()
{
  for (std::int32_t id = 0; id < MaxTracks; id++)
  {
    if (g_tracks[id] == nullptr)
      return id;
  }

  DEBUG_ASSERT_TEXT(false, "AudioSystem::AvailableTrackId: unused track not found!\n");
  return SAMPLE_NOT_ALLOCATED;
}

/***************************************************************************/

std::uint32_t AudioSystem::TrackHashName(std::int32_t _id)
{
  DEBUG_ASSERT_TEXT(g_tracks[_id] != nullptr, "AudioSystem::TrackHashName: unallocated track");
  return g_tracks[_id]->resID;
}

/***************************************************************************/

bool AudioSystem::ValidTrack(std::int32_t _id) { return CheckTrackId(_id); }

/***************************************************************************/

bool AudioSystem::TrackLooped(std::int32_t _id)
{
  CheckTrackId(_id);

  return g_tracks[_id]->bLoop == TRUE;
}

/***************************************************************************/

std::int32_t AudioSystem::TrackAudibleRadius(std::int32_t _id)
{
  CheckTrackId(_id);

  return g_tracks[_id]->iAudibleRadius;
}

/***************************************************************************/
/* playback */
/***************************************************************************/

void AudioSystem::PlayTrack(std::int32_t _id)
{
  if (g_enabled == false || g_stopAll)
    return;

  auto sample = std::make_unique<AUDIO_SAMPLE>();
  sample->iTrack = _id;

  if (PlayTrackOnSample(*sample, false, false) == false)
  {
    Neuron::DebugTrace("AudioSystem::PlayTrack: couldn't play sample\n");
    return;
  }

  g_samples.push_front(std::move(sample));
}

/***************************************************************************/

bool AudioSystem::PlayStaticTrack(std::int32_t _mapX, std::int32_t _mapY, std::int32_t _id)
{
  if (g_enabled == false)
    return false;

  std::int32_t x, y, z;
  g_world.staticPosition(_mapX, _mapY, x, y, z);

  return Play3DTrack(x, y, z, _id, nullptr, nullptr);
}

/***************************************************************************/

bool AudioSystem::PlayObjectTrack(void* _object, std::int32_t _id, AUDIO_CALLBACK _callback)
{
  if (g_enabled == false)
    return false;

  std::int32_t x, y, z;
  g_world.objectPosition(_object, x, y, z);

  return Play3DTrack(x, y, z, _id, _object, _callback);
}

/***************************************************************************/

void AudioSystem::StopObjectTrack(void* _object, std::int32_t _id)
{
  if (g_enabled == false || g_stopAll)
    return;

  /* newest first, the order the head-inserted list has always been walked */
  for (const auto& sample : g_samples)
  {
    if (sample->psObj == _object && sample->iTrack == _id)
    {
      StopSampleVoice(*sample);
      return;
    }
  }
}

/***************************************************************************/

bool AudioSystem::PlayStream(const char* _fileName, std::int32_t _volume, AUDIO_CALLBACK _callback)
{
  if (g_enabled == false)
    return false;

  auto sample = std::make_unique<AUDIO_SAMPLE>();
  sample->pCallback = _callback;

  SetVolume3D(AUDIO_VOL_MAX);

  if (AudioMixer::PlayStream(*sample, _fileName, _volume) == false)
    return false;

  /* The sample has to be on the list or nothing ever frees it: the mixer
   * reports the stream finished by setting bRemove, and the sweep only
   * looks at samples it can see.
   */
  g_samples.push_front(std::move(sample));

  return true;
}

/***************************************************************************/

void AudioSystem::StopAll()
{
  if (g_enabled == false)
    return;

  Neuron::DebugTrace("AudioSystem::StopAll called\n");

  /* Callbacks run inside the stops below, and a callback that starts a new
   * sound during a stop-the-world would leak it into the teardown.
   */
  g_stopAll = true;

  /* stop everything on the list - the sweep frees the samples once their
   * completions have come back in
   */
  for (const auto& sample : g_samples)
    StopSampleVoice(*sample);

  g_queue.clear();

  g_stopAll = false;

  Neuron::DebugTrace("AudioSystem::StopAll done\n");
}

/***************************************************************************/
/* the speech queue */
/***************************************************************************/

void AudioSystem::QueueTrack(std::int32_t _id)
{
  if (g_enabled == false || g_stopAll)
    return;

  QueueSample(_id);
}

/***************************************************************************/

/* Will only play the track if _minDelayMs has elapsed since it last
 * finished.
 */
void AudioSystem::QueueTrackMinDelay(std::int32_t _id, std::uint32_t _minDelayMs)
{
  if (g_enabled == false)
    return;

  if (CheckTrackId(_id) == false)
    return;

  const std::uint32_t delay = g_world.gameTimeMs() - g_tracks[_id]->iTimeLastFinished;
  if (delay <= _minDelayMs)
    return;

  if (QueueSample(_id) != nullptr)
    g_tracks[_id]->iTimeLastFinished = g_world.gameTimeMs();
}

/***************************************************************************/

void AudioSystem::QueueTrackMinDelayPos(std::int32_t _id, std::uint32_t _minDelayMs, std::int32_t _x, std::int32_t _y,
                                        std::int32_t _z)
{
  if (g_enabled == false)
    return;

  if (CheckTrackId(_id) == false)
    return;

  const std::uint32_t delay = g_world.gameTimeMs() - g_tracks[_id]->iTimeLastFinished;
  if (delay <= _minDelayMs)
    return;

  AUDIO_SAMPLE* sample = QueueSample(_id);
  if (sample == nullptr)
    return;

  g_tracks[_id]->iTimeLastFinished = g_world.gameTimeMs();
  sample->x = _x;
  sample->y = _y;
  sample->z = _z;
}

/***************************************************************************/

void AudioSystem::QueueTrackPos(std::int32_t _id, std::int32_t _x, std::int32_t _y, std::int32_t _z)
{
  if (g_enabled == false)
    return;

  AUDIO_SAMPLE* sample = QueueSample(_id);
  if (sample == nullptr)
    return;

  sample->x = _x;
  sample->y = _y;
  sample->z = _z;
}

/***************************************************************************/

bool AudioSystem::PreviousQueueTrackPos(std::int32_t& _x, std::int32_t& _y, std::int32_t& _z)
{
  if (g_previousX == SAMPLE_COORD_INVALID || g_previousY == SAMPLE_COORD_INVALID || g_previousZ == SAMPLE_COORD_INVALID)
  {
    _x = _y = _z = 0;
    return false;
  }

  _x = g_previousX;
  _y = g_previousY;
  _z = g_previousZ;
  return true;
}

/***************************************************************************/
/* music */
/***************************************************************************/

bool AudioSystem::PlayMusic(const char* _fileName, std::int32_t _volume, bool _loop)
{
  if (g_enabled == false)
    return false;

  return AudioMixer::PlayMusic(_fileName, _volume, _loop);
}

/***************************************************************************/

void AudioSystem::StopMusic()
{
  if (g_enabled)
    AudioMixer::StopMusic();
}

/***************************************************************************/

void AudioSystem::PauseMusic()
{
  if (g_enabled)
    AudioMixer::PauseMusic();
}

/***************************************************************************/

void AudioSystem::ResumeMusic()
{
  if (g_enabled)
    AudioMixer::ResumeMusic();
}

/***************************************************************************/
/* volume */
/***************************************************************************/

std::int32_t AudioSystem::FxVolume() { return AudioMixer::GlobalVolume(); }

void AudioSystem::SetFxVolume(std::int32_t _volume) { AudioMixer::SetGlobalVolume(_volume); }

std::int32_t AudioSystem::MusicVolume() { return AudioMixer::MusicVolume(); }

void AudioSystem::SetMusicVolume(std::int32_t _volume) { AudioMixer::SetMusicVolume(_volume); }

std::int32_t AudioSystem::Volume3D() { return g_volume3d; }

void AudioSystem::SetVolume3D(std::int32_t _volume) { g_volume3d = _volume; }

/***************************************************************************/

/* The requested volume, the track's authored volume and (for 3D sounds) the
 * duck factor all scale by AUDIO_VOL_RANGE onto the mixer's 0..MaxVolume
 * scale.
 */
std::int32_t AudioSystem::SampleMixVolume(AUDIO_SAMPLE& _sample, std::int32_t _volume, bool _scale3d)
{
  CheckTrackId(_sample.iTrack);

  std::int32_t mixVolume = _volume * AudioMixer::MaxVolume() / AUDIO_VOL_RANGE;
  mixVolume = mixVolume * g_tracks[_sample.iTrack]->iVol / AUDIO_VOL_RANGE;
  if (_scale3d)
    mixVolume = mixVolume * g_volume3d / AUDIO_VOL_RANGE;

  return mixVolume;
}

/***************************************************************************/

/* The mixer reports a finished sample here, on the game thread. The user
 * callback runs first, then the remove flag hands the sample to the sweep.
 */
void AudioSystem::FinishedCallback(AUDIO_SAMPLE& _sample)
{
  CheckSampleHandle(_sample);

  if (g_tracks[_sample.iTrack] != nullptr)
    g_tracks[_sample.iTrack]->iTimeLastFinished = g_world.gameTimeMs();

  if (_sample.pCallback != nullptr)
    (_sample.pCallback)(&_sample);

  _sample.bRemove = TRUE;
}

} // namespace Neuron

/***************************************************************************/
