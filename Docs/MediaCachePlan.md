# On-Demand Media Loading: audio tracks and texture pages

This document answers one question: **should the WAV and DDS media leave the
manifest-driven upfront load and move to demand-loaded caches** — an audio
cache and a texture cache, each an `std::unordered_map` keyed by file,
filled the first time the game asks for a file and retained after? It is a
design document in the pattern of [AssetPipeline.md](AssetPipeline.md); no
code or data changes accompany it. Figures were measured on the tree at the
head of this branch (2026-08-17), method: counted from `datasets.json` and
the `GameData/` tree directly.

**All open decisions are settled by owner instruction (2026-08-17)** and
are folded into the design below rather than left as questions: the seven
duplicate `Console/PCV48x.wav` files are deleted, `frontaud.json` is
absorbed into `audio.json` with the in-game track volumes winning — the
plan's one deliberate audible change, §3.1.1 — and this work is an
**unnumbered follow-up** in the pattern of the asset-pipeline stages, since
Phase 10 belongs to the DirectXMath renderer maths. The record, including
one decision re-put after I got a fact wrong, is in
[§5](#5-decisions--settled).

The short answer is **yes, with honest expectations**: the load-time win is
real but modest — the heavy remaining load costs are script recompiles and
PIE parsing, not media ([AssetPipeline.md §3.5](AssetPipeline.md)) — and the
durable wins are architectural: 601 of the manifest's 1,762 entries (34%)
are deleted, the media cache survives campaign switches that today re-read
and re-decode everything, the collision-prone filename-hash keys stop
covering the two highest-count resource types, and the resource system
narrows toward what a headless `NeuronServer` will need. The proposal is
demand loading, **not** async loading — every load stays synchronous on the
game thread, which the file sizes justify (§4.4).

---

## 1. What loads today, measured

| | On disk | In the manifest | Loaded when |
|---|---|---|---|
| `.wav` | 539 files, 12 MB, under `GameData/audio/` | 463 `WAV` entries + 2 `AUDIOCFG` | 9 at frontend boot; 347 (`wrf/audio`) on entering any campaign; +78 tutorial; ~5–30 per mission (briefing speech) |
| `.dds` texture pages | 53 files, 36 MB, under `GameData/texpages/` | 136 `TEXPAGE` entries | 25 entries (~19 pages after the hard/soft variant skip) at frontend boot; 25 per campaign (`wrf/vidmem*`); 5-page swaps in `camchange` sets |

For scale: `datasets.json` has 84 units and 1,762 entries total, so these
two types are a third of it — and they are exactly the "enumerations of
everything in a directory" that [AssetPipeline.md §3.2](AssetPipeline.md)
flagged as the weakest job the manifests do. Music (232 MB) and sequence
audio already stream on demand through `AudioMixer::PlayStream`/`PlayMusic`
(XAudio2 source-voice streaming, `NeuronClient/AudioMixer.cpp:977`); FMV
streams through Media Foundation. The upfront-loaded media is only the two
rows above.

### 1.1 The audio path

A `WAV` manifest entry decodes the RIFF to 16-bit PCM immediately
(`dataAudioLoad`, `Outpost/Data.cpp:891` → `AudioSystem::LoadTrackFromBuffer`
→ `Neuron::WavData`), so entering a campaign decodes all 347 base WAVs into
resident memory whether or not a single one plays. The `AUDIOCFG` entries —
`audio/audio.json` (347 records) and `audio/frontaud.json` (9, all of them
names `audio.json` also carries, §3.1.1) — then attach
per-track metadata and assign IDs (`dataAudioCfgLoad`, `Data.cpp:924` →
`AudioSystem::SetTrackVals`, `NeuronClient/AudioSystem.cpp:438`). ID
assignment: the game's fixed `AudioID.cpp` table wins
(`g_world.trackIdForName`), otherwise the next free slot of the 600-entry
dense table. Everything downstream plays by dense ID.

Three details matter for the design:

- **Registration is already lazy in one place.** A `.vlo` `SOUND` value
  that names an unregistered WAV registers it on the spot with default
  metadata (volume 100, priority 1, radius 1800) —
  `Outpost/ScriptValsParse.cpp:356-363`. Mission briefing WAVs get their
  IDs this way. The pattern this plan proposes is already in the tree.
- **Registration requires the PCM.** `SetTrackVals` starts from
  `resGetData("WAV", …)` (`AudioSystem.cpp:446`), so metadata cannot today
  be attached to a track whose data has not loaded. That coupling is the
  thing to break.
- **`SetTrackValsByHash` has zero callers** — a save/load remnant
  (`AudioSystem.cpp:468`); it goes whenever this area is next touched.

Campaign teardown releases every track (`dataAudioRelease`,
`CheckAllUnloaded` asserted from `Outpost/Init.cpp:1019`) and the next
campaign re-reads and re-decodes the same 347 files.

### 1.2 The texture path

A `TEXPAGE` manifest entry loads eagerly and completely
(`bufferTexPageLoad`, `Outpost/Data.cpp:768`): skip the hard/soft variant
that does not match the renderer mode, re-key the resource to its bare
`page-NN` id, decode the DDS to a system-memory `iSprite`, and immediately
claim a `_TEX_PAGE` slot **and create the D3D9 texture**
(`pie_AddBMPtoTexPages` → `dtm_LoadTexSurface`). Models then bind pages by
name at PIE load (`POST_LEVEL_TEXTURELOAD`, `NeuronClient/IMDLoad.cpp:397`)
through `Neuron::TexLoadNew` (`NeuronClient/Tex.cpp:107`), which finds the
already-created slot — or fatals if the page was never a resource
(`Tex.cpp:122`; the from-disk fallback `TexLoad` below it is unreachable in
the shipped configuration).

Details that shape the design:

- **The `page-NN` name → file binding is level data, not a convention.**
  Pages 6/7/8/9 name different files per tileset (`vidmem` arizona,
  `vidmem2` urban, `vidmem3` rockies, `vidmemc` kevlar), while pages 10–24
  are set-independent. A `camchange` dataset swaps exactly the
  tileset-dependent pages by re-listing their files, which lands in
  `pie_ReloadTexPage` (`Tex.cpp:156`): overwrite the existing slot's bitmap
  in place, keep the index — that is what keeps `psShape->texpage` indices
  in already-loaded models valid across the campaign-2 switch. Any lazy
  design must reproduce this rebind-and-refresh, and must not require the
  slot to exist (today `pie_ReloadTexPage` fatals if it does not).
- **Slot indices are load-bearing.** `TEX_MAX` is 32 (`Tex.h:13`), the
  radar page is pinned at index 31 (`RADAR_TEXPAGE_D3D`,
  `PieState.h:71`), and terrain pages are appended after the model pages
  (`firstTexturePage = pie_GetLastPageDownloaded() + 1`,
  `Outpost/Texture.cpp:174`). Slots are never compacted, so demand-created
  slots stay valid; only the *assignment order* changes under laziness.
- **The system-memory copies are the device-reset backing store.**
  `dtm_ReloadAllTextures` re-uploads from `_TEX_PAGE[i].tex.bmp`
  (`TexMan.cpp:134`), which aliases the `TEXPAGE` resource's sprite. A lazy
  cache must keep owning decoded sprites for exactly this reason — the
  cache *is* the backing store, same bytes as today under a different
  owner.
- **The frontend loads ~19 pages it barely uses.** `wrf/frontend` lists
  the same 25 `TEXPAGE` entries as `wrf/vidmem` (the duplication
  [AssetPipeline.md §3.3](AssetPipeline.md) called out), but the frontend
  loads no models (`wrf/piestats` is not in its unit) — the pages are
  decoded and uploaded for nothing. This is the purest waste the change
  removes.
- Terrain tiles (`HWTERTILES`, one large DDS split into 256×256 device
  pages, `Data.cpp:683`) are a separate mechanism with working
  reload-in-place semantics. They stay as they are in this plan.

### 1.3 A data defect found while surveying: duplicate WAV names

`GameData/audio/MemResSp/MissMesg/` and `…/MemResSp/Console/` both hold
`PCV480.wav`–`PCV486.wav` (seven names), **with different content** (md5s
differ pairwise). The manifest references only the `MissMesg` copies; the
`Console` copies are unreachable data today — nothing in the tree can load
them, and nothing ever did once the WRFs settled on `MissMesg`. This is
also a live hazard in the current tree: resource keys are bare-filename
hashes, so if both copies were ever listed, the second would silently be
skipped ([AssetPipeline.md §2.2](AssetPipeline.md)).

**Resolved by owner decision (2026-08-17): the seven `Console` copies are
deleted** as part of stage A, git history keeping them. That leaves the
`GameData/audio/` tree free of duplicate bare names, which is what lets the
index treat a duplicate as an error rather than a case to arbitrate (§3.1)
— the alternative, an explicit winner-per-name table in the data, would
have made seven files permanently special.

---

## 2. Assessment: is the proposal sound?

**Yes.** WAV and DDS are the right two candidates — they are the only
upfront-loaded types whose identity is a plain file and whose consumers
already ask by name at run time (`TrackId(fileName)`,
`TexLoadNew(pageName)`). They sit at the leaves of the load graph: nothing
orders against them the way stats tables order against messages and
research, so removing them from the manifest does not disturb the load
phases that remain. And the file sizes make synchronous demand loads
unnoticeable: a 256 KB page or a 50 KB WAV is well under a millisecond from
a modern disk plus a memcpy-shaped decode (`Dds.cpp` is "a validation and a
copy"; `WavData` is a linear PCM conversion).

What it buys, in decreasing order of value:

1. **601 manifest entries deleted** (463 WAV + 136 TEXPAGE + 2 AUDIOCFG) —
   the last of the bulk hand-maintained enumerations, and with them the
   `wrf/audio`, `wrf/tutorial/tutaudio` and four `wrf/vidmem*` units and the
   frontend's pasted copy of the vidmem list.
2. **The cache outlives the level.** Campaign switches and returns to the
   frontend stop re-reading and re-decoding 12 MB of PCM and the page set.
   (Within a campaign the base data already persists; across campaigns it
   does not.)
3. **Keyed maps instead of hashed linked lists** for the two
   highest-count resource types — the exact "loader hygiene" item of
   [AssetPipeline.md §5.7](AssetPipeline.md), including full-path keys in
   place of the 28-bit case-folded bare-filename hash and its silent
   collisions.
4. **Faster frontend boot and campaign entry** — ~19 unused pages and 9
   WAVs at boot, 12 MB of eager PCM per campaign entry. Real, but modest
   against the script-recompile and PIE-parse costs that dominate.
5. **A cleaner client/server split.** WAV/DDS handling moves wholly into
   `NeuronClient` code paths; the shared resource system stops naming
   media types a headless server will never load.

What it costs, honestly:

- **Failure surfacing moves from the load screen to mid-play.** Today a
  missing WAV fatals under the loading bar; lazily it surfaces at first
  play. The mitigation is the same one the manifest conversion used:
  `tools/validate_assets.py` in CI proves every referencable name resolves
  to a file *before* a build ships (§4.5). With that in place the runtime
  path is defense in depth, not the first line.
- **First-use hitches.** Bounded by file size (§4.4); if a hitch were ever
  measured, the lever is a prefetch pass over `audio.json` during the
  loading bar — kept as an option, not a default, because it recreates the
  upfront load this plan removes.
- **Two new pieces of machinery to own** (a path index and two caches) —
  small, but they replace resource-system behavior that is currently
  uniform across all 45 types with type-specific behavior for two of them.
  The trade is deliberate: these two types were already the least
  uniform (eager device uploads, re-keying, variant skips, config
  sidecars).

Non-goals, so the scope is unambiguous: no async or streaming (the doc's
standing position, [AssetPipeline.md §5](AssetPipeline.md), unchanged); no
eviction policy (worst case ~50 MB resident if every file is eventually
touched — acceptable, and eviction can be added to a cache without
changing its callers); music/FMV/sequence audio untouched (already
streamed); `.pie`, `IMGPAGE` atlases, backdrops and terrain tiles untouched
(candidates for the same cache later, §3.4); no new dependencies
(`std::filesystem` and `std::unordered_map` are the MSVC standard library —
R14-clean).

---

## 3. Design

On naming: the request said "an `AudioManager` class and a
`TextureManager` class". The texture side gets exactly that (spelled
`TextureCache`, since R2 asks the name to say what the thing is — it
caches; `TexMan.cpp` already answers to "texture manager"). The audio side
deliberately does **not** get a new class: `AudioSystem` *is* the audio
manager — it already owns the track registry, the ID space and every play
entry point — and a second manager beside it would split one
responsibility across two owners. What audio needs is two internal
additions, not a new surface.

### 3.1 Audio: the registry decouples from the data

`AudioSystem` gains, internal to the module:

- **A name → path index**, built once at `Init` by walking
  `GameData/audio/` recursively (`std::filesystem`, exact on-disk names
  kept so the case-exactness rule stays checkable). 532 entries after the
  seven deletions of §1.3. **A duplicate bare name is a startup `Fatal`**,
  with no arbitration table and no exceptions — replacing today's silent
  hash dedupe with a loud check, and the reason the `Console` copies go
  rather than get resolved in data.
- **Lazy PCM on `TRACK`.** A registered track holds metadata and a path;
  `pMem` may be null. Every play/queue entry point passes through one
  `EnsureLoaded(id)`: if the PCM is absent, read the file (a per-load
  buffer, not `DisplayBuffer`), decode through `WavData`, keep it for the
  life of the process. The decoded-track map keyed by path *is* the audio
  cache; 12 MB is its ceiling.

Registration reroutes to the same three sources it has today, minus the
resource system:

- **One `audio/audio.json`, parsed once at `Init`** — the two configs
  leave the manifest with the `AUDIOCFG` type, and `frontaud.json` is
  absorbed into `audio.json` (owner decision, §5). The merge is **not** a
  concatenation: all nine `frontaud.json` records name WAVs `audio.json`
  already names, so the merged file stays 347 records and the frontend's
  nine values are dropped (§3.1.1). Fixed IDs still come from the
  `AudioID.cpp` table via `trackIdForName`; the handful of names without a
  fixed ID take dense slots exactly as now, just at init instead of at
  campaign entry. Track IDs thereby become stable for the process
  lifetime — an improvement with no dependents to break (nothing persists
  IDs since the save/load removal).

#### 3.1.1 The one deliberate behaviour change

The two configs overlap completely, and today they **alternate**: the
frontend's block registers the nine with `frontaud.json`'s values, level
load releases them, `wrf/audio` re-registers them with `audio.json`'s, and
returning to the frontend swaps back. (That alternation is load-bearing
today — `RegisterTrack` fatals on a double registration,
`AudioSystem.cpp:493`, so the release between the two is what keeps the
game from dying on campaign entry.) Init-time registration happens once, so
one set of values must win for the process.

Six of the nine differ only in `iPriority`, which is **write-only** —
assigned in `RegisterTrack` and read nowhere since Phase 4 started stealing
voices by distance (`Track.h:50`, verified across the tree). Merging those
six is a true no-op. Three differ in `iVol`, which *is* read
(`SampleMixVolume`, `AudioSystem.cpp:873`):

| WAV | `AudioID` | In game | Frontend |
|---|---|---|---|
| `Beep4.wav` | `ID_SOUND_SELECT` | 15 | 30 |
| `Beep9.wav` | `ID_SOUND_MESSAGEEND` | 100 | 30 |
| `GmeShtDn.wav` | `ID_SOUND_GAME_SHUTDOWN` | 100 | 30 |

**Owner decision: the in-game values win everywhere** and the frontend's
are dropped, rather than carrying a per-context override to preserve both.
So in the frontend the select beep gets quieter (30 → 15) and the
message-end and shutdown sounds get louder (30 → 100). This is the one
audible change in the plan; it is deliberate, and it is on the stage A
checklist to confirm by ear on a Windows run (§4).
- `.vlo` `SOUND` values keep their register-on-demand fallback, now
  against the index instead of `resGetData` — same defaults, same IDs.
- Stats loaders are untouched: they resolve names against the `AudioID`
  table only (`statsGetAudioIDFromString`, `Outpost/Stats.cpp:2243`).

Deleted: the `WAV` and `AUDIOCFG` resource types and their loaders
(`dataAudioLoad/Release/CfgLoad`), 465 manifest entries, the `wrf/audio`
and `wrf/tutorial/tutaudio` units and every per-mission WAV entry,
`SetTrackValsByHash` (no callers), and the per-campaign release/reload of
tracks — `CheckAllUnloaded` becomes a shutdown-only assertion.

### 3.2 Textures: bindings become data, pixels become demand

Two pieces:

- **Texture sets in `datasets.json`.** A named table per tileset —
  `arizona`, `urban`, `rockies`, `kevlar` — mapping page id → file list
  (the hard/soft variant pair stays two files filtered at load, exactly
  today's rule, so no new semantics). Campaign and `camchange` datasets
  name their texture set; the four `wrf/vidmem*` units, the `camchange`
  page entries and the frontend's copy all collapse into these four
  tables. `tools/convert_manifests.py` grows a one-shot emitter for them
  from the current units.
- **`Neuron::TextureCache`** (`NeuronClient/TextureCache.h/.cpp`): an
  `unordered_map` from file path to decoded `iSprite`, process-lifetime,
  owning the system-memory copies (thereby staying the device-reset
  backing store — `_TEX_PAGE[i].tex.bmp` points at cache-owned pixels,
  the same aliasing as today with a safer owner). On top of it, the two
  operations the game needs:
  - `PageIndex(name)` — the demand path behind `TexLoadNew`'s existing
    seam: slot exists → return it; else resolve the name through the
    active texture set, read the file through the cache, and claim the
    slot + device texture (`pie_AddBMPtoTexPages`, unchanged). The
    `Fatal("Texture not in resources")` branch and the dead `TexLoad`
    fallback go.
  - `SetActiveTextureSet(set)` — at dataset load: diff the new bindings
    against the current ones; a changed page that has a slot refreshes in
    place (today's `pie_ReloadTexPage` semantics); a changed page without
    a slot just rebinds and loads lazily if ever asked. This reproduces
    the campaign-2/3 `camchange` five-page swap without requiring
    presence.

Slot behavior is deliberately unchanged: `TEX_MAX` 32, radar pinned at 31,
slots never compact, terrain pages still append after whatever model pages
exist. Assignment order changes from manifest order to first-use order,
which nothing depends on (§1.2) — and demand creation only lowers slot
pressure, since pages no loaded model references never claim one. The
frontend stops creating pages at all.

Deleted: the `TEXPAGE` resource type (`bufferTexPageLoad`,
`dataTexPageRelease`), 136 manifest entries, the vidmem units, and
`pie_ReloadTexPage`'s must-exist constraint. `HWTERTILES`/`TERTILES` stay
exactly as they are.

### 3.3 What deliberately does not change

The load-time model binding (`TexLoadNew` at PIE load) stays — meaning in
practice most page loads still happen during the loading bar, just only
for pages actually referenced; "demand" mostly means "referenced", not
"first drawn frame". Same for audio: registration at init is cheap; only
PCM defers. This keeps the change behavior-preserving where behavior is
visible: no pop-in, no silent frames, no new mid-mission I/O beyond a
first-play read measured in tenths of a millisecond.

### 3.4 Later, if wanted (not this plan)

`IMGPAGE` atlases and the frontend backdrops read DDS through their own
paths and could share `TextureCache`'s byte cache; terrain tile sources
likewise. A loading-bar prefetch lever (§2) exists if a first-play hitch
is ever measured. None of it blocks on, or is blocked by, this plan.

---

## 4. Staging

**Stage A landed (2026-08-17).** What it did, and where it deviated from
the design below:

- `AudioSystem` indexes `GameData/audio` at `Init` (532 files, keyed by
  lower-cased bare filename) and registers the 347 tracks `audio.json`
  names; `TRACK::pMem` stays null until `PlayTrackOnSample` — the one
  choke point every play path already funnelled through — reads and
  decodes the WAV. The `WAV` and `AUDIOCFG` resource types, their three
  loaders, and 465 manifest entries are gone, taking the `wrf/audio` and
  `wrf/tutorial/tutaudio` units with them (eight datasets lost a slot).
  `frontaud.json` is deleted; `audio.json` is unchanged at 347 records,
  which is the whole of the merge (§3.1.1).
- Tracks are the audio module's own now: a `TrackSlot` owns its `TRACK`,
  its name and its path for the life of the process, where the resource
  system used to allocate and free them per block.
- **Deviations.** `CheckAllUnloaded` was deleted rather than made
  shutdown-only as §4 proposed: it asserted that every track had been
  released at block teardown, which is precisely the invariant this change
  removes, and after `Shutdown` frees the slots the assertion would be
  tautological. `TrackHashName` and `SetTrackValsByHash` went too — both
  had zero callers, and with them `TRACK::resID`, so the hash half of the
  registry is gone rather than left dangling. `AudioMixer::LoadTrack`
  downgraded its undecodable-WAV `Fatal` to a trace: decoding now happens
  mid-mission, where killing the run over one bad effect is the worse
  trade. `loadFile`/`loadFile2` took `const STRING*` so the
  `/permissive-` client library can pass a literal without the
  `const_cast` R16 forbids.
- **Verified:** `crosscheck.py` 180/180 clean in Debug and Release,
  `check_case.py`, `check_scripts.py` (59 compiled, 123 value files) and
  `validate_assets.py` (0 errors) all green; a container-side harness ran
  the index logic over the real tree (532 indexed, 0 duplicates, all 347
  config records resolving and readable) and a negative case confirmed the
  duplicate-name refusal fires across case. An offline simulation of
  init-time registration found 0 ID collisions and no track whose id
  exceeds `g_trackCount`. **None of it has been run** — this container
  cannot launch the game, per the standing caveat; the run checklist below
  is outstanding.
- **Found while doing it:** eight names in `AudioID.cpp` have no file on
  disk (`CybGrnd`, `EMP`, `HevLsr`, `HvCybMov`, `LasStrk`, `PlasFlm`,
  `UpLink`, `VtolMove`), so those sounds are silent. Only `VtolMove` was
  visible before, as a stats cross-reference warning; the validator now
  names all eight. They are content gaps that predate this work and are
  left recorded, not fixed.


Both stages follow the migration's standing pattern: independently
shippable, converter does the data work, validator proves it in CI,
`tools/crosscheck.py` and the MSVC CI build gate the code, and a run on a
real Windows box is the final word (`Debug\Outpost.exe -window -game
CAM_1A`, plus for stage B: a campaign-2 mission across the `camchange`
boundary, the force editor, and a device reset via alt-tab in fullscreen).

- **Stage A — audio.** The index, lazy PCM, configs-at-init, the `.vlo`
  reroute; absorb `frontaud.json` into `audio.json` and delete the seven
  `Console/PCV48x.wav` duplicates; delete the `WAV`/`AUDIOCFG` types and
  their 465 entries; validator learns to build the same index offline,
  **error on any duplicate bare name**, and check every referencable
  name — `audio.json`, the `AudioID.cpp` table, stats sound fields, `.vlo`
  `SOUND` strings — against the filesystem, case-exact. (The known
  `VtolMove.wav` defect stays a recorded warning.) The two data edits are
  the one place this stage touches `GameData/` binaries, so they land in
  their own commit ahead of the code, where the deletion is reviewable on
  its own.

  **Stage A's run is not just `-game CAM_1A`.** Because registration moves
  from per-block to once-at-init, the run has to cross the boundaries that
  used to re-register: frontend → campaign → frontend, a mission briefing
  (the `.vlo` `SOUND` lazy-registration path), and a second campaign entry
  without restarting (the case where the old code re-read 12 MB and the
  new code must not re-register). Listen for the three changed volumes of
  §3.1.1 in the frontend while there.
- **Stage B — textures.** Texture-set tables emitted by the converter;
  `TextureCache`; demand slots behind `TexLoadNew`; set-diff rebinding;
  delete the `TEXPAGE` type and its 136 entries; validator learns the
  tables (every page id any shipped `.pie` `TEXTURE` directive names must
  resolve in every texture set, both variants present where the pair
  convention says so).

**Stage B landed (2026-08-17).** What it did, and where the design below
turned out to be wrong:

- `datasets.json` grew a `texturePages` table of five groups —
  `arizona`, `urban`, `rockies`, `kevlar` and `rockies-kevlar`, the last
  three extending another — and each of the eight units that listed pages
  now carries one `TEXSET` entry naming a group, in the place its
  `TEXPAGE` run occupied. 136 entries became 8. `Neuron::TextureCache`
  (new, `NeuronClient`) holds the bindings and the decoded pixels; the
  `TEXPAGE` resource type, `bufferTexPageLoad`, `dataTexPageRelease`,
  `Neuron::TexLoad`, `pie_ReloadTexPage` and the `TEXTUREPAGE` struct are
  gone, and `TexLoadNew` is now a lookup into the cache.
- **The design's premise about laziness was wrong, and the mechanism
  changed accordingly.** §3.2 argued demand creation "only lowers slot
  pressure, since pages no loaded model references never claim one". In
  fact **all 19 page ids are named by shipped `.pie` files**, so a campaign
  loads every one either way and laziness buys nothing there. What it does
  buy is the front end, which loads no models until the force editor is
  entered. So creation is eager where the manifest created — a `TEXSET`
  carries `"create": true` by default — and the front end's entry sets
  `"create": false`, binding the pages the force editor needs without
  building any. Demand creation via `PageIndex` is the path that then
  serves it.
- **That also removed the plan's main risk.** Creating pages where the
  `TEXPAGE` entries were keeps page creation ahead of the terrain tiles
  appended after it, so slot assignment order is unchanged — where fully
  lazy creation would have flipped model and terrain pages around. (The
  flip looked safe: `pageId[]` holds real slot indices and
  `firstTexturePage` feeds only `freeTileTextures`, whose loop never runs
  because `numTexturePages` is assigned after a `goto` that always jumps
  past it. Unchanged order means none of that has to be relied on.)
- **The tileset model is richer than "one set per campaign".**
  `wrf/cam1/cam1kevlar` is a *per-level* page override used by twelve
  campaign-1 datasets, swapping page-7 to the Kevlar barbarians; and
  `cam3change` binds rockies' pages *plus* that Kevlar page-7, where
  starting campaign 3 fresh through `vidmem3` binds the Arizona one. The
  two routes into campaign 3 genuinely differ in the shipped data. That is
  preserved exactly — `rockies-kevlar` exists for it — rather than
  normalised away, which would have been a content decision.
- **Verified:** `crosscheck.py` 181/181 in Debug and Release; `check_case`
  and `validate_assets` (0 errors) green. A parity harness replays
  `Manifest.cpp`'s group resolution against the deleted `TEXPAGE` entries
  at the previous commit and confirms all eight units bind the same
  page→file map, in the same order, for **both** translucency settings; a
  second harness drives `TextureCache`'s state machine over the real files
  and confirms a fresh set creates each page once in binding order, a
  re-applied set does nothing, a camchange refills exactly the changed
  pages without moving a slot, switching back re-uses cached pixels, and
  binding without creating creates nothing until first use. **Not run** —
  the container cannot launch the game.
- **Found while doing it:** the manifests spell one texture two ways
  (`cam3change`'s `Page-7-Barbarians-Kevlar.dds` against `vidmemc`'s
  `page-7-barbarians-Kevlar.dds`), which a path-keyed cache would decode
  twice and treat as a rebind; the cache keys case-insensitively, as NTFS
  resolves. Separately, releasing base data used to free the pixels
  `_TEX_PAGE[].tex.bmp` still pointed at, and reloading it allocated fresh
  slots rather than reusing them; cache-owned pixels and stable slots end
  both.

A is independent of B and lands first — it is the larger entry-count win
and touches no renderer seam while Phases 8 and 10 are in flight (R13:
`Tex.cpp`/`TexMan.cpp` are stage B's only render-adjacent files, and it
changes their internals behind existing signatures, not their callers).

## 5. Decisions — settled

All three were put to the owner and answered on 2026-08-17, before any
implementation. They are recorded here and already folded into §1–§4, so
nothing in this plan is waiting on an answer.

1. **The seven duplicate WAV names** (§1.3) — **delete the unreachable
   `Console/PCV480–486.wav` copies.** They differ in content from their
   `MissMesg` namesakes but nothing in the tree can reach them, and git
   history keeps them. Deleting is what allows a duplicate bare name to be
   a flat error in both the runtime index and the validator (§3.1), rather
   than seven files needing an arbitration table forever. This is the only
   binary-data edit in the plan; it lands in its own stage A commit.
2. **The audio configs** stay in `GameData/audio/` — they are audio data,
   not manifest data — and **`frontaud.json` is absorbed into
   `audio.json`**, which stays 347 records.

   *This decision was first put to the owner on a premise that was wrong.*
   I stated the two files did not overlap; in fact all nine `frontaud.json`
   records name WAVs `audio.json` also names, and today the two alternate
   by block rather than covering different sounds. Re-put with the
   corrected facts (§3.1.1): six of the nine differ only in the dead
   `iPriority` field, and for the three that differ in the live `iVol`,
   **the owner chose the in-game values everywhere**, accepting that the
   frontend's select beep gets quieter and its message-end and shutdown
   sounds get louder, in preference to carrying a per-context override.
   The merge decision itself stands; only its cost is now stated
   correctly.
3. **Numbering** — this is an **unnumbered follow-up**, not a phase.
   `Docs/MediaCachePlan.md` is the record, in the pattern the
   asset-pipeline stages set; `Docs/MigrationPlan.md` gets a pointer to it
   when stage A lands. Phase 10 is the DirectXMath renderer maths and this
   work claims no number, consistent with
   [AssetPipeline.md decision 6](AssetPipeline.md).
