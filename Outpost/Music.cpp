#include "pch.h"
/***************************************************************************/
/*
 * Music.cpp
 *
 * In-game music, served from files on disk through the audio system's music
 * slot. Replaces CDAudio.cpp, which drove the CD with MCI: it opened the
 * cdaudio device, subclassed the main window to catch MM_MCINOTIFY, and
 * restarted the track when it heard one finish.
 *
 * The track numbers are unchanged, because the scripts pass them --
 * playCDAudio is still in the script function table and the compiled scripts
 * under GameData/script are matched against that table by position. Track 2 is
 * the front end music, which is the only number the C++ ever passes.
 */
/***************************************************************************/

#include <format>
#include <string>

#include "Frame.h"
#include "AudioSystem.h"
#include "Music.h"

/***************************************************************************/

namespace
{

constexpr const char* MusicDirectory = "music";

} // namespace

/***************************************************************************/
/*
 * The music assets are not in the repository: they were on the disc. Until
 * they are, this resolves to nothing and the game is quiet, which is why a
 * missing file traces rather than failing.
 */
/***************************************************************************/

bool Music::PlayTrack(SDWORD _track)
{
  const std::string fileName = std::format("{}\\track{}.wav", MusicDirectory, _track);

  /* the CD looped the track until something else was asked for */
  if (AudioSystem::PlayMusic(fileName.c_str(), AUDIO_VOL_MAX, true) == false)
  {
    Neuron::DebugTrace("Music::PlayTrack: no music for track {} ({})\n", _track, fileName);
    return false;
  }

  return true;
}

/***************************************************************************/

void Music::Stop() { AudioSystem::StopMusic(); }

/***************************************************************************/

void Music::Pause() { AudioSystem::PauseMusic(); }

/***************************************************************************/

void Music::Resume() { AudioSystem::ResumeMusic(); }

/***************************************************************************/
