#include "pch.h"
/***************************************************************************/
/*
 * AudioMixer.cpp
 *
 * The XAudio2 half of the audio module (was XA2Track.cpp, which replaced
 * QMixer's QSTrack.cpp in Phase 4). The behaviour is the Phase 4 contract,
 * unchanged; what Phase 9 changed is the shape - a class over RAII voice
 * lifetimes, WavData for decoding instead of mmio, and one atomic where the
 * completion mask crossed a thread boundary under a lock.
 *
 * Two things carry over deliberately, because the code above depends on
 * them. AUDIO_SAMPLE::iSample is this module's handle, and the first four
 * voice slots keep fixed meanings: speech queues on one, the single stream
 * plays on another, 2D effects share a third the way they shared one QMixer
 * channel, and music has the fourth - it used to be the CD and so must be
 * independent of the stream slot. The rest are the pool 3D sounds are drawn
 * from, stolen by greatest listener distance.
 */
/***************************************************************************/

#include <windows.h>
#include <mmreg.h>
#include <xaudio2.h>
#include <x3daudio.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "Frame.h"
#include "AudioMixer.h"
#include "AudioSystem.h"
#include "WavData.h"

namespace Neuron
{

namespace
{

/* Slot layout. The four fixed slots are the ones AudioSystem routes to by
 * meaning rather than by allocation.
 */
constexpr std::int32_t QueueSlot = 0;
constexpr std::int32_t StreamSlot = 1;
constexpr std::int32_t FxSlot = 2;
constexpr std::int32_t MusicSlot = 3;
constexpr std::int32_t FixedSlots = 4;

/* QMixer was configured for 200 channels with 20 prioritised for 3D. The
 * game has never had anything like that many audible at once.
 */
constexpr std::int32_t MaxSlots = 32;

/* A sound queued behind another on a fixed slot. XAudio2 will queue buffers
 * on a voice by itself, but only in the voice's own format, and the game's
 * WAVs run from 11025 to 22050 Hz - SetSourceSampleRate cannot be called
 * while buffers are still queued. So the queue is kept here and the next
 * sound starts when the voice reports the previous one done.
 */
constexpr std::int32_t PendingMax = 16;

/* QMixer's distance mapping, which every 3D sound was given: audible out to
 * the track's radius, no attenuation within the minimum, and a rolloff scale
 * between the two. X3DAudio has no such model built in, so it is handed the
 * same shape as an explicit volume curve.
 */
constexpr float MinDistance = 300.0f;
constexpr float Rolloff = 1.5f;
constexpr std::int32_t CurvePoints = 8;

/* X3DAudio writes one coefficient per destination channel, and handles at
 * most eight of them.
 */
constexpr std::uint32_t MaxDestChannels = 8;

/* iSample carries a generation for pool slots, so a handle held past the end
 * of its sound cannot stop whatever has taken the slot over since.
 * Generations start at one, which keeps every pool handle above the fixed
 * slot indices. The fixed slots need none: stopping one is meant to flush
 * the channel.
 */
constexpr std::int32_t SlotBits = 8;
constexpr std::uint32_t SlotMask = 0xFF;
constexpr std::uint32_t GenerationMax = 0x7FFFFF;

constexpr std::uint32_t DefaultRate = 22050;

constexpr double Pi = 3.14159265359;

struct VoiceDestroyer
{
  void operator()(IXAudio2SourceVoice* _voice) const { _voice->DestroyVoice(); }
  void operator()(IXAudio2MasteringVoice* _voice) const { _voice->DestroyVoice(); }
};

struct EngineReleaser
{
  void operator()(IXAudio2* _engine) const { _engine->Release(); }
};

using SourceVoice = std::unique_ptr<IXAudio2SourceVoice, VoiceDestroyer>;
using MasteringVoice = std::unique_ptr<IXAudio2MasteringVoice, VoiceDestroyer>;
using Engine = std::unique_ptr<IXAudio2, EngineReleaser>;

struct Pending
{
  TRACK* track = nullptr;
  AUDIO_SAMPLE* sample = nullptr;
  std::int32_t baseVolume = 0;
  bool loop = false;
};

struct Slot
{
  SourceVoice voice;
  TRACK* track = nullptr; /* what is playing, for FreeTrack */
  AUDIO_SAMPLE* sample = nullptr; /* null when nothing is playing on it */
  std::uint32_t generation = 1;
  std::uint32_t rate = DefaultRate; /* rate the voice is currently tuned to */
  bool busy = false; /* playing, or holding queued sounds */
  bool positional = false;
  bool music = false;
  std::int32_t baseVolume = 0; /* 0 -> AudioMixer::MaxVolume() */
  std::int32_t radius = 0; /* audible radius of the track */
  std::int32_t x = 0, y = 0, z = 0;
  std::array<Pending, PendingMax> pending;
  std::int32_t pendingCount = 0;

  /* the stream and music voices take the file's own format, so their data
   * is owned per file rather than shared from a TRACK
   */
  std::unique_ptr<WavData> streamWav;
};

Engine g_engine;
MasteringVoice g_master;
std::array<Slot, MaxSlots> g_slots;
std::uint32_t g_outputChannels = 2;

/* X3DAudio does the spatialisation: it takes the listener and an emitter and
 * returns the per-speaker coefficients, attenuation included.
 */
X3DAUDIO_HANDLE g_x3d;
bool g_x3dReady = false;
X3DAUDIO_LISTENER g_listener;

std::int32_t g_globalVolume = AUDIO_VOL_MAX;
std::int32_t g_musicVolume = AUDIO_VOL_MAX;

/* The listener's position is kept in game units as well, because stealing a
 * pool slot picks the emitter furthest from it.
 */
std::int32_t g_listenerX = 0, g_listenerY = 0, g_listenerZ = 0;

/* Completion crosses a thread boundary here and nowhere else. XAudio2 calls
 * OnStreamEnd on its own audio thread; all that does is set the slot's bit,
 * and Update drains them on the game thread once a frame.
 */
std::atomic<std::uint32_t> g_finishedMask = 0;

void ApplySlotOutput(std::int32_t _slot);
void StopStreamSlot(std::int32_t _slot, bool _notify);

/***************************************************************************/

class SlotCallback final : public IXAudio2VoiceCallback
{
public:
  std::int32_t slot = -1;

  void STDMETHODCALLTYPE OnStreamEnd() override
  {
    if (slot < 0)
      return;

    g_finishedMask.fetch_or(1u << slot);
  }

  void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
  void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
  void STDMETHODCALLTYPE OnBufferStart(void*) override {}
  void STDMETHODCALLTYPE OnBufferEnd(void*) override {}
  void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
  void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override {}
};

std::array<SlotCallback, MaxSlots> g_callbacks;

/***************************************************************************/
/* handles */
/***************************************************************************/

std::int32_t HandleForSlot(std::int32_t _slot)
{
  if (_slot < FixedSlots)
    return _slot;

  return static_cast<std::int32_t>((g_slots[_slot].generation << SlotBits) | static_cast<std::uint32_t>(_slot));
}

/* Returns the slot a handle refers to, or -1 if the sound it was issued for
 * has since finished and the slot been reused.
 */
std::int32_t SlotFromHandle(std::int32_t _handle)
{
  if (_handle < 0)
    return -1;

  if (_handle < FixedSlots)
    return _handle;

  const auto slot = static_cast<std::int32_t>(static_cast<std::uint32_t>(_handle) & SlotMask);
  const auto generation = static_cast<std::uint32_t>(_handle) >> SlotBits;

  if (slot < FixedSlots || slot >= MaxSlots)
    return -1;

  if (g_slots[slot].generation != generation)
    return -1;

  return slot;
}

/***************************************************************************/
/* spatialisation */
/***************************************************************************/

/* QMixer's distance mapping as an X3DAudio volume curve, over normalised
 * distance where 1.0 is the track's audible radius: flat inside the minimum
 * distance, inverse distance with QMixer's rolloff beyond it, and silent at
 * the radius. X3DAudio's own default curve never reaches zero, and the
 * audible radius is authored per track, so the cutoff has to be stated.
 *
 * The points are strictly increasing and end at exactly 1.0, which X3DAudio
 * requires.
 */
void BuildVolumeCurve(float _radius, X3DAUDIO_DISTANCE_CURVE_POINT* _points)
{
  /* nearNorm, not near: windef.h #defines near away */
  const float nearNorm = _radius > MinDistance ? MinDistance / _radius : 0.0f;

  _points[0].Distance = 0.0f;
  _points[0].DSPSetting = 1.0f;

  for (std::int32_t i = 1; i < CurvePoints; i++)
  {
    const float t = static_cast<float>(i) / static_cast<float>(CurvePoints - 1);
    const float norm = nearNorm + (1.0f - nearNorm) * t;
    const float distance = norm * _radius;

    _points[i].Distance = norm;
    _points[i].DSPSetting = distance <= MinDistance ? 1.0f : MinDistance / (MinDistance + Rolloff * (distance - MinDistance));
  }

  /* inaudible at the audible radius, which is what the radius means */
  _points[CurvePoints - 1].Distance = 1.0f;
  _points[CurvePoints - 1].DSPSetting = 0.0f;
}

/***************************************************************************/

/* Runs X3DAudio for one positional slot and hands the result to the voice.
 * The coefficients carry the distance attenuation, so the voice's own volume
 * stays the sound's mix volume and nothing else.
 */
void ApplySpatial(std::int32_t _slot)
{
  Slot& slot = g_slots[_slot];
  X3DAUDIO_DISTANCE_CURVE_POINT points[CurvePoints];
  X3DAUDIO_DISTANCE_CURVE curve;
  X3DAUDIO_EMITTER emitter = {};
  X3DAUDIO_DSP_SETTINGS dsp = {};
  float matrix[MaxDestChannels] = {};

  const float radius = slot.radius > 0 ? static_cast<float>(slot.radius) : MinDistance;

  BuildVolumeCurve(radius, points);
  curve.pPoints = points;
  curve.PointCount = CurvePoints;

  emitter.OrientFront.z = 1.0f;
  emitter.OrientTop.y = 1.0f;
  emitter.Position.x = static_cast<float>(slot.x);
  emitter.Position.y = static_cast<float>(slot.z);
  emitter.Position.z = static_cast<float>(slot.y);
  emitter.ChannelCount = 1;
  emitter.CurveDistanceScaler = radius;
  emitter.pVolumeCurve = &curve;

  /* No doppler: nothing in the game gives a sound a velocity. */
  emitter.DopplerScaler = 0.0f;

  dsp.SrcChannelCount = 1;
  dsp.DstChannelCount = g_outputChannels;
  dsp.pMatrixCoefficients = matrix;

  X3DAudioCalculate(g_x3d, &g_listener, &emitter, X3DAUDIO_CALCULATE_MATRIX, &dsp);

  slot.voice->SetOutputMatrix(nullptr, 1, g_outputChannels, matrix);
}

/***************************************************************************/

void ApplySlotOutput(std::int32_t _slot)
{
  Slot& slot = g_slots[_slot];

  if (slot.voice == nullptr)
    return;

  auto volume = static_cast<float>(slot.baseVolume) / static_cast<float>(AudioMixer::MaxVolume());

  if (slot.music)
    volume = volume * static_cast<float>(g_musicVolume) / static_cast<float>(AUDIO_VOL_MAX);

  slot.voice->SetVolume(volume);

  /* Only positional sounds are spatialised; everything else keeps the
   * default output matrix, which is what the 2D channels had.
   */
  if (slot.positional && g_x3dReady)
    ApplySpatial(_slot);
}

/***************************************************************************/
/* slots */
/***************************************************************************/

/* A voice is not clear the moment it is flushed: the buffer has to drain
 * before SetSourceSampleRate is legal on it again.
 */
bool SlotIsClear(std::int32_t _slot)
{
  XAUDIO2_VOICE_STATE state;

  if (g_slots[_slot].voice == nullptr)
    return false;

  g_slots[_slot].voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);

  return state.BuffersQueued == 0;
}

/***************************************************************************/

/* Drops a completion the audio thread raised but Update has not drained yet.
 * Without this, stealing a slot from a sound that has just ended would hand
 * its pending completion to whatever starts on the slot next, and stop that
 * instead.
 */
void ClearFinished(std::int32_t _slot) { g_finishedMask.fetch_and(~(1u << _slot)); }

/***************************************************************************/

/* Waits for a flushed voice to let go of the buffer it was given. XAudio2
 * removes buffers on its own thread, so a flush is not the moment the memory
 * stops being read - and FreeTrack is about to give it back.
 */
void WaitForSlotClear(std::int32_t _slot)
{
  for (std::int32_t spin = 0; spin < 100 && SlotIsClear(_slot) == false; spin++)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

/***************************************************************************/

/* Stops a slot and hands its samples back. QMixer played everything with
 * QMIX_PLAY_NOTIFYSTOP, so a flushed sound reported completion just as a
 * finished one did, and the sample lifecycle relies on that to free the
 * sample.
 */
void ReleaseSlot(std::int32_t _slot, bool _notify)
{
  Slot& slot = g_slots[_slot];
  AUDIO_SAMPLE* sample = slot.sample;
  std::array<Pending, PendingMax> pending;
  const std::int32_t pendingCount = slot.pendingCount;

  if (slot.voice != nullptr)
  {
    slot.voice->Stop(0, XAUDIO2_COMMIT_NOW);
    slot.voice->FlushSourceBuffers();
  }

  ClearFinished(_slot);

  for (std::int32_t i = 0; i < pendingCount; i++)
    pending[i] = slot.pending[i];

  /* the slot is clear before any callback runs, so one that starts a new
   * sound finds it free rather than half torn down
   */
  slot.pendingCount = 0;
  slot.track = nullptr;
  slot.sample = nullptr;
  slot.busy = false;
  slot.positional = false;
  slot.music = false;

  if (_notify == false || AudioSystem::Active() == false)
    return;

  if (sample != nullptr)
    AudioSystem::FinishedCallback(*sample);

  for (std::int32_t i = 0; i < pendingCount; i++)
  {
    if (pending[i].sample != nullptr)
      AudioSystem::FinishedCallback(*pending[i].sample);
  }
}

/***************************************************************************/

/* Submits a track to a slot and starts it. The slot must be free and clear. */
bool StartSlot(std::int32_t _slot, TRACK& _track, AUDIO_SAMPLE* _sample, std::int32_t _baseVolume, bool _loop)
{
  Slot& slot = g_slots[_slot];
  auto* wav = static_cast<WavData*>(_track.pMem);
  XAUDIO2_BUFFER buffer = {};

  if (slot.voice == nullptr || wav == nullptr || wav->Samples().empty())
    return false;

  if (SlotIsClear(_slot) == false)
    return false;

  if (slot.rate != wav->SampleRate())
  {
    if (FAILED(slot.voice->SetSourceSampleRate(wav->SampleRate())))
    {
      Neuron::DebugTrace("AudioMixer: SetSourceSampleRate {} failed on slot {}\n", wav->SampleRate(), _slot);
      return false;
    }
    slot.rate = wav->SampleRate();
  }

  buffer.Flags = XAUDIO2_END_OF_STREAM;
  buffer.AudioBytes = static_cast<UINT32>(wav->Samples().size_bytes());
  buffer.pAudioData = reinterpret_cast<const BYTE*>(wav->Samples().data());
  buffer.LoopCount = _loop ? XAUDIO2_LOOP_INFINITE : 0;

  /* the slot is clear, so the last sound's completion has already been
   * raised; anything after this belongs to the one being started
   */
  ClearFinished(_slot);

  if (FAILED(slot.voice->SubmitSourceBuffer(&buffer, nullptr)))
    return false;

  slot.track = &_track;
  slot.sample = _sample;
  slot.baseVolume = _baseVolume;
  slot.busy = true;

  ApplySlotOutput(_slot);

  if (FAILED(slot.voice->Start(0, XAUDIO2_COMMIT_NOW)))
  {
    slot.voice->FlushSourceBuffers();
    slot.track = nullptr;
    slot.sample = nullptr;
    slot.busy = slot.pendingCount > 0;
    return false;
  }

  return true;
}

/***************************************************************************/

/* Play on a fixed slot, queueing behind whatever is on it. This is what
 * QMIX_QUEUEWAVE did on the speech and effects channels.
 */
bool PlayOnFixedSlot(std::int32_t _slot, TRACK& _track, AUDIO_SAMPLE& _sample, std::int32_t _baseVolume, bool _loop)
{
  Slot& slot = g_slots[_slot];

  if (slot.sample == nullptr && slot.pendingCount == 0 && StartSlot(_slot, _track, &_sample, _baseVolume, _loop))
    return true;

  if (slot.pendingCount >= PendingMax)
  {
    Neuron::DebugTrace("AudioMixer: slot {} queue full, dropping {}\n", _slot, _track.pName);
    return false;
  }

  slot.pending[slot.pendingCount++] = Pending{&_track, &_sample, _baseVolume, _loop};

  /* queued counts as on the slot: stopping it has to flush the queue too */
  slot.busy = true;

  return true;
}

/***************************************************************************/

/* Starts whatever is queued on a fixed slot, once it is free again. Called
 * every frame, because a slot can also fall idle without a completion - an
 * immediate start refused because the voice had not finished draining.
 */
void PumpFixedSlot(std::int32_t _slot)
{
  Slot& slot = g_slots[_slot];

  while (slot.sample == nullptr && slot.pendingCount > 0)
  {
    const Pending next = slot.pending[0];

    for (std::int32_t i = 1; i < slot.pendingCount; i++)
      slot.pending[i - 1] = slot.pending[i];
    slot.pendingCount--;

    if (StartSlot(_slot, *next.track, next.sample, next.baseVolume, next.loop))
      return;

    /* The voice is not ready yet, so put it back and try next frame. */
    if (SlotIsClear(_slot) == false)
    {
      for (std::int32_t i = slot.pendingCount; i > 0; i--)
        slot.pending[i] = slot.pending[i - 1];
      slot.pending[0] = next;
      slot.pendingCount++;
      return;
    }

    /* It will never play, so report it done or the sample is kept forever. */
    if (next.sample != nullptr && AudioSystem::Active())
      AudioSystem::FinishedCallback(*next.sample);
  }

  slot.busy = slot.sample != nullptr || slot.pendingCount > 0;
}

/***************************************************************************/

/* Takes a slot from the pool for a 3D sound: a free one if there is one,
 * else the sound furthest from the listener, which is what QMixer's
 * QMIX_FINDCHANNEL_DISTANCE picked.
 */
std::int32_t AcquirePoolSlot()
{
  std::int32_t furthest = -1;
  float furthestDistance = -1.0f;

  for (std::int32_t i = FixedSlots; i < MaxSlots; i++)
  {
    if (g_slots[i].busy == false && SlotIsClear(i))
      return i;
  }

  for (std::int32_t i = FixedSlots; i < MaxSlots; i++)
  {
    if (g_slots[i].positional == false)
      continue;

    const auto dx = static_cast<float>(g_slots[i].x - g_listenerX);
    const auto dy = static_cast<float>(g_slots[i].y - g_listenerY);
    const auto dz = static_cast<float>(g_slots[i].z - g_listenerZ);
    const float distance = dx * dx + dy * dy + dz * dz;

    if (distance > furthestDistance)
    {
      furthestDistance = distance;
      furthest = i;
    }
  }

  if (furthest < 0)
    return -1;

  ReleaseSlot(furthest, true);

  /* Releasing runs the stopped sound's callback, which can start another -
   * and a flushed voice is not clear until its buffer has drained either.
   */
  if (g_slots[furthest].busy || SlotIsClear(furthest) == false)
    return -1;

  return furthest;
}

/***************************************************************************/
/* streams */
/***************************************************************************/

std::vector<std::byte> ReadWholeFile(std::string_view _fileName)
{
  std::ifstream file(std::string(_fileName), std::ios::binary | std::ios::ate);
  if (!file)
    return {};

  const auto size = static_cast<std::size_t>(file.tellg());
  std::vector<std::byte> bytes(size);
  file.seekg(0);
  file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
  if (!file)
    return {};

  return bytes;
}

/***************************************************************************/

/* Tears a per-file voice down. The stream and music slots build theirs when
 * they are given a file, because they have to take its own channel count.
 */
void StopStreamSlot(std::int32_t _slot, bool _notify)
{
  Slot& slot = g_slots[_slot];

  ReleaseSlot(_slot, _notify);

  slot.voice.reset();
  slot.streamWav.reset();
}

/***************************************************************************/
/*
 * Reads a WAV whole and plays it on one of the per-file slots.
 *
 * Whole rather than streamed: the material is small - the largest file in
 * the game is 737 KB and all of sequenceAudio is 2.5 MB - so a buffer thread
 * would be machinery for nothing. Worth revisiting if the music served from
 * disk turns out to be large.
 */
/***************************************************************************/

bool StartStreamSlot(std::int32_t _slot, std::string_view _fileName, std::int32_t _volume, bool _music, bool _loop)
{
  Slot& slot = g_slots[_slot];
  WAVEFORMATEX voiceFormat = {};
  XAUDIO2_BUFFER buffer = {};

  if (g_engine == nullptr)
    return false;

  const auto bytes = ReadWholeFile(_fileName);
  if (bytes.empty())
  {
    Neuron::DebugTrace("AudioMixer: cannot open {}\n", _fileName);
    return false;
  }

  /* Stereo is kept here. The sequence audio has a stereo track in it, and
   * unlike the pooled voices this one is built to fit what it is given.
   */
  auto wav = WavData::FromBytes(bytes, false);
  if (!wav)
  {
    Neuron::DebugTrace("AudioMixer: cannot decode {}\n", _fileName);
    return false;
  }

  slot.streamWav = std::make_unique<WavData>(std::move(*wav));

  voiceFormat.wFormatTag = WAVE_FORMAT_PCM;
  voiceFormat.nChannels = slot.streamWav->Channels();
  voiceFormat.nSamplesPerSec = slot.streamWav->SampleRate();
  voiceFormat.wBitsPerSample = 16;
  voiceFormat.nBlockAlign = static_cast<WORD>(slot.streamWav->Channels() * sizeof(std::int16_t));
  voiceFormat.nAvgBytesPerSec = voiceFormat.nSamplesPerSec * voiceFormat.nBlockAlign;

  IXAudio2SourceVoice* rawVoice = nullptr;
  if (FAILED(g_engine->CreateSourceVoice(&rawVoice, &voiceFormat, 0, XAUDIO2_DEFAULT_FREQ_RATIO, &g_callbacks[_slot], nullptr,
                                         nullptr)))
  {
    Neuron::DebugTrace("AudioMixer: CreateSourceVoice failed for {}\n", _fileName);
    slot.streamWav.reset();
    return false;
  }
  slot.voice.reset(rawVoice);

  buffer.Flags = XAUDIO2_END_OF_STREAM;
  buffer.AudioBytes = static_cast<UINT32>(slot.streamWav->Samples().size_bytes());
  buffer.pAudioData = reinterpret_cast<const BYTE*>(slot.streamWav->Samples().data());
  buffer.LoopCount = _loop ? XAUDIO2_LOOP_INFINITE : 0;

  slot.track = nullptr;
  slot.busy = true;
  slot.positional = false;
  slot.music = _music;
  slot.baseVolume = _volume * AudioMixer::MaxVolume() / AUDIO_VOL_RANGE;

  ApplySlotOutput(_slot);

  if (FAILED(slot.voice->SubmitSourceBuffer(&buffer, nullptr)) || FAILED(slot.voice->Start(0, XAUDIO2_COMMIT_NOW)))
  {
    Neuron::DebugTrace("AudioMixer: cannot start {}\n", _fileName);
    StopStreamSlot(_slot, false);
    return false;
  }

  return true;
}

} // namespace

/***************************************************************************/
/* library */
/***************************************************************************/

bool AudioMixer::Init()
{
  WAVEFORMATEX format = {};
  XAUDIO2_VOICE_DETAILS details;
  DWORD channelMask = 0;

  for (Slot& slot : g_slots)
    slot = Slot{};

  g_finishedMask = 0;

  IXAudio2* rawEngine = nullptr;
  if (FAILED(XAudio2Create(&rawEngine, 0, XAUDIO2_DEFAULT_PROCESSOR)))
  {
    Neuron::DebugTrace("AudioMixer::Init: XAudio2Create failed\n");
    Shutdown();
    return false;
  }
  g_engine.reset(rawEngine);

  IXAudio2MasteringVoice* rawMaster = nullptr;
  if (FAILED(g_engine->CreateMasteringVoice(&rawMaster)))
  {
    Neuron::DebugTrace("AudioMixer::Init: CreateMasteringVoice failed\n");
    Shutdown();
    return false;
  }
  g_master.reset(rawMaster);

  g_master->GetVoiceDetails(&details);
  g_outputChannels = details.InputChannels;

  /* The listener starts at the origin facing north, which is what
   * SetListenerOrientation would compute for an angle of zero.
   */
  g_listener = {};
  g_listener.OrientFront.z = 1.0f;
  g_listener.OrientTop.y = 1.0f;

  /* X3DAudio writes one coefficient per destination channel and handles at
   * most eight, so an unusually wide endpoint means no spatialisation rather
   * than a buffer overrun.
   */
  g_x3dReady = false;

  /* GetChannelMask's result is deliberately not tested. It returns HRESULT
   * in the Windows SDK but void in mingw-w64, so wrapping it in FAILED
   * compiles on one and not the other; the mask staying zero is the same
   * check by another route, and a mask of zero is no use to X3DAudio anyway.
   */
  g_master->GetChannelMask(&channelMask);

  if (g_outputChannels > MaxDestChannels)
    Neuron::DebugTrace("AudioMixer::Init: {} output channels, 3D audio off\n", g_outputChannels);
  else if (channelMask == 0)
    Neuron::DebugTrace("AudioMixer::Init: no speaker mask, 3D audio off\n");
  else if (FAILED(X3DAudioInitialize(channelMask, X3DAUDIO_SPEED_OF_SOUND, g_x3d)))
    Neuron::DebugTrace("AudioMixer::Init: X3DAudioInitialize failed, 3D audio off\n");
  else
    g_x3dReady = true;

  /* The pooled voices are 16 bit mono. The rate here is only where they
   * start: each is retuned to the track it is given.
   */
  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = 1;
  format.nSamplesPerSec = DefaultRate;
  format.wBitsPerSample = 16;
  format.nBlockAlign = 2;
  format.nAvgBytesPerSec = DefaultRate * 2;

  for (std::int32_t i = 0; i < MaxSlots; i++)
  {
    g_callbacks[i].slot = i;

    /* the stream and music voices take the file's own format, so they are
     * built when one starts rather than here
     */
    if (i == StreamSlot || i == MusicSlot)
      continue;

    IXAudio2SourceVoice* rawVoice = nullptr;
    if (FAILED(g_engine->CreateSourceVoice(&rawVoice, &format, 0, XAUDIO2_DEFAULT_FREQ_RATIO, &g_callbacks[i], nullptr, nullptr)))
    {
      Neuron::DebugTrace("AudioMixer::Init: CreateSourceVoice failed for slot {}\n", i);
      Shutdown();
      return false;
    }
    g_slots[i].voice.reset(rawVoice);
  }

  SetGlobalVolume(g_globalVolume);

  return true;
}

/***************************************************************************/

IXAudio2* AudioMixer::Engine() { return g_engine.get(); }

/***************************************************************************/

void AudioMixer::Shutdown()
{
  for (Slot& slot : g_slots)
  {
    if (slot.voice != nullptr)
    {
      /* DestroyVoice waits for the audio thread, so no callback can still
       * be in flight once it returns
       */
      slot.voice->Stop(0, XAUDIO2_COMMIT_NOW);
      slot.voice.reset();
    }
    slot.track = nullptr;
    slot.sample = nullptr;
    slot.busy = false;
    slot.pendingCount = 0;
    slot.streamWav.reset();
  }

  g_master.reset();
  g_engine.reset();

  g_x3dReady = false;
  g_finishedMask = 0;
}

/***************************************************************************/
/* tracks */
/***************************************************************************/

bool AudioMixer::LoadTrack(TRACK& _track, std::span<const std::byte> _riff)
{
  /* Every track is normalised to 16 bit mono, which is what makes one pool
   * of voices able to play any of them: SetSourceSampleRate can retune a
   * voice, but nothing can restate its sample size or channel count.
   */
  auto wav = WavData::FromBytes(_riff, true);

  if (!wav || wav->SampleRate() == 0)
  {
    Neuron::Fatal("AudioMixer::LoadTrack: cannot decode {}", _track.pName);
    return false;
  }

  _track.bCompressed = wav->HasFactChunk() ? TRUE : FALSE;

  /* duration in milliseconds; +1 keeps the figure the old byte arithmetic
   * produced, and above zero for the shortest tracks
   */
  _track.iTime = static_cast<SDWORD>(wav->DurationMs()) + 1;
  _track.pMem = new WavData(std::move(*wav));

  return true;
}

/***************************************************************************/

void AudioMixer::FreeTrack(TRACK& _track)
{
  auto* wav = static_cast<WavData*>(_track.pMem);

  if (wav == nullptr)
  {
    Neuron::DebugTrace("AudioMixer::FreeTrack: invalid data pointer\n");
    return;
  }

  /* XAudio2 keeps the pointer a submitted buffer was given until the buffer
   * has been consumed, so nothing may still be reading these samples
   */
  for (std::int32_t i = 0; i < MaxSlots; i++)
  {
    if (g_slots[i].track == &_track)
    {
      ReleaseSlot(i, false);
      WaitForSlotClear(i);
    }

    for (std::int32_t j = g_slots[i].pendingCount - 1; j >= 0; j--)
    {
      if (g_slots[i].pending[j].track == &_track)
      {
        for (std::int32_t k = j + 1; k < g_slots[i].pendingCount; k++)
          g_slots[i].pending[k - 1] = g_slots[i].pending[k];
        g_slots[i].pendingCount--;
      }
    }
  }

  delete wav;
  _track.pMem = nullptr;
}

/***************************************************************************/
/* playing */
/***************************************************************************/

bool AudioMixer::Play2D(TRACK& _track, AUDIO_SAMPLE& _sample, bool _queued)
{
  const std::int32_t slot = _queued ? QueueSlot : FxSlot;

  _sample.iSample = slot;

  const std::int32_t baseVolume = AudioSystem::SampleMixVolume(_sample, AUDIO_VOL_MAX, false);

  if (PlayOnFixedSlot(slot, _track, _sample, baseVolume, AudioSystem::TrackLooped(_sample.iTrack)) == false)
  {
    _sample.iSample = SAMPLE_NOT_ALLOCATED;
    return false;
  }

  return true;
}

/***************************************************************************/

bool AudioMixer::Play3D(TRACK& _track, AUDIO_SAMPLE& _sample)
{
  const std::int32_t slotIndex = AcquirePoolSlot();

  if (slotIndex < 0)
  {
    _sample.iSample = SAMPLE_NOT_ALLOCATED;
    return false;
  }

  Slot& slot = g_slots[slotIndex];

  /* A new sound on the slot invalidates any handle held for the last one.
   * The wrap keeps the shifted handle positive, and skips zero so a pool
   * handle can never look like a fixed slot index.
   */
  slot.generation = slot.generation >= GenerationMax ? 1 : slot.generation + 1;

  slot.positional = true;
  slot.music = false;
  slot.radius = AudioSystem::TrackAudibleRadius(_sample.iTrack);
  slot.x = _sample.x;
  slot.y = _sample.y;
  slot.z = _sample.z;

  _sample.iSample = HandleForSlot(slotIndex);

  const std::int32_t baseVolume = AudioSystem::SampleMixVolume(_sample, AUDIO_VOL_MAX, true);

  if (StartSlot(slotIndex, _track, &_sample, baseVolume, AudioSystem::TrackLooped(_sample.iTrack)) == false)
  {
    slot.positional = false;
    _sample.iSample = SAMPLE_NOT_ALLOCATED;
    return false;
  }

  return true;
}

/***************************************************************************/
/*
 * PlayStream honours the volume it is given; QMixer's took iVol and then
 * ignored it, scaling by whatever track zero's volume happened to be.
 */
/***************************************************************************/

bool AudioMixer::PlayStream(AUDIO_SAMPLE& _sample, std::string_view _fileName, std::int32_t _volume)
{
  /* one stream at a time, which is what every caller already assumes */
  StopStreamSlot(StreamSlot, true);

  if (StartStreamSlot(StreamSlot, _fileName, _volume, false, false) == false)
  {
    _sample.iSample = SAMPLE_NOT_ALLOCATED;
    return false;
  }

  _sample.iSample = StreamSlot;
  g_slots[StreamSlot].sample = &_sample;

  return true;
}

/***************************************************************************/
/*
 * Music. Its own slot, because the CD it replaces was a device of its own:
 * starting a briefing's audio must not stop the music, and the music staying
 * paused across a video is the point of the pause. No AUDIO_SAMPLE is
 * involved - nothing above waits on music finishing.
 */
/***************************************************************************/

bool AudioMixer::PlayMusic(std::string_view _fileName, std::int32_t _volume, bool _loop)
{
  StopStreamSlot(MusicSlot, false);

  return StartStreamSlot(MusicSlot, _fileName, _volume, true, _loop);
}

/***************************************************************************/

void AudioMixer::StopMusic() { StopStreamSlot(MusicSlot, false); }

/***************************************************************************/

/* Stop on a source voice holds its position, so this is a pause and not a
 * stop: the buffer stays queued and Start picks up where it left off.
 */
void AudioMixer::PauseMusic()
{
  if (g_slots[MusicSlot].voice != nullptr)
    g_slots[MusicSlot].voice->Stop(0, XAUDIO2_COMMIT_NOW);
}

/***************************************************************************/

void AudioMixer::ResumeMusic()
{
  if (g_slots[MusicSlot].voice != nullptr)
    g_slots[MusicSlot].voice->Start(0, XAUDIO2_COMMIT_NOW);
}

/***************************************************************************/

void AudioMixer::StopSample(std::int32_t _handle)
{
  const std::int32_t slot = SlotFromHandle(_handle);

  /* A handle that no longer names a live sound is not an error: the sound
   * it was issued for has already finished and reported itself.
   */
  if (slot < 0)
    return;

  if (slot == StreamSlot || slot == MusicSlot)
    StopStreamSlot(slot, true);
  else
    ReleaseSlot(slot, true);
}

/***************************************************************************/

void AudioMixer::StopAllVoices()
{
  /* Called from shutdown, which frees the sample lists itself, so nothing
   * is notified here.
   */
  for (std::int32_t i = 0; i < MaxSlots; i++)
  {
    if (i == StreamSlot || i == MusicSlot)
      StopStreamSlot(i, false);
    else
      ReleaseSlot(i, false);
  }
}

/***************************************************************************/

bool AudioMixer::QueueSlotBusy() { return g_slots[QueueSlot].busy; }

/***************************************************************************/
/* volume */
/***************************************************************************/

std::int32_t AudioMixer::MaxVolume() { return 32767; }

/***************************************************************************/
/*
 * The master volume, which the FX slider and the mute around video playback
 * move. It used to be waveOutSetVolume on one side and the Windows mixer's
 * wave line on the other, which are different controls - so the game saved
 * one and restored the other. It is the mastering voice now, and the two
 * halves cannot disagree.
 */
/***************************************************************************/

std::int32_t AudioMixer::GlobalVolume() { return g_globalVolume; }

/***************************************************************************/

void AudioMixer::SetGlobalVolume(std::int32_t _volume)
{
  g_globalVolume = std::clamp(_volume, static_cast<std::int32_t>(AUDIO_VOL_MIN), static_cast<std::int32_t>(AUDIO_VOL_MAX));

  if (g_master != nullptr)
    g_master->SetVolume(static_cast<float>(g_globalVolume) / static_cast<float>(AUDIO_VOL_MAX));
}

/***************************************************************************/

std::int32_t AudioMixer::MusicVolume() { return g_musicVolume; }

/***************************************************************************/

void AudioMixer::SetMusicVolume(std::int32_t _volume)
{
  g_musicVolume = std::clamp(_volume, static_cast<std::int32_t>(AUDIO_VOL_MIN), static_cast<std::int32_t>(AUDIO_VOL_MAX));

  /* take effect on what is playing, not just on the next track */
  if (g_slots[MusicSlot].busy)
    ApplySlotOutput(MusicSlot);
}

/***************************************************************************/
/* positioning */
/***************************************************************************/

/*
 * The game's audio space is x east, y north and z height - the game adapter
 * inverts world y on the way in, which is why north is positive. X3DAudio is
 * left handed with x right, y up and z forward, so the two differ by
 * swapping y and z, and nothing else.
 *
 * Checking the handedness comes out right: at an angle of zero the listener
 * faces north, which is X3DAudio's +z, with +y up; the listener's right is
 * then +x, which is east. Facing north, east is on your right.
 */
void AudioMixer::SetListenerPosition(std::int32_t _x, std::int32_t _y, std::int32_t _z)
{
  g_listenerX = _x;
  g_listenerY = _y;
  g_listenerZ = _z;

  g_listener.Position.x = static_cast<float>(_x);
  g_listener.Position.y = static_cast<float>(_z);
  g_listener.Position.z = static_cast<float>(_y);
}

/***************************************************************************/
/*
 * Orientation given as an angle in degrees about the vertical axis. QMixer
 * was handed a direction vector of (-sin, cos, 0) built from it in the
 * game's axes; the same vector goes to X3DAudio with y and z swapped.
 */
/***************************************************************************/

void AudioMixer::SetListenerOrientation(std::int32_t _degrees)
{
  const double angle = static_cast<double>(_degrees) * Pi / 180.0;

  g_listener.OrientFront.x = static_cast<float>(-sin(angle));
  g_listener.OrientFront.y = 0.0f;
  g_listener.OrientFront.z = static_cast<float>(cos(angle));

  g_listener.OrientTop.x = 0.0f;
  g_listener.OrientTop.y = 1.0f;
  g_listener.OrientTop.z = 0.0f;
}

/***************************************************************************/

void AudioMixer::SetEmitterPosition(std::int32_t _handle, std::int32_t _x, std::int32_t _y, std::int32_t _z)
{
  const std::int32_t slot = SlotFromHandle(_handle);

  if (slot < 0)
    return;

  g_slots[slot].x = _x;
  g_slots[slot].y = _y;
  g_slots[slot].z = _z;
}

/***************************************************************************/
/* frame */
/***************************************************************************/

void AudioMixer::Update()
{
  if (g_engine == nullptr)
    return;

  /* Take the completions XAudio2's thread has raised since the last frame.
   * Everything below this line runs on the game thread.
   */
  const std::uint32_t finished = g_finishedMask.exchange(0);

  for (std::int32_t i = 0; i < MaxSlots; i++)
  {
    Slot& slot = g_slots[i];

    if ((finished & (1u << i)) == 0)
      continue;

    AUDIO_SAMPLE* sample = slot.sample;

    slot.track = nullptr;
    slot.sample = nullptr;
    slot.positional = false;
    slot.busy = slot.pendingCount > 0;

    if (sample != nullptr && AudioSystem::Active())
      AudioSystem::FinishedCallback(*sample);

    if (i == StreamSlot || i == MusicSlot)
      StopStreamSlot(i, false);
  }

  /* Start anything queued on the fixed slots. This runs every frame rather
   * than only after a completion, because a slot can also fall idle when an
   * immediate start was refused by a voice that had not finished draining.
   */
  PumpFixedSlot(QueueSlot);
  PumpFixedSlot(FxSlot);

  /* Refresh attenuation and pan for everything positional. QMixer applied
   * its distance mapping continuously as positions were fed in; this is
   * where that happens now.
   */
  for (std::int32_t i = FixedSlots; i < MaxSlots; i++)
  {
    if (g_slots[i].busy && g_slots[i].positional)
      ApplySlotOutput(i);
  }
}

} // namespace Neuron

/***************************************************************************/
