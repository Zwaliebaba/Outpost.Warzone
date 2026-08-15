#include "pch.h"

#include "MovieStream.h"

#include "Frame.h"
#include "TrackLib.h"

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <xaudio2.h>

#include <algorithm>
#include <cstring>

namespace Neuron
{

namespace
{

/* Media Foundation counts in 100-nanosecond units everywhere. */
constexpr std::int64_t HundredNanosPerSecond = 10'000'000;

/* Every shipped movie is 25 fps and every subtitle cue is a frame number
 * against that. Used only when a file fails to report its own rate.
 */
constexpr int DefaultFrameTimeMs = 40;

/* MFStartup is reference counted, but there is no natural place to call
 * MFShutdown: seq_ShutDown runs at the end of every sequence, not at the end
 * of the process. So it is started once and left running, which costs a
 * platform object for the life of the game.
 */
bool g_mediaFoundationStarted = false;

bool StartMediaFoundation()
{
  if (g_mediaFoundationStarted)
    return true;

  const HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
  if (FAILED(hr))
  {
    /* Every movie fails identically from here, and the placeholder fallback
     * would quietly hide it. On Windows N without the Media Feature Pack this
     * is exactly what happens, so say so rather than playing 179 blank videos.
     */
    Neuron::Fatal("Media Foundation failed to start (0x{:08x}).\n"
                  "FMV playback needs the Media Foundation H.264 and AAC decoders.\n"
                  "On Windows N or KN editions, install the Media Feature Pack.",
                  static_cast<unsigned>(hr));
    return false;
  }

  g_mediaFoundationStarted = true;
  return true;
}

std::wstring WidenPath(const char* _fileName)
{
  const int needed = MultiByteToWideChar(CP_ACP, 0, _fileName, -1, nullptr, 0);
  if (needed <= 0)
    return {};

  std::wstring wide(static_cast<size_t>(needed - 1), L'\0');
  MultiByteToWideChar(CP_ACP, 0, _fileName, -1, wide.data(), needed);
  return wide;
}

} // namespace

MovieStream::~MovieStream() { Close(); }

bool MovieStream::Open(const char* _fileName)
{
  Close();

  if (_fileName == nullptr || !StartMediaFoundation())
    return false;

  const std::wstring path = WidenPath(_fileName);
  if (path.empty())
    return false;

  winrt::com_ptr<IMFAttributes> attributes;
  if (FAILED(MFCreateAttributes(attributes.put(), 1)))
    return false;

  /* Let the reader hand back frames the video processor has already converted,
   * which is what makes an RGB32 output type available at all.
   */
  attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

  const HRESULT hr = MFCreateSourceReaderFromURL(path.c_str(), attributes.get(), m_reader.put());
  if (FAILED(hr))
  {
    /* A missing movie is ordinary -- 9 of the 181 the game references have no
     * source in any format -- and SeqDisp.cpp answers it with the placeholder.
     */
    Neuron::DebugTrace("MovieStream::Open: cannot open {} (0x{:08x})\n", _fileName, static_cast<unsigned>(hr));
    m_reader = nullptr;
    return false;
  }

  if (!OpenVideoStream())
  {
    Close();
    return false;
  }

  m_hasAudio = OpenAudioStream();
  if (m_hasAudio)
  {
    std::vector<std::uint8_t> pcm;
    std::uint32_t samplesPerSecond = 0;
    std::uint16_t channels = 0;
    if (ReadAudioTrack(pcm, samplesPerSecond, channels) && !pcm.empty())
      StartAudio(pcm, samplesPerSecond, channels);
    else
      m_hasAudio = false;
  }

  m_currentFrame = -1;
  m_finished = false;
  return true;
}

bool MovieStream::OpenVideoStream()
{
  m_reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
  if (FAILED(m_reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE)))
    return false;

  winrt::com_ptr<IMFMediaType> wanted;
  if (FAILED(MFCreateMediaType(wanted.put())))
    return false;

  wanted->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  /* RGB32 is X8R8G8B8 in memory, which is the back buffer's format, so the
   * blit is a copy rather than the per-pixel 565 conversion the .rpl path did.
   */
  wanted->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);

  if (FAILED(m_reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, wanted.get())))
    return false;

  winrt::com_ptr<IMFMediaType> actual;
  if (FAILED(m_reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, actual.put())))
    return false;

  UINT32 width = 0, height = 0;
  if (FAILED(MFGetAttributeSize(actual.get(), MF_MT_FRAME_SIZE, &width, &height)) || width == 0 || height == 0)
    return false;

  m_width = static_cast<int>(width);
  m_height = static_cast<int>(height);

  /* A negative default stride means the decoder is handing back a bottom-up
   * image, which is the usual answer for RGB32. The row copy below flips it.
   */
  INT32 stride = 0;
  if (SUCCEEDED(actual->GetUINT32(MF_MT_DEFAULT_STRIDE, reinterpret_cast<UINT32*>(&stride))) && stride != 0)
    m_stride = stride / 4;
  else
    m_stride = -m_width;

  UINT32 rateNumerator = 0, rateDenominator = 0;
  if (SUCCEEDED(MFGetAttributeRatio(actual.get(), MF_MT_FRAME_RATE, &rateNumerator, &rateDenominator)) && rateNumerator != 0)
    m_frameTimeMs = static_cast<int>((1000LL * rateDenominator) / rateNumerator);
  else
    m_frameTimeMs = DefaultFrameTimeMs;

  if (m_frameTimeMs <= 0)
    m_frameTimeMs = DefaultFrameTimeMs;

  PROPVARIANT duration;
  PropVariantInit(&duration);
  if (SUCCEEDED(m_reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &duration)) &&
      duration.vt == VT_UI8 && m_frameTimeMs > 0)
  {
    const std::int64_t milliseconds = static_cast<std::int64_t>(duration.uhVal.QuadPart) / (HundredNanosPerSecond / 1000);
    m_totalFrames = static_cast<int>(milliseconds / m_frameTimeMs);
  }
  PropVariantClear(&duration);

  m_frame.assign(static_cast<size_t>(m_width) * static_cast<size_t>(m_height), 0);
  return true;
}

bool MovieStream::OpenAudioStream()
{
  /* Eleven of the shipped movies have no soundtrack at all: they are the ones
   * the game pairs with a sequenceAudio\*.wav, played by the mixer and used as
   * the sequence's clock. Selecting a stream that is not there is not an error.
   */
  return SUCCEEDED(m_reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE));
}

bool MovieStream::ReadAudioTrack(std::vector<std::uint8_t>& _pcm, std::uint32_t& _samplesPerSecond, std::uint16_t& _channels)
{
  winrt::com_ptr<IMFMediaType> wanted;
  if (FAILED(MFCreateMediaType(wanted.put())))
    return false;

  wanted->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
  wanted->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);

  if (FAILED(m_reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, wanted.get())))
    return false;

  winrt::com_ptr<IMFMediaType> actual;
  if (FAILED(m_reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, actual.put())))
    return false;

  UINT32 value = 0;
  actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &value);
  _samplesPerSecond = value;
  value = 0;
  actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &value);
  _channels = static_cast<std::uint16_t>(value);

  if (_samplesPerSecond == 0 || _channels == 0)
    return false;

  /* The whole track is decoded up front rather than streamed. The longest
   * soundtrack in the game is 85 seconds of 22 kHz stereo -- about 7 MB -- so
   * a buffer thread would be machinery for nothing, and XA2Track.cpp made the
   * same call for the same reason.
   */
  for (;;)
  {
    DWORD flags = 0;
    winrt::com_ptr<IMFSample> sample;
    if (FAILED(m_reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, sample.put())))
      return false;

    if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0)
      break;
    if (sample == nullptr)
      continue;

    winrt::com_ptr<IMFMediaBuffer> buffer;
    if (FAILED(sample->ConvertToContiguousBuffer(buffer.put())))
      return false;

    BYTE* bytes = nullptr;
    DWORD length = 0;
    if (FAILED(buffer->Lock(&bytes, nullptr, &length)))
      return false;

    _pcm.insert(_pcm.end(), bytes, bytes + length);
    buffer->Unlock();
  }

  /* Rewind: the video read below has to start at the beginning again. */
  PROPVARIANT start;
  PropVariantInit(&start);
  start.vt = VT_I8;
  start.hVal.QuadPart = 0;
  m_reader->SetCurrentPosition(GUID_NULL, start);
  PropVariantClear(&start);

  return true;
}

void MovieStream::StartAudio(const std::vector<std::uint8_t>& _pcm, std::uint32_t _samplesPerSecond, std::uint16_t _channels)
{
  IXAudio2* engine = sound_GetEngine();
  if (engine == nullptr)
    return; // the movie plays silently, as it did when DirectSound was unavailable

  WAVEFORMATEX format = {};
  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = _channels;
  format.nSamplesPerSec = _samplesPerSecond;
  format.wBitsPerSample = 16;
  format.nBlockAlign = static_cast<WORD>(_channels * 2);
  format.nAvgBytesPerSec = _samplesPerSecond * format.nBlockAlign;

  if (FAILED(engine->CreateSourceVoice(&m_voice, &format)))
  {
    m_voice = nullptr;
    return;
  }

  /* The buffer has to outlive the submission, so it is kept in the member and
   * only released once the voice is destroyed.
   */
  m_audio = _pcm;

  XAUDIO2_BUFFER buffer = {};
  buffer.AudioBytes = static_cast<UINT32>(m_audio.size());
  buffer.pAudioData = m_audio.data();
  buffer.Flags = XAUDIO2_END_OF_STREAM;

  if (FAILED(m_voice->SubmitSourceBuffer(&buffer)) || FAILED(m_voice->Start(0)))
  {
    StopAudio();
  }
}

void MovieStream::StopAudio()
{
  if (m_voice != nullptr)
  {
    m_voice->Stop(0);
    m_voice->FlushSourceBuffers();
    m_voice->DestroyVoice();
    m_voice = nullptr;
  }
  m_audio.clear();
  m_audio.shrink_to_fit();
}

bool MovieStream::ReadOneFrame(bool _keepPixels)
{
  DWORD flags = 0;
  winrt::com_ptr<IMFSample> sample;

  if (FAILED(m_reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, nullptr, sample.put())))
    return false;

  if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0)
    return false;

  if (sample == nullptr)
    return true; // a gap rather than an end; the caller asks again

  if (!_keepPixels)
    return true;

  winrt::com_ptr<IMFMediaBuffer> buffer;
  if (FAILED(sample->ConvertToContiguousBuffer(buffer.put())))
    return false;

  BYTE* bytes = nullptr;
  DWORD length = 0;
  if (FAILED(buffer->Lock(&bytes, nullptr, &length)))
    return false;

  const int rowPixels = m_width;
  const auto* source = reinterpret_cast<const std::uint32_t*>(bytes);

  if (m_stride < 0)
  {
    /* Bottom-up: the first row in memory is the last row of the picture. */
    source += static_cast<size_t>(-m_stride) * static_cast<size_t>(m_height - 1);
    for (int y = 0; y < m_height; ++y)
    {
      std::memcpy(&m_frame[static_cast<size_t>(y) * rowPixels], source, static_cast<size_t>(rowPixels) * 4);
      source += m_stride; // negative, so this walks backwards
    }
  }
  else
  {
    for (int y = 0; y < m_height; ++y)
    {
      std::memcpy(&m_frame[static_cast<size_t>(y) * rowPixels], source, static_cast<size_t>(rowPixels) * 4);
      source += m_stride;
    }
  }

  buffer->Unlock();
  return true;
}

int MovieStream::DecodeNextFrame(int _framesToSkip)
{
  if (m_reader == nullptr || m_finished)
    return -1;

  /* Anything above one means the caller is behind and wants the intervening
   * frames thrown away rather than drawn.
   */
  const int toDiscard = std::max(0, _framesToSkip - 1);
  for (int i = 0; i < toDiscard; ++i)
  {
    if (!ReadOneFrame(false))
    {
      m_finished = true;
      return -1;
    }
    ++m_currentFrame;
  }

  if (!ReadOneFrame(true))
  {
    m_finished = true;
    return -1;
  }

  ++m_currentFrame;
  return m_currentFrame;
}

void MovieStream::Close()
{
  StopAudio();

  m_reader = nullptr;
  m_frame.clear();
  m_frame.shrink_to_fit();

  m_width = 0;
  m_height = 0;
  m_stride = 0;
  m_currentFrame = -1;
  m_totalFrames = -1;
  m_frameTimeMs = DefaultFrameTimeMs;
  m_hasAudio = false;
  m_finished = false;
}

} // namespace Neuron
