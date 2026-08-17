/***************************************************************************/

#ifndef _TRACK_H_
#define _TRACK_H_

/***************************************************************************/
/* The audio module's legacy wire types and constants. Everything that used
 * to be declared here besides these moved into Neuron::AudioSystem and
 * Neuron::AudioMixer in Phase 9; this header and Audio.h survive as the shim
 * surface until stage F renames the call sites.
 */

#define	SAMPLE_NOT_ALLOCATED	-1
#define	SAMPLE_NOT_FOUND		-3
#define	SAMPLE_COORD_INVALID	-5

#define	AUDIO_VOL_MIN			0L
#define	AUDIO_VOL_MAX			100L
#define	AUDIO_VOL_RANGE			(AUDIO_VOL_MAX-AUDIO_VOL_MIN)

/***************************************************************************/
/* forward definitions
 */

struct AUDIO_SAMPLE;

/***************************************************************************/
/* typedefs
 */

using AUDIO_CALLBACK = BOOL(*)(struct AUDIO_SAMPLE* psSample);

/***************************************************************************/
/* structs */

using AUDIO_SAMPLE = struct AUDIO_SAMPLE
{
  SDWORD iTrack;
  SDWORD iSample;
  SDWORD x, y, z;
  BOOL bRemove;
  AUDIO_CALLBACK pCallback;
  void* psObj;
};

using TRACK = struct TRACK
{
  BOOL bLoop;
  SDWORD iVol;
  SDWORD iPriority; /* authored in audio.json; no reader since Phase 4 stole by distance */
  SDWORD iAudibleRadius;
  SDWORD iTime; /* duration in milliseconds */
  UDWORD iTimeLastFinished; /* time last finished in ms */
  BOOL bCompressed; /* compression data flag    */
  void* pMem; /* the decoded Neuron::WavData */
  STRING* pName; // resource name of the track
  UDWORD resID; // hashed name of the WAV
};

/***************************************************************************/

#endif	// _TRACK_H_

/***************************************************************************/
