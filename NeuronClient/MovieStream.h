#pragma once

/***************************************************************************/
/*
 * MovieStream -- FMV playback on Media Foundation.
 *
 * Phase 6 replaces WINSTR.LIB, the 1997 Eidos streaming library that decoded
 * the game's ESCAPE 130 `.rpl` movies. The assets are re-encoded to H.264/AAC
 * in MP4 offline (tools/convert_sequences.py and tools/convert_ogg.py) and
 * played back through this, which decodes with IMFSourceReader and mixes its
 * soundtrack on the game's own XAudio2 graph.
 *
 * Sequence.h is unchanged and still the public face of FMV: this class sits
 * behind it, and Sequence.cpp is now a shim that owns one instance. SeqDisp.cpp
 * drives the timing exactly as it always has, so nothing above the seam had to
 * learn a new model.
 */
/***************************************************************************/

#include <cstdint>
#include <string>
#include <vector>

/* winrt::com_ptr instantiates its member's destructor wherever the class is
 * destroyed, so IMFSourceReader has to be complete here rather than forward
 * declared. IXAudio2SourceVoice comes along for the same reason.
 */
#include <mfapi.h>
#include <mfidl.h>      // mfreadwrite.h uses IMFMediaSource and friends from here
#include <mfreadwrite.h>
#include <xaudio2.h>
#include <winrt/base.h>

namespace Neuron
{

class MovieStream
{
public:
  MovieStream() = default;
  ~MovieStream();

  MovieStream(const MovieStream&) = delete;
  MovieStream& operator=(const MovieStream&) = delete;

  /* Opens a movie and starts its soundtrack. False means the file is missing
   * or undecodable; the caller falls back to the noVideo placeholder, which is
   * what the .rpl code did too.
   */
  bool Open(const char* _fileName);
  void Close();

  bool IsOpen() const { return m_reader != nullptr; }

  /* Decodes the next frame, discarding _framesToSkip - 1 before it so a caller
   * running behind can catch up. Returns the index of the frame now held, or
   * -1 once the movie has ended.
   */
  int DecodeNextFrame(int _framesToSkip);

  /* The frame last decoded, as X8R8G8B8 with no padding between rows. Empty
   * until DecodeNextFrame has returned a frame.
   */
  const std::uint32_t* FramePixels() const { return m_frame.empty() ? nullptr : m_frame.data(); }

  int Width() const { return m_width; }
  int Height() const { return m_height; }
  int CurrentFrame() const { return m_currentFrame; }
  int TotalFrames() const { return m_totalFrames; }

  /* Milliseconds per frame. SeqDisp.cpp uses this as its pacing interval and
   * converts subtitle cue frames through it, and every subtitle in the game is
   * authored against 25 fps, so this must stay at 40 for the shipped assets.
   */
  int FrameTimeMs() const { return m_frameTimeMs; }

private:
  bool OpenVideoStream();
  bool OpenAudioStream();
  bool ReadAudioTrack(std::vector<std::uint8_t>& _pcm, std::uint32_t& _samplesPerSecond, std::uint16_t& _channels);
  void StartAudio(const std::vector<std::uint8_t>& _pcm, std::uint32_t _samplesPerSecond, std::uint16_t _channels);
  void StopAudio();
  bool ReadOneFrame(bool _keepPixels);

  winrt::com_ptr<IMFSourceReader> m_reader;

  /* Not a com_ptr: a source voice is destroyed through DestroyVoice rather
   * than Release, and it must not outlive the engine that made it.
   */
  IXAudio2SourceVoice* m_voice = nullptr;
  std::vector<std::uint8_t> m_audio;

  std::vector<std::uint32_t> m_frame;
  int m_width = 0;
  int m_height = 0;
  int m_stride = 0; // in pixels, negative when Media Foundation hands back a bottom-up image
  int m_currentFrame = -1;
  int m_totalFrames = -1;
  int m_frameTimeMs = 40;
  bool m_hasAudio = false;
  bool m_finished = false;
};

} // namespace Neuron
