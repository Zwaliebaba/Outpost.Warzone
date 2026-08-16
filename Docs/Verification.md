# Verification runsheet

Everything the tree owes a running build, in one ordered session.

Five phases have landed since the last time the game was actually run, and each
left its own checklist in its own document. This gathers them, orders them so
each screen is visited once, and says what "pass" means for each item. Work
down it and record the result in [Results](#results) as you go.

## Why this exists

Phase 2 was the last work verified by running it. Everything after — Phase 3
input, Phase 4 audio, Phase 5 networking above what the `NetTest` harness once
covered, Phase 6's Media Foundation FMV, Phase 8 stages A, B, C and D, Phase
9's audio rewrite — is **built and linked and nothing more**. MSVC CI is green
on all of it, and `tools/crosscheck.py` is clean in both configurations, but
neither can say whether a renderer draws or a mixer sounds right.

**CI stopped running anything on 2026-08-16**, when `NetTest/` was deleted with
the client/server restructure. It was the only executable CI started. Until
something replaces it, a green CI means the tree compiles and links — nothing
more.

The individual checklists live in [Phase8Plan.md](Phase8Plan.md#verification),
[Phase9Plan.md](Phase9Plan.md#verification), [Phase4Plan.md](Phase4Plan.md#verification)
and [Phase6Plan.md](Phase6Plan.md). This document does not replace them; it is
the order to do them in.

## Before you start

**Build.** Debug and Release, Win32, both. Release is not optional: it is the
only configuration that links with `/INCREMENTAL:NO`, so it is the only one
that exercises `/SAFESEH` — Phase 8 found that the hard way, twice.

**Working directory.** `resSetBaseDir` defaults to empty
([FrameResource.cpp:113](../NeuronCore/FrameResource.cpp#L113)), so every asset
path is relative to the working directory. Run from a directory that contains
`GameData`, or pass `-datapath`, which sets the base directory and `_chdir`s to
it. Getting this wrong looks exactly like a rendering failure — see below.

**Attach the debug listener first.** `Neuron::Fatal` calls `__debugbreak()`, so
an assertion under a launcher surfaces only as exit code `0xC0000003` with no
message on screen. The message goes to `OutputDebugString`. Run
`python tools/dbg.py` in another window before starting the game and it will
print them. This is how Phase 2 told "Couldn't open `wrf\demo\democam3.gam`"
apart from a black screen, and every pass below can fail that way.

**Reference screenshots.** There are none in the repository, and the game
cannot save one — nothing in the tree writes a surface to a file. Where a step
below says "compare", it means compare against the other run described in the
same step, not against a stored reference. Use whatever external capture you
prefer, and keep the pairs: two of the outstanding decisions are settled by
them.

### Switches you will need

All parsed in [ClParse.cpp](../Outpost/ClParse.cpp).

| Switch | Effect |
|---|---|
| `-window` | windowed rather than fullscreen |
| `-game <name>` | boot straight into a level, skipping the menus |
| `-title` / `-intro` | start at the title screen / the intro video |
| `-datapath <dir>` | set the asset base directory and `chdir` to it |
| `-640` … `-1280` | pick the resolution |
| `-noFog` / `-greyFog` | cap render fog off / to grey |
| `-noTranslucent` / `-noAdditive` | disable translucency / additive effects |
| `-seqSmall` / `-seqSkip` | play sequences small / skip them |

**A trap worth knowing before the fog pass.** There are two unrelated fogs.
Render fog is the distance fog the switches above control. `game.fog` is fog of
war and has nothing to do with it. Worse, the `visfog` registry key **inverts**:
`visfog = 1` calls `war_SetFog(FALSE)` and turns advanced visibility on, and
`visfog = 0` is what turns distance fog on
([Config.cpp:159](../Outpost/Config.cpp#L159)). Set `visfog` to 0 for pass G or
you will verify the wrong thing.

---

## Pass A — boot and smoke

`Outpost.exe -window -game CAM_1A`

This is the highest-value single command in the document: it puts the 3D world,
the HUD, the terrain, the units and the translucent build overlay on screen in
one shot without needing menu input.

1. It reaches the level without a fatal. If it does not, read the listener
   window before concluding anything about the renderer.
2. Terrain draws, with water translucency.
3. Unit models draw, including team-colour texture animation frames.
4. The HUD draws: images plain, tiled and stretched.
5. Text draws in both fonts, including coloured and rotated text.

Stop here if this fails. Nothing below is meaningful until it passes.

## Pass B — the front end

`Outpost.exe -window -title`

Phase 8 stages A and C rewrote most of what this screen touches, and Phase 9
rewired both sliders.

1. Backdrop screen draws (this is the `CLEAR_OFF` keep-frame path).
2. Menus and buttons draw and respond.
3. Both options sliders move **and are audible** — Phase 4 moved them off the
   Windows system mixer onto the XAudio2 graph, and Phase 9 rewrote the graph.
4. Menu sounds play.
5. The design screen draws its 3D component buttons.
6. Console text and `pie_TransBoxFill` filter boxes draw.
7. The intelligence screen draws.
8. Radar draws, rotated radar draws, and the radar viewing-window quad draws.
   Look at this one properly — it is half of D1's parity gate.

## Pass C — device loss

Fullscreen, then alt-tab away and back. **Twice.**

This is the single most important item in the document. Phase 2 built the reset
path; Phase 8 stage B then deleted one of the two state caches it depended on
and removed the `g_bStateCacheStale` machinery that reconciled them. That change
is behaviour-preserving by construction and has never been executed.

1. The device recovers and the scene redraws.
2. Blend states are right on the **first** frame back, not the second — a
   one-frame flash of wrong translucency is the failure this pass is looking
   for.
3. Texture pages are still bound: no white or garbage models.
4. Do it a second time. A reset that works once and not twice is a different
   defect from one that never works.

## Pass D — the `bClip` experiment (Phase 8 D1b)

This answers a decision, not just a checklist item, and it needs no new code.

`bClip` is a parameter, not a constant
([RenderModel.cpp:515](../NeuronClient/RenderModel.cpp#L515)), and seven live
sites in `Render2D.cpp` already pass `FALSE` — handing raw screen-space
vertices to the device and letting it clip them at the viewport. That is what
D1 proposes to do everywhere.

1. Screenshot the screen edges and the radar viewing window from pass B.
2. Force `bClip` to `FALSE` on the clipped paths, rebuild, repeat.
3. Compare.

If the difference is invisible, D1b is worth writing — point `pie_Set2DClip` at
`SetScissorRect` and delete `RenderClip.cpp`'s remaining 595 lines. If it is
visible, **D1b closes as attempted-and-rejected** and the clipper stays. Keep
the screenshots either way; they are the record the decision cites.

The sub-rects `pie_Set2DClip` sets — the radar and the design screen — still
need a real scissor rect, since a viewport alone does not express them. That is
why the radar viewing window is the case to look at hardest.

## Pass E — the listening pass

Phase 9 rewrote the audio module and Phase 4 replaced the backend under it.
This is inherited wholesale from
[Phase4Plan.md](Phase4Plan.md#verification) and is the part most likely to be
subtly wrong.

1. Unit acknowledgements play, in the queue slot, with the duck audible under
   them.
2. Weapons and explosions pan and attenuate as the camera moves. **This is the
   one to concentrate on** — 3D positioning went from QMixer's `bScale3D` to
   X3DAudio and no shim can speak to whether it matches.
3. A research-message stream plays.
4. Music survives a briefing, and stays paused across a video.
5. ~~Save and load a campaign game with a script-assigned sound in flight.~~
   Gone with save/load (owner decision, 2026-08-16) — the track-hash
   round-trip no longer exists. In its place: finish a mission and confirm
   the results screen offers Continue and Quit, and that Continue starts the
   next mission — the mission-transition path still loads level `.gam` files
   through the same reader the removal narrowed.

## Pass F — FMV and the content gates

Covers Phase 6 B3 and the four gates B5 was never run against.

1. A briefing sequence plays, with audio, in sync.
2. Subtitles draw over it.
3. A research video plays.
4. The gates that used to test for a CD — **new game from the front end, a
   mission continue, and startup** — each proceed with no CD dialog appearing
   and no path resolving to a drive letter. B5 removed the apparatus; this
   confirms it removed only the apparatus. (The fourth gate, save-game load,
   went with save/load itself.)
5. The multiplayer force picker still opens, lists `.For` files, and saves a
   force under a typed name — it shares the requester the save/load removal
   narrowed.
6. Sequences still play after `-seqSmall`.

## Pass G — fog parity

The one Phase 2 item never confirmed. The game's fog is the per-vertex specular
colour `pie_AddFogandMist` writes plus the clear colour, not fixed-function fog,
so it wants a comparison rather than a glance.

Set `visfog` to 0 first (see the trap above), then run the same level twice —
once normally, once with `-noFog` — and compare. `-greyFog` is a third point of
comparison if the coloured path looks wrong.

## Pass H — the draw-call counters

A cheap regression tripwire for Phase 8 stage B, from
[Phase8Plan.md](Phase8Plan.md#verification).

`pie_GetResetCounts` is called every frame in
[Loop.cpp:628](../Outpost/Loop.cpp#L628) and is compiled into Release too. On
the same scene, before and after stage B:

- **state-change count should drop** — one cache means no forced re-sends
- **poly count should be identical** — if it moved, geometry changed, which
  stage B was not supposed to do

---

## Results

| Pass | Result | Notes |
|---|---|---|
| A — boot and smoke | | |
| B — front end | | |
| C — device loss | | |
| D — `bClip` experiment | | |
| E — listening pass | | |
| F — FMV and gates | | |
| G — fog parity | | |
| H — counters | | |

## What each result unblocks

- **C passing** unblocks Phase 8 D2 and Phase 6 B4. Both add a
  `D3DPOOL_DEFAULT` resource to the device-loss path, and neither should land
  until that path has been seen to work.
- **D** settles Phase 8 D1b outright, either way.
- **G** closes the last open item from Phase 2.
- **A, B, E and F** close the standing verification debt for Phases 3 to 9 and
  are what lets those phases be described as done without the qualifier.
