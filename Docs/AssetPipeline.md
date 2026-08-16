# Asset Pipeline: Survey and Design Proposal

This document records how game data is loaded today — the `.wrf` manifests,
the `.PIE` models, the `.ANI` scripts, the stats tables and everything else
under `GameData/` — what is wrong with it, and a staged proposal for
simplifying it. The two questions it was written to answer: **can the `.wrf`
layer be reduced or removed**, and **where does moving to JSON pay for
itself**. It is a design document; no code or data changes accompany it.
Figures quoted were measured against the tree at `d314720`, not estimated.

Defects noticed during the survey are recorded in
[Appendix B](#appendix-b--defects-noticed-during-the-survey) but deliberately
not fixed here.

---

## 1. Inventory

`GameData/` is ~550 MB, but almost all of it is media: 288 MB of `.mp4`
sequences (Phase 6's output) and 232 MB of music. The data this document is
about — everything that is parsed rather than decoded — is under 30 MB:

| Format | Files | Size | What it is | Parsed by |
|---|---|---|---|---|
| `.wrf` | 89 | ~180 KB | Load manifests (1,795 `file` entries, 344 `directory` entries) | MKS-yacc parser, `NeuronCore/resource_y.cpp` |
| `.lev` | 1 | 24 KB | `GameDesc.lev` — 101 dataset declarations naming which WRFs each level loads | flex lexer + hand-written state machine, `Outpost/Levels.cpp:155` |
| `.pie` | 516 | 570 KB | 3D models, all `PIE 2` text format | `NeuronCore/IMDLoad.cpp` |
| `.ani` | 10 | 20 KB | Keyframe animation scripts (+ `anim.cfg`) | MKS-yacc parser, `NeuronCore/parser_y.cpp` |
| Stats `.txt` | 210 | 416 KB | Positional CSV: weapons, bodies, structures, research… | hand-rolled `sscanf` loops, `Outpost/Stats.cpp` et al. |
| Messages `.txt` | ~102 | 592 KB | View data (briefings, proximity) + string tables | `sscanf1` cursor parser + `StrRes` yacc grammar |
| `.slo` / `.vlo` | 70 / 134 | 1.9 MB | Script source + typed value files, **compiled from source on every load** | yacc script compiler, `NeuronCore/Script_y.cpp` |
| `.wav` | 555 | 12 MB | Audio (+ `audio.cfg` / `frontaud.cfg` track configs) | `WavData.cpp` (modernised, Phase 9) |
| `.pcx` | 74 | 9 MB | Texture pages, UI atlases, backdrops (+ `palette.bin`) | `NeuronCore/Pcx.cpp` |
| `.img` / `.jbf` | 2 / 2 | — | Sprite-atlas indexes / **Paint Shop Pro thumbnail caches (dead)** | `BitImage.cpp` / nothing |
| `.map` / `.gam` / `.bjo` / `.ttp` | 42 / 49 / 194 / 41 | ~1 MB | Map tiles, save headers, binary game objects, terrain types — raw struct images | `Outpost/Map.cpp`, `Outpost/Game.cpp` |
| `.tag` | 46 | 2 KB | **Dead.** No reader or writer in the tree; content is uninitialised 1998 heap memory | nothing |
| `keymap.map` | 1 | 20 KB | Serialised keybindings, raw struct dump keyed to build timestamp | `Outpost/KeyEdit.cpp` |
| `sequences.json` | 1 | 92 KB | Build manifest from the Phase 6 movie conversion — **the only JSON, and no C++ reads it** | `tools/convert_sequences.py` |

Two incidental findings about the layout itself: the `GameData/WRF/`
directory is doing double duty — besides the 89 manifests it holds the
campaign missions' `.map`/`.gam`/`.bjo`/`.ttp` data (105 `.bjo` files live
under `WRF/CamN/<mission>/`); and the shipped binary data contains
uninitialised memory from the 1998 tools that wrote it — `Feat.bjo` files
carry recognisable Win32 heap pointers, every `TagList.tag` is 36 bytes of
stack garbage, and `keymap.map` pads names with MSVC's `0xCC` debug fill
(details in Appendix B).

## 2. How loading works today

### 2.1 The flow

```
WinMain
 └ systemInitialise                        Outpost/Init.cpp:667
     loadFile("GameDesc.lev") → levParse   Init.cpp:682   (direct fopen, not WRF)
     dataInitLoadFuncs()  — registers 46 resource types   Init.cpp:734
 └ GS_TITLE_SCREEN → resLoad("wrf\frontend.wrf", block 0)
 └ GS_NORMAL       → levLoadData(levelName, …)            Outpost/Levels.cpp:561
       for each of ≤9 base-dataset WRFs:  resLoad(file, blockID i)
       for each of ≤9 level WRFs:         resLoad(file, blockID i+9)
```

A `.lev` dataset names up to nine files (`LEVEL_MAXFILES`,
`Outpost/Levels.h:11`); slot order in the file **is** the load order, and one
slot is the `.gam` map. `levLoadData` (`Outpost/Levels.cpp:561-924`) is a
364-line function that interleaves resource loading with the three
`stageNInitialise` engine phases and branches on five axes (base data present,
save name present, save type, camchange, dataset type).

`resLoad` (`NeuronCore/FrameResource.cpp:116`) parses a WRF with a generated
yacc parser, and **loading happens inline in the grammar action**
(`NeuronCore/Resource.y:88-97`): each `file TYPE "name"` line synchronously
reads the file into a shared scratch buffer and runs the registered loader
before the parser advances to the next line. There is no manifest object, no
deferral, no dependency graph, no background thread. Progress feedback is a
per-file callback that repaints the loading bar.

### 2.2 The resource system

`Outpost/Data.cpp:1175-1202` registers 46 types — from `IMD` and `WAV`
through 23 stats-table types to `SCRIPT`/`SCRIPTVAL` (the full table is
[Appendix A](#appendix-a--registered-resource-types)). Loaded resources are
stored in **nested singly-linked lists** (type list → per-type data list,
`NeuronCore/FrameResource.h:29-60`); every `resGetData` is a linear scan.
Keys are a 28-bit case-folded PJW hash of the **bare filename** — the
directory is not part of the key, and the case fold (`c & 0xdf`,
`NeuronCore/Frame.cpp:819`) mangles `-`, `.` and space, which the data set
uses heavily (``page-21-fx`s-hard.pcx``). Nothing detects collisions; a
collision silently returns the wrong resource.

Lifetime is managed by numeric block IDs stamped on each resource at load:
`0..8` base-dataset slots, `9..17` current-level slots, `500..502` the force
editor, with frontend and the single-WRF fallback both reusing `0`. The IDs
are magic numbers assigned at call sites; there is no central registry.
`resReleaseBlockData(id)` walks every resource of every type per call, and a
level teardown calls it nine times.

### 2.3 The second loading path

Roughly a third of the data never touches the resource system: maps, saves
and `.bjo` objects, `TTypes.ttp`, `palette.bin`, `keymap.map`, music,
sequence audio and subtitles, backdrops, multiplayer forces and stats are all
loaded by direct `fopen`/`CreateFile` with hard-coded `"\\"`-separated paths
(`Outpost/Game.cpp:1649`, `Outpost/SeqDisp.cpp:739`,
`NeuronCore/Render2D.cpp:534`, …). Most of them share one global 1.5 MB
scratch buffer (`DisplayBuffer`, `Outpost/Init.cpp:714-722`) with a fatal
error as the overflow strategy — and `mapLoad`'s error paths `delete[]` that
shared buffer (`Outpost/Map.cpp:468,478,489`).

Path case in this second path only works because NTFS is case-insensitive:
the loader asks for `game.map`/`templ.bjo`/`feat.bjo` where the disk has
`Game.map`/`Templ.bjo`/`Feat.bjo`, and `Game.cpp`'s own load and save paths
spell `CompL.bjo`/`compL.bjo` differently (`Outpost/Game.cpp:2156` vs
`:2661`). `tools/check_case.py` polices exactly this class of bug for
`#include` lines but does not look at data paths.

### 2.4 What the parsers are made of

Six separate grammar stacks parse the text formats: `Resource.l/.y` (WRF),
`Level.l` + a hand-written state machine (`.lev`), `parser.l/.y` (audio
config, anim config **and** `.ani` bodies, one shared grammar),
`StrRes.l/.y` (string tables), `script.l/.y` (`.slo`), `ScriptVals.l/.y`
(`.vlo`). All were generated with **MKS lex/yacc, which the project no
longer has**. Only the generated `.cpp` files build
(`NeuronCore.vcxproj:263-264` lists the `.l`/`.y` as `None`), and the
generated files have been hand-edited since — `parser_y.cpp:748` calls the
Phase 9 C++ audio API while its "source" `parser.y:79` still calls the
deleted C one. **Any grammar change now means hand-editing 1990s generated
C.** The stats tables use no grammar at all: hand-rolled `sscanf` loops with
three different cursor-advance idioms (`Outpost/Stats.cpp:727`,
`NeuronCore/IMDLoad.cpp:49`, `Outpost/Research.cpp:264`).

## 3. The WRF layer, measured

### 3.1 The format is two directives

```
directory "structs"
file      IMD  "blpower0.PIE"
```

That is the entire language (`NeuronCore/Resource.y:51-97`): `directory`
sets a sticky current directory, `file` names a type and a filename. No
includes, no variables, no conditionals, no versioning. The 89 files divide
into four base/bulk manifests (`Basic.wrf` 87 entries, `PieStats.wrf` 409,
`Stats.wrf` 39, `audio.wrf` 348), four mutually-exclusive texture sets
(`VidMem*.wrf`), `frontend.wrf` (48), `forcedit2.wrf` (46), per-campaign
string/research sets, and 52 tiny per-level files of 5–14 entries each
(scripts, briefings, the occasional texture swap).

### 3.2 What a WRF actually does

Three jobs, and each is done the weakest possible way:

1. **Enumerate** what a dataset needs. But 1,103 of the 1,795 entries (61%)
   are in the always-loaded base manifests — enumerations of *everything in a
   directory* that must be hand-maintained as art is added or removed.
2. **Type** each file so the right loader runs. But the type is derivable
   from the filename for essentially every entry — `.pie`→`IMD`,
   `.wav`→`WAV`, `.ani`→`ANI`, and the stats types are keyed to fixed
   filenames (`Weapons.txt` is always `SWEAPON`).
3. **Order** the loads so cross-references resolve. The ordering rules are
   real — `IMG` after `IMGPAGE`, weapon sounds after weapons, messages
   before research — but they are **enforced nowhere and documented only as
   comments in the data** (`GameData/WRF/frontend.wrf:7`,
   `GameData/WRF/Stats.wrf:23-44`).

### 3.3 Duplication is structural, and a hack makes it survivable

Because the format cannot express sharing, shared content is pasted:
`cam1daynight.slo` is listed in **24** different WRFs, `genexp.slo` in 21,
`frontend.wrf` reproduces `VidMem.wrf`'s entire 23-page TEXPAGE list plus
nine WAVs from `audio.wrf`, `forcedit2.wrf` re-lists 20 IMDs from
`Basic.wrf` and 22 stats files from `Stats.wrf`, and the three campaign
research WRFs are the same eight filenames under different `directory`
lines. What makes this tolerable at runtime is a silent-failure hack:
`resLoadFile` (`NeuronCore/FrameResource.cpp:471-483`) sees a filename hash
it already has and returns success without loading — commented in the code
as *"assume that they are actually both the same and silently fail"*. The
check ignores the directory, so two genuinely different files with one name
resolve to whichever loaded first.

### 3.4 Half the machinery is dead

The WRF path drags a WDG-archive layer that has nothing to serve — there are
zero `.wdg` files in the tree, yet every `resLoad` calls
`WDG_ProcessWRF` (`FrameResource.cpp:137`), a 2 MB WDG read cache is
allocated at startup (`FrameResource.cpp:79`) and never used, and
`addon.lev` discovery scans archives that cannot exist
(`Outpost/Init.cpp:689-701`). The `SAVEGAME` resource type is registered
through a file-load mechanism that is compiled out (`NORESHASH` is defined
nowhere), so the one WRF that uses it (`minimal.wrf`) hits a fatal error by
construction. `PRIMCATALOG`, `COVERMOUNT`, `FINALBUILD`, `BINARY_PIES` guard
further swathes that no configuration defines. `GameDesc.lev` also references
WRFs that are not on disk (`test.wrf`, `wrf/Demo/DemoCam3.wrf`,
`wrf/cam2/cam2c2.wrf`) — nothing validates the level list against the
filesystem, so the failure would surface as a fatal mid-load.

### 3.5 Why the WRF layer costs what it costs

Load-time performance is **not** the problem — the parsed data is small and
the real load cost is elsewhere (recompiling 70 `.slo` scripts from source
on every run, decoding PCX pages). The cost is fragility and maintenance:
ordering invariants nobody checks, duplication nobody reconciles, a silent
dedupe that can mask real mistakes, magic block IDs, a grammar that cannot
be extended without hand-editing generated C, and a second identical
enumeration of the art tree that must be kept in sync by hand.

## 4. The other formats, briefly

**`.pie` models.** All 516 files are `PIE 2`; the loader's version 1/3/4
branches serve no shipped data. The format itself is adequate — compact,
diff-able, tool-friendly — and Phase 8 already ruled the model format out of
scope (`Docs/Phase8Plan.md`). The problems are in the loader and consumers,
not the syntax: 63 files carry BSP trees that are parsed, allocated and
never traversed (the renderer was deleted in Phase 8 stage A, the loader
kept); every field is parsed twice by the `sscanf1` replay trick; loading
the set costs ~17,700 `malloc`s for 1.28 MB, of which ~307 KB is dead
`iVertex.x/y/z` fields; face normals are computed and never read; and the
1,693 texture-animated polygons have their UV offsets recomputed per polygon
per frame (`NeuronCore/RenderModel.cpp:548-576`, complete with a
`// HACK - fix this!!!!`) when every offset is derivable at load. Those are
Phase 8 stage D concerns (vertex buffers, GPU transform, clipper
retirement) plus a load-time baking pass — a *loader* rewrite against the
same on-disk format. Changing the file format would buy nothing.

**`.ani` animations.** Ten files, one of which (`BLDerik.ani`, the oil
derrick) is the only non-trivial one; two ship but never play. The
`ANIMOBJECT` names and indices in the format are parsed and discarded —
binding is purely positional — and the game addresses animations by
hardcoded IDs (`Outpost/AnimID.h`) that must manually agree with
`anim.cfg`. Parsed by the shared `audp_` grammar. Scale values are parsed,
stored, and never applied. This is a tiny system wearing a full yacc
grammar.

**Stats `.txt`.** 210 files of header-less positional CSV. Column order is
the contract; `sscanf` return values are never checked (a short row silently
yields zeros); several loaders use unbounded `%[^',']` into 60-byte stack
buffers; `loadPropulsionTypes` ignores the file's row count entirely and
reads exactly nine records. The deepest coupling: **row order is identity** —
a stat's reference number is `REF_<CLASS>_START + row index`
(`Outpost/Stats.h:60-75`), and those refs persist into save games. Every
cross-reference between files is a name string resolved by linear `strcmp`,
with a load-order requirement documented only in WRF comments.

**Scripts.** `.slo` source is compiled by the generated script compiler on
every run; `.vlo` files bind typed values (`WEAPON "TUTMG"`,
`RESEARCHSTAT "R-Sys-…"`) — a third place stats names are spelled. The
shipped `.slo` files pin parts of the C symbol tables in place
(`Outpost/ScriptTabs.cpp:155`, `Outpost/Music.cpp:11-14`). The script
*language* is out of scope here; what matters to the pipeline is that
`SCRIPT`/`SCRIPTVAL` entries dominate the per-level WRFs.

**Messages and strings.** View data is variable-arity CSV whose column count
depends on a type field mid-record; text fields are string-resource IDs
resolved against `STR_RES` tables (`IDENT "text"` pairs, their own yacc
grammar) that must load first.

**Audio.** The `.wav` path is the one modernised parser in the tree
(`WavData.cpp`, `std::expected`, Phase 9). But a WAV's identity is spelled in
up to three places: the WRF entry, `audio.cfg` (parsed by the `audp_`
grammar, whose in-file format comment mis-describes its own columns —
`GameData/audio/audio.cfg` says `[id]` where the parser reads volume), and
the 500-entry hand-maintained enum/name table in `Outpost/AudioID.cpp` whose
array order must match its enum values (asserted at runtime, `:265-280`).

**Maps, saves, binary objects.** `.map`/`.gam`/`.bjo`/`.ttp` are raw struct
images with 4-byte magics, versioned by stacked macros
(`#define GAME_SAVE_V22 GAME_SAVE_V20; <new fields>`) and a `sizeof` ladder
(`Outpost/Game.cpp:2926-2960`). Struct padding, enum widths and 32-bit
pointer slots are load-bearing; `writeFXData` stores a hash *in* a pointer
field (`Outpost/Effects.cpp:2512-2518`). Phase 7's rule — anything
serialised into saves stays byte-identical — protects this area, and an
x64 port is already known to need an audit here (`Docs/MigrationPlan.md`,
Phase 6).

**Keybindings.** `keymap.map` stores raw structs plus an 8-byte build
timestamp; **every rebuild silently discards the user's bindings**
(`Outpost/KeyEdit.cpp:543-547`), and functions are stored as indices into a
C table, so reordering `keyMapSaveTable[]` rebinds every key.

**Dead weight in the data.** 46 `.tag` files with no reader anywhere, two
`.jbf` Paint Shop Pro thumbnail caches, `prog.wrf`/`minimal.wrf` dev
leftovers (the latter on a provably broken path), and 63 commented-out `IMD`
entries across the WRFs.

## 5. Improvement areas, ranked

1. **Delete the dead machinery and dead data.** The WDG layer, the PSX
   shims, the `NORESHASH`/`SAVEGAME` file-load path, the dead loaders and
   the 336 KB `tp_PieList` registry, the `.tag`/`.jbf` files. This is the
   same move Phases 8 and 9 made first, and it shrinks every later decision.
2. **Make load order a property of the code, not the data.** The type
   dependency graph (`IMG` after `IMGPAGE`, sounds after weapons, …) is
   fixed by what the loaders do; the loader should own it, and manifests
   should be order-free sets.
3. **Replace enumeration with convention where the enumeration is total.**
   61% of WRF entries list "everything in this directory".
4. **One manifest layer, not two.** `.lev` datasets and their WRFs describe
   one thing — what a level loads — split across two custom formats and two
   parsers.
5. **Named-field data with schemas for the tables.** The stats CSV's
   column-order coupling, silent short-row zeros and string cross-references
   want a self-describing format and an offline validator.
6. **Validation in CI.** Today the only validator is `Neuron::Fatal` at
   runtime on a Windows box. Manifest references, stats cross-references,
   name/ID table agreement (`AnimID.h` vs `anim.cfg`, `AudioID.cpp` array
   order) and data-path case are all checkable by a script.
7. **Loader hygiene** (independent of any format change): keyed maps instead
   of linked-list scans and the 28-bit case-folded hash; full-path dedupe
   keys; error messages that name file and line instead of fatal dialogs;
   per-load buffers instead of the shared `DisplayBuffer`; the `.pie`
   load-time baking that Phase 8 stage D will want anyway.

Async/streaming loading is deliberately **not** on the list: the parsed data
is a few megabytes, level loads are already sub-second-shaped on modern
hardware, and the loading-screen callback machinery works. Robustness is the
problem, not throughput.

## 6. Design: reducing the `.wrf` layer

The conclusion of the survey is that the WRF layer should not be *optimised*
— it should be **absorbed**. Its three jobs all have better owners, and the
format cannot be evolved anyway without regenerating MKS-yacc output that
the project has no tool for. Extending WRF (an include directive, say) is
therefore *more* expensive than replacing it.

### 6.1 Target shape

One JSON manifest per dataset, replacing `GameDesc.lev` **and** the WRFs.
Sketch, not a final schema:

```jsonc
// GameData/manifests/cam1a.json
{
  "name": "CAM_1A",
  "kind": "camstart",
  "base": "cam1",                        // named shared group, declared once
  "game": "wrf/cam1/cam1a.gam",
  "load": {
    "scripts":    ["cam1a.slo", "cam1accelerate.slo", "cam1daynight.slo"],
    "scriptVals": ["cam1a.vlo", "cam1daynight.vlo"],
    "messages":   ["cam1amsg.txt", "cam1aprox.txt"]
  }
}

// GameData/manifests/groups/cam1.json — the former Basic+PieStats+Stats+
// audio+VidMem quintet, declared once and referenced by every Cam1 dataset
{
  "name": "cam1",
  "models":   { "directories": ["structs", "components", "features", "misc"] },
  "audio":    { "directories": ["audio"], "config": "audio/audio.cfg" },
  "texpages": ["page-7-barbarians-arizona.pcx", "…"],
  "stats":    "default",                 // the fixed stats-table set
  "strings":  ["strings.txt", "cam1strings.txt"]
}
```

The important properties, in decreasing order of value:

- **Sharing is by reference, not by paste.** Named groups replace the 24×
  duplication and let the silent filename-hash dedupe hack die. A file
  listed twice becomes a validation *error*, because it no longer needs to
  be tolerated.
- **Bulk enumerations become directory conventions.** `PieStats.wrf`'s 409
  lines and `audio.wrf`'s 348 become "load these directories". The 516
  `.pie` files against 509 active entries (plus 63 commented-out ones)
  shows the hand-kept enumeration is already drifting from the disk.
- **Type strings disappear from the data.** Extension plus section name
  determines the loader; the stats tables are named by fixed filename as
  they already are in practice.
- **Order stops being data.** The loader loads by type phase (pages →
  images → models → stats in registry order → messages → research → scripts
  → configs), which encodes today's comment-documented constraints exactly
  once, in code that can assert them.
- **The `.lev` layer merges in.** Dataset kind, player count, base-data
  reference and the `.gam` slot are manifest fields; `levParse`'s state
  machine and the nine-slot `apDataFiles` array with its
  order-is-load-order convention go away. Block IDs become two named scopes
  (base / level) plus the force-editor one, owned by the loader.
- **It is validatable offline.** A `tools/validate_assets.py` can check
  every reference against the filesystem (catching the three datasets that
  reference missing WRFs today), enforce case-exactness, and later check
  stats cross-references — in CI, where this project already does its
  verification, instead of as a fatal dialog on a Windows box.

Net effect on file count: 90 hand-written manifest files (89 `.wrf` +
`GameDesc.lev`) become roughly a dozen shared-group manifests plus one small
file per dataset — and per-level manifests shrink to the 5–14 genuinely
level-specific entries they already contain. Code deleted: the WRF grammar
(`Resource.l/.y`, `resource_l.cpp`, `resource_y.cpp`), the `.lev` machinery
(`Level.l`, `Level_l.cpp`, `levParse`), the WDG layer, and the dedupe hack.

### 6.2 What survives unchanged

The resource-type registry and the per-type loaders keep their exact
signatures — a manifest section resolves to the same `resLoadFile` calls in
the same relative order the WRFs produce today, so stage one of the
conversion is behaviour-preserving by construction. The `.gam`-driven map
path, the second (direct-`fopen`) loading path, and the script compiler are
untouched by this design; they have their own entries in §7.

### 6.3 An alternative considered and rejected

*Keep the WRF format, clean up the data* — regroup the files, delete the
duplicates, add the missing entries. Rejected: every structural weakness
(ordering-by-comment, no sharing, silent dedupe, magic blocks, the
unregenerable parser) survives, and the cleanup work is the same work the
conversion needs anyway. The converter that emits JSON manifests *is* the
cleanup, run once and kept.

## 7. Where JSON makes sense — and where it does not

The project already has JSON precedent on the tooling side:
`GameData/sequences/sequences.json` is the Phase 6 conversion manifest, and
`MovieTest/Fixtures/reference.json` drives its verification. Nothing in the
game reads JSON yet. For what it is worth, the open-source descendant of
this codebase (Warzone 2100) walked this exact road — its stats and level
data are JSON today — so the destination is proven for this data, not
speculative.

| Data | Today | Verdict | Reasoning |
|---|---|---|---|
| Manifests (`.wrf` + `.lev`) | 2 custom grammars | **Yes — first** | §6. Kills two parsers and the WDG layer; enables CI validation; expresses sharing. |
| `audio.cfg`, `frontaud.cfg`, `anim.cfg` | `audp_` yacc grammar | **Yes — with manifests** | Tiny key-value lists. With the `.ani` conversion this deletes the whole shared grammar. |
| Stats `.txt` (210 files) | positional CSV | **Yes — second wave** | Named fields end column-order coupling and silent zeros; schema validation offline. Array order preserved ⇒ `REF_*` ids unchanged ⇒ save-compatible (§7.1). |
| Messages view data | variable-arity CSV | **Yes — with stats** | Arity currently depends on a mid-record type field; JSON objects model that naturally. |
| `.ani` (10 files) | `audp_` yacc grammar | **Yes — cheap** | Ten files, one non-trivial. Names/indices the current parser throws away become real bindings; kills the grammar's other half. |
| `keymap.map` | raw struct dump | **Yes — targeted** | Names instead of table indices, no build-timestamp key ⇒ bindings survive rebuilds and reorders. User-facing fix. |
| String tables (`STR_RES`) | `IDENT "text"` + yacc | Optional, later | Already simple and localisation-shaped; convert for parser-count reasons only. |
| `.vlo` script values | yacc grammar | Later, maybe | Data-shaped, but entangled with the script compiler's type system; decide when the script system is on the table. |
| `.slo` scripts | script language | **No** | It is code, not data. |
| `.pie` models | custom text | **No** | Compact, diff-able, tool-supported; JSON would multiply size and buy nothing. The wins are loader-side (§4). |
| `.wav`/`.pcx`/`.mp4` | binary media | **No** | Media stays media. (`.pcx`→PNG is a conceivable separate modernisation; different discussion, touches `TexMan`.) |
| `.map`/`.gam`/`.bjo`/`.ttp` | raw structs | **Not now** | Phase 7's byte-identical save rule stands, and the readers need a real serialiser before any format talk. Scenario `.bjo` (map content, not saves) are the eventual candidates — also the way to purge the shipped heap garbage — but only after a serialiser exists. |

### 7.1 The stats conversion and save compatibility

The scariest-looking constraint is a non-issue if respected deliberately: a
stat's persistent identity is its row index (`REF_<CLASS>_START + i`), and
JSON arrays are ordered, so a converter that emits one object per row **in
file order** preserves every reference number, and the loader keeps deriving
refs from array position. Explicit `id` fields can wait until something
actually needs them. Save games also store *name* strings for objects and
templates; names don't change in a format conversion. The conversion is
behaviour-preserving; only the parser changes.

What the conversion buys concretely: each field named once in a schema
instead of positionally in a 52-conversion `sscanf` string and its argument
list; short rows become errors instead of silent zeros; the
`MAX_PLAYERS`-dependent column layout (`Outpost/Stats.cpp:850`) becomes an
explicit array; the load-order comments become loader-enforced phases; and
`tools/` can validate every cross-reference (weapon→IMD, structure→ECM,
research→message, template→component) before a Windows build exists.

### 7.2 The parser question (AGENTS.md R14)

R14 forbids new third-party dependencies, with MsQuic the recorded
exception. Two compliant options:

1. **A hand-written strict JSON reader in `NeuronCore`** (`Json.h`/`Json.cpp`
   in `namespace Neuron`, DOM-style, ~400 lines with tests). JSON's grammar
   is small and closed; the game parses only trusted local files; and the
   project already prefers owning small things over depending on large ones
   (`WavData.cpp` is the precedent). **Recommended.**
2. **A vendored single-header library** under a new R14 exception. More
   surface than the problem needs, and R14 exceptions are supposed to be
   rare and argued.

Comments deserve a decision rather than a drift: strict JSON has none, and
the current WRFs and stats files lean on them. Most existing comments
document ordering (obsolete under §6) or provenance; the rest become
`"note"` fields, which validators ignore by convention. Recommendation is
strict JSON — the moment the files take `//` comments they stop being "an
open standard" to every other tool, which was the point.

## 8. Proposed staging

Each stage is independently shippable and CI-checkable, in the pattern the
migration has used since Phase 1. Verification for stages B and C is a
parity harness in the same spirit as `tools/crosscheck.py`: a debug dump of
the loaded stats/resource tables before and after conversion, compared
byte-for-byte, plus the offline validator in CI. (A run on a real Windows
box remains the final word, as always.)

- **Stage A — deletion and guard rails.** Remove the WDG layer, PSX shims,
  `NORESHASH`/`SAVEGAME` machinery, dead loaders, `tp_PieList`; delete the
  `.tag`/`.jbf` files, `prog.wrf`/`minimal.wrf` and the three dead
  `GameDesc.lev` entries. Write `tools/validate_assets.py` **against the
  current formats** and wire it into CI — it becomes the safety net for
  everything after, and its data model becomes the converter.
- **Stage B — manifests.** `Neuron::Json`; JSON dataset manifests emitted by
  a converter from the existing `.lev`+`.wrf` data; loader-owned type-phase
  ordering; named block scopes. Delete the WRF and `.lev` parsers. The
  85-file WRF tree and `GameDesc.lev` leave the data set together.
- **Stage C — tables.** Stats and messages to JSON with schemas,
  array-order-preserving; delete the `sscanf` loops and `numCR`. The
  validator learns cross-reference checking.
- **Stage D — the small wins.** `.ani` + `anim.cfg` + `audio.cfg` to JSON
  (the `audp_` grammar dies); `keymap.map` to named JSON bindings; string
  tables if and when parser-count argues for it.
- **Explicitly not in scope:** `.pie` format (loader baking belongs to Phase
  8 stage D), media formats, `.map`/`.gam`/`.bjo` and anything else under
  Phase 7's save-file rule, and the script language.

Ordering against the rest of the plan: stage A collides with nothing and
could land tomorrow. Stages B–D touch only loaders, so they are independent
of Phase 8 stage D (renderer) and can proceed in parallel with it; the one
shared file is `Data.cpp`, where coordination is trivial.

## 9. Decisions needed

1. **JSON reader under R14** — hand-written `Neuron::Json` (recommended) or
   a vendored single-header under a new recorded exception?
2. **Strict JSON vs. commented JSON** — strict recommended (§7.2); confirm,
   because it constrains what the converter does with existing comments.
3. **Save compatibility across the stats conversion** — the design preserves
   it (§7.1); confirm it is a requirement so the parity harness treats any
   ref-number drift as a failure rather than a curiosity.
4. **First-wave scope** — stages A+B only, or A through C in one push? A+B
   recommended: B deletes the most fragile machinery, C is bigger and
   benefits from B's validator being battle-tested first.
5. **The shipped binary scenario data** — regenerate the campaign/multiplayer
   `.bjo`/`.tag` content to purge the 1998 uninitialised memory (and delete
   `.tag` outright), or freeze it as-is until the serialiser work? Deleting
   `.tag` is safe now (no reader exists); regenerating `.bjo` needs the map
   tooling question answered first.
6. **Phase numbering** — whether this becomes Phase 10 in
   `MigrationPlan.md` or folds into an existing phase's remainder is the
   owner's call; this document deliberately claims no number.

---

## Appendix A — Registered resource types

46 types registered in `Outpost/Data.cpp:1175-1223`. Condensed; loader
line numbers are in `Data.cpp` unless noted.

| Types | Loaders | Notes |
|---|---|---|
| `IMD` | `dataIMDBufferLoad` :650 | `.pie`; `BPIE` branch is a stub that fatals |
| `TEXPAGE`, `IMGPAGE`, `IMG`, `TERTILES`, `HWTERTILES` | :837, :702, :811, :733, :752 | `.pcx` pages and atlases; `TERTILES` is a no-op stub; `TEXPAGE` re-keys itself to `page-NN` mid-load |
| `WAV`, `AUDIOCFG` | :999, :1032 | Phase 9 `WavData`; `audp_` grammar |
| `ANI`, `ANIMCFG` | :1042, :1058 | `.ani`; name→ID rebinding via `anim.cfg` |
| `SWEAPON` `SBODY` `SBRAIN` `SPROP` `SSENSOR` `SECM` `SREPAIR` `SCONSTR` `SPROPTYPES` `SPROPSND` `STERRTABLE` `SSPECABIL` `SBPIMD` `SWEAPSND` `SWEAPMOD` `STEMPL` `STEMPWEAP` `SSTRUCT` `SSTRFUNC` `SSTRWEAP` `SSTRMOD` `SFEAT` `SFUNC` | :94–:477 | The 23 stats tables → `Outpost/Stats.cpp`, `Droid.cpp`, `Structure.cpp`, `Feature.cpp`, `Function.cpp` |
| `RESCH` `RPREREQ` `RCOMPRED` `RCOMPRES` `RSTRREQ` `RSTRRED` `RSTRRES` `RFUNC` | :494–:608 | Research tables → `Outpost/Research.cpp`; `RESCH` self-releases so campaigns can reload the same filename |
| `SMSG`, `STR_RES` | :621, :1071 | View data; string tables |
| `SCRIPT`, `SCRIPTVAL` | :1100, :1126 | `.slo` compile-on-load; `.vlo` skipped when loading a save |
| `SAVEGAME` | :1145 | Registered via the compiled-out file-load path — unreachable (§3.4) |

## Appendix B — Defects noticed during the survey

Recorded, not fixed — this is a design document. Grouped by severity; all
line numbers verified against `d314720`.

**Would crash or corrupt at runtime**

- `ID_ANIM_SUPERCYBORG_RUN` (10) has no entry in `anim.cfg` (which defines
  0–9); a super cyborg moving dereferences the null from `anim_GetAnim` in
  release (`Outpost/Move.cpp:3429` → `NeuronCore/AnimObj.cpp:168`).
- `mapLoad`'s failure paths `delete[]` the shared global `DisplayBuffer`
  (`Outpost/Map.cpp:468,478,489`), which every later load reuses.
- `functionType()` falls off the end of a non-void function on an unknown
  type; the return value indexes a 20-entry function-pointer table
  (`Outpost/Function.cpp:1949-2041`, `:88`).
- Unbounded `%[^',']` reads into 60-byte stack buffers in the structure,
  view-data, research, feature and function loaders (e.g.
  `Outpost/Structure.cpp:729`); `strcpy` into fixed buffers in the anim
  parser (`NeuronCore/parser_l.cpp:661`).
- `_imd_load_bsp` has no return statement — UB, currently benign because the
  caller discards the value (`NeuronCore/IMDLoad.cpp:592-722`).
- A `.pie` with >256 points overruns `vertexTable[256]` at load and
  `scrPoints[256]` at draw — the loader only `DebugTrace`s
  (`IMDLoad.cpp:1113-1114`); shipped maximum is 243.
- `resLoadFile` leaks one of the six resource-file slots permanently on any
  `buffLoad` failure (`NeuronCore/FrameResource.cpp:526-527`).

**Silently wrong behaviour**

- `anim_GetFrame3D` tests `if (dwTime < 0)` on an unsigned `DWORD`, so
  `ANIM_DELAYED` is unreachable and a delayed animation indexes a garbage
  state (`NeuronCore/Anim.cpp:325,333`); moot only because every caller
  passes delay 0.
- `hashTable_RemoveElement` double-advances the iterator during
  `animObj_Update`, skipping the neighbour of any anim that expires
  (`NeuronCore/HashTabl.cpp:284`).
- `(uwID != ID_ANIM_DROIDRUN || uwID != ID_ANIM_DROIDRUN)` is always true —
  the run animation is torn down and rebuilt every move order
  (`Outpost/Move.cpp:3142-3143`).
- PCX header validation is `(manufacturer != 10) && (version != 5)` — either
  field alone passes a corrupt file (`NeuronCore/Pcx.cpp:134` and three
  clones); the RLE decoder ignores `bytes_per_line`, skewing any padded
  image.
- `loadTerrainTypeMap` range check uses `>` against `TER_MAX`, letting the
  sentinel value through, and its version check is commented out
  (`Outpost/Game.cpp:7184-7203`).
- `ScriptFuncs.cpp:2580` plays `outro.txa`, which does not exist on disk;
  `loadFileToBufferNoError` makes it a silent no-op.
- `Research.cpp` advances its cursor by `strlen(name) + 1`, assuming exactly
  one comma and no whitespace (`Outpost/Research.cpp:264,274,333`).
- The 28-bit case-folded hash keys plus filename-only dedupe (§2.2, §3.3)
  make a silent wrong-resource lookup possible; nothing detects collisions.

**Blockers already known to matter later (x64)**

- Parent pointers truncated to `int` for hashing in the anim system
  (`NeuronCore/AnimObj.cpp:124,184,226,233`).
- `writeFXData` stores a 32-bit hash in an `iIMDShape*` field on disk
  (`Outpost/Effects.cpp:2512-2518`); the `.gam`/`.bjo` macro-stacked structs
  bake 32-bit layout generally (Phase 6's `UDWORD`-holds-a-pointer audit).

**Dead code with live cost**

- 63 shipped `.pie` files carry BSP trees that are parsed, allocated, and
  never traversed (`NeuronCore/IMDLoad.cpp:592`, renderer deleted in Phase 8
  stage A).
- `tp_PieList`: 336 KB of static tables filled on every model load for five
  accessor functions with zero callers (`IMDLoad.cpp:1318-1352`).
- The 2 MB WDG cache, `WDG_ProcessWRF` on every load, and the `addon.lev`
  scans, with zero `.wdg` files in existence (§3.4).
- Per-polygon face normals computed at load and never read
  (`IMDLoad.cpp:520`); `iVertex.x/y/z` — 307 KB dead across the model set.
- `walkanim.ani` and `cybdprun.ani` load on every campaign and are never
  played; `ANIM2D` exists only to be size-checked against `ANIM3D`
  (`NeuronCore/Anim.cpp:30-34`).
- `keymap.map` writes 128-byte name fields from uninitialised stack
  (`0xCC` fill visible in the shipped file, `Outpost/KeyEdit.cpp:421,451`).
