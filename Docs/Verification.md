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
[Phase9Plan.md](Phase9Plan.md#verification), [Phase4Plan.md](Phase4Plan.md#verification),
[Phase6Plan.md](Phase6Plan.md) and [Phase10Plan.md](Phase10Plan.md#f--verification).
This document does not replace them; it is the order to do them in.

## Before you start

**Build.** Debug and Release, Win32, both. Release is not optional: it is the
only configuration that links with `/INCREMENTAL:NO`, so it is the only one
that exercises `/SAFESEH` — Phase 8 found that the hard way, twice.

**Working directory.** `resSetBaseDir` defaults to empty
([FrameResource.cpp:113](../NeuronCore/FrameResource.cpp#L113)), so every asset
path is relative to the working directory, and the paths are bare names —
`"datasets.json"`, not `"GameData/datasets.json"`. The working directory must
therefore be **`GameData` itself**, not the directory containing it. Run the
exe by its full path with `GameData` as the working directory, or pass
`-datapath`. Getting this wrong looks exactly like a rendering failure — see
below. It was got wrong on the first run of this sheet, and the listener is
the only reason it took seconds rather than an afternoon: the tell then was
`Couldn't open palette.bin`, from a load that has since been deleted with the
palette; the equivalent failure now names whichever manifest loads first.

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
| `-window` | **nothing — accepted and ignored.** See below |
| `-game <name>` | boot straight into a level, skipping the menus |
| `-title` / `-intro` | start at the title screen / the intro video |
| `-datapath <dir>` | set the asset base directory and `chdir` to it |
| `-noFog` / `-greyFog` | cap render fog off / to grey |
| `-noTranslucent` / `-noAdditive` | disable translucency / additive effects |
| `-seqSmall` / `-seqSkip` | play sequences small / skip them |

**There is one display mode and no switch for it.** The display is a
borderless window covering the desktop at the desktop's own resolution,
presented through a windowed swap chain
([Window.cpp `frameInitialise`](../NeuronClient/Window.cpp)). The game lays
out on a logical canvas — the desktop size divided by the integer display
scale `ChooseDisplayScale` picks — and `D3DDrawPoly` multiplies every vertex
back up. The `-640` … `-1280` switches, the `resolution` registry key, the
Direct3D exclusive mode and the Alt+Enter toggle that reached it are all
gone; `-window` is still accepted so old shortcuts keep working, but it does
nothing.

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

Since the asset-pipeline conversion (2026-08-16,
[AssetPipeline.md](AssetPipeline.md) §8), reaching the level at all also
proves the new data path end to end: the `datasets.json` manifest replay that
replaced the `.wrf` layer, every stats and message table through
`Neuron::Json`, and the anim/audio configs that replaced the `audp_` parser.
A data error now stops the boot with a named table/row/field fatal rather
than playing on with zeroed stats — a fatal here is diagnostic, not noise.

Since the script rewrite (2026-08-17, [ScriptRewrite.md](ScriptRewrite.md)),
this boot carries more weight still: **it is the acceptance test for the new
script compiler.** Every `.slo` the level names is compiled from source at
load, so reaching the level proves the compiler accepts the shipped corpus,
and a compile error stops the boot naming the file, line and column. Add a
skirmish match to the pass — the campaign scripts and `skirmishAI.slo`
exercise different halves of the language (callback triggers with `ref`
parameters, arrays, object member access), and only running them proves the
generated code *behaves*, which no compile can.

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
9. The keymap editor opens, a binding can be changed, and the change survives
   a quit and relaunch. Bindings live in `keymap.json` by function name since
   the asset-pipeline conversion — the old binary `keymap.map` was
   invalidated by a build-time stamp, so surviving a relaunch of a *rebuilt*
   executable is precisely what the old format never did.

## Pass C — device loss

*(Recorded against the build that still had exclusive full screen; the pass
below was run and passed then. The display is a borderless window on a
windowed swap chain now — Alt+Enter is gone, and a windowed device survives
alt-tab without a reset. Device loss still exists — locking the workstation
(Win+L) or a remote-desktop session can still provoke it — so the reset path
it exercised remains live, just rarer.)*

Start windowed, **Alt+Enter to full screen** (there is no switch for it — see
above), then alt-tab away and back. **Twice.**

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
   X3DAudio and no shim can speak to whether it matches. One known silence is
   *not* a Phase 9 defect: the VTOL move loop never sounds, because
   `PropulsionSounds` and `AudioID.cpp` name `VtolMove.wav` while the shipped
   file is `Vtol-Move.wav` — recorded by the asset validator, fix pending an
   owner decision ([AssetPipeline.md](AssetPipeline.md) §8, stage C).
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

## Pass I — Phase 10: DirectXMath and the radian flip

Added 2026-08-16, after the first session below — Phase 10 landed later the
same day, so nothing in it has been run. It replaced the renderer's
fixed-point arithmetic with DirectXMath and, by owner ruling, migrated every
stored angle in game state from integer degrees and 16-bit binary angles to
float radians. Its surface is therefore both rendering *and* gameplay: droid
turning, formations, projectiles, turrets and the camera all compute in new
units. The cross-check and MSVC CI are green on all of it, which per this
sheet's rule means it compiles and links — nothing more.

**Intended behaviour changes first**, so a difference from memory is not
misread as a regression. Seven latent angle defects were fixed during the
migration (each recorded in the stage E status blocks of
[Phase10Plan.md](Phase10Plan.md)): the ballistic pitch-swap that flattened
some indirect-fire arcs, an unreachable negative-pitch branch, perpendicular
speed lost past a 180° heading difference, the unwrapped vtol roll, an
uninitialised track-angle average, the tracking camera's garbage offsets on
west-ish average headings, and a degrees-versus-radians comparison in the
component renderer's winding choice. Separately, the old 1°-per-frame turn
floors are gone — slow-turning heavies now turn at their stated rate, which
reads as *slightly slower* than before at low frame rates, deliberately.

1. **Re-run pass A on the Phase 10 head.** Same criteria, plus: any
   *systematic* offset in terrain or model placement means a
   projection-constant error (the focal length is 1024, depth is world-z ×4);
   turrets and muzzle flashes sit correctly on their hulls (the hierarchy is
   now composed `XMMATRIX` pre-multiplies); the radar draws rotated and its
   viewing-window quad tracks the camera (`RotateVector2D` takes radians);
   effect circles — fireworks, gravitons — look round and tumble smoothly.
2. **The parity figure.** Check out the stage-B head `048f430`, build Debug,
   run `-game CAM_1A`, quit: the fixed-point parity shadow prints the worst
   screen-space divergence at shutdown. Expected ≤ ~1 pixel; anything larger
   is a conversion bug to find, not a tolerance to widen. The shadow was
   retired at stage C, so the figure exists only at that commit.
3. **Gameplay in radians.** Droids drive and turn cleanly at all speeds;
   formation lines form and fill at the right angles; indirect fire arcs and
   lands (low-trajectory shots differ deliberately — see above); turrets
   track and settle, including the vtol ±45° traverse limit.
4. **The tracking camera.** Track a single droid, then more than two
   selected, then a group — and make some of them head *west*, the heading
   band the fixed defect used to garbage. Track a vtol (roll now wraps).
   Radar-jump with alignment on and off; the track must come to rest and
   hand back control.
5. **Camera controls.** Pitch clamps at both ends and reset-pitch;
   face North/South/East/West; drag-rotate, normal and inverted mouse;
   keyboard spin and pitch (a full turn in 2 seconds); edge scroll runs in
   the compass direction of the drag at every yaw; screen shake; the
   intelligence screen; save a view on a qwerty map marker and jump back to
   it — the stored spin is radians now.
6. **Audio pan.** Weapons and explosions stay positioned as the camera yaws —
   the listener pose now converts radians to the mixer's degrees.
7. **Two instances, one skirmish.** Long enough to see the direction-sync
   tolerance checks hold in radians: no oscillating unit headings, no
   rubber-banding snap-backs. The wire still carries whole integer degrees;
   both peers run the same binary, so lockstep is unchanged by design.

---

## Results

First session: **2026-08-16**, Debug and Release both rebuilt clean from scratch
under MSVC 18.9.1 (0 errors; Release links, so `/SAFESEH` holds). Passes driven
programmatically — window forced foreground, frames captured off the desktop.
Anything needing ears or menu navigation is not done.

| Pass | Result | Notes |
|---|---|---|
| A — boot and smoke | **pass** | Reaches `Entering main loop`. Terrain, unit models, translucent build overlay, reticule HUD and text all draw. Water translucency and team-colour animation frames not separately isolated — no water in the start view |
| B — front end | **partial** | Backdrop (the `CLEAR_OFF` keep-frame path), menu, buttons, translucent filter boxes and **rotated text** all draw. Menus driven by keyboard as far as Options → Game Options; both volume sliders exist and take focus. Item 8 (radar + viewing-window quad) confirmed incidentally in CAM_2A during pass D. Items 3–5, 7 still open — see "Driving the UI" below |
| C — device loss | **pass** | Alt+Enter to 640x480 exclusive full screen, alt-tab away and back **twice**. Survived both; textures rebound, translucency correct, sim kept running. Caveat: the mode switch blanks the screen, so the literal *first* frame back is not capturable — earliest observed frame (~600 ms) was already correct |
| D — `bClip` experiment | **run — see below** | Difference is measurable but sub-perceptual. Edges structurally clean; radar sub-rect unaffected; behind-camera rejection proven independent of `bClip` |
| E — listening pass | not run | Cannot be automated — it is a listening pass |
| F — FMV and gates | not run | Needs menu navigation to reach a briefing |
| G — fog parity | **pass** | Three-point comparison: default = warm haze over distance, `-noFog` = none, `-greyFog` = grey haze. The hue tracks the switch names, which excludes the day/night script as the cause. Closes the last open Phase 2 item |
| H — counters | not run | `pie_GetResetCounts` has no visible readout from a plain launch |
| I — Phase 10 | **pass** | Owner-run session, 2026-08-16. The boot surfaced two boundary-conversion escapes — an unwrapped 222° map heading and a visibility ray index past the trig tables — both fixed during the session and recorded in [Phase10Plan.md](Phase10Plan.md#f--verification); after the fixes the run came back clean. Covers the pass-A re-flag too, since reaching gameplay re-exercises the rewritten renderer |

Two defects found by running, both fixed in the same session: `tools/dbg.py`
crashed on its first-ever Windows run (`ctypes.wintypes` is a submodule and was
never imported), and this document's working-directory and `-window` guidance
were both wrong — corrected above.

### Driving the UI from a script — what works, and what stops you

Recorded because the next person will otherwise rediscover all of it.

- **Keyboard injection works** (`keybd_event`). Alt+Enter, arrows and Enter all
  reach the game.
- **The front end is fully keyboard-navigable**: arrows move the cursor-snap
  between widgets, Enter activates
  ([FrontEnd.cpp:177-203](../Outpost/FrontEnd.cpp#L177-L203)). Main menu →
  Options → Game Options was driven that way.
- **Mouse position is NOT the DirectInput mickeys.** The UI reads
  `mouseXPos`/`mouseYPos` from `WM_MOUSEMOVE`
  ([Input.cpp:251](../NeuronClient/Input.cpp#L251),
  [391](../NeuronClient/Input.cpp#L391)), so absolute `SetCursorPos` drives it
  and relative `mouse_event` motion does nothing. The mickeys in
  `DXInput.cpp` feed something else. This is the single biggest time-waster
  here — injected relative motion looks like it should work and never does.
- **The window is DPI-unaware and its client is often not 640x480.** The game
  presents its 640x480 back buffer into whatever the client is, so a smaller
  client *crops* the view — the reticule falls off the bottom — and every
  coordinate you compute from a screenshot is wrong. Force the client to
  640x480 before clicking anything, and re-measure: the resize needs more than
  one pass to converge, and sometimes does not converge at all.
- **Keymapped functions stop responding once the game is paused.** F5 opens the
  intelligence screen and pauses; after that F4/F5/F7/F8/Esc are all ignored,
  because `keyProcessMappings` is skipped. Alt+Enter still works, since it is
  tested directly in the loop rather than through the mapping table. So the
  intelligence screen can be opened by key but must be closed by mouse.
- **`GameData/keymap.map` overrides the `KeyMap.cpp` defaults**, so the F-key
  bindings in the source are not necessarily the ones in force. Move it aside
  to get the documented defaults; the game rewrites it on every startup, which
  is why it shows as modified in `git status` after any run.
- **Pass H needs debug mappings enabled first.** `kf_FrameRate` is Ctrl+Y but
  registered `KEYMAP__DEBUG` ([KeyMap.cpp:292](../Outpost/KeyMap.cpp#L292)), and
  pressing it in a normal run prints nothing. Its readout is a `CONPRINTF` to
  the in-game console ([KeyBind.cpp:281](../Outpost/KeyBind.cpp#L281)) — the
  sheet's old claim that it had no readout was wrong, and its `Loop.cpp:628`
  reference is stale: the call is at
  [Loop.cpp:584](../Outpost/Loop.cpp#L584).

### Pass D result (2026-08-16)

Only **two** call sites pass `bClip = TRUE`:
[RenderModel.cpp:254](../NeuronClient/RenderModel.cpp#L254) (the 3D poly path)
and [Render2D.cpp:77](../NeuronClient/Render2D.cpp#L77). Both were flipped to
`FALSE`, Debug rebuilt, and the same two scenes captured. The change is
reverted; the tree is back as it was.

Scene choice matters. CAM_1A is near-deterministic frame to frame and is the
only usable comparison; CAM_2A is so animated that its run-to-run noise swamps
the signal, so it measures nothing. Per-pixel max channel deviation on CAM_1A:

| | identical px | mean | p95 | p99 |
|---|---|---|---|---|
| control (same binary, two runs) | 99.9% | 0.09 | 1 | 1 |
| experiment (`TRUE` vs `FALSE`) | 53.3% | 3.70 | 17 | 30 |

So the difference is real and pervasive — about half the pixels move — but
small, and it is **not** concentrated at the screen border (border band 10.11%
against 9.20% overall). It shows up as a faint speckle in broad diagonal bands
across terrain: sub-pixel vertex differences from the clipper rewriting every
vertex it touches, not a clipping failure. At 1:1 the two frames are not
distinguishable by eye.

Three things the experiment settled beyond the pixel count:

- **Screen edges are structurally clean** with the clipper off — no geometry
  escaping the viewport, no torn or missing triangles, no gaps.
- **The radar sub-rect is unaffected**, because the radar does not draw through
  the `bClip` path at all. This pass therefore does *not* answer the
  `pie_Set2DClip` sub-rect question — D1b still has to point it at
  `SetScissorRect`, and that remains the real work.
- **Behind-camera rejection does not depend on the clipper.** Two guards sit
  outside it — [RenderModel.cpp:242](../NeuronClient/RenderModel.cpp#L242) and
  the funnel's own test at [Render.cpp:227](../NeuronClient/Render.cpp#L227) —
  and the sentinel `1<<15` is written to both coordinates against a `LONG_TEST`
  of `1<<14`. (The clipper's own check at
  [RenderClip.cpp:138](../NeuronClient/RenderClip.cpp#L138) tests `sy == -1<<15`,
  the wrong sign, and so never fires — harmless only because the funnel catches
  it first.)

**Reading:** by the sheet's own criterion — invisible means write it — D1b is
viable, and the two risks that would have killed it (edge artefacts,
behind-camera leakage) are both disproved. The qualifier is that it is not
pixel-identical, so it is a *looks-the-same* change rather than a
behaviour-preserving one, which is a different bar from the one Phase 8 has
held itself to so far. That call belongs to the owner.

## What each result unblocks

- **C passing** unblocks Phase 8 D2 and Phase 6 B4. Both add a
  `D3DPOOL_DEFAULT` resource to the device-loss path, and neither should land
  until that path has been seen to work.
- **D** settles Phase 8 D1b outright, either way.
- **G** closes the last open item from Phase 2.
- **A, B, E and F** close the standing verification debt for Phases 3 to 9 and
  are what lets those phases be described as done without the qualifier.
