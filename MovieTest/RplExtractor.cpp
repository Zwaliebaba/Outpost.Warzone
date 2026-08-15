/*
 * RplExtractor -- a reference decode of the game's ESCAPE 130 `.rpl` movies.
 *
 * Phase 6 replaces WINSTR.LIB, which means the `.rpl` assets have to be
 * re-encoded, which means something has to say whether the re-encode is
 * faithful. This is that something: it drives the shipping decoder through the
 * same twenty-one entry points Sequence.cpp uses, with the same parameters, and
 * writes what comes out. Whatever the converter produces is compared against
 * these dumps, not against a second guess at the format.
 *
 * It links WINSTR.LIB, which Phase 6 deletes. That is deliberate and it is why
 * the dumps are checked in as fixtures before the deletion lands -- see
 * Docs/Phase6Plan.md, stage B1. After that this project stops building and is
 * removed with the library; MovieTest keeps only the fixture comparison.
 *
 *   RplExtractor <gameDataDir> <movieRelativePath> <outputDir>
 *
 * Modelled on NetTest: a console program, not part of the game, built and run
 * by CI so that a failure says why.
 */

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <direct.h>
#include <filesystem>
#include <string>
#include <vector>

#include "STREAMER.H"

namespace
{

/* Every constant here is Sequence.cpp's, and has to stay Sequence.cpp's. The
 * point of the tool is to reproduce what the shipping decoder produces, so the
 * parameters handed to the streamer are copied from seq_SetSequence rather than
 * chosen.
 */
constexpr UINT SoundBufferBytes = 16384;
constexpr UINT SoundRingBytes = SoundBufferBytes * 2;
constexpr UINT SoundTransferBytes = 4096;
constexpr ULONG MovieBufferBytes = 2 << 20;

/* RGB565 with no alpha -- the SEQ_*_POS / SEQ_*_BITS block at the top of
 * Sequence.cpp.
 */
constexpr BYTE AlphaPos = 0;
constexpr BYTE AlphaBits = 0;
constexpr BYTE RedPos = 11;
constexpr BYTE RedBits = 5;
constexpr BYTE GreenPos = 5;
constexpr BYTE GreenBits = 6;
constexpr BYTE BluePos = 0;
constexpr BYTE BlueBits = 5;

/* Movie_GetTotalFrames is advisory: BrfCom.rpl reports 60 but has 59 chunks,
 * and 59 is what decodes. This only stops a decoder that never reports an end
 * from writing until the disk fills.
 */
constexpr int FrameCountSlack = 8;

/* Frames are decoded one at a time; the game passes a lag-derived skip count,
 * which would drop frames a reference dump has to keep.
 */
constexpr LONG FramesPerStep = 1;

class RplExtractor
{
public:
  bool Open(const std::filesystem::path& _moviePath);
  bool Run(const std::filesystem::path& _outputDir, const std::string& _stem);
  void Close();

  ~RplExtractor() { Close(); }

  /* Called by the streamer when it has finished filling one half of the ring.
   * The signature is a bare function pointer with no user data, so the active
   * extractor has to be reachable from a global -- as it is in Sequence.cpp.
   */
  void OnSoundBufferFilled(LPSOUNDHANDLE _handle);

private:
  bool InitialiseSound();
  void RequestNextSoundBuffer(int _frameIndex);
  void AppendSoundBuffer(int _which);

  LPMOVIEHANDLE m_movie = nullptr;
  LPVIDEOHANDLE m_video = nullptr;
  LPSOUNDHANDLE m_sound = nullptr;

  LONG m_width = 0;
  LONG m_height = 0;
  float m_frameRate = 0.0f;
  LONG m_totalFrames = 0;

  LONG m_soundChannels = 0;
  LONG m_soundPrecision = 0;
  LONG m_soundRate = 0;

  /* One contiguous block so the two halves are laid out exactly as the
   * DirectSound buffer's were.
   */
  std::vector<unsigned char> m_soundRing;
  std::vector<unsigned char> m_audio;
  std::vector<unsigned char> m_frame;

  LONG m_lastUpdated = SSDM_SECONDBUFFER;
  int m_framesWritten = 0;
  bool m_audioFinished = false;
};

RplExtractor* g_activeExtractor = nullptr;

void SoundBufferCallback(LPSOUNDHANDLE _handle)
{
  if (g_activeExtractor != nullptr)
    g_activeExtractor->OnSoundBufferFilled(_handle);
}

const char* StreamerResultName(STRESULT _result)
{
  switch (_result)
  {
  case STREAMER_OK: return "OK";
  case STREAMER_NO_SOUND_IN_RPL: return "NO_SOUND_IN_RPL";
  case STREAMER_FILE_OPEN_ERROR: return "FILE_OPEN_ERROR";
  case STREAMER_BAD_MOVIE_HEADER: return "BAD_MOVIE_HEADER";
  case STREAMER_BADDATARATE: return "BADDATARATE";
  case STREAMER_FINISHEDVIDEO: return "FINISHEDVIDEO";
  case STREAMER_FINISHEDAUDIO: return "FINISHEDAUDIO";
  case STREAMER_BADMOVIEFORMAT: return "BADMOVIEFORMAT";
  case STREAMER_130CODECOPENERROR: return "130CODECOPENERROR";
  default: return "other";
  }
}

/* A 16-bit PCM RIFF header. Written last, once the sample count is known. */
bool WriteWav(const std::filesystem::path& _path, const std::vector<unsigned char>& _pcm, LONG _channels, LONG _rate)
{
  FILE* file = nullptr;
  if (fopen_s(&file, _path.string().c_str(), "wb") != 0 || file == nullptr)
    return false;

  const unsigned int dataBytes = static_cast<unsigned int>(_pcm.size());
  const unsigned short channels = static_cast<unsigned short>(_channels);
  const unsigned short bitsPerSample = 16;
  const unsigned short blockAlign = static_cast<unsigned short>(channels * bitsPerSample / 8);
  const unsigned int byteRate = static_cast<unsigned int>(_rate) * blockAlign;
  const unsigned int riffBytes = 36 + dataBytes;
  const unsigned int formatBytes = 16;
  const unsigned short formatTag = 1;

  fwrite("RIFF", 1, 4, file);
  fwrite(&riffBytes, 4, 1, file);
  fwrite("WAVEfmt ", 1, 8, file);
  fwrite(&formatBytes, 4, 1, file);
  fwrite(&formatTag, 2, 1, file);
  fwrite(&channels, 2, 1, file);
  fwrite(&_rate, 4, 1, file);
  fwrite(&byteRate, 4, 1, file);
  fwrite(&blockAlign, 2, 1, file);
  fwrite(&bitsPerSample, 2, 1, file);
  fwrite("data", 1, 4, file);
  fwrite(&dataBytes, 4, 1, file);
  if (dataBytes != 0)
    fwrite(_pcm.data(), 1, dataBytes, file);

  fclose(file);
  return true;
}

bool RplExtractor::Open(const std::filesystem::path& _moviePath)
{
  std::string path = _moviePath.string();

  STRESULT result = Streamer_InitMovie(&m_movie, nullptr, 0, path.data(), MovieBufferBytes, SIM_NONE);
  if (result != STREAMER_OK)
  {
    fprintf(stderr, "Streamer_InitMovie(%s) failed: %ld (%s)\n", path.c_str(), result, StreamerResultName(result));
    return false;
  }

  m_width = Movie_GetXSize(m_movie);
  m_height = Movie_GetYSize(m_movie);
  m_frameRate = Movie_GetFrameRate(m_movie);
  m_totalFrames = Movie_GetTotalFrames(m_movie);

  /* No DFLAG_DOUBLED. The game doubles the small research movies for display;
   * a reference dump wants the frames at their authored size.
   */
  LONG bufferPixelWidth = 0;
  LONG bufferPixelDepth = 0;
  result = Streamer_InitVideo(&m_video, m_movie, m_width, m_height, 0, 0, 0, 0, 0, 0, DFLAG_INVIEWPORT, &bufferPixelWidth,
                              &bufferPixelDepth);
  if (result != STREAMER_OK)
  {
    fprintf(stderr, "Streamer_InitVideo failed: %ld (%s)\n", result, StreamerResultName(result));
    return false;
  }

  if (Streamer_SetVideoPitch(m_video, m_width, m_height) != STREAMER_OK)
  {
    fprintf(stderr, "Streamer_SetVideoPitch failed\n");
    return false;
  }

  if (!InitialiseSound())
    return false;

  result = Streamer_InitStreaming(m_movie, m_video, m_sound);
  if (result != STREAMER_OK)
  {
    fprintf(stderr, "Streamer_InitStreaming failed: %ld (%s)\n", result, StreamerResultName(result));
    return false;
  }

  /* InitStreaming preloads both halves of the ring, which is why the game
   * locked them before calling it. Both hold audio now, so both are captured
   * before anything asks for a refill.
   */
  if (m_sound != nullptr)
  {
    AppendSoundBuffer(0);
    AppendSoundBuffer(1);
    Streamer_SetSoundDecodeMode(m_sound, SSDM_IDLE);
    m_lastUpdated = SSDM_SECONDBUFFER;
  }

  /* After InitStreaming, exactly as in seq_SetSequence. */
  result = Streamer_SetPixelFormat(m_video, SPF_BPP16, AlphaPos, AlphaBits, RedPos, RedBits, GreenPos, GreenBits, BluePos, BlueBits);
  if (result != STREAMER_OK)
  {
    fprintf(stderr, "Streamer_SetPixelFormat failed: %ld (%s)\n", result, StreamerResultName(result));
    return false;
  }

  m_frame.assign(static_cast<size_t>(m_width) * static_cast<size_t>(m_height) * 2, 0);
  return true;
}

bool RplExtractor::InitialiseSound()
{
  m_soundChannels = Movie_GetSoundChannels(m_movie);
  m_soundPrecision = Movie_GetSoundPrecision(m_movie);
  m_soundRate = Movie_GetSoundRate(m_movie);

  /* The silent movies take their audio from sequenceAudio\*.wav instead, and
   * there is nothing here to extract for them.
   */
  if (m_soundChannels == 0 || m_soundPrecision == 0 || m_soundRate == 0)
    return true;

  UINT compression = 1;
  BOOL pcm = TRUE;
  if (m_soundPrecision == 4)
  {
    compression = 4;
    pcm = FALSE;
  }

  STRESULT result = Streamer_InitSound(SoundBufferCallback, &m_sound, SoundBufferBytes, compression, pcm, SoundTransferBytes,
                                       static_cast<UINT>(m_soundChannels));
  if (result != STREAMER_OK)
  {
    fprintf(stderr, "Streamer_InitSound failed: %ld (%s)\n", result, StreamerResultName(result));
    return false;
  }

  m_soundRing.assign(SoundRingBytes, 0);
  Streamer_SetSoundBuffer(m_sound, 0, m_soundRing.data());
  Streamer_SetSoundBuffer(m_sound, 1, m_soundRing.data() + SoundBufferBytes);
  return true;
}

void RplExtractor::AppendSoundBuffer(int _which)
{
  const unsigned char* start = m_soundRing.data() + static_cast<size_t>(_which) * SoundBufferBytes;
  m_audio.insert(m_audio.end(), start, start + SoundBufferBytes);
}

void RplExtractor::OnSoundBufferFilled(LPSOUNDHANDLE _handle)
{
  const LONG state = Streamer_GetSoundDecodeMode(_handle);

  if (state == SSDM_FIRSTBUFFER)
    AppendSoundBuffer(0);
  else if (state == SSDM_SECONDBUFFER)
    AppendSoundBuffer(1);

  Streamer_SetSoundDecodeMode(_handle, SSDM_IDLE);
  m_lastUpdated = state;
}

/* seq_RenderOneFrame asks DirectSound where its play cursor is and refills the
 * half the hardware has just left. There is no DirectSound here, so the cursor
 * is computed from the frame index at the movie's own rate. Same ring, same
 * boundaries, same order -- which is what makes the captured bytes the bytes
 * the game would have played.
 */
void RplExtractor::RequestNextSoundBuffer(int _frameIndex)
{
  if (m_sound == nullptr || m_audioFinished)
    return;

  if (Streamer_GetSoundDecodeMode(m_sound) != SSDM_IDLE)
    return;

  const double bytesPerSecond = static_cast<double>(m_soundRate) * static_cast<double>(m_soundChannels) * 2.0;
  const double rate = (m_frameRate > 0.0f) ? static_cast<double>(m_frameRate) : 25.0;
  const double played = static_cast<double>(_frameIndex) * bytesPerSecond / rate;
  const double cursor = std::fmod(played, static_cast<double>(SoundRingBytes));

  if (m_lastUpdated == SSDM_SECONDBUFFER && cursor > SoundBufferBytes)
    Streamer_SetSoundDecodeMode(m_sound, SSDM_FIRSTBUFFER);
  else if (m_lastUpdated == SSDM_FIRSTBUFFER && cursor < SoundBufferBytes)
    Streamer_SetSoundDecodeMode(m_sound, SSDM_SECONDBUFFER);
}

bool RplExtractor::Run(const std::filesystem::path& _outputDir, const std::string& _stem)
{
  const std::filesystem::path framePath = _outputDir / (_stem + ".rgb565");

  FILE* frameFile = nullptr;
  if (fopen_s(&frameFile, framePath.string().c_str(), "wb") != 0 || frameFile == nullptr)
  {
    fprintf(stderr, "cannot open %s for writing\n", framePath.string().c_str());
    return false;
  }

  STRESULT result = STREAMER_OK;
  bool videoFinished = false;

  while (!videoFinished)
  {
    result = Streamer_Stream(m_movie, m_video, m_sound, nullptr, FramesPerStep, m_frame.data(), nullptr, nullptr, 0);

    if (result == STREAMER_FINISHEDAUDIO)
    {
      m_audioFinished = true;
      m_sound = nullptr;
      result = STREAMER_OK;
    }

    if (result == STREAMER_OK)
    {
      fwrite(m_frame.data(), 1, m_frame.size(), frameFile);
      m_framesWritten++;
      RequestNextSoundBuffer(m_framesWritten);
    }
    else
    {
      videoFinished = true;
    }

    if (m_totalFrames > 0 && m_framesWritten > m_totalFrames + FrameCountSlack)
      videoFinished = true;
  }

  fclose(frameFile);

  /* Capture stops with the video, and the audio is then cut to the video's
   * exact duration.
   *
   * Both halves of that are deliberate. The ring is filled a buffer at a time,
   * so the last one captured always overshoots the end of the movie and its
   * tail is whatever the decoder left there -- the game never plays those bytes
   * because playback stops with the picture. And there is no draining past the
   * end: asked to stream zero frames after the video is done, this decoder
   * keeps handing back buffers and never reports STREAMER_FINISHEDAUDIO, so a
   * drain loop bounded only by an iteration count produces megabytes of
   * repeated audio. The eight movies that carry a sound track have it authored
   * to the length of their video, so there is nothing past the end to want.
   */
  const size_t rawAudioBytes = m_audio.size();
  if (!m_audio.empty() && m_frameRate > 0.0f)
  {
    const double seconds = static_cast<double>(m_framesWritten) / static_cast<double>(m_frameRate);
    const double frames = seconds * static_cast<double>(m_soundRate);
    const size_t exactBytes = static_cast<size_t>(frames + 0.5) * static_cast<size_t>(m_soundChannels) * 2;
    if (exactBytes < m_audio.size())
      m_audio.resize(exactBytes);
  }

  const bool hasAudio = !m_audio.empty();
  if (hasAudio)
  {
    const std::filesystem::path wavPath = _outputDir / (_stem + ".wav");
    if (!WriteWav(wavPath, m_audio, m_soundChannels, m_soundRate))
    {
      fprintf(stderr, "cannot write %s\n", wavPath.string().c_str());
      return false;
    }
  }

  const std::filesystem::path manifestPath = _outputDir / (_stem + ".json");
  FILE* manifest = nullptr;
  if (fopen_s(&manifest, manifestPath.string().c_str(), "wb") != 0 || manifest == nullptr)
  {
    fprintf(stderr, "cannot write %s\n", manifestPath.string().c_str());
    return false;
  }

  fprintf(manifest,
          "{\n"
          "  \"stem\": \"%s\",\n"
          "  \"width\": %ld,\n"
          "  \"height\": %ld,\n"
          "  \"frameRate\": %.6f,\n"
          "  \"totalFrames\": %ld,\n"
          "  \"framesWritten\": %d,\n"
          "  \"pixelFormat\": \"rgb565\",\n"
          "  \"frameBytes\": %zu,\n"
          "  \"soundChannels\": %ld,\n"
          "  \"soundRate\": %ld,\n"
          "  \"soundPrecision\": %ld,\n"
          "  \"audioBytes\": %zu,\n"
          "  \"audioBytesCaptured\": %zu\n"
          "}\n",
          _stem.c_str(), m_width, m_height, static_cast<double>(m_frameRate), m_totalFrames, m_framesWritten, m_frame.size(),
          m_soundChannels, m_soundRate, m_soundPrecision, m_audio.size(), rawAudioBytes);
  fclose(manifest);

  const double audioSeconds =
    hasAudio ? static_cast<double>(m_audio.size()) / (static_cast<double>(m_soundRate) * static_cast<double>(m_soundChannels) * 2.0) : 0.0;
  printf("%-16s %ldx%ld  frames=%d/%ld @%.3f fps (%.2fs)  audio=%zu bytes (%.2fs, %ld Hz, %ld ch)\n", _stem.c_str(), m_width, m_height,
         m_framesWritten, m_totalFrames, static_cast<double>(m_frameRate),
         (m_frameRate > 0.0f) ? m_framesWritten / static_cast<double>(m_frameRate) : 0.0, m_audio.size(), audioSeconds, m_soundRate,
         m_soundChannels);

  return m_framesWritten > 0;
}

void RplExtractor::Close()
{
  if (m_sound != nullptr)
    Streamer_ShutDownSound(&m_sound);
  if (m_video != nullptr)
    Streamer_ShutDownVideo(&m_video);
  if (m_movie != nullptr)
    Streamer_ShutDownMovie(&m_movie);

  m_sound = nullptr;
  m_video = nullptr;
  m_movie = nullptr;
}

} // namespace

int main(int _argc, char** _argv)
{
  if (_argc != 4)
  {
    fprintf(stderr, "usage: RplExtractor <gameDataDir> <movieRelativePath> <outputDir>\n");
    return 2;
  }

  const std::filesystem::path gameDataDir = _argv[1];
  const std::filesystem::path relativeMovie = _argv[2];
  const std::filesystem::path outputDir = _argv[3];

  std::error_code error;
  std::filesystem::create_directories(outputDir, error);
  const std::filesystem::path absoluteOutput = std::filesystem::absolute(outputDir, error);

  /* The decoder loads Dec130.dll by name, so the process has to be sitting in
   * the directory that holds it -- which is also the directory the game's
   * relative "sequences\\..." paths are written against.
   */
  if (_chdir(gameDataDir.string().c_str()) != 0)
  {
    fprintf(stderr, "cannot enter %s\n", gameDataDir.string().c_str());
    return 1;
  }

  const std::string stem = relativeMovie.stem().string();

  RplExtractor extractor;
  g_activeExtractor = &extractor;

  bool ok = extractor.Open(relativeMovie);
  if (ok)
    ok = extractor.Run(absoluteOutput, stem);

  extractor.Close();
  g_activeExtractor = nullptr;

  return ok ? 0 : 1;
}
