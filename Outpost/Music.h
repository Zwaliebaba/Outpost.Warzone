#pragma once

/***************************************************************************/
/*
 * Music.h
 *
 * In-game music, served from files on disk. Replaces CDAudio.h, which drove
 * the CD through MCI.
 */
/***************************************************************************/

/// The music player. Track numbers are the ones the game and its scripts
/// already use - track 2 is the front end music - and each maps onto a file
/// under the music directory.
class Music
{
public:
  static bool PlayTrack(SDWORD _track);
  static void Stop();
  static void Pause();
  static void Resume();
};

/***************************************************************************/
