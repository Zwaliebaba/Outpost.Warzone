/***************************************************************************/
/*
 * Music.h
 *
 * In-game music, served from files on disk. Replaces CDAudio.h, which drove
 * the CD through MCI.
 */
/***************************************************************************/

#ifndef _MUSIC_H_
#define _MUSIC_H_

/***************************************************************************/

/* Track numbers are the ones the game and its scripts already use; track 2 is
 * the front end music. Each maps onto a file under MUSIC_DIRECTORY.
 */
BOOL music_PlayTrack(SDWORD iTrack);
void music_Stop(void);
void music_Pause(void);
void music_Resume(void);

/***************************************************************************/

#endif	// _MUSIC_H_

/***************************************************************************/
