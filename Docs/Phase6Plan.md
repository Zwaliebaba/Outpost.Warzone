# Phase 6 — Removing Mplayer.lib and WINSTR.LIB

Working plan for the phase described in
[MigrationPlan.md](MigrationPlan.md#phase-6--removing-mplayerlib-and-winstrlib).
As with Phases 4 and 5, every figure here was measured against the tree and the
shipped assets rather than estimated.

The phase is two unrelated pieces sharing a heading. The first is an afternoon.
The second is the last remaining *rewrite* in the migration, and it is gated on
an asset decision the code cannot make for you — see
[The asset problem](#the-asset-problem-165-of-184-movies-are-not-in-this-repo),
which is the part of this document worth reading first.

---

## Part 1 — Mplayer.lib

Nothing here contradicts MigrationPlan.md. `Mplayer.lib` is the Mpath
Interactive DirectPlay Extras library for the Mplayer.com service, dead since
2001.

| File | Lines | Fate |
|---|---:|---|
| `Outpost/MPDPXtra.cpp` + `.h` | 1401 | delete |
| `Outpost/MPlayer.cpp` | 83 | delete |
| `Mplayer.lib` in `AdditionalDependencies` | — | remove (both configs) |
| Mplayer hooks in `NetLobby.cpp` | — | remove with the file |

Phase5Plan.md already lists all three files as deletions, and `NetLobby.cpp` is
scheduled for deletion there too. **Do this inside Phase 5, not here.** Keeping
it as a separate Phase 6 item only creates a merge conflict with the phase that
deletes the same files. What is left for Phase 6 is the two `<AdditionalDependencies>`
lines in `Outpost/Outpost.vcxproj` (lines 342 and 369), which also carry
`WINSTR.LIB` and are edited once at the end of Part 2.

---

## Part 2 — WINSTR.LIB

### The audio question, answered

MigrationPlan.md says the `.rpl` assets "each carry **audio as well as video**".
That is half right, and the half that is wrong is what makes `GameData/sequenceAudio/`
look like a contradiction. It is not a contradiction — **the two mechanisms are
mutually exclusive, per movie, by design.**

Reading the ARMovie text headers of all 19 shipped `.rpl` files:

| Movie | Size | Frames @25fps | Embedded audio | External `.wav` |
|---|---:|---:|---|---|
| `BrfCom.rpl` | 320x240 | 59 | 22050 Hz, 1 ch, 4-bit | — |
| `BrfCom4s.rpl` | 320x240 | 99 | 22050 Hz, 1 ch, 4-bit | — |
| `IncomInt.rpl` | 320x240 | 81 | 22050 Hz, 1 ch, 4-bit | — |
| `IncomTns.rpl` | 320x240 | 79 | 22050 Hz, 1 ch, 4-bit | — |
| `Plyr4sec.rpl` | 320x240 | 98 | 22050 Hz, 1 ch, 4-bit | — |
| `PrjUpDat.rpl` | 320x240 | 75 | 22050 Hz, 1 ch, 4-bit | — |
| `Victory.rpl` | 320x240 | 48 | 22050 Hz, 1 ch, 4-bit | — |
| `end.rpl` | 320x240 | 34 | 22050 Hz, 1 ch, 4-bit | — |
| `nexend.rpl` | 320x240 | 24 | **none** | `NEXEND.WAV` |
| `npstart.rpl` | 320x240 | 200 | **none** | `NP1.WAV` |
| `npend.rpl` | 320x240 | 24 | **none** | `NP2.WAV` |
| `player.rpl` | 320x240 | 49 | **none** | `P-id.wav` |
| `res_com.rpl` | 192x168 | 177 | **none** | `Res_com.wav` |
| `res_droid.rpl` | 192x168 | 177 | **none** | `Res_Droid.wav` |
| `res_pow.rpl` | 192x168 | 177 | **none** | `Res_Pow.wav` |
| `res_struttech.rpl` | 192x168 | 177 | **none** | `Res_StruTech.wav` |
| `res_systech.rpl` | 192x168 | 177 | **none** | `Res_SysTech.wav` |
| `res_weapons.rpl` | 192x168 | 156 | **none** | `Res_Weapons.wav` |
| `noVideo.rpl` | 320x240 | 9 | **none** | — (fallback placeholder) |

Every movie is `ESCAPE 2.0`, video format `130`, 16 bpp, 25 fps. Eight carry a
sound track; eleven are silent. **No movie has both.** The rule is content-shaped:
the talking-head comms overlays carry their dialogue inside the file, and the
short looping stingers are silent because their narration is longer than the
picture.

The game data says the same thing. `GameData/messages/*.txt` pairs a movie with
an *optional* wav in the same record:

```
MB2_4_MSG,0,3,2,player.rpl,1,1,TRANS_MSG1,p-id.wav,0000,...
MB2_5_MSG,0,3,2,BrfCom.rpl,1,1,TRANS_MSG1,0,0000,...
```

`player.rpl` (silent) names `p-id.wav`; `BrfCom.rpl` (embedded audio) names `0`.
Those fields become `SEQLIST::pSeq` / `SEQLIST::pAudio` in `Outpost/SeqDisp.cpp`,
and `seq_StartFullScreenVideo` prefixes the audio name with `sequenceAudio\`
([SeqDisp.cpp:418](../Outpost/SeqDisp.cpp#L418)). Meanwhile `Sequence.cpp` tests
`Movie_GetSoundChannels() && Movie_GetSoundPrecision() && Movie_GetSoundRate()`
([Sequence.cpp:320](../NeuronCore/Sequence.cpp#L320)) and only builds a
DirectSound buffer when the file has a track. Neither path knows about the other,
and neither ever fires for the same movie.

`sequenceAudio/` also holds the **subtitle text**, which is why it has 42 `.txt`
and `.txa` files under `Cam1/`, `Cam2/`, `Cam3/` and none of them are audio:
`seq_AddTextFromFile` reads them from the same directory
([SeqDisp.cpp:777](../Outpost/SeqDisp.cpp#L777)).

#### The external audio is the clock, and that constrains the conversion

This is the part that matters for the format decision. Where a wav exists, its
duration drives the sequence, not the video's:

| Movie | Video | External wav | Ratio |
|---|---:|---:|---|
| `res_droid.rpl` | 7.08 s | `Res_Droid.wav` 7.08 s | 1.00x |
| `res_weapons.rpl` | 6.24 s | `Res_Weapons.wav` 6.24 s | 1.00x |
| `nexend.rpl` | 0.96 s | `NEXEND.WAV` 1.00 s | 1.04x |
| `player.rpl` | 1.96 s | `P-id.wav` 3.96 s | **2.02x** |
| `npstart.rpl` | 8.00 s | `NP1.WAV` 17.12 s | **2.14x** |
| `npend.rpl` | 0.96 s | `NP2.WAV` 13.46 s | **14.0x** |

`seq_UpdateFullScreenVideo` handles the mismatch: when the video runs out while
`bAudioPlaying` is still set, it either re-opens the movie (`bSeqLoop`) or holds
the last frame (`bHoldSeqForAudio`), and the sequence ends on `SeqEndCallBack`
from the audio ([SeqDisp.cpp:608-629](../Outpost/SeqDisp.cpp#L608-L629)).

**So the external wavs must not be muxed into the converted movies.** Muxing
`NP2.WAV` into `npend.rpl` would turn a 1-second loop played fourteen times into
a 13-second video, which is a different thing. The eleven silent movies convert
to video-only files and keep using `audio_PlayStream`, which is already XAudio2
under Phase 4 and needs no work at all.

The eight movies with embedded audio are the only ones where "the replacement
has to carry the audio and keep it in sync" applies.

### The asset problem: 165 of 184 movies are not in this repo

`GameData/sequences/` holds 18 movies plus `noVideo.rpl`, 15.7 MB, 1920 frames,
**77 seconds in total**. The game references **184 distinct `.rpl` names** across
`Outpost/*.cpp` and `GameData/messages/*.txt`. The other 165 — every `cam1\`,
`cam2\` and `cam3\` briefing, plus `eidos-logo`, `pumpkin`, `titles`,
`devastation`, `factory`, `outro`, `inflight`, `transport` and the whole
`sub1_*` set — live on the CDs, under `<drive>\warzone\sequences\`, which is
what `seq_SetVideoPath` probes via `cdspan_GetCDLetter`
([SeqDisp.cpp:314-340](../Outpost/SeqDisp.cpp#L314-L340)).

Three consequences, and they are the reason this phase needs an owner decision
before any code is written:

1. **The 19 files in the repo are not a representative sample.** They are the
   hard-disk residents: short loops and research stingers. The CD set is the
   actual campaign content and is an order of magnitude larger. A converter
   validated only against `GameData/sequences/` proves very little.
2. **`GameData/` is marked "may you edit it? **No**" in
   [AGENTS.md §2](../AGENTS.md).** Converting the shipped movies in place, or
   adding converted ones beside them, needs that rule relaxed for this phase.
3. **This is where the CD question from Phase 4 lands.** `CDSpan.cpp` survived
   Phase 4 because removing it "rests on game data being installed to disk".
   FMV is the last thing still read from the CD. If the conversion is a one-time
   offline step producing a disk-installed set, `cdspan_*` and the
   `addCDChangeInterface` path in `seq_StartNextFullScreenVideo` go with it.
   If not, the converter has to run at install time against the user's CDs.

**Decided** (see [Decisions](#decisions)): conversion is an offline, one-time
step whose output is installed to disk. It replaces the `.rpl` files in
`GameData/sequences/` in place, it retires `CDSpan.cpp` and the CD-swap UI, and
the source `.rpl` files do not stay in the repository — the converter and a hash
manifest do.

### What `Sequence.cpp` actually uses

`STREAMER.H` is 436 lines declaring 64 exports across the `Movie_*`, `Alpha_*`
and `Streamer_*` families. `NeuronCore/Sequence.cpp` (840 lines) uses **21** of
them, and the `Alpha_*` family not at all:

| Group | Used |
|---|---|
| Lifecycle | `Streamer_InitMovie`, `InitVideo`, `InitSound`, `InitStreaming`, `ShutDownMovie`, `ShutDownVideo`, `ShutDownSound` |
| Per-frame | `Streamer_Stream` |
| Configuration | `Streamer_SetVideoPitch`, `Streamer_SetPixelFormat`, `Streamer_SetSoundBuffer`, `Streamer_SetSoundDecodeMode`, `Streamer_GetSoundDecodeMode` |
| Queries | `Movie_GetXSize`, `GetYSize`, `GetCurrentFrame`, `GetTotalFrames`, `GetFrameRate`, `GetSoundChannels`, `GetSoundPrecision`, `GetSoundRate` |

The public surface above it is `NeuronCore/Sequence.h`, 95 lines, and it is
already the clean seam this phase needs — exactly the shape `TrackLib.h` had for
Phase 4. Eleven functions, two playback paths:

- **Fullscreen** — `seq_SetSequence` / `seq_RenderOneFrame`. Decodes into a
  640x480 16-bit staging buffer, then converts and blits row by row into the
  locked D3D9 back buffer, darkening the subtitle band as it goes. One caller:
  `SeqDisp.cpp`.
- **Windowed** — `seq_SetSequenceForBuffer` / `seq_RenderOneFrameToBuffer`.
  Decodes into a caller buffer that `pie_DownLoadBufferToScreen` hands to
  `pie_D3DSetupRenderForFlip` for a deferred CPU blit at flip time. One caller:
  the intelligence-screen research window in `IntelMap.cpp`, which is where the
  192x168 `res_*.rpl` movies play.

`SeqDisp.cpp` (940 lines) sits above `Sequence.h` and does not touch WINSTR at
all. **It should not need to change**, which is what keeps the blast radius of
this phase at two files.

### One more thing that falls out

`Sequence.cpp` and `Sequence.h` are the **only remaining DirectSound users in
the tree** — Phase 5 already removed `NetAudio.cpp`, and Phase 4 moved the mixer
to XAudio2. Finishing Part 2 lets `dsound.lib` come off both link lines and
`#include <dsound.h>` out of the tree entirely.

---

## The target

**Media Foundation, H.264 video and AAC audio in MP4, decoded through
`IMFSourceReader`.** The reasoning:

- **[AGENTS.md §5 R14](../AGENTS.md) forbids new third-party dependencies**, and
  MigrationPlan.md's stated endpoint for Phases 4-6 is "the only remaining
  non-system dependencies are the DirectX libraries themselves". Bundling
  libvpx, libtheora or ffmpeg contradicts both. Media Foundation is a system
  component: `mfplat.lib`, `mfreadwrite.lib`, `mfuuid.lib`, all in the Windows
  SDK, no redistributable.
- The source is 320x240 and 192x168 at 25 fps. Any modern codec is
  overqualified; the choice is about what ships with Windows, not about quality.
- Size goes *down*, substantially. The current set averages ~1.7 Mbit/s for
  320x240 — 15.7 MB for 77 seconds. H.264 at a visually-lossless CRF on this
  material lands in the low hundreds of kbit/s.

**The one caveat to record:** the Media Foundation H.264 and AAC decoders are
absent on Windows "N" editions until the Media Feature Pack is installed. That
is a documented install requirement, not a blocker, but it should be a
`Neuron::Fatal` with a legible message rather than a silent black screen.

**Rejected:** frames as DDS textures, for the reasons already in
MigrationPlan.md — no `Texture2DArray` in D3D9, no temporal compression, no
audio track. Nothing found here changes that.

---

## Stages

Each stage is separately verifiable. B1 and B2 are offline tooling and touch no
game code; B3 onward is the runtime.

### B1 — Read the format, prove it round-trips

Before choosing an encoder, establish a **known-good reference decode** of the
`.rpl` files. Two routes, and it is worth having both:

1. **ffmpeg.** Its `rpl` demuxer handles ARMovie and its `escape130` decoder
   handles video format 130; sound format 101 at 4 bits maps to
   `adpcm_ima_ea_sead`. **This is unverified here — ffmpeg is not installed on
   this machine and it is the first thing to check:**
   ```
   ffmpeg -i GameData/sequences/BrfCom.rpl -f null -
   ```
   If it decodes 59 video frames and 2.36 s of audio, the conversion is a
   one-line batch job.
2. **A throwaway extractor linked against `WINSTR.LIB`.** A ~150-line console
   EXE that calls the same 21 entry points `Sequence.cpp` does and dumps raw
   RGB565 frames and 16-bit PCM. This is guaranteed bit-exact against what ships
   today, because it *is* what ships today.

Build route 2 regardless of whether route 1 works: it is the oracle that decides
whether ffmpeg's decoder is faithful. Compare frame-by-frame; Escape 130 is an
obscure codec and "it decoded without erroring" is not the same as "it decoded
correctly".

**Verifies:** a per-movie PSNR/SSIM report against the extractor's output, and a
sample-count match on the audio.

### B2 — The converter

`tools/ConvertSequences.py` (the `tools/` directory is already the home for
repository tooling), driving ffmpeg over a directory of `.rpl`:

- Video-only movies → MP4, H.264, no audio track. Frame rate stays 25 fps and
  the pixel dimensions stay exactly 320x240 / 192x168 — **do not upscale**;
  `Movie_GetXSize` feeds the on-screen placement arithmetic and the
  `DFLAG_DOUBLED` decision at [Sequence.cpp:299](../NeuronCore/Sequence.cpp#L299).
- Movies with embedded audio → MP4, H.264 + AAC, audio muxed.
- Emit a manifest: input name, SHA-256, output name, frame count, duration,
  audio present. This is what makes a partial or wrong-CD conversion detectable
  instead of silently producing a short movie.

Naming: keep the stem and change the extension (`BrfCom.rpl` → `BrfCom.mp4`),
so `GameData/messages/*.txt` and the hardcoded call sites keep working with one
extension substitution at the point where `SeqDisp.cpp` builds `aVideoName`.
Note the subtitle lookup in `seq_AddSeqToList` derives the `.txt` name by
truncating four characters ([SeqDisp.cpp:848](../Outpost/SeqDisp.cpp#L848)) —
`.rpl` and `.mp4` are both four characters, so that arithmetic survives, but it
is worth a comment saying why.

Output goes **in place**: `GameData/sequences/*.mp4` replacing `*.rpl`, and
`GameData/noVideo.rpl` → `noVideo.mp4` beside it. The `.rpl` files come out of
the repository in the same commit as the `.mp4` files going in, so the tree is
never carrying both. That commit is the one place this phase edits `GameData/`,
and it is covered by the exception recorded under [Decisions](#decisions).

**Verifies:** the manifest, plus every one of the 184 referenced names resolving
to an output file (or being explicitly listed as CD-only and absent).

### B3 — The new backend behind `Sequence.h`

Replace `NeuronCore/Sequence.cpp` with a Media Foundation implementation,
`NeuronCore/MovieStream.cpp` plus a `MovieStream` class per
[AGENTS.md §1](../AGENTS.md), keeping `Sequence.h`'s eleven functions as the
C-style shim `SeqDisp.cpp` and `IntelMap.cpp` call. This mirrors Phase 4 exactly
(`QSTrack.cpp` → `XA2Track.cpp` behind an unchanged `TrackLib.h`).

Design notes worth fixing now rather than discovering later:

- **Audio drives the clock.** Read the whole audio track into one PCM buffer at
  open and submit it to a single XAudio2 source voice. These tracks are at most
  ~60 s of 22 kHz mono — under 3 MB — so there is no case for a streaming
  thread, which is the same judgement `XA2Track.cpp` already made for
  `sequenceAudio`. Present video frames against
  `IXAudio2SourceVoice::GetState().SamplesPlayed`. This is *better* sync than
  today's open-loop `GetTickCount` timing, and it deletes the entire
  `SSDM_FIRSTBUFFER` / `SSDM_SECONDBUFFER` double-buffer dance and its
  `SoundCallBackFunc`.
- **Silent movies keep the existing clock.** No audio stream means `GetTickCount`
  pacing exactly as now, and `SeqDisp.cpp`'s `bHoldSeqForAudio` / `bSeqLoop`
  logic continues to end the sequence on the external wav's callback. Do not
  touch it.
- **Request `MFVideoFormat_RGB32` output** and let Media Foundation insert the
  Video Processor MFT. That removes the RGB565 intermediate, the
  `SEQ_RED_POS`-family constants, `SEQ_LOW_BIT_MASK` and every `screen565To32`
  call in the module.
- **A missing movie falls back to `noVideo`, not to an error.** The
  `DUMMY_VIDEO` branch already does this
  ([SeqDisp.cpp:444](../Outpost/SeqDisp.cpp#L444)) and it stays. What matters is
  *why* it stays: it keeps the rest of the sequence machinery running, so the
  external wav still plays, subtitles still draw, and `SeqEndCallBack` and
  `CALL_VIDEO_QUIT` still fire. A missing briefing must not be able to wedge a
  campaign transition. Layer a `DEBUG_WARNING` on the fallback so a partial
  conversion is loud in Debug and survivable in Release.
- **A missing *decoder* is a different failure and should be loud.** If
  `MFStartup` succeeds but no H.264 decoder resolves — the Windows N case —
  every movie fails identically and the `noVideo` fallback would paper over it.
  `Neuron::Fatal` with a legible message naming the Media Feature Pack, once, at
  init.
- **`seq_GetFrameTimeInClicks` must keep returning ~40.** `SeqDisp.cpp` uses it
  as `RPL_FRAME_TIME` for the text-overlay frame arithmetic
  ([SeqDisp.cpp:606](../Outpost/SeqDisp.cpp#L606)), and every subtitle `.txt`
  file has start/end frame numbers authored against 25 fps. Frame numbers are a
  public part of this interface, not an implementation detail.

**Verifies:** `Debug\Outpost.exe -window` — the intro sequence, then a research
completion (windowed `res_*` path) and a mission briefing (fullscreen path with
embedded audio). Subtitles must appear on the right frames.

### B4 — Dynamic texture and quad

MigrationPlan.md Phase 2 deliberately left the FMV blit as a CPU write into the
locked back buffer, on the grounds that "a dynamic texture and a quad would be
the right shape once Phase 6 replaces the decoder; doing it now would mean
rewriting the same function twice". This is that moment.

- One `IDirect3DTexture9`, `D3DUSAGE_DYNAMIC`, `D3DFMT_X8R8G8B8`,
  `D3DPOOL_DEFAULT`, sized to the next power of two above the movie if the
  device needs it. `LockRect` with `D3DLOCK_DISCARD` per frame, copy rows, draw
  a quad.
- The subtitle band darkening becomes a translucent quad instead of the
  per-pixel `pixel &= SEQ_LOW_BIT_MASK; pixel >>= 1;` loop over three row
  ranges. That is roughly 90 lines of `seq_RenderOneFrame` gone.
- `pie_D3DSetupRenderForFlip` / `pie_D3DRenderForFlip` and their deferred
  16-bit blit in `PieBlitFunc.cpp` lose their only FMV caller. Check whether
  anything else uses them before deleting.
- `seq_SetupVideoBuffers` in `SeqDisp.cpp` allocates 640x480x2 bytes plus a
  32 KB 555 palette table built by `pal_GetNearestColour`
  ([SeqDisp.cpp:281-312](../Outpost/SeqDisp.cpp#L281-L312)). Both go. This is
  the one place `SeqDisp.cpp` does change.

**Verifies:** same run as B3, plus a windowed/fullscreen toggle to confirm the
quad scales where the old code hard-coded `borderX` / `borderY`.

### B5 — Retire the CD path

Movies were the last thing read from the CD, so with B2's output installed to
disk the CD-presence apparatus becomes dead weight. This is the item Phase 4
deferred.

**It is larger than MigrationPlan.md implies, and the file is not where that
document says.** Two corrections from measuring the tree:

- `CDSpan.cpp` is in **`Outpost/`, not `NeuronCore/`**, and it is **535 lines**
  plus a 38-line header — not the 631 quoted under Phase 4.
- It has **19 `cdspan_*` call sites across seven files**, not the handful the
  "related but distinct" description suggests. `seq_StartFullScreenVideo` is
  two of them.

The call sites divide into five groups, and only the first is really this
phase's business:

| Group | Sites | Work |
|---|---:|---|
| Video path — `seq_SetVideoPath`, `seq_StartNextFullScreenVideo` | 2 | delete with the rest of the phase |
| Content gates — new game, load game, mission continue, startup | 5 | each collapses to its success branch |
| Change-CD widget plumbing — `cdspan_ProcessCDChange`, `RemoveChangeCDBox`, `showChangeCDBox`, `addCDChangeInterface` | 10 | woven into five widget message loops |
| `cdspan_PlayInGameAudio` | 1 | **delete, with its script function** |
| `cdspan_DontTest` | 1 | `DONTTEST` compile switch, goes with the file |

Notes on the parts that are not mechanical:

- **`cdspan_PlayInGameAudio` is dead and goes.** An earlier draft of this plan
  said relocate rather than delete, on the grounds that deleting it would take a
  script function with it. Measuring the scripts shows that concern was
  unfounded: its only caller is `scrPlayBackgroundAudio`
  ([ScriptFuncs.cpp:2670](../Outpost/ScriptFuncs.cpp#L2670)), registered as
  `playBackgroundAudio` in `ScriptTabs.cpp:154`, and **no `.slo` in
  `GameData/script/` calls it** — 0 references across all 67 script sources.
  Delete the chain entire: the `ScriptTabs.cpp` entry, `scrPlayBackgroundAudio`,
  its declaration in `ScriptFuncs.h:226`, and `cdspan_PlayInGameAudio` itself.
- **The widget plumbing is the bulk.** `cdspan_ProcessCDChange(id)` is called
  first in five separate widget-message loops — `FrontEnd.cpp` (x2),
  `HCI.cpp` (x2), `LoadSave.cpp`, `Mission.cpp` — each guarding the real handler
  behind `if (!cdspan_ProcessCDChange(id))`. Removing it means unwrapping those
  conditions, which is where a mistake would silently swallow widget input.
- **The gates collapse, but check each callback pair.** `showChangeCDBox` takes
  an OK and a cancel continuation (`frontEndNewGame`/`startSinglePlayerMenu`,
  `loadSaveCDOK`/`loadSaveCDCancel`, `missionContineButtonPressed`/
  `missionCDCancelPressed`, `seqDispCDOK`/`seqDispCDCancel`). The OK
  continuation is what runs unconditionally afterwards; the cancel one usually
  becomes dead, but `missionCDCancelPressed` and `startSinglePlayerMenu` are
  reachable from elsewhere and must not be deleted blind.
- In `SeqDisp.cpp` specifically: `seq_StartNextFullScreenVideo` collapses to its
  `seqDispCDOK` body, `seq_SetVideoPath` loses `bCDPath`/`aCDPath`, and the
  fopen-probe-then-fall-back-to-CD dance goes from both
  [SeqDisp.cpp:153-180](../Outpost/SeqDisp.cpp#L153-L180) and
  [388-407](../Outpost/SeqDisp.cpp#L388-L407). Watch `g_bResumeInGame` — it
  exists only to undo what the CD dialog did to the reticule and design pause
  state, so it goes too, but confirm nothing else sets it.

#### The `*CDAudio` script functions are not CD audio, and one of them is live

Worth stating explicitly, because "remove the CD audio" reads as though it
covers these and it must not. Phase 4 already deleted the real CD audio —
`CDAudio.cpp` and its MCI usage are gone. What still carries CD names is four
script functions that Phase 4 rewired to `Music.cpp`, which serves files from
disk:

| Script name | Calls | Used by scripts |
|---|---|---|
| `playCDAudio` | `music_PlayTrack` | **yes — 5 call sites** |
| `stopCDAudio` | `music_Stop` | no |
| `pauseCDAudio` | `music_Pause` | no |
| `resumeCDAudio` | `music_Resume` | no |
| `playBackgroundAudio` | `cdspan_PlayInGameAudio` | no — deleted above |

`playCDAudio(1)` is called by `fastplay.slo`, the three `camNdaynight.slo`
campaign scripts and `tutorial3.slo`. **Deleting it would break level load for
all five**, because `.slo` files are text compiled by the game at load time and
an unknown function name is a compile error, not a warning. It stays.

The three unused ones can go with their `ScriptTabs.cpp` entries, on the same
evidence that retires `playBackgroundAudio`. Rename the C++ side to
`scrPlayMusicTrack` so the code stops lying; **keep the script-visible name
`playCDAudio`** and comment why. Renaming the script-visible name would mean
editing five campaign `.slo` files for cosmetics, which is not worth the risk
here — it belongs with any future scripting pass.

**This stage is severable.** Nothing in B1-B4 or B6 depends on it: WINSTR.LIB
can be removed with `CDSpan.cpp` still in the tree, merely never triggering. If
B5 runs long, land it after B6 rather than holding the phase open.

**Verifies:** the CAM_1A boot, a new game from the front end, a save-game load
and a mission-continue — the four gates — confirming no CD dialog can appear and
no path resolves to a drive letter. This needs a real run, not a build.

### B6 — Removal

Once B3, B4 and B5 run:

| Removed | Where |
|---|---|
| `WINSTR.LIB` | `Outpost.vcxproj` lines 342, 369 |
| `Mplayer.lib` | same two lines (if Phase 5 has not already) |
| `dsound.lib` | same two lines — last user was `Sequence.cpp` |
| `NeuronCore/STREAMER.H` | 436 lines |
| `NeuronCore/Sequence.cpp` | 840 lines, replaced |
| `Outpost/CDSpan.cpp` + `.h` | 573 lines, per B5 |
| `NeuronCore/WINSTR.LIB`, `NeuronCore/Mplayer.lib` | the checked-in libraries themselves — last 32-bit binaries in the tree |
| `scrPlayBackgroundAudio` + `ScriptTabs.cpp:154` | dead script function, per B5 |
| `scrStopCDAudio`, `scrPauseCDAudio`, `scrResumeCDAudio` + entries | registered, called by no script |
| `GameData/winstr.dll` | 80 KB |
| `GameData/Dec130.dll` | 123 KB |
| `GameData/*.rpl` | 15.7 MB, replaced by `.mp4` in B2's commit |
| `GameData/edec.dll`, `winsdec.dll` | **verify first** — 117 KB and 90 KB, no reference found anywhere in the tree, but they sit beside the other two and are probably the same family |

Add `mfplat.lib`, `mfreadwrite.lib` and `mfuuid.lib` to the same two lines. The
net change to `AdditionalDependencies` is three system libraries in, three
entries out, and the last non-system dependency in the tree is gone.

**Do not remove `playCDAudio` / `music_PlayTrack`** — see
[the `*CDAudio` note under B5](#the-cdaudio-script-functions-are-not-cd-audio-and-one-of-them-is-live).
It is disk music with a misleading name and five live script callers.

---

## Modernisation

**Decided: this phase modernises everything it rewrites, rather than deferring
it to Phase 7.** That needs stating carefully, because two standing rules point
the other way and this is a deliberate, scoped exception to both.

[AGENTS.md §4](../AGENTS.md) says "do not modernise a file as a side effect of
editing three lines of it", and MigrationPlan.md schedules modernisation for
Phase 7, "once the churn is over". Neither is being overturned. The line drawn
here is:

- **Rewritten wholesale → fully modern.** `Sequence.cpp` is replaced outright
  and `SeqDisp.cpp` loses its CD path, its buffer allocation and its blit loops.
  Neither is "three lines". Writing 1998-shaped code into a file being rebuilt,
  purely to rewrite it again in Phase 7, is the churn Phase 7 exists to avoid.
- **Merely touched → minimal edits.** `IntelMap.cpp`, `HCI.cpp`, `FrontEnd.cpp`,
  `LoadSave.cpp`, `Mission.cpp`, `WinMain.cpp` and `ScriptFuncs.cpp` lose CD
  calls and nothing else. §4 applies to them unchanged.

### What that means concretely

New code follows [AGENTS.md §1](../AGENTS.md) as written — `PascalCase` types
and functions, `m_` members, `_camelCase` parameters — and §5 R10's plain
`new`/`delete`/RAII and standard containers.

| Legacy shape | Becomes | Where |
|---|---|---|
| `LPMOVIEHANDLE` / `LPVIDEOHANDLE` / `LPSOUNDHANDLE` globals + `seq_ShutDown` | one `MovieStream` class owning its handles, destructor closes | B3 |
| Manual COM `->Release()` chains | `Microsoft::WRL::ComPtr` from `<wrl/client.h>` — Windows SDK, not a new dependency (§5 R14 satisfied) | B3, B4 |
| `LPBYTE soundbuffer1/2`, `temp`, `LastUpdated`, `bPlayerOn` file-scope globals | members, or gone with the double-buffer scheme | B3 |
| `SEQLIST::pSeq` / `pAudio` — raw `char*` **aliasing caller-owned strings** | `std::string` by value | B4 |
| `SEQTEXT aText[32]` × `MAX_SEQ_LIST`, each `char[MAX_STR_LENGTH]` | `std::vector<Subtitle>` | B4 |
| `strtok` over the shared `DisplayBuffer` in `seq_AddTextFromFile` | `std::string_view` line splitting, no shared mutable buffer | B4 |
| `sscanf("%d %d %d %d")` + `strchr`/`strrchr` quote hunting | `std::from_chars`, explicit parse with a reported failure | B4 |
| `strcpy`/`strcat` path building into `char aVideoName[MAX_STR_LENGTH]` globals | `std::filesystem::path` | B4, B5 |
| `char* pVideoBuffer = new char[...]` + `seq_ReleaseVideoBuffers` | gone entirely — the dynamic texture replaces it | B4 |

The `SEQLIST::pSeq` row is worth calling out as more than tidiness. `seq_AddSeqToList`
stores the **caller's pointer** ([SeqDisp.cpp:836](../Outpost/SeqDisp.cpp#L836)).
String literals are fine; `psViewReplay->pSeqList[i].sequenceName` from
`IntelMap.cpp:625` is a pointer into message data whose lifetime nothing here
guarantees. Taking a `std::string` by value closes a real lifetime hole, not a
stylistic one.

### Two things deliberately not done

- **The `seq_*` free functions keep their names.** There are **61 call sites
  across eight files**, all in files otherwise untouched by this phase. The
  modern classes go behind them; the rename is a mechanical sweep that belongs
  in Phase 7, where it is one commit instead of a diff buried inside a rewrite.
  §4's ban on drive-by renames still holds.
- **`ConformanceMode` stays `false`.** Both configurations build with
  `/permissive-` off ([Outpost.vcxproj:332, 361](../Outpost/Outpost.vcxproj)).
  Turning it on is the single largest remaining modernisation win in the build,
  and it is tree-wide — not something to smuggle in under an FMV phase. Flagged
  for Phase 7 with a measurement, not attempted here.

### The x64 question this phase unblocks

`NeuronCore/WINSTR.LIB` and `NeuronCore/Mplayer.lib` are the last checked-in
32-bit static libraries, and `GameData/`'s four decoder DLLs are 32-bit
binaries. B6 removes all six. After that **nothing in the tree pins the build to
x86** — the DX9 SDK vendored in `DX9/Lib` has x64 libraries, and XAudio2,
DirectInput 8, WinSock2 and Media Foundation are all 64-bit clean.

[AGENTS.md §3](../AGENTS.md) says one platform exists and adding one is a
stop-and-report. So this phase does not add x64; it **reports that the
constraint is gone** and leaves the decision to the owner. Doing it would mean
auditing the `UDWORD`-holds-a-pointer assumptions in the save-game fixup that
Phase 1 already had to touch, which is its own piece of work.

## Verification

Better placed than Phase 5, worse than Phase 4. The output is visual and audible
and there is a working reference to compare against, but the reference is the
thing being deleted — so **capture it before B5**:

1. Record the current build playing all 19 shipped movies, video and audio.
2. Keep the B1 extractor's raw dumps as the frame-accurate ground truth.
3. After B3/B4, the same 19 movies must show the same frames at the same
   subtitle timings, and the eight with embedded audio must stay in sync to the
   end rather than drifting.

What **cannot** be verified without the CDs: 165 of the 184 movies, including
every campaign briefing. That is not a gap the converter can close — it is the
main risk in the phase, and it argues for running B1's comparison over a full CD
set before committing to the format.

---

## Order of work

```
Phase 5 ──► delete MPDPXtra/MPlayer/NetLobby (Part 1 rides along)

B1 ──► B2 ──► B3 ──► B4 ──► B6
 │      │             │
 │      │             └──► B5 (severable — may land after B6)
 └──────┴── offline, no game code, starts immediately and in parallel
```

B1 and B2 are independent of Phase 5 and of Phase 2's remaining work, and they
are where the unknowns are. **Start there**, because if ffmpeg cannot decode
Escape 130 faithfully the whole shape of the phase changes — the fallback is
shipping the extractor's raw output through a hand-rolled encoder step, which is
a different and larger project than a batch script.

B5 is the one stage that can slip without holding anything up, and it is also
the one with the widest reach into unrelated files. Do not let it gate B6.

## Decisions

All six were settled before B2. Recorded here because most of them constrain
code that has not been written yet, and two are exceptions to standing rules.

**1. Converted movies replace the `.rpl` files in place, and `GameData/` opens
for exactly that commit.** [AGENTS.md §2](../AGENTS.md) marks `GameData/` as
not editable; this phase is the exception, scoped to B2's conversion commit.
Everything downstream keeps working through one extension substitution, and the
directory shrinks rather than doubling. Neither alternative survived: a sibling
`movies/` directory means the tree carries both sets and `seq_SetVideoPath`
grows a third path root, and writing outside the repo means a fresh clone cannot
run the game with FMV, which breaks the CAM_1A check
[AGENTS.md §3](../AGENTS.md) relies on.

**2. The `.rpl` sources do not stay in the repository.** `tools/ConvertSequences.py`
and a manifest of names, SHA-256 hashes, frame counts and durations go in
instead. The deciding argument is proportion: the 19 local files are 10% of the
set, and the 165 CD movies could never live here, so keeping the local ones
would build a 10%-complete archive that reads as a full one.

**3. Conversion retires `CDSpan.cpp` and the CD-swap UI** — B5. Movies were the
last CD reader; nothing else was holding that machinery up. The alternative,
converting at install time from the user's discs, would make the converter a
shipped and supported product surface with an encoder redistributed alongside
it, which is a much larger commitment than a script this project runs once.

**4. A missing movie falls back to the `noVideo` placeholder.** The
`DUMMY_VIDEO` branch at [SeqDisp.cpp:444](../Outpost/SeqDisp.cpp#L444) already
does this and stays, with a `DEBUG_WARNING` added so a partial conversion is
loud in Debug and survivable in Release. It costs nothing and, unlike skipping
the sequence, it keeps the external audio, the subtitles and the
`SeqEndCallBack` / `CALL_VIDEO_QUIT` callbacks firing — a missing briefing must
not be able to wedge a campaign transition. A missing *decoder* is the separate,
louder failure described under B3.

**5. The remaining CD-audio surface is removed outright, not relocated.**
`cdspan_PlayInGameAudio`, `scrPlayBackgroundAudio` and its `ScriptTabs.cpp`
entry go, together with `scrStopCDAudio`, `scrPauseCDAudio` and
`scrResumeCDAudio`. All six are unreferenced by any of the 67 script sources.
**One exception, and it is not optional:** `playCDAudio` is *not* CD audio —
Phase 4 rewired it to `music_PlayTrack`, which serves music from disk — and five
campaign and tutorial scripts call it. Removing it would break level load for
all five, since `.slo` files are compiled by the game at load and an unknown
function name is a compile error. It stays, with the C++ side renamed to
`scrPlayMusicTrack` and the script-visible name kept.

**6. This phase modernises what it rewrites**, rather than leaving it for
Phase 7 — a scoped exception to [AGENTS.md §4](../AGENTS.md), covering
`Sequence.cpp` and `SeqDisp.cpp` only. Files that merely lose a CD call are
edited minimally. The full standard, the two things deliberately left alone and
the x64 constraint this unblocks are under [Modernisation](#modernisation).
