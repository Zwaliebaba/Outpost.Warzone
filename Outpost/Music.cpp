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

#include "Frame.h"
#include "Audio.h"
#include "Music.h"

/***************************************************************************/

#define	MUSIC_DIRECTORY			"music"
#define	MUSIC_NO_TRACK			-1

/***************************************************************************/

static SDWORD g_iCurTrack = MUSIC_NO_TRACK;

/***************************************************************************/
/*
 * The music assets are not in the repository: they were on the disc. Until
 * they are, this resolves to nothing and the game is quiet, which is why a
 * missing file traces rather than failing.
 */
/***************************************************************************/

BOOL music_PlayTrack(SDWORD iTrack)
{
  char szFileName[MAX_STR];

  g_iCurTrack = iTrack;

  wsprintf(szFileName, "%s\\track%d.wav", MUSIC_DIRECTORY, iTrack);

  /* the CD looped the track until something else was asked for */
  if (audio_PlayMusic(szFileName, AUDIO_VOL_MAX, TRUE) == FALSE)
  {
    Neuron::DebugTrace("music_PlayTrack: no music for track {} ({})\n", iTrack, szFileName);
    return FALSE;
  }

  return TRUE;
}

/***************************************************************************/

void music_Stop(void)
{
  g_iCurTrack = MUSIC_NO_TRACK;
  audio_StopMusic();
}

/***************************************************************************/

void music_Pause(void) { audio_PauseMusic(); }

/***************************************************************************/

void music_Resume(void) { audio_ResumeMusic(); }

/***************************************************************************/
