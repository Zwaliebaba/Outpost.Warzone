# Phase 4 — Audio: XAudio2, dropping QMixer and CD audio

Working plan for the phase described in [MigrationPlan.md](MigrationPlan.md#phase-4--audio-xaudio2-dropping-qmixer-and-cd-audio).
As there, the figures here were measured against the tree, not estimated; the
method is at the end.

**Status: implemented.** What was built, and where it departed from the plan
below, is recorded under [What was built](#what-was-built). The six open
decisions are settled; the reasoning is kept with each one.

## Current state

| File | Lines | Role |
|---|---:|---|
| `NeuronCore/QSTrack.cpp` | 708 | QMixer backend — **replaced** |
| `NeuronCore/QMIXER.H` | 885 | QMixer SDK header — **deleted** |
| `NeuronCore/QMixer.lib`, `GameData/QMixer.dll` | — | QMixer binaries — **deleted** |
| `NeuronCore/EAX.H` | 130 | vestigial, zero references, not in any project — **deleted** |
| `NeuronCore/TrackLib.h` | 75 | the backend interface — trimmed, otherwise kept |
| `NeuronCore/Track.cpp` | 580 | track table, backend-agnostic — two functions change |
| `NeuronCore/Audio.cpp` | 1029 | sample lifecycle, backend-agnostic — one function changes |
| `NeuronCore/Mixer.cpp` | 221 | Win9x `mixerXxx` volume control — **deleted** |
| `Outpost/CDAudio.cpp` | 202 | MCI CD audio — **deleted** |
| `Outpost/CDSpan.cpp` | 534 | multi-CD data location — see [CD spanning](#cd-spanning-is-a-separate-decision) |

MigrationPlan.md quotes larger figures for several of these (849 for
`QSTrack.cpp`, 1289 for `Audio.cpp`, 703 for `Track.cpp`). Those predate the
debug-system and allocator work, which reformatted and shortened them. The
table above is current.

## Why this is smaller than it looks

Three measurements set the size of the job, and all three point the same way.

**The live interface is 15 functions, not 31.** `TrackLib.h` declares 31 entry
points. Five are declared and never defined anywhere — `sound_SetSampleFreq`,
`sound_GetNumSamples`, `sound_GetObject`, `sound_SetObject`,
`sound_SetCallback`. Five more are implemented in `QSTrack.cpp` but have no
caller outside it: `sound_SetSampleVolAll`, `sound_SampleIsFinished`,
`sound_PauseSample`, `sound_ResumeSample`, and `sound_SetSamplePan` (which
`QSTrack.cpp` calls internally, always with the centre value). Two more,
`sound_PauseAll` and `sound_ResumeAll`, are reachable only from
`audio_PauseAll`/`audio_ResumeAll`, which have no callers either.
`sound_GetGameTime` is not a backend function at all — it lives in
`Outpost/Aud.cpp` and returns `gameTime`. And `sound_ReadTrackFromFile` is
dead: every WAV enters through the resource system as a memory buffer
(`Data.cpp` registers `dataAudioLoad` for the `"WAV"` type, which calls
`audio_LoadTrackFromBuffer`), so `audio_LoadTrackFromFile` has no callers.

What is actually reached from game code:

| | |
|---|---|
| Lifecycle | `sound_InitLibrary`, `sound_ShutdownLibrary`, `sound_Update` |
| Tracks | `sound_ReadTrackFromBuffer`, `sound_FreeTrack` |
| Playback | `sound_Play2DSample`, `sound_Play3DSample`, `sound_PlayStream`, `sound_StopSample`, `sound_StopAll` |
| Positioning | `sound_SetPlayerPos`, `sound_SetPlayerOrientation`, `sound_SetObjectPosition` |
| Volume | `sound_GetMaxVolume`, `sound_SetSampleVol` |
| Query | `sound_QueueSamplePlaying` |
| Leak | `sound_GetDirectSoundObj` — see [below](#the-fmv-and-voice-paths-borrow-qmixers-directsound-object) |

**Every asset is uncompressed PCM.** All 551 `.wav` files under `GameData/`
carry format tag 1. There is no ADPCM anywhere, so the new backend needs no
codec work at all — and `USE_COMPRESSED_SPEECH`, already `0`, can go with the
dead branches it guards.

| Format | Files |
|---|---:|
| 8-bit mono, 11025 Hz | 513 |
| 16-bit mono, 16000 Hz | 22 |
| 16-bit mono, 22050 Hz | 4 |
| everything else (8-bit 16/22 kHz, 16-bit 15000/17050 Hz, two stereo) | 12 |

XAudio2 accepts 8-bit unsigned PCM directly, and any rate between
`XAUDIO2_MIN_SAMPLE_RATE` and `XAUDIO2_MAX_SAMPLE_RATE`, so nothing here is
out of range. 16-bit is documented as the optimal format, which is one reason
to normalise at load — the other is voice pooling, below.

**QMixer's own surface is 25 functions across 80 call sites**, all inside
`QSTrack.cpp`. Nothing above `TrackLib.h` names QMixer at all. The containment
the plan claims is real.

## Two things the phase has to deal with first

### The FMV and voice paths borrow QMixer's DirectSound object

`sound_GetDirectSoundObj` calls `QSWaveMixGetDirectSound` and hands out the
`IDirectSound` that QMixer created for itself. `audio_GetDirectSoundObj` wraps
it, and three call sites consume it:

- `Outpost/SeqDisp.cpp:193` and `:449` — `seq_SetSequenceForBuffer` and
  `seq_SetSequence` create a 32 KB looping `IDirectSoundBuffer` that the
  `WINSTR.LIB` codec's sound callback double-buffers into. This is FMV audio.
- `Outpost/MultiOpt.cpp:488` — `NETinitPlaybackBuffer`, the multiplayer voice
  chat playback buffer.

Delete QMixer and all three lose their `IDirectSound`. This is the one place
where Phase 4 reaches outside the audio module, and it is not mentioned in
MigrationPlan.md. Both fixes are small:

- `NetAudio.cpp` already handles it. `NETinitPlaybackBuffer(nullptr)` takes the
  `ourDSPointer = TRUE` branch and calls `setupSoundPlay()`, which does its own
  `DirectSoundCreate` + `SetCooperativeLevel`. Change the one call site in
  `MultiOpt.cpp` to pass `nullptr`. `NetAudio.cpp` is dropped entirely in
  Phase 5, so this is a stop-gap by design.
- `Sequence.cpp` needs its own. About fifteen lines: `DirectSoundCreate`,
  `SetCooperativeLevel(hwnd, DSSCL_PRIORITY)`, held for the process lifetime,
  released at shutdown. DirectSound and XAudio2 are independent clients of the
  same audio engine and coexist without trouble.

Doing it this way keeps `Sequence.cpp` untouched in every other respect, which
matters because Phase 2 rewrites its video path and Phase 6 replaces its
decoder. Routing FMV audio through XAudio2 instead is the better end state, but
it belongs with that rewrite, not here. Once both call sites are converted,
`sound_GetDirectSoundObj`, `audio_GetDirectSoundObj` and the `#include
"dsound.h"` in `TrackLib.h` and `Audio.h` all come out.

### The volume sliders drive the Windows mixer, not the game

`Mixer.cpp` is the Win9x `mixerXxx` API: it finds the `SRC_COMPACTDISC` and
`SRC_WAVEOUT` lines on mixer device 0 and moves the *system* volume for them,
saving and restoring the user's settings around the game session
(`WinMain.cpp:296-311`). The FX slider in `FrontEnd.cpp` and `InGameOp.cpp`
calls `mixer_SetWavVolume`; the music slider calls `mixer_SetCDVolume`.

Two things follow. First, `mixer_Open` returns `FALSE` unless it finds *both*
lines, and a modern machine has no CD line — so `bMixerOK` is almost certainly
already `FALSE` in practice and both sliders already do nothing. Second, even
where it works, changing a system mixer line from a game is behaviour that
should not survive the migration.

So `Mixer.cpp` goes, and both sliders move onto the XAudio2 graph. That is the
natural home for them: the FX slider becomes the mastering voice volume (or an
SFX submix), and the music slider becomes the volume of the streaming voice
that replaces CD audio. `Config.cpp` persists both as `fxvol` and `cdvol`;
keep those key names so existing `.cfg` files still load.

`Track.cpp`'s `sound_GetGlobalVolume`/`sound_SetGlobalVolume` are the same
problem in a second place — they call `waveOutGetVolume`/`waveOutSetVolume`
with a null device handle. They move onto the mastering voice too.

## Target: XAudio 2.9 from the Windows SDK

The vendored `DX9/Include` has no XAudio2 — that SDK predates it. XAudio2 first
shipped in the March 2008 DirectX SDK and last shipped there as 2.7 in June
2010; the DirectX SDK is retired and 2.7 has known bugs. The current answer is
**XAudio 2.9 via the Windows 10/11 SDK**, which the v145 toolset already
requires:

- `#include <xaudio2.h>` and `<x3daudio.h>` — Windows SDK, no vendoring.
- Link `xaudio2.lib`. Since 2.8, X3DAudio and XAPOFX are merged into the
  XAudio2 DLL, so that single import library covers both headers.
- `XAUDIO2_9.DLL` is part of Windows 10 and later. Nothing to redistribute.

Down-level support (Windows 7 SP1 / 8.x) is available through the
`Microsoft.XAudio2.Redist` NuGet package, which adds `XAUDIO2_9REDIST.DLL`
beside the executable. **Decision to confirm:** whether the project's floor is
Windows 10. If it is, take the SDK path and add nothing. If Windows 7 has to
keep working, take the redist — it changes the include path and the import
library and nothing else in the code below.

`dsound.lib` stays in `AdditionalDependencies` for now: `Sequence.cpp` and
`NetAudio.cpp` still need it. It leaves with Phases 5 and 6.

## The work, in order

Each step below is meant to build and run on its own. The first two are
prerequisites; steps 3-6 are the new backend and are hard to split further
because the game has no audio until `sound_InitLibrary` succeeds.

### 1. Sever the DirectSound coupling

`MultiOpt.cpp` passes `nullptr`; `Sequence.cpp` creates its own `IDirectSound`.
Still on QMixer, so FMV and voice chat can be verified against the current
behaviour before anything else moves. Then delete `sound_GetDirectSoundObj`,
`audio_GetDirectSoundObj`, and the `dsound.h` includes from `TrackLib.h` and
`Audio.h`.

### 2. Trim the dead interface

Remove the five never-defined declarations, the five defined-but-uncalled
functions, `audio_PauseAll`/`audio_ResumeAll` and the `sound_PauseAll`/
`sound_ResumeAll` beneath them, `sound_ReadTrackFromFile` /
`sound_LoadTrackFromFile` / `audio_LoadTrackFromFile`, and the
`USE_COMPRESSED_SPEECH` branches. Also `audio_SetTrackPan`, `audio_SetTrackVol`
and `audio_SetTrackFreq`, which have no callers.

This is behaviour-preserving deletion against the *existing* backend, so it is
cheap to verify, and it shrinks what step 3 has to reimplement by a third. Do
it before writing the new backend, not after.

### 3. `XA2Track.cpp` — engine and voice pool

New file replacing `QSTrack.cpp`, implementing the trimmed `TrackLib.h`.

`XAudio2Create` → `CreateMasteringVoice`. The mastering voice runs at the
device's native rate; source voices resample for free, which is why the 11025 /
15000 / 16000 / 17050 / 22050 Hz spread in the assets needs no attention.

The pool is the part that has to be got right, because `AUDIO_SAMPLE::iSample`
is the backend's handle and the code above depends on what it means:

- `sound_StopSample(SDWORD iSample)` and `sound_SetObjectPosition(SDWORD
  iSample, ...)` take it by value, from `Track.cpp` and `Audio.cpp`.
- `SAMPLE_NOT_ALLOCATED` (`-1`) means "no voice", and `Track.cpp` tests for it.
- QMixer reserved three fixed channels — `QS_CHANNEL_QUEUE` (0),
  `QS_CHANNEL_STREAM` (1), `QS_CHANNEL_FX` (2) — and allocated 3D sounds
  dynamically above them.

Keep exactly that shape: `iSample` stays an index into a voice-slot array, slots
0-2 keep their meanings, slots 3..N are the 3D pool. Nothing above
`TrackLib.h` then has to change, and `sound_QueueSamplePlaying` stays a
one-liner against slot 0.

Pool the voices rather than creating one per sound. Normalise every track to
16-bit mono at load (two stereo files in the whole game —
`SFX/Explons/Richet2.wav` and `sequenceAudio/NEXEND.WAV` — downmix or get their
own stereo slot), so all pooled voices share a format, and call
`SetSourceSampleRate` when a slot takes a track at a different rate. That is
what the method exists for. Two constraints from the documentation: the voice
must be created without `XAUDIO2_VOICE_NOPITCH` or `XAUDIO2_VOICE_NOSRC`, and
it must have no buffers queued — and a voice is not clear of its buffers the
instant `FlushSourceBuffers` returns, only once `OnBufferEnd` has fired or
`GetState` reports `BuffersQueued == 0`. So a recycled slot must not be reissued
in the same frame it was flushed.

Sizing: QMixer was configured for 200 channels with 20 prioritised for 3D
(`QS_CHANNELS`, `QS_QSOUND_CHANNELS`). 32 pooled voices is comfortably more
than the game has ever had audible at once.

### 4. Completion callbacks: cross the thread boundary once, explicitly

This is the one genuine behavioural difference between the two APIs, and it is
worth being deliberate about.

QMixer delivered completion through `QMIXPLAYPARAMS::callback`, and
`sound_QSoundFinishedCallback` called straight into `sound_FinishedCallback`,
which touches `g_apTrack[]` and invokes the game's own callback.
`IXAudio2VoiceCallback::OnStreamEnd` is called on the XAudio2 audio thread
instead, and calling game code from there is not safe — `Audio.cpp` walks
`g_psSampleList` unsynchronised from the game thread, and `Audio.cpp`'s
`critSecAudio` guards only shutdown.

`sound_Update()` is the answer, and it is already in place: it is called from
`audio_Update` every frame and is currently an empty function. Have
`OnStreamEnd` do nothing but record the slot under a lock, and have
`sound_Update` drain that list on the game thread and call
`sound_FinishedCallback` from there. Completion then arrives up to one frame
late and always on the thread that owns the data — which is what the existing
`bRemove` flag and `audio_Update`'s deferred delete already assume.

### 5. Static samples: `sound_Play2DSample`, `sound_Play3DSample`

`TRACK::pMem` currently holds `QSTrack.cpp`'s `RIFFDATA`. It becomes the
normalised PCM block plus its `WAVEFORMATEX`. `sound_ReadTrackFromBuffer`
keeps `sound_ReadRiffMemResFile` almost as it stands — it already parses `fmt `
and `data` out of a memory RIFF with `mmio*` — and adds the 8-bit-to-16-bit
conversion. `sound_FreeTrack` frees that block.

`TRACK::iTime` (duration in ms) is computed today in `sound_SaveTrackData` from
QMixer's `wh.dwBufferLength`. It has to keep being computed —
`sound_GetTrackTime` is part of the track API — now as
`bytes / nAvgBytesPerSec * 1000`.

2D play submits the buffer to slot 0 (queued speech) or slot 2 (FX). Note that
QMixer serialised all non-queued 2D FX onto one channel with `QMIX_QUEUEWAVE`,
so `sound_StopSample(2)` flushed every pending FX at once. **Decision to
confirm:** reproduce that, or give 2D FX their own pool slots. Reproducing it
is the safer parity choice and is what this plan assumes; giving them slots is
an improvement that should be made deliberately and listened to.

3D play takes a free slot, or steals one. QMixer used
`FindChannel(QMIX_FINDCHANNEL_DISTANCE)`, which picks the channel whose source
is furthest from the listener. Implement the same policy directly — the backend
already knows every active slot's position from `sound_SetObjectPosition`.

Looping comes from `sound_TrackLooped(psSample->iTrack)`, and maps onto
`XAUDIO2_BUFFER::LoopCount = XAUDIO2_LOOP_INFINITE`.

### 6. Streaming: `sound_PlayStream`

The only place the new backend does materially more work than the old one, and
the only one that needs its own buffer thread or a per-frame pump.

QMixer streamed from a file handle with `QMIX_FILESTREAM`. XAudio2 has no file
concept: submit a chain of buffers and refill on `OnBufferEnd`. Three ring
buffers of a quarter-second each, refilled from the frame pump (`sound_Update`,
same as step 4) rather than from the callback, is enough and keeps the file I/O
off the audio thread.

The material is small — the largest streamed file in the tree is
`sequenceAudio/NP1.WAV` at 737 KB, and the whole `sequenceAudio` directory is
2.5 MB. **Decision to confirm:** whether to stream at all, or simply load these
whole. Loading whole is much less code and costs under 3 MB of memory. Keeping
a real streaming path only earns its keep if the disk-based music that replaces
CD audio turns out to be large. Recommendation: load whole for now, and revisit
when the music assets exist.

Only one stream plays at a time (slot 1), which the callers already assume —
`cdspan_PlayInGameAudio` calls `audio_StopAll()` first.

### 7. Volume

`sound_GetMaxVolume()` returns 32767 today, and `audio_GetMixVol` and
`audio_GetSampleMixVol` in `Audio.cpp` compose the track volume, the requested
volume and the 3D duck factor into that range. Leave both functions untouched:
keep `sound_GetMaxVolume()` at 32767 and divide by `32767.0f` where the backend
calls `SetVolume`. XAudio2 volume is a linear amplitude multiplier with 1.0 as
unity, so if QMixer's scale was also linear the result is identical, and if it
was not the correction is a single curve in one place.

Then retire `Mixer.cpp` per [above](#the-volume-sliders-drive-the-windows-mixer-not-the-game):
`mixer_SetWavVolume` and `sound_SetGlobalVolume` both become mastering-voice
volume; `mixer_SetCDVolume` becomes the music stream's volume; the save/restore
pairs in `WinMain.cpp` disappear along with the system-wide state they were
protecting.

Pan needs almost nothing. `sound_SetSamplePan` is only ever called with the
centre value, from inside `QSTrack.cpp` itself, and the mapping
`iPan*30/AUDIO_PAN_RANGE` is a correct projection of the game's 0-100 range
onto QMixer's documented 0 = left / 15 = centre / 30 = right arc. If it is kept
at all, it becomes a two-channel output matrix on the voice.

### 8. 3D positioning

`sound_SetPlayerPos`, `sound_SetPlayerOrientation` and
`sound_SetObjectPosition` feed QMixer's listener and source vectors, with
`SetDistanceMapping` giving each source a min distance of 300, a max of the
track's `iAudibleRadius`, and a scale of 1.5.

X3DAudio is the direct equivalent: `X3DAudioInitialize`, an `X3DAUDIO_LISTENER`
fed from the same two setters, an `X3DAUDIO_EMITTER` per active 3D slot with
`CurveDistanceScaler` from `iAudibleRadius`, and `X3DAudioCalculate` per slot
in the frame pump feeding `SetOutputMatrix`.

`Outpost/Aud.cpp` inverts world Y into "QSOUND axes" in four places
(`audio_Get3DPlayerPos`, `audio_GetStaticPos`, `audio_GetObjectPos`,
`audio_GetClusterCentre`) and `sound_SetPlayerOrientation` takes degrees about
the vertical and builds a direction vector from them. Both conventions are
QMixer's, and X3DAudio's handedness is not the same. Getting this wrong is
inaudible in testing until something pans the wrong way, so build the emitter
and listener conversion in one function with the axis convention written down,
rather than distributing sign flips.

The plan's fallback — "a simple attenuation model" — remains available: with a
mono asset set and a top-down camera, distance attenuation plus stereo pan
reproduces most of what is audible today, and is perhaps forty lines against
X3DAudio's couple of hundred. **Decision to confirm**, and best made by ear
after step 5 rather than up front.

### 9. Remove CD audio

`CDAudio.cpp` and `CDAudio.h` go. The callers are: `Init.cpp` (open/close, and
`cdAudio_PlayTrack(2)` for front-end music), `Mission.cpp` (front-end music,
stop), `Loop.cpp` and `IntelMap.cpp` (pause/resume around video and the intel
screen), `SeqDisp.cpp` (pause), and four script functions.

The script functions are the constraint. `playCDAudio`, `stopCDAudio`,
`pauseCDAudio` and `resumeCDAudio` are in `ScriptTabs.cpp`'s function table, and
the compiled scripts under `GameData/script` are matched against that table by
position and signature. **Do not remove the table entries.** Reimplement them
over the disk-based music player instead: `scrPlayCDAudio(iTrack)` maps track
numbers onto music files, and stop/pause/resume act on the music stream. Track
2 is the front-end music, which is the only number the C++ passes.

`cdspan_PlayInGameAudio` already tries the CD first and falls back to
`audio\\<name>` on disk. Delete the CD branch and keep the fallback, which is
what makes disk-based music mostly free.

**Decision to confirm:** what the music assets are. Nothing in the repository
supplies them — the CD tracks were on the disc and the streamed music
`playBackgroundAudio` names is not in `GameData/audio`. Until they exist, the
music player is a working path with nothing to play, which is fine as long as
it is a known state and not a surprise.

### CD spanning is a separate decision

`CDSpan.cpp` is not CD audio. It locates game data across three discs: it finds
the CD drive letter, reads the volume label, checks the right disc is inserted
and raises a change-CD box when it is not. Its calls are spread through
`FrontEnd.cpp`, `Mission.cpp`, `LoadSave.cpp`, `HCI.cpp`, `WinMain.cpp` and
`SeqDisp.cpp` — thirteen call sites across six files, and `WinMain.cpp:197`
refuses to start if `cdspan_CheckCDAvailable()` fails.

MigrationPlan.md is right to call this a decision rather than an assumption. If
game data is installed to disk — which is how the repository is laid out, with
everything under `GameData/` — then every check is vestigial and the file goes,
leaving `cdspan_PlayInGameAudio`'s disk fallback behind as the music path. That
is the recommendation, but it is a separate change from the audio backend and
should be its own commit, verifiable on its own. It is not a prerequisite for
anything else in this phase.

## Latent defects this work will pass over

Five things found while reading. None is a reason to delay, and each disappears
or becomes trivially fixable in the rewrite — but each is a place where "the
new code behaves differently" will be the correct answer rather than a
regression.

- **`sound_PlayStream` ignores its `iVol` argument.** It takes `iVol` and never
  uses it, calling `sound_SetSampleVol(psSample, AUDIO_VOL_MAX, FALSE)`
  instead. Worse, `audio_PlayStream` `memset`s the sample to zero, so
  `psSample->iTrack` is 0 and `audio_GetSampleMixVol` scales the stream by
  whatever track 0's volume happens to be. Every caller passes
  `AUDIO_VOL_MAX`, so nobody has noticed.
- **`sound_PlayStream` tests an uninitialised result.** `if (g_uiRet != 0) goto
  streamError;` at line 470 reads `g_uiRet` before this function assigns it —
  it is a file-scope global holding the result of whatever QMixer call ran
  last. Harmless in practice, meaningless as written.
- **`sound_PlayStream` leaks its `MIXWAVE`.** `OpenWave(..., QMIX_FILESTREAM)`
  is never matched by a `FreeWave`, once per stream played.
- **`audio_PlayStream` leaks its `AUDIO_SAMPLE`.** The sample is never added to
  `g_psSampleList`, so `audio_Update`'s `bRemove` sweep never sees it and it is
  never deleted.
- **`Loop.cpp` saves and restores volume through two different controls.** Line
  785 saves `sound_GetGlobalVolume()` (the `waveOut` volume) and line 886
  restores it with `mixer_SetWavVolume()` (the mixer line). Unifying both onto
  the mastering voice in step 7 fixes this by construction.

## Verification

Same asymmetry as Phase 1, and the same order: cross-check first, MSVC as the
authority.

`tools/crosscheck.py` compiles every unit with mingw-w64 against a shadow tree.
mingw-w64 ships `xaudio2.h` and `x3daudio.h`, so the new backend should
cross-check — but that needs confirming as the first thing in step 3, not
assumed, and the container the harness runs in currently has no mingw-w64
installed. If the headers turn out to be absent or too old, the fallback is to
exclude `XA2Track.cpp` from the shadow the way the harness already neutralises
other Windows-only content, and accept that this one file is MSVC-verified
only.

The cross-check cannot link, which matters more here than usual: removing
`QMixer.lib` from `AdditionalDependencies` and adding `xaudio2.lib` is exactly
the kind of change it is blind to. CI builds both Win32 configurations and is
the real check.

Beyond compiling, this phase changes behaviour, so it needs listening to. A
parity pass against the Phase 0 reference run:

- front-end: menu sounds, the volume sliders actually moving the volume;
- in a skirmish: unit acknowledgements (the queued speech channel, slot 0),
  weapons fire and explosions (3D pool), the audio ducking that
  `audio_UpdateQueue` applies while speech plays;
- 3D: pan and attenuation as the camera moves and rotates — the step most
  likely to be subtly wrong, per step 8;
- streams: a research sequence (`sequenceAudio\Res_*.wav` via `IntelMap.cpp`),
  and an FMV with its audio, which exercises the DirectSound severing from
  step 1 rather than the new backend;
- voice chat playback in a multiplayer game, for the same reason.

## Sequencing

Severable from Phase 2, as MigrationPlan.md says, with one qualification: step 1
touches `Sequence.cpp`, which Phase 2 also rewrites. It is fifteen self-contained
lines at the top of two functions, but if both phases are in flight at once, do
step 1 first and rebase Phase 2 onto it rather than the other way round.

Phase 5 removes `NetAudio.cpp` entirely, which deletes the `MultiOpt.cpp` change
from step 1 along with it. Phase 6 replaces the FMV decoder, at which point
`Sequence.cpp`'s own `IDirectSound` should give way to an XAudio2 voice and
`dsound.lib` can leave the link line for good.

## Decisions, as settled

1. **Windows floor** — **Windows 10+**, `xaudio2.lib` from the Windows SDK,
   nothing to redistribute. `Microsoft.XAudio2.Redist` remains a drop-in if
   Windows 7 SP1 has to work: it changes the include path and the import
   library and nothing in the code.
2. **3D audio** — **X3DAudio**. The first pass used a hand-rolled distance
   attenuation and constant-power pan, on the grounds that `x3daudio.h` is not
   in mingw-w64 and so an X3DAudio backend could not be cross-checked at all.
   That was solved rather than accepted: `tools/stubs/x3daudio.h` is a
   checked-in declaration-only shim the harness copies into its shadow tree,
   so the file compiles under the cross-check like everything else. See
   [The X3DAudio shim](#the-x3daudio-shim) for what that does and does not
   prove.
3. **Music assets** — still absent, and deliberately not invented.
   `Music.cpp` resolves track *n* to `music\track<n>.wav`; a missing file
   traces and the game stays quiet rather than failing.
4. **CD spanning** — **`CDSpan.cpp` stays.** Only its CD-audio coupling was
   removed. Deleting it rests on game data being installed to disk, which is a
   separate decision and not a prerequisite for anything here.
5. **2D FX channel semantics** — **kept**, effects still serialise on one slot.
   The queue moved into the backend rather than onto the voice; see below.
6. **Streaming** — **whole**, as recommended.

## What was built

Nine commits, each cross-checked in both configurations. Four things came out
differently from the plan above, and they are the parts worth knowing.

**The cross-check was lying, and had to be fixed first.** `tools/crosscheck.py`
defined `CINTERFACE`, which no longer matches the tree: neither `.vcxproj`
defines it, no header does, and the D3D9 work moved the COM call sites to C++
syntax. Seven units failed under it — `D3DRender`, `DXInput`, `NetAudio`,
`NetProv`, `Screen`, `Sequence` and `TexMan` — against a build that is green.
Without that fix there was no working verification to build Phase 4 against.
MigrationPlan.md's "206 of 206 units clean" is stale in two ways: the count is
200 after the removals below, and the `CINTERFACE` rationale no longer applies.

**Music needed a slot of its own.** The plan treated the single streaming
channel as sufficient. It is not: FMV and research narration go through
`audio_PlayStream` on that channel, so music sharing it would mean a briefing
silencing the music, and `cdAudio_Pause` — whose whole purpose is to hold the
music across a video — tearing it down instead. The CD was a separate device
and the replacement has to be too. So there are four fixed slots, not three,
and music takes no `AUDIO_SAMPLE` because nothing above waits on it.

**The 2D effects queue moved into the backend.** XAudio2 queues buffers on a
voice natively, which looked like a free reproduction of `QMIX_QUEUEWAVE` — but
only within one voice format, and `SetSourceSampleRate` is illegal while
buffers are queued. With the assets spread across 11025, 15000, 16000, 17050
and 22050 Hz, keeping native queueing would have meant resampling everything to
one rate at load, which is what QMixer did internally and would have cost about
48 MB resident and a resampler. A queue in the backend costs neither, at the
price of a frame's gap between queued sounds.

**3D is X3DAudio, and it needed a shim to be checkable.** The distance model
is the one QMixer was given — flat inside 300 units, inverse distance with a
1.5 rolloff beyond it, silent at the track's audible radius — but stated as an
`X3DAUDIO_DISTANCE_CURVE` rather than computed by hand, because X3DAudio's own
default curve never reaches zero and the audible radius is authored per track.
X3DAudio returns the per-speaker coefficients with the attenuation already in
them, so the voice's own volume carries the mix volume and nothing else.

The axis mapping is the one place to be careful. The game's audio space is x
east, y north, z height; X3DAudio is left-handed with x right, y up, z forward,
so the two differ by swapping y and z. The check that it comes out right: at an
angle of zero the listener faces north, which is X3DAudio's +z; its right is
then +x, which is east — and facing north, east is on your right.

**Two hazards the plan did not name.** A pool slot recycled between unrelated
sounds means a handle held past the end of its sound can stop whatever took the
slot over, so `iSample` carries a generation for pool slots. And XAudio2 keeps
reading a submitted buffer until it has consumed it, which a flush does not
make immediate — so `sound_FreeTrack` waits for the voice to let go before
giving the memory back.

Normalising every track to 16 bit mono at load is what makes one voice pool
work: `SetSourceSampleRate` can retune a voice, but nothing can restate its
sample size or channel count. It doubles the resident audio to roughly 24 MB.

### The defects

Three of the five recorded above disappeared with the function they were in.
`audio_PlayStream` not putting its sample on the list is fixed. The `Loop.cpp`
volume mismatch is fixed by construction — and the same mismatch turned out to
be in both volume sliders, which read `sound_GetGlobalVolume` (the `waveOut`
volume) and wrote `mixer_SetWavVolume` (the mixer line).

### The X3DAudio shim

mingw-w64 ships `xaudio2.h`, `xaudio2fx.h`, `xapo.h` and `xapofx.h`, but no
`x3daudio.h`. Rather than leave `XA2Track.cpp` unchecked, `tools/stubs/`
now holds a declaration-only `x3daudio.h` that `crosscheck.py` copies into its
shadow tree — the same idea as the empty Concurrency Runtime stubs already
there, but with real declarations because the code actually uses the types.

Be clear about what this buys. It checks the backend's *use* of X3DAudio
against a transcription of the API, not against the SDK: it catches arity,
spelling and type errors in our code, and cannot catch an error in the
transcription. The declarations were taken from two independent sources that
agree — FAudio's `F3DAudio.h`, a reimplementation of the same ABI, for the
structure layouts, and DirectXTK's `Audio.h` and `SoundCommon.cpp`, Microsoft's
own code against the Windows 10 SDK, for the signatures. That is also what
settled two facts worth recording: `X3DAudioInitialize` returns `HRESULT` in
these headers where XAudio 2.7's returned `void`, and `xaudio2.lib` is the only
import library needed, because X3DAudio has been merged into XAudio2 since 2.8.

Writing it turned up a divergence of exactly the kind Phase 1 catalogued, and
in the less usual direction — mingw stricter than the SDK by accident.
`IXAudio2MasteringVoice::GetChannelMask` returns `HRESULT` in the Windows SDK
and `void` in mingw-w64, so `FAILED(GetChannelMask(...))` compiles on MSVC and
fails the cross-check. The call goes unchecked and the speaker mask is tested
for zero instead, which is the same guard by another route.

### Still unverified

Everything above is cross-checked and none of it is *run*. The cross-check is a
different compiler and cannot link, so the swap of `QMixer.lib` for
`xaudio2.lib` is exactly the kind of change it is blind to; CI is the first
real check. Beyond that, this phase changes behaviour and needs the listening
pass described under [Verification](#verification) — most of all the 3D panning,
which is the part most likely to be subtly wrong, and which the shim above
cannot speak to at all.

---

*Measured on the tree at the time of writing: line counts from `wc -l`, call
sites from `rg` across `NeuronCore/` and `Outpost/`, and WAV formats by parsing
the `fmt ` chunk of all 551 files under `GameData/`. XAudio2 behaviour cited
from the current Microsoft Learn documentation for `IXAudio2::CreateSourceVoice`,
`IXAudio2SourceVoice::SetSourceSampleRate`, `FlushSourceBuffers` and the
XAudio2 versions and redistributable guides.*
