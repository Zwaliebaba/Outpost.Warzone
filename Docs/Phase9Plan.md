# Phase 9 — Audio: retiring the QMixer-shaped stack

Working plan for modernising the audio module. Phase 4 swapped the backend —
QMixer out, XAudio2 in — behind an interface it deliberately did not change.
This phase changes the interface: it removes the layering that existed to host
that swap, rewrites the module as C++23 in `namespace Neuron`, and shapes it so
the features the game does not have yet (buses, effects, more codecs, real
streaming) land as additions rather than rewrites.

This is [Phase 7](MigrationPlan.md#phase-7--incremental-c-modernisation)'s
method applied to the audio stack, taken as a numbered phase of its own the way
Phase 8 took the render layer — and it is the same shape of work: a layer built
to abstract over multiple backends now abstracts over exactly one, so the layer
is cost with no remaining purpose.

**Status: planned.** Nothing below is implemented. The figures were measured
against the tree at the head of this branch; the method is at the
[end](#measurement). Decisions the plan needs confirmed are collected under
[Decisions to confirm](#decisions-to-confirm).

## Where the stack stands

| File | Lines | Role |
|---|---:|---|
| `NeuronCore/Audio.h` / `Audio.cpp` | 69 / 999 | sample lifecycle: intrusive linked lists of `AUDIO_SAMPLE`, the speech queue, same-sound gates, ducking, music pass-throughs |
| `NeuronCore/Track.h` / `Track.cpp` | 130 / 421 | track table: fixed 600-slot `TRACK*` array, ID allocation, metadata accessors, play gate, finished callback |
| `NeuronCore/TrackLib.h` | 88 | the backend seam Phase 4 kept |
| `NeuronCore/XA2Track.cpp` | 1,488 | the XAudio2 backend: 32 voice slots (4 fixed + pool), X3DAudio, RIFF parse, 16-bit-mono normalisation, completion handoff |
| `NeuronCore/Aud.h` | 39 | **declarations the engine calls but the game defines** — see [the upcall](#the-upcall-neuroncore-calls-into-outpost) |
| `Outpost/Aud.cpp` | 269 | those definitions: object/camera positions, object-death checks, the y-inversion into "QSOUND axes" |
| `Outpost/AudioID.h` / `AudioID.cpp` | 492 / 284 | the `ID_SOUND_*` enum (~400 values) and the name→ID table |
| `Outpost/Music.h` / `Music.cpp` | 27 / 73 | track number → `music\track<n>.wav`, four thin wrappers |

4,379 lines in twelve files. Above them sit **182 `audio_*` references across
43 translation units** (a handful of those are grammar-symbol strings in the
generated `parser_y.cpp`, not calls). `NeuronCore/MovieStream.cpp` is *not*
part of this: it is Phase 6 output, already modern C++ — its only contact is
borrowing the engine through `sound_GetEngine()`.

The call flow is four layers deep for a one-layer job:

```
game code (43 TUs)
  → audio_*   (Audio.cpp — lists, gates, ducking)
    → sound_* (Track.cpp — table lookup, then straight down)
      → sound_* backend (XA2Track.cpp — the only part that does audio)
        ⇡ audio_GetObjectPos etc. (Aud.h → Outpost/Aud.cpp — the upcall)
```

The `audio_*`/`sound_*` split exists because QMixer's backend had to be
swappable underneath a stable middle. It was — once, in Phase 4 — and exactly
one backend has existed since. Every remaining `sound_*` call is either a
one-line pass-through (`audio_PlayMusic` → `sound_PlayMusic`), a table lookup
the caller could do, or a name for something `Audio.cpp` should own outright.

## What the analysis found

### The dead surface, measured

Tree-wide grep over `NeuronCore/` and `Outpost/`, including headers and the
generated parsers, per the rule in [AGENTS.md §6](../AGENTS.md); the
`tools/check_case.py` feature-macro allow-list was checked and its only
audio-adjacent entry (`qmdx.h`, dead under `#if QMIXER`) does not reach any of
these. Zero callers anywhere:

- **The cluster-sound family** — `audio_GetClusterCentre`,
  `audio_GetNewClusterObject`, `audio_ClusterEmpty`,
  `audio_GetClusterIDFromObj`: 96 lines of `Outpost/Aud.cpp` plus their
  `Aud.h` declarations. Group-movement sound that nothing has invoked in the
  lifetime of this repository.
- `audio_GetScreenWidth`, `audio_Get2DPlayerRotAboutVerticalAxis`,
  `audio_PlayPreviousQueueTrack` (KeyBind.cpp uses only its sibling
  `audio_GetPreviousQueueTrackPos`), `audio_GetMixVol`.
- `sound_GetNumPlaying`, `sound_GetTrackPriority`, `sound_GetTrackName`,
  `sound_GetTrackTime` — defined, never called. `sound_PlayWithCallback` and
  `sound_SetCallbackFunction` — declared in `Track.h`, **never even defined**.
- **`TRACK` fields written and never read**: `bMemBuffer`, `iNumPlaying`, and —
  with the accessors above gone — `iPriority` and `iTime` have no reader left.
  Priority deserves a note: `audio.cfg` authors a priority per track, QMixer
  used it to arbitrate channel stealing, and the XAudio2 backend steals by
  distance instead. The value has been parsed and ignored since Phase 4. Keep
  parsing it (the file format is data, not code), keep storing it (a future
  feature may want it back), but its accessor is dead today.
- **`AUDIO_SAMPLE::iLoops`** — never touched. `SAMPLEVALIDFUNC` — a typedef
  nothing uses. `MIN_PITCH`/`MAX_PITCH`/`CENTER_PITCH` and the four
  `AUDIO_PAN_*` constants — QMixer-era knobs with no consumer.
  `AUDIO_QUEUE_SIZE` in `Audio.cpp` — defined, unused; the queue is unbounded.
- **`Track.cpp`'s `g_iSamples`/`g_iMaxSamples`/`g_iMaxSameSamples`** —
  `g_iSamples` is decremented and never incremented, the other two are stored
  and never read (`Audio.cpp` uses its own `MAX_SAME_SAMPLES` directly). With
  them goes `sound_Init`'s `iMaxSameSamples` parameter, and its `hWnd`
  parameter, which is already `hWnd;`-discarded — XAudio2 needs no window.
- **`VagID`** — the PlayStation VAG id threaded through `audio_SetTrackVals`,
  `audio_SetTrackValsHashName` and `sound_SetTrackVals`, discarded at the
  bottom (`VagID;`). Every caller passes 0. The PSX has been gone from this
  codebase longer than XAudio2 has existed.

Two things are worse than dead:

- **Two stray `printf`s** in `audio_QueueSample`/`audio_QueueTrack`
  (`Audio.cpp:258`, `:270`) — debug leftovers that fire on every queued speech
  sample, on a code path hot enough to be in the frame loop. They predate
  `Neuron::DebugTrace` discipline and go first.
- **`audio_GetPreviousQueueTrackPos` reads an uninitialised output.** Its
  failure path is `*iX = *iY = *iZ;` (`Audio.cpp:145`) — it assigns the
  *caller's uninitialised* `*iZ` into the other two outputs instead of
  clearing all three. Harmless today only because the one caller
  (`KeyBind.cpp:1271`) checks the return value. It gets fixed, not preserved.

### The threading truth: one lock guards against a thread that no longer exists

`Audio.cpp` initialises `critSecAudio` and takes it around every list splice.
Under QMixer that was load-bearing: completion callbacks arrived on QMixer's
thread and touched the sample lists. Phase 4 deliberately confined the thread
boundary to the backend — `OnStreamEnd` only sets a bit in
`g_udwFinishedMask` under `g_csFinished`, and `sound_Update` drains it on the
game thread. Everything above the backend now runs on the game thread,
exclusively. So:

- `critSecAudio` protects lists that only one thread ever touches. It goes,
  along with the `InitializeCriticalSection`/`DeleteCriticalSection`
  choreography and the shutdown ordering it implies.
- `g_csFinished` + `g_bFinishedInit` + mask is the one real crossing, and it is
  a textbook `std::atomic<std::uint32_t>`: `fetch_or` in the callback,
  `exchange(0)` in the update. The init-order flag exists only because a
  `CRITICAL_SECTION` has a lifetime; an atomic does not, so the flag goes too.

### The upcall: NeuronCore calls into Outpost

`Aud.h` lives in `NeuronCore/` and declares eleven functions; `Outpost/Aud.cpp`
defines them. The engine's audio update literally cannot link without the game
supplying `audio_GetObjectPos`, `audio_ObjectDead`, `audio_Get3DPlayerPos`,
`audio_Get3DPlayerRotAboutVerticalAxis`, `audio_Display3D`,
`audio_GetStaticPos`, `audio_GetIDFromStr` and `sound_GetGameTime`. That is the
engine depending on the game — the one place in the tree where the dependency
edge between the two projects points backwards, made invisible by C linkage.

It also means every `AUDIO_SAMPLE` carries a `void* psObj` that only the game
can interpret, checked for death and position every frame by casts inside
`Outpost/Aud.cpp`.

The modern shape is explicit: the game hands the audio system a small provider
at init — listener pose, object position, object liveness, current game time —
and the engine stores it. The dependency then points the right way, the `void*`
is confined to the provider the game wrote, and the audio module becomes
testable without a game attached (which is what "prepared for additional
features" needs more than any single feature).

### What is load-bearing and must not change

These are the constraints everything below is built around. Each was verified
against the tree, not assumed:

1. **Save games round-trip track IDs by WAV-name hash.** `ScriptObj.cpp:549`
   saves `sound_GetTrackHashName(id)`; `:793` restores with
   `audio_GetTrackIDFromHash(hash)` and, if the track is not yet registered,
   `:804` re-registers it via `audio_SetTrackValsHashName`. Track *IDs* are
   dynamic per run; the *hash* is the persistent name. The new module keeps
   all three operations and the hash function's output byte-identical.
2. **Compiled scripts reach audio by table position and by WAV name.**
   `playCDAudio` (rewired to `music_PlayTrack` in Phase 4) must keep its
   `ScriptTabs.cpp` slot, and `ScriptVals_y.cpp:1352` resolves WAV names to
   IDs at script load via `audio_GetTrackID`. Signatures stay.
3. **The generated resource parser calls `audio_SetTrackVals`**
   (`parser_y.cpp:748`, `:756`) for every `audio.cfg` entry. Generated-but-
   patched code, per Phase 1 precedent: the two call sites may be edited in
   place (dropping `VagID`), but nothing forces regeneration.
4. **`Config.cpp` persists `fxvol` and `cdvol`.** Key names are part of
   existing `.cfg` files. They stay.
5. **`MovieStream.cpp` borrows the engine** through `sound_GetEngine()` so FMV
   audio lives on the game's graph. The handout survives under a new name.
6. **`AUDIO_DISABLED`** builds must still no-op the whole system
   (`Init.cpp:724`), and every public entry must stay callable-and-inert when
   init failed or audio is disabled — the game runs silent, it does not crash.

And the behaviour the rewrite must reproduce, which is the Phase 4 contract:

- Four fixed slots — speech queue, single stream, serialised 2D FX, music —
  and a pool for 3D, stolen from by greatest listener distance.
- Music independent of the stream slot: a briefing neither stops nor un-pauses
  it; pause holds position across FMV.
- 3D duck to 25% while queued speech plays.
- `MAX_SAME_SAMPLES` (2) gates on the queue and within a track's audible
  radius; min-delay repeat suppression on `iTimeLastFinished`.
- Completion callbacks on the game thread, at most one frame late.
- The distance model: flat inside 300 units, inverse-distance rolloff 1.5,
  silent at the track's authored radius; the y/z axis swap into X3DAudio and
  the y-inversion in `Outpost/Aud.cpp` stay exactly as documented in
  [Phase4Plan.md](Phase4Plan.md#what-was-built).
- Handles that outlive their sound are inert: the generation scheme (or a
  typed equivalent) stays.

## The target

### Module layout

Seven engine files (`Audio.*`, `Track.*`, `TrackLib.h`, `XA2Track.cpp`,
`Aud.h`) become six, and the game side keeps three modules under conforming
names. New engine code in `namespace Neuron`, per
[AGENTS.md §1](../AGENTS.md) and the `TexturePage` worked example; the class
shape follows the Phase 5 `Transport` precedent — one implementation, no
interface ceremony, state internal to the translation unit.

| New | Absorbs | Role |
|---|---|---|
| `NeuronCore/AudioSystem.h` / `.cpp` | `Audio.h/.cpp`, `Track.h/.cpp`, `TrackLib.h` | the public surface: tracks, samples, queue, gates, ducking, music, volumes |
| `NeuronCore/AudioMixer.h` / `.cpp` | `XA2Track.cpp` | the XAudio2 graph: engine, mastering voice, slots, X3DAudio, completion |
| `NeuronCore/WavData.h` / `.cpp` | the RIFF half of `XA2Track.cpp` | parse + normalise a WAV from bytes; the one decode seam |
| `Outpost/GameAudio.h` / `.cpp` | `Aud.h`, `Outpost/Aud.cpp` | the provider: positions, liveness, listener, game time |
| `Outpost/AudioID.h` / `.cpp` | itself | unchanged values, conforming type/file names |
| `Outpost/Music.h` / `.cpp` | itself | unchanged behaviour, conforming names |

A sketch of the seams (shapes, not signatures to hold to):

```cpp
// NeuronCore/AudioSystem.h
namespace Neuron
{

inline constexpr std::int32_t MaxVolume = 100;      // was AUDIO_VOL_MAX

struct AudioWorld                                    // implemented by the game
{
  using ObjectRef = void*;                           // confined here, nowhere else
  std::move_only_function<bool(ObjectRef)> objectDead;
  std::move_only_function<Vec3i(ObjectRef)> objectPosition;
  std::move_only_function<ListenerPose()> listener;
  std::move_only_function<std::uint32_t()> gameTimeMs;
};

class AudioSystem                                    // Transport-style: static surface
{
public:
  static bool Init(AudioWorld _world, bool _enabled);
  static void Update();                              // once per frame, game thread
  static void Shutdown();

  static bool PlayTrack(std::int32_t _trackId);      // was audio_PlayTrack
  static bool PlayObjectTrack(void* _object, std::int32_t _trackId, SampleCallback _done);
  static void QueueTrack(std::int32_t _trackId);
  // ... the surviving surface, one name each
};

} // namespace Neuron
```

```cpp
// NeuronCore/WavData.h
namespace Neuron
{

enum class WavError : std::uint8_t { NotRiff, NoFormat, NoData, Unsupported, Compressed };

struct WavData
{
  std::vector<std::int16_t> samples;                 // normalised 16-bit
  std::uint32_t sampleRate;
  std::uint16_t channels;                            // 1 for pool tracks, source count for streams

  static std::expected<WavData, WavError> FromBytes(std::span<const std::byte> _riff, bool _forceMono);
};

} // namespace Neuron
```

### What each legacy idiom becomes

| Today | Becomes | Why |
|---|---|---|
| `AUDIO_SAMPLE` intrusive `psPrev`/`psNext` lists, hand-spliced | `std::vector<std::unique_ptr<Sample>>` + `std::erase_if` in `Update` | the `bRemove` deferred sweep is `erase_if`'s exact contract; splice code and its null-pointer discipline vanish |
| `g_apTrack[600]` fixed array, linear scans | `std::vector<std::unique_ptr<Track>>` indexed by ID, growable | IDs stay small dense integers (save/script constraint); `MAX_TRACKS` becomes a sanity bound, not a sizing |
| `BOOL`/`TRUE`/`FALSE`, `SDWORD`-family (243 uses across the module) | `bool`, `std::int32_t`-family | module-local; the rest of the tree keeps its typedefs — no repo-wide churn |
| `AUDIO_CALLBACK` function pointers | `std::move_only_function<bool(Sample&)>` | same call sites work (free functions convert), capturing lambdas become possible for future features |
| `iSample` int with hand-packed generation bits | `struct VoiceHandle { std::uint8_t slot; std::uint32_t generation; }` | the packing scheme becomes types; stale handles stay inert by the same generation test |
| raw `IXAudio2SourceVoice*` + manual `DestroyVoice` | `SourceVoice` RAII wrapper (`std::unique_ptr` + deleter calling `DestroyVoice`) | leak-proof by construction; the engine itself via `winrt::com_ptr<IXAudio2>` (the sanctioned C++/WinRT helpers, R14 exception 2) |
| `mmioOpen`/`mmioDescend` RIFF walking | `WavData::FromBytes` over `std::span<const std::byte>` | **removes the last winmm API use in the tree** — `winmm.lib` comes off both link lines, measured below |
| `CRITICAL_SECTION` ×2 + init flags | nothing, and one `std::atomic<std::uint32_t>` | per [the threading truth](#the-threading-truth-one-lock-guards-against-a-thread-that-no-longer-exists) |
| `new (std::nothrow)` + null-check + `memset` | value-initialised members, `std::vector` | the OOM-returns-null contract mattered to `malloc`-era callers; the module's own callers all treat failure as "no sound", which exceptions-off value-init preserves |
| `#define` constants | `inline constexpr` PascalCase (R3) | type-checked, scoped |
| fixed slot indices `#define XA2_SLOT_*` | `enum class FixedSlot : std::uint8_t { Queue, Stream, Fx, Music }` | `std::to_underlying` at the array edge |
| `char szFileName[]` paths | `std::string_view` in, `std::string` stored | the resource layer hands out `char*`; conversion at the boundary only |
| `wsprintf` in `Music.cpp` | `std::format` | already the house formatter since Phase 7's debug work |

**Deliberately unchanged**, because Phase 4 already got them right and they are
behaviour, not style: the X3DAudio math and axis conventions, the explicit
distance curve, normalise-to-16-bit-mono for the pool, load-whole rather than
streaming (revisit when music assets exist — see
[features](#prepared-for-additional-features-shaped-not-built)), the four-slot
layout, the backend-side pending queue for fixed slots, and the
drain-completions-then-pump `Update` order.

**C++23 actually used**: `std::expected` (decode results),
`std::move_only_function` (callbacks and the provider), `std::span` /
`std::byte` (parsing), `std::to_underlying`, `std::erase_if` and ranges
algorithms where they shorten a loop, designated initializers for the
`WAVEFORMATEX`/`XAUDIO2_BUFFER` fills. **Deliberately not used**: coroutines
(nothing here is async in a way a frame pump doesn't already express), modules
(the build is `.vcxproj` + PCH and this is not the phase to disturb it),
`std::jthread` (the module owns no thread — XAudio2 owns its own), and any
metaprogramming. The measure of this phase is lines removed, not features
exercised.

## Prepared for additional features: shaped, not built

The point of the reshape is that each future feature lands against one seam.
None of these are built in this phase; the design leaves each a socket:

- **Submix buses.** An `enum class Bus { Fx, Speech, Music, Movie }` and one
  XAudio2 submix voice per bus, sends set at voice creation. The FX/music
  sliders become bus gains (today the FX slider is the *mastering* volume, so
  it also scales music and movie audio — a wrong-but-familiar coupling worth
  fixing the day buses exist). Ducking becomes a bus gain too, which would
  also fix a latent quirk: today the duck factor is baked into a sample's
  volume at start, so a sound that starts during speech stays quiet for its
  whole life even after speech ends. Both are behaviour changes, so they are
  a decision, not a default.
- **Effects.** XAPO reverb/EQ attach per submix voice — buses are the
  prerequisite, nothing else changes.
- **More codecs.** `WavData::FromBytes` is the single decode seam. An
  MF-`IMFSourceReader`-backed sibling (the exact pattern `MovieStream.cpp`
  already proves, R14-clean) adds MP3/AAC/WMA music without touching the
  mixer. Music from disk is where this pays first.
- **Real streaming.** If music assets arrive large, the music slot grows a
  ring of buffers refilled from `Update` — confined to `AudioMixer`, invisible
  above it. Phase 4's load-whole decision stands until then.
- **Doppler / velocity.** `AudioWorld` grows an `objectVelocity` accessor and
  the emitter gains a velocity; the X3DAudio call already computes it when
  `DopplerScaler` is nonzero.
- **Pause-all** (menu pause) — `AudioMixer` stops/starts the pool and FX
  slots; the fixed-slot queue machinery already tolerates idle slots.
- **Priorities** — `Track::priority` is retained storage; a future steal
  policy can weigh it against distance without a format change.

## Stages

Ordered so every stage builds, cross-checks and runs on its own, with the big
rewrite fenced in the middle. A–C are preparation on the *existing* code,
which is what makes D reviewable: by the time the rewrite lands, everything it
does not reimplement is already gone.

### A — Sweep the dead surface, fix the two defects  *(behaviour-preserving, plus two visible fixes)*

Delete everything under [the dead surface](#the-dead-surface-measured):
functions, declarations, fields, constants, the `VagID`/`hWnd`/
`iMaxSameSamples` parameters (touching the two generated-parser call sites in
place), the two `printf`s, and the `audio_GetPreviousQueueTrackPos`
uninitialised-read fix. On the order of 200 lines out (an estimate, unlike the
grep evidence itself — the deletions, not their exact count, are what was
measured), each item individually verifiable by the same grep that condemned
it.

### B — Single-thread the bookkeeping  *(small, isolated)*

Remove `critSecAudio` and its lifecycle; replace `g_csFinished` +
`g_bFinishedInit` + mask with one `std::atomic<std::uint32_t>`. This stage is
deliberately alone: if any behaviour shifts, the diff that caused it is twenty
lines, not two thousand.

### C — Collapse `Track.cpp` into `Audio.cpp`  *(behaviour-preserving by construction)*

Fold the track table and its accessors into the sample layer, exactly as
Phase 8 stage B folded `PieState.cpp` into `D3DRender.cpp`. `TrackLib.h`
shrinks to the true backend seam (play/stop/position/volume/update plus track
load/free). The `audio_*` names and every call site above are untouched; one
translation unit and one header disappear.

### D — The rewrite: `AudioSystem` / `AudioMixer` / `WavData`  *(the phase's centre)*

The new module per [the target](#the-target), with the old public names kept
as a **shim header** — `Audio.h` becomes ~60 inline `audio_*` functions
delegating to `AudioSystem`, so the 182 call sites in 43 files do not move in
this stage. `git mv` where a file survives recognisably (`XA2Track.cpp` →
`AudioMixer.cpp`) to keep history. `winmm.lib` comes off both configurations'
link lines when `WavData` lands. `.vcxproj` and `.filters` updated in the same
commits, `tools/check_case.py` run on each.

Ends with the full [listening pass](#verification) against the current
behaviour — this is the stage that can regress something audible.

### E — Sever the upcall  *(the layering fix)*

`AudioSystem::Init` takes the `AudioWorld` provider; `Outpost/GameAudio.cpp`
(from `Aud.cpp`) implements it; `NeuronCore/Aud.h` is deleted. The engine
library stops referencing game symbols. `void*` object references now cross
the boundary only inside the provider the game itself wrote.

### F — Rename the call sites, delete the shim  *(mechanical, gated on an owner decision)*

The Phase 8 stage C move, applied here: `audio_PlayObjDynamicTrack(...)` →
`Neuron::AudioSystem::PlayObjectTrack(...)` and its 181 siblings, then the
shim header goes, then `AudioID`/`Music` take conforming type and function
names. Entirely mechanical, entirely greppable, and the largest diff of the
phase by file count — which is exactly why it is last and why it is a
[decision](#decisions-to-confirm), not an assumption. The script-facing and
save-facing surfaces (constraint items 1–4) keep their observable behaviour
bit-for-bit regardless of what the functions are called.

## Sequencing

- **A–C are independent of everything in flight** and can land now: they do
  not touch `Sequence.cpp`, `SeqDisp.cpp`, `CDSpan.cpp` or any render file.
- **D and E** touch only the module and `Init.cpp`'s init call. No contact
  with Phase 8's render work.
- **F waits for Phase 6 stage B6** (the `CDSpan.cpp` deletion — it holds three
  `audio_*` call sites that would otherwise be renamed and then deleted) and
  should not overlap Phase 8 stage C, purely so two tree-wide renames never
  share a merge window.
- `MovieStream.cpp`'s `sound_GetEngine()` call is renamed in D (one site, one
  line) — coordinate is too strong a word for it.

## Verification

Same regime as Phases 4 and 8, stated plainly:

- `tools/crosscheck.py` both configurations at every stage; `x3daudio.h` still
  arrives via `tools/stubs/` and the new `AudioMixer.cpp` must keep compiling
  against it. GCC 13+ has `std::expected` and `std::move_only_function`;
  if the container's toolchain proves older, that is a stage-D blocker to
  report, not to code around.
- `tools/check_case.py` on every commit that adds, removes or renames a file,
  with the `.vcxproj`/`.filters` edits in the same commit.
- MSVC CI (Debug and Release, Win32) is the authority; the shim header in
  stage D keeps every intermediate commit linkable.
- **Run it.** `Debug\Outpost.exe -window -game CAM_1A`, plus the listening
  pass from [Phase4Plan.md](Phase4Plan.md#verification), which this phase
  inherits wholesale: menu sounds and both sliders; unit acknowledgements
  (queue slot) with the duck audible under them; weapons/explosions panning
  and attenuating as the camera moves; a research-message stream and an FMV
  with audio; music surviving a briefing and staying paused across a video.
  Save/load a campaign game with a script-assigned sound in flight, for
  constraint 1.
- Stage D's diff is the one to hold to the standard of "builds clean, not run"
  never being reported as more than it is.

## Decisions to confirm

1. **The stage-F rename: do it, and when.** Recommendation: yes, as its own
   stage after Phase 6 B6, mirroring the Phase 8 stage C owner decision. The
   alternative — keeping the `audio_*` shim indefinitely — leaves the module
   modern inside and 1998 at every call site.
2. **Facade shape.** Recommendation: `AudioSystem` as a class of static
   methods over TU-internal state, the `Transport` precedent. An instance
   passed around would be more testable still, but 43 consuming files argue
   for the static surface until a second audio consumer exists.
3. **Submix buses now or later.** Recommendation: later, as the first feature
   *on* the new module, because both things buses fix (FX slider scaling
   music/movies; duck baked in at sample start) are audible behaviour changes
   that deserve their own listening pass — and stage D is explicitly a
   parity stage.
4. **`AudioID` values.** Recommendation: keep the plain enum and its exact
   values and order; rename only the type and file. An `enum class` would
   churn several hundred game-code sites for type safety the ID system's
   dynamic half (name-hash lookups) cannot benefit from anyway.
5. **`std::move_only_function` for `AUDIO_CALLBACK`.** Recommendation: yes —
   free-function callers convert silently and future features get captures.
   The conservative alternative (keep the raw pointer type under a new name)
   costs nothing now and one more migration later.

## Measurement

Line counts from `wc -l` at the head of this branch. Call-site figures from
`grep -rE` over `NeuronCore/` and `Outpost/` (`.cpp`/`.h`, generated parsers
included): the 182/43 blast radius counts `audio_[A-Za-z0-9_]+` occurrences in
translation units outside the module itself and `Outpost/Aud.cpp`/`Music.cpp`.
Dead-surface claims: per-identifier tree-wide grep with zero hits outside the
declaring/defining files, cross-checked against the `tools/check_case.py`
feature-macro allow-list. The winmm claim: `waveOut|mciSend|PlaySound|
timeBeginPeriod|timeEndPeriod|timeGetTime|mixer[A-Z]|mmio|joyGet|midiOut`
matches nothing outside `XA2Track.cpp`; `winmm.lib` appears only in
`Outpost.vcxproj`'s two `AdditionalDependencies` lines. Typedef density (243):
`grep -c` of the `BOOL|SDWORD|UDWORD|UBYTE|SWORD` family across the six
NeuronCore module files and `Outpost/Aud.cpp`. XAudio2/X3DAudio behaviour
cited from the Phase 4 record rather than re-derived; nothing in this plan
changes what that phase verified.
