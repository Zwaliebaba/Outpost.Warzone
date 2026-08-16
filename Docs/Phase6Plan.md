# Phase 6 — Removing Mplayer.lib and WINSTR.LIB

Working plan for the phase described in
[MigrationPlan.md](MigrationPlan.md#phase-6--removing-mplayerlib-and-winstrlib).
As with Phases 4 and 5, every figure here was measured against the tree and the
shipped assets rather than estimated.

## Status

**FMV plays on Media Foundation and the removals have landed. B4 is the only
stage still open.**

| Stage | |
|---|---|
| B1 — reference decode and fixtures | **done** |
| B2 — conversion | **done** for every movie there is a source for: 179 in `GameData`, 19 from `.rpl` and 160 from `.ogg` |
| B3 — Media Foundation backend | **done** — briefings, research videos and subtitles all play |
| B4 — dynamic texture and quad | **not started** — the FMV still blits through the locked back buffer |
| B5 — retire the CD path | **done**, in `c50ac20` |
| B6 — delete `WINSTR.LIB`, `dsound.lib`, `CDSpan` | **done**, in `c50ac20` |

B5 and B6 landed as a single commit, and `59a8c26` recorded that in
[MigrationPlan.md](MigrationPlan.md) without touching this document — so the
table above said "not started" for both while the tree said otherwise. Measured
at `d314720`: no `WINSTR.LIB`, `STREAMER.H`, `CDSpan.cpp`/`.h` or `GameData`
decoder DLLs; no `cdspan_*`, `CDSpan`, `showChangeCDBox`, `g_bResumeInGame` or
`DONTTEST` reference anywhere in `Outpost/` or `NeuronCore/`; and
`AdditionalDependencies` carrying neither `WINSTR.LIB` nor `dsound.lib`.

Ten of the 187 names the game can ask for still have no movie behind them: nine
were never in any source set, and one is inside a commented-out line.

**None of it has been run.** B5's own verification note asks for the four
content gates to be exercised, and that is outstanding along with the rest of
the tree's unrun verification.

The phase was two unrelated pieces sharing a heading. **Part 1 went with
Phase 5.** The asset decision that gated everything is settled — see
[The asset problem](#the-asset-problem-164-of-181-movies-are-not-in-this-repo)
for how, and [Decisions](#decisions) for what was chosen.

---

## What the Phase 5 merge changed

`ef6d927` merged the completed Phase 5. It did not touch `Sequence.cpp`,
`Sequence.h`, `STREAMER.H`, `SeqDisp.cpp`, `CDSpan.cpp`, `ScriptFuncs.cpp`,
`ScriptTabs.cpp`, `IntelMap.cpp` or any `GameData/` movie asset, so the analysis
in this document survives intact. Five things did move:

1. **Part 1 is complete.** `MPDPXtra.cpp`/`.h`, `MPlayer.cpp`, `NetLobby.cpp`
   and `NeuronCore/Mplayer.lib` are all deleted, and `Mplayer.lib` and
   `dplayx.lib` are off both link lines. Not one Mplayer reference is left in
   the tree. The section that used to be Part 1 is gone with it.
2. **`WINSTR.LIB` is now the only checked-in third-party static library**, and
   with `GameData/`'s four decoder DLLs the last 32-bit binary in the tree.
   That sharpens [the x64 question](#the-x64-question-this-phase-unblocks)
   rather than changing it.
3. **`AdditionalDependencies` moved to lines 343 and 370**, and
   `ConformanceMode` / `LanguageStandard` to 333 and 362. It also gained
   `crypt32.lib` and `ncrypt.lib`. Line references below are updated.
4. **R14 now has one sanctioned exception, and it is explicitly closed.**
   See [The target](#the-target) — this strengthens the Media Foundation choice
   rather than reopening it.
5. **`NetTest/` establishes a harness precedent** that B1 and B2 should copy.
   See [B1](#b1--read-the-format-prove-it-round-trips).

One correction to my own arithmetic, unrelated to the merge: the `cdspan_*` call
sites are **18**, not the 19 quoted in an earlier draft, and one of those is
inside a comment block at `Mission.cpp:3191`. The seven-file spread is unchanged.

---

## WINSTR.LIB — what the phase actually is

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

### The asset problem: 164 of 181 movies are not in this repo

`GameData/sequences/` holds 18 movies plus `noVideo.rpl`, 15.7 MB, 1920 frames,
**77 seconds in total**. The game references **181 distinct `.rpl` names** across
`Outpost/*.cpp` and `GameData/messages/*.txt`. The other 164 — every `cam1\`,
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
to XAudio2. Finishing this phase lets `dsound.lib` come off both link lines and
`#include <dsound.h>` out of the tree entirely.

---

## The target

**Media Foundation, H.264 video and AAC audio in MP4, decoded through
`IMFSourceReader`.** The reasoning:

- **[AGENTS.md §5 R14](../AGENTS.md) forbids new third-party dependencies.**
  Phase 5 opened exactly one exception — MsQuic, via NuGet — and the rule now
  spells out that it "covers MsQuic and the NuGet restore that fetches it, and
  nothing else; a second one needs the same conversation." That makes the
  argument here *stronger*, not weaker: the NuGet machinery existing does not
  make a bundled video decoder cheap, it makes it an owner conversation. Media
  Foundation needs no such conversation, and it is the only option consistent
  with MigrationPlan.md's stated endpoint for Phases 4-6 — "the only remaining
  non-system dependencies are the DirectX libraries themselves". Bundling
  libvpx, libtheora or ffmpeg contradicts that. Media Foundation is a system
  component: `mfplat.lib`, `mfreadwrite.lib`, `mfuuid.lib`, all in the Windows
  SDK, no redistributable, no package.
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

**Done.** `MovieTest/RplExtractor.cpp` drives the shipping decoder and has
extracted all 19 movies; `MovieTest/Fixtures/reference.json` holds a SHA-256 per
frame — 1920 frames, 143 KB — and `MovieTest/Fixtures/Audio/` the eight
reference WAVs. A clean rebuild reproduces every frame bit-identically.
[What B1 established](#what-b1-established) records what it found; the rest of
this section is the design it followed.

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
2. **An extractor linked against `WINSTR.LIB`.** A ~150-line console EXE that
   calls the same 21 entry points `Sequence.cpp` does and dumps raw RGB565
   frames and 16-bit PCM. This is guaranteed bit-exact against what ships today,
   because it *is* what ships today.

Build route 2 regardless of whether route 1 works: it is the oracle that decides
whether ffmpeg's decoder is faithful. Compare frame-by-frame; Escape 130 is an
obscure codec and "it decoded without erroring" is not the same as "it decoded
correctly".

**Model it on `NetTest/`, and do not call it throwaway.** Phase 5 set the
precedent: a console project that compiles the sources it exercises directly
rather than linking `NeuronCore.lib`, built *and run* by CI
([build.yml](../.github/workflows/build.yml)) because "everything before it
proves the tree compiles and links" and nothing else actually runs the code. FMV
is in the same position — a green build says nothing about whether a movie
decodes. A `MovieTest/` project alongside `NetTest/` gives B1 its oracle, gives
B2 a regression check, and after B3 gives the Media Foundation path somewhere to
be exercised without launching the game. It is the difference between this phase
being verifiable in CI and being verifiable only by watching it.

It has to outlive `WINSTR.LIB` by one commit, though: the extractor links the
library B6 deletes. Sequence it so the reference dumps are captured and checked
in as fixtures *before* B6, and `MovieTest` then compares Media Foundation
output against fixtures rather than against a live legacy decoder.

**Verifies:** a per-movie PSNR/SSIM report against the extractor's output, and a
sample-count match on the audio.

#### What B1 established

The decode is sound. Every movie's frame count matches its ARMovie header chunk
count exactly, every audio track's length matches its video's duration to the
sample, and rendered frames come out with correct colour and stride — the
`BrfCom` orbital animation and the `res_droid` wireframe schematic both read
correctly, HUD text included. The eight-with-audio / eleven-silent split this
document predicted from the headers is confirmed by the decoder itself.

Six things it turned up that the plan had wrong or did not know:

1. **`WINSTR.LIB` is an import library for `winstr.dll`, not a static library.**
   45 KB of link stubs; the code is the 80 KB DLL in `GameData/`. Every
   description of it as "the last checked-in static library" is corrected below.
   It also means the *DLL* is the 32-bit artefact, not the `.lib`.
2. **It forces SafeSEH off.** `Outpost.vcxproj` carries
   `ImageHasSafeExceptionHandlers=false` at lines 342 and 369 purely because a
   1997 import library has no safe exception handler table. **B6 can delete that
   opt-out and re-enable SafeSEH**, which is a real hardening win nobody had
   costed.
3. **`winstr.dll` binds at load time**, so it must sit beside the executable
   before `main` runs — a `chdir` into `GameData` is far too late. The project
   copies the four decoder DLLs to its output directory, as CI does for
   `msquic.dll`.
4. **The decoder never reports `STREAMER_FINISHEDAUDIO` past end of video.**
   Asked to stream zero frames after the picture ends it keeps handing back
   buffers indefinitely — a drain loop bounded only by an iteration count
   produced 67 MB of repeated audio before this was caught. Audio is captured on
   the video clock and cut to the video's exact duration instead, which is
   sound because the eight tracks are authored to their video's length.
5. **`Movie_GetTotalFrames` is consistently one high** — 60 reported against 59
   chunks, 178 against 177. The header's chunk count is the authority.
6. **`BrfCom.rpl` and `BrfCom4s.rpl` share one 50-frame visual loop.** Frame 50
   is byte-identical to frame 0, and `BrfCom4s` is the same loop held longer for
   a longer line of dialogue. Two encoding consequences for B2: keyframe
   placement should respect the loop point, and these two are candidates for
   sharing a single encoded asset.

#### Route 1, measured

ffmpeg is now available and route 1 has been run against the oracle. **It works,
and it is not what ships.**

| | Result |
|---|---|
| Demuxer / decoders | `rpl`, `escape130`, `adpcm_ima_escape` — all present |
| **Audio** | **bit-exact.** All 52,038 overlapping samples of `BrfCom` identical to the shipping decoder |
| Video, ffmpeg defaults | **31.5 dB** against the reference |
| Video, `scale=in_range=full` | **42.4 dB** |
| Frame count | **60 against the shipping decoder's 59** — one spurious trailing frame |

Two things go wrong quietly. `escape130` decodes to `yuv420p` and ffmpeg assumes
limited range, so without `in_range=full` every movie ships 11 dB darker in the
shadows than it does today — and nothing about that failure announces itself.
And the extra frame means a naive conversion lengthens every movie by 40 ms.

So route 1 is the **fallback**, not the plan. B2 encodes from the extractor's
frames, which are the shipping pixels by construction. Route 1 stays documented
and implemented because the 164 CD movies will still need converting after B6
deletes the extractor.

### B2 — The converter

**Done for the 19 local movies.** `tools/convert_sequences.py` converts them all
and self-checks; results and settings are under
[What B2 produced](#what-b2-produced). The 164 CD movies remain, and they are
blocked on the discs, not on the tool.

`tools/convert_sequences.py`, driving ffmpeg over a directory of `.rpl`:

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

**Verifies:** the manifest, plus every one of the 181 referenced names resolving
to an output file (or being explicitly listed as CD-only and absent).

#### What B2 produced

`--verify` decodes each output back and measures it against the frames it was
made from, so the numbers below come from the tool rather than from a spot
check. Settings: **H.264 libx264, crf 18, `veryslow`, yuv420p, `-color_range pc`,
AAC 96 kbit/s, `+faststart`.**

| | |
|---|---|
| Movies converted | 19 of 19, all via the exact path, no fallbacks |
| Size | **16,033 KB → 6,103 KB (2.6x smaller)** |
| Video PSNR | 33.9 – 39.1 dB, mean ≈ 35 |
| Audio SNR | 30.8 – 41.8 dB |
| Audio sample drift | **±0 samples on all eight tracks** |
| Frame counts | every movie matches its ARMovie chunk count exactly |
| Output streams | H.264 **High** profile, `yuvj420p(pc)`, 8-bit; AAC-LC |

**Read the PSNR against its ceiling, not against 100.** RGB565 → YUV → RGB565
costs **40.96 dB with no codec involved at all** — measured, by running the
conversion with no encoder in the path. 4:2:0 subsampling adds almost nothing
(40.13 dB). So ~35 dB is roughly 5 dB of actual codec loss beneath an
unavoidable colour-space floor, and H.264 for Media Foundation must be 8-bit
YUV, so that floor cannot be bought out. Rendered side by side the reference and
the crf 18 encode are indistinguishable; the residual is scattered sub-visible
noise on bright gradient edges, with no blocking and no banding. crf 14 buys
2 dB for 1.6x the size and was judged not worth it.

Two things for B3 to check rather than assume:

- **`yuvj420p(pc)` is full-range flagged.** If Media Foundation ignores the VUI
  range flag, every movie renders washed out. It is one comparison against
  `MovieTest/Fixtures/` to find out, and the fix if it does is a shader-side or
  conversion-side range expansion.
- **High profile, 8-bit, 4:2:0** is squarely inside what MF's H.264 decoder
  supports, and AAC-LC likewise — but that is documentation, not a test.

### B3 — The new backend behind `Sequence.h`

**Done, and the sequences play.** `NeuronCore/MovieStream.h`/`.cpp` (104 + 416
lines) decode through `IMFSourceReader`; `Sequence.cpp` went from 840 lines to
273 and is now a shim over one `MovieStream`. What it took, and the three places
the design below turned out to be wrong, are under
[What B3 took](#what-b3-took).


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

#### What B3 took

The shape above survived. `Sequence.h` is unchanged, `IMFSourceReader` decodes
straight to `MFVideoFormat_RGB32` — which is the back buffer's own format, so
the per-pixel 565 conversion is gone and rows are copied — and the whole
soundtrack is decoded at open and submitted to one XAudio2 voice, which deleted
the double buffer, its callback and the `SSDM_*` state machine along with it.

`sound_GetEngine()` was added to `TrackLib.h` for that voice. The old module
created **its own DirectSound object**, because QMixer's had gone and there was
nothing left to borrow; the movie soundtrack now mixes on the game's graph and
obeys its volume. `Sequence.h` no longer includes `<dsound.h>`, and nothing in
the tree does.

Three things the design above got wrong:

1. **`SeqDisp.cpp` did have to change**, and the claim that it "should not need
   to" was the most expensive error here. It builds `aVideoName` and then
   *probes it with `fopen`*, and `seq_SetVideoPath` decides whether the
   hard-disk video path exists at all by globbing `sequences\*.rpl`. Any name
   translation deeper than that leaves those probes testing a file that is not
   there — the glob alone reports "no videos installed" and every sequence in
   the game stops. The first attempt put the translation inside `Sequence.cpp`
   and looked correct until the assets were removed.
2. **The `.rpl` → `.mp4` translation is gone entirely**, and should never have
   been the plan. Rewriting a name on the way past means the name is stored
   wrong; the fix was to rename the data — **531 names across 61 files**, the
   hardcoded call sites in `WinMain`, `Mission`, `FrontEnd`, `ScriptFuncs` and
   `SeqDisp`, plus every record in `GameData/messages`. Save games were never
   an obstacle: they hold message ids, and the movie name comes back from the
   message data on load. `seq_BuildVideoName` is now a plain path join.
3. **`DFLAG_DOUBLED` had to be reimplemented.** Nothing in this document
   recorded that the old decoder scaled: `seq_SetSequence` asked
   `Streamer_InitVideo` to double any movie no larger than half the 640x480
   playback area, then doubled `movieWidth`/`movieHeight` to match. Media
   Foundation has no equivalent, so a 320x240 briefing drew at 320x240 in the
   top-left corner. The blit now scales 2x with nearest-neighbour — pixel
   replication, which is what the flag did — and walks *output* rows so the
   scaling and the subtitle band share one coordinate space.

One latent defect fixed on the way past: the file probe in
`seq_RenderVideoToBuffer` called `fclose` on a null handle whenever the movie
was missing and no CD path was set. Its twin in `seq_StartFullScreenVideo`
guarded it; this one never had. Nine of the movies the game names have no source
in any format, so the path is reachable.

##### Shutdown order, which the first version got wrong

Quitting the game produced `Invalid address specified to RtlValidateHeap`. The
cause is worth recording, because it is a trap for anything that takes a voice
from the shared mixer.

An FMV soundtrack is an XAudio2 source voice on the game's own graph, and a
voice has to be destroyed while the engine that created it still exists.
`systemShutdown` released the engine through `audio_Shutdown`, and nothing tore
FMV down first — `seq_ShutDown` was only ever called at the end of a sequence.
The player was then a file-scope object, so its destructor ran during CRT static
teardown, long after the engine had gone, and destroyed a voice into freed
memory.

Two changes, and both are needed:

- `systemShutdown` calls `seq_ShutDown()` **before** `audio_Shutdown()`, so the
  voice goes while its engine is alive.
- The player is heap allocated and **never deleted**. Leaking one object at
  process exit costs nothing; freeing it after COM and the mixer have gone costs
  a corrupted heap. Nothing about FMV now depends on static destruction order.

Also hardened while in there: the frame copy trusted the stride from the media
type without checking the sample was actually that long, and would have walked
off the end of a short buffer a row at a time.

**Deliberately not done: audio as the master clock.** The plan wanted video
presented against `SamplesPlayed`. That conflicts with leaving `SeqDisp.cpp`'s
pacing loop alone, and between the two the smaller change won — sync is as good
as the `.rpl` player's and no better. Driving from the audio clock is a real
improvement and belongs with B4, which is already rewriting that loop.

### B4 — Dynamic texture and quad

**Status: not started**, and the only open stage of the phase.

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

Two of those bullets have been overtaken since they were written, and the rest
have not moved. `pie_D3DSetupRenderForFlip` and `pie_D3DRenderForFlip` no longer
exist anywhere in the tree, so there is nothing left to check before deleting
them. And B3's rewrite of `seq_RenderOneFrame` already replaced the
`SEQ_LOW_BIT_MASK` shift with a `darken` flag inside the row loop
([Sequence.cpp:248](../NeuronCore/Sequence.cpp#L248)) — still per-pixel, and
still what the translucent quad would replace, but no longer 90 lines.

What has not moved: `seq_SetupVideoBuffers` still allocates the 640x480x2 buffer
and the 32 KB 555 palette table ([SeqDisp.cpp:262](../Outpost/SeqDisp.cpp#L262),
called from [HCI.cpp:769](../Outpost/HCI.cpp#L769)), `Sequence.cpp` still takes
`screenLockBackBuffer` to draw a frame, and
`D3DPRESENTFLAG_LOCKABLE_BACKBUFFER` is still set
([Screen.cpp:145](../NeuronCore/Screen.cpp#L145)) — the Phase 2 item this stage
holds open.

**Verifies:** same run as B3, plus a windowed/fullscreen toggle to confirm the
quad scales where the old code hard-coded `borderX` / `borderY`.

### B5 — Retire the CD path

**Status: done**, in `c50ac20`, together with B6.

Movies were the last thing read from the CD, so with B2's output installed to
disk the CD-presence apparatus becomes dead weight. This is the item Phase 4
deferred.

**It is larger than MigrationPlan.md implies, and the file is not where that
document says.** Two corrections from measuring the tree:

- `CDSpan.cpp` is in **`Outpost/`, not `NeuronCore/`**, and it is **535 lines**
  plus a 38-line header — not the 631 quoted under Phase 4.
- It has **18 `cdspan_*` call sites across seven files**, not the handful the
  "related but distinct" description suggests — 17 live, plus one inside a
  comment block at `Mission.cpp:3191`. `seq_StartFullScreenVideo` is two of them.

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
no path resolves to a drive letter. This needs a real run, not a build. **That
run has not happened**, and it is the part of B5 still owed.

#### What B5 did differently

It did not run long, so the severability above went unused: it landed inside
B6's commit rather than after it. One departure worth recording — the C++ side
of the surviving script function is still named `scrPlayCDAudio`, not the
`scrPlayMusicTrack` this plan asked for. The script-visible name `playCDAudio`
was always going to stay, and it carries the explanatory comment at
[ScriptTabs.cpp:154](../Outpost/ScriptTabs.cpp#L154), so the code no longer
misleads; only the rename that would have made the C++ symbol say `music` is
outstanding, and it is cosmetic. The three unused `*CDAudio` functions and
`playBackgroundAudio` are gone as planned, and the five `.slo` callers of
`playCDAudio` still resolve.

### B6 — Removal

**Status: done**, in `c50ac20`. The table below is what it removed; two entries
resolved differently and are noted after it.

Once B3, B4 and B5 run:

| Removed | Where |
|---|---|
| `WINSTR.LIB` | `Outpost.vcxproj` lines 343, 370 |
| `dsound.lib` | same two lines — last user was `Sequence.cpp` |
| `NeuronCore/STREAMER.H` | 436 lines |
| `NeuronCore/Sequence.cpp` | 840 lines, replaced |
| `Outpost/CDSpan.cpp` + `.h` | 573 lines, per B5 |
| `NeuronCore/WINSTR.LIB` | the import library itself — the last checked-in third-party library in the tree |
| `ImageHasSafeExceptionHandlers=false` | `Outpost.vcxproj` lines 342, 370 — present only for WINSTR.LIB; removing it re-enables SafeSEH |
| `MovieTest/RplExtractor.cpp` + the project's WINSTR link | the extractor goes with the library; `MovieTest/Fixtures/` stays |
| `scrPlayBackgroundAudio` + `ScriptTabs.cpp:154` | dead script function, per B5 |
| `scrStopCDAudio`, `scrPauseCDAudio`, `scrResumeCDAudio` + entries | registered, called by no script |
| `GameData/winstr.dll` | 80 KB |
| `GameData/Dec130.dll` | 123 KB |
| `GameData/*.rpl` | 15.7 MB, replaced by `.mp4` in B2's commit |
| `GameData/edec.dll`, `winsdec.dll` | **verify first** — 117 KB and 90 KB, no reference found anywhere in the tree, but they sit beside the other two and are probably the same family |

Add `mfplat.lib`, `mfreadwrite.lib` and `mfuuid.lib` to the same two lines. The
net change to `AdditionalDependencies` is three system libraries in, two entries
out — `Mplayer.lib` and `dplayx.lib` having already gone with Phase 5 — and the
last vendored non-system dependency in the tree is gone. What remains after that
is DirectX, the Windows SDK, and MsQuic under its sanctioned exception.

**Do not remove `playCDAudio` / `music_PlayTrack`** — see
[the `*CDAudio` note under B5](#the-cdaudio-script-functions-are-not-cd-audio-and-one-of-them-is-live).
It is disk music with a misleading name and five live script callers.

#### What B6 did differently

- **SafeSEH did not stay re-enabled.** Removing
  `ImageHasSafeExceptionHandlers=false` on the assumption it existed for
  `WINSTR.LIB` passed Debug and failed Release with LNK2026 / LNK1281. The real
  culprit is `DX9\Lib\dinput8.lib`, whose `dilib1.obj` predates SafeSEH and
  carries no handler table; incremental linking silently disables the check, so
  only the `/INCREMENTAL:NO` Release configuration ever exercised it. The
  property is back on both link lines with the real reason recorded at the site,
  and re-enabling SafeSEH now needs a clean `dinput8.lib` first. Phase 8 records
  the same finding from the other end.
- **`edec.dll` and `winsdec.dll` were the same family**, as suspected. The
  "verify first" held up: nothing in the tree referenced either, and both went
  with the other two. `GameData/` now contains no DLL at all.

`MovieTest/` kept its fixtures and lost everything else — the project is out of
`Outpost.slnx` and `RplExtractor.cpp` is deleted, so the directory is now purely
the record of what the original decoder produced.

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
  `/permissive-` off ([Outpost.vcxproj:333, 362](../Outpost/Outpost.vcxproj)).
  Turning it on is the single largest remaining modernisation win in the build,
  and it is tree-wide — not something to smuggle in under an FMV phase. Flagged
  for Phase 7 with a measurement, not attempted here.

### The x64 question this phase unblocks

Phase 5 already removed `NeuronCore/Mplayer.lib`, so **`NeuronCore/WINSTR.LIB`
is the last checked-in third-party library**. B1 established it is an *import*
library rather than a static one, which sharpens the point: the 32-bit artefacts
are `GameData/`'s four decoder DLLs, and `WINSTR.LIB` is only the 45 KB of stubs
that bind to one of them. B6 removes all five. After that **nothing vendored in
the tree pins the build to x86** — the DX9 SDK in `DX9/Lib` has x64 libraries,
and XAudio2, DirectInput 8, WinSock2 and Media Foundation are all 64-bit clean.

Two things the Phase 5 merge added to this question:

- **MsQuic is not an obstacle.** The `Microsoft.Native.Quic.MsQuic.Schannel`
  package ships native binaries per architecture; CI currently copies from
  `build\native\bin\x86\`, and that path is a per-platform variable, not a pin.
- **CI is Win32-only** and now has more in it than a build — a NuGet restore per
  project and the NetTest harness run. Adding a platform means doubling that
  matrix, which is a real cost to weigh rather than a checkbox.

[AGENTS.md §3](../AGENTS.md) says one platform exists and adding one is a
stop-and-report. So this phase does not add x64; it **reports that the vendored
constraint is gone** and leaves the decision to the owner. Doing it would also
mean auditing the `UDWORD`-holds-a-pointer assumptions in the save-game fixup
that Phase 1 already had to touch, which is its own piece of work.

## Verification

Better placed than Phase 5, worse than Phase 4. The output is visual and audible
and there is a working reference to compare against, but the reference is the
thing being deleted — so **capture it before B5**:

1. Record the current build playing all 19 shipped movies, video and audio.
2. Keep the B1 extractor's raw dumps as the frame-accurate ground truth.
3. After B3/B4, the same 19 movies must show the same frames at the same
   subtitle timings, and the eight with embedded audio must stay in sync to the
   end rather than drifting.

What **cannot** be verified without the CDs: 164 of the 181 movies, including
every campaign briefing. That is not a gap the converter can close — it is the
main risk in the phase, and it argues for running B1's comparison over a full CD
set before committing to the format.

---

## Order of work

```
Phase 5 ──► DONE (took Mplayer.lib with it)

B1 ──► B2 ──► B3 ──► B5+B6     B4  (independent, improves what already works)
DONE   DONE   DONE   DONE      open
```

**B1, B2 and B3 are done, and the movies play.** The asset problem that shaped
this whole document is closed: an OGG set covering 155 of the 164 CD movies
turned up, so 179 of the 187 names the game can ask for now resolve, and the
`.rpl` files are gone from the repository.

**B5 and B6 went together** rather than in sequence, which the severability note
under B5 allowed for. Nothing included `<dsound.h>` and nothing called a
`Streamer_*` entry point by then, so the removal was as mechanical as predicted;
the CD path's widget plumbing was the only part that reached into live code.
`WINSTR.LIB`, `STREAMER.H`, the four `GameData` decoder DLLs, `dsound.lib`,
`CDSpan.cpp` and the `MovieTest` extractor are all gone. The fixtures stay: they
are the only remaining record of what the original decoder produced.

**B4 is what is left, and it gates nothing.** It replaces a working CPU blit
with a textured quad and is the right home for the audio-clock change B3
deferred. Its one outward claim is on Phase 2's lockable back buffer, which
cannot come off the present parameters until the FMV stops locking it.

What cannot be fixed by any of them: **nine movies have no source in any
format**, and 57 of the OGG sources are 12.5 fps where the originals were 25.
Both need better assets, not more code.

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
set, and the 164 CD movies could never live here, so keeping the local ones
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
