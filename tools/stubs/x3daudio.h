/* Cross-check shim for x3daudio.h. NOT part of the build.
 *
 * crosscheck.py copies this into the shadow tree's stub directory because
 * mingw-w64 ships xaudio2.h, xaudio2fx.h, xapo.h and xapofx.h but no
 * x3daudio.h, so XA2Track.cpp could not be syntax-checked at all without it.
 * MSVC never sees this file: it uses the real header from the Windows SDK.
 *
 * That makes this weaker than the rest of the cross-check. It verifies the
 * backend's use of X3DAudio against a transcription of the API rather than
 * against the SDK, so it catches arity, spelling and type errors in our code
 * but cannot catch a mistake in the transcription itself. The declarations
 * below were taken from two independent sources that agree:
 *
 *   - FAudio's F3DAudio.h, a reimplementation of the same ABI, for the
 *     structure layouts and field order.
 *   - DirectXTK's Audio.h and SoundCommon.cpp, Microsoft's own code against
 *     the Windows 10 SDK, for the function signatures -- in particular that
 *     X3DAudioInitialize returns HRESULT here, where XAudio 2.7's returned
 *     void, and that xaudio2.lib is the only import library needed.
 *
 * The Windows CI build remains the authority, as it does for the linker.
 */

#ifndef _X3DAUDIO_CROSSCHECK_STUB_H_
#define _X3DAUDIO_CROSSCHECK_STUB_H_

#include <windows.h>

#define X3DAUDIO_HANDLE_BYTESIZE 20
typedef BYTE X3DAUDIO_HANDLE[X3DAUDIO_HANDLE_BYTESIZE];

#define X3DAUDIO_PI 3.141592654f
#define X3DAUDIO_2PI 6.283185307f

#define X3DAUDIO_SPEED_OF_SOUND 343.5f

#define X3DAUDIO_CALCULATE_MATRIX 0x00000001
#define X3DAUDIO_CALCULATE_DELAY 0x00000002
#define X3DAUDIO_CALCULATE_LPF_DIRECT 0x00000004
#define X3DAUDIO_CALCULATE_LPF_REVERB 0x00000008
#define X3DAUDIO_CALCULATE_REVERB 0x00000010
#define X3DAUDIO_CALCULATE_DOPPLER 0x00000020
#define X3DAUDIO_CALCULATE_EMITTER_ANGLE 0x00000040
#define X3DAUDIO_CALCULATE_ZEROCENTER 0x00010000
#define X3DAUDIO_CALCULATE_REDIRECT_TO_LFE 0x00020000

#pragma pack(push, 1)

typedef struct X3DAUDIO_VECTOR
{
  float x;
  float y;
  float z;
} X3DAUDIO_VECTOR;

typedef struct X3DAUDIO_DISTANCE_CURVE_POINT
{
  float Distance;
  float DSPSetting;
} X3DAUDIO_DISTANCE_CURVE_POINT;

typedef struct X3DAUDIO_DISTANCE_CURVE
{
  X3DAUDIO_DISTANCE_CURVE_POINT* pPoints;
  UINT32 PointCount;
} X3DAUDIO_DISTANCE_CURVE;

typedef struct X3DAUDIO_CONE
{
  float InnerAngle;
  float OuterAngle;
  float InnerVolume;
  float OuterVolume;
  float InnerLPF;
  float OuterLPF;
  float InnerReverb;
  float OuterReverb;
} X3DAUDIO_CONE;

typedef struct X3DAUDIO_LISTENER
{
  X3DAUDIO_VECTOR OrientFront;
  X3DAUDIO_VECTOR OrientTop;
  X3DAUDIO_VECTOR Position;
  X3DAUDIO_VECTOR Velocity;
  X3DAUDIO_CONE* pCone;
} X3DAUDIO_LISTENER;

typedef struct X3DAUDIO_EMITTER
{
  X3DAUDIO_CONE* pCone;
  X3DAUDIO_VECTOR OrientFront;
  X3DAUDIO_VECTOR OrientTop;
  X3DAUDIO_VECTOR Position;
  X3DAUDIO_VECTOR Velocity;
  float InnerRadius;
  float InnerRadiusAngle;
  UINT32 ChannelCount;
  float ChannelRadius;
  float* pChannelAzimuths;
  X3DAUDIO_DISTANCE_CURVE* pVolumeCurve;
  X3DAUDIO_DISTANCE_CURVE* pLFECurve;
  X3DAUDIO_DISTANCE_CURVE* pLPFDirectCurve;
  X3DAUDIO_DISTANCE_CURVE* pLPFReverbCurve;
  X3DAUDIO_DISTANCE_CURVE* pReverbCurve;
  float CurveDistanceScaler;
  float DopplerScaler;
} X3DAUDIO_EMITTER;

typedef struct X3DAUDIO_DSP_SETTINGS
{
  float* pMatrixCoefficients;
  float* pDelayTimes;
  UINT32 SrcChannelCount;
  UINT32 DstChannelCount;
  float LPFDirectCoefficient;
  float LPFReverbCoefficient;
  float ReverbLevel;
  float DopplerFactor;
  float EmitterToListenerAngle;
  float EmitterToListenerDistance;
  float EmitterVelocityComponent;
  float ListenerVelocityComponent;
} X3DAUDIO_DSP_SETTINGS;

#pragma pack(pop)

extern "C" {
HRESULT __stdcall X3DAudioInitialize(UINT32 SpeakerChannelMask, float SpeedOfSound, X3DAUDIO_HANDLE Instance);

void __stdcall X3DAudioCalculate(const X3DAUDIO_HANDLE Instance, const X3DAUDIO_LISTENER* pListener, const X3DAUDIO_EMITTER* pEmitter,
                                 UINT32 Flags, X3DAUDIO_DSP_SETTINGS* pDSPSettings);
}

#endif	// _X3DAUDIO_CROSSCHECK_STUB_H_
