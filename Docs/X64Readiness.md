# x64 readiness

What stands between this tree and an x64 build, found by auditing for the
hazards a Win32 build cannot report: on Win32 `sizeof(void*) == sizeof(UDWORD)`,
so every pointer-through-integer round trip compiles clean and works. On x64
those same lines truncate a 64-bit pointer to 32 bits and hand back an address
that is not the one that went in.

The x86 CI build's own 343 warnings were cleaned up alongside this audit; that
work is separate and is described in the commit history. Nothing here showed up
as a warning on Win32 — it only appeared once `Platform=x64` was built, and
some of it never warns at all, because a truncating cast is legal C++.

**All four configurations now build warning-free**: Debug and Release, Win32
and x64, zero warnings and zero errors.

Status key: **Fixed** — done, and behaviour-identical on Win32.
**Blocker** — must be designed and done before x64 can run.
**Watch** — survives x64 by luck or convention; know it is there.

**State as of 2026-08-17: x64 compiles and links, both configurations, zero
errors.** The script VM, the one item that was a project rather than an edit,
was resolved by the module rewrite ([`ScriptRewrite.md`](ScriptRewrite.md)).
CI builds x64 on every push, non-blocking, and the diagnostics below are
measured from those builds rather than predicted. It had not been *run* at
that point; it has since -- see the 2026-08-26 note below.

**2026-08-26.** `tools/crosscheck.py` grew an `--x64` flag, and the four
findings under *Fixed on the second pass* below came out of it and out of a
re-audit of what crosses a process boundary. The harness had only ever run
`i686-w64-mingw32-g++`, so the Linux pre-CI gate was blind to exactly the
class of defect this document exists for. All four crosscheck configurations
-- x86 and x64, debug and release -- are now 180/180 clean.

**x64 has been run.** The owner booted it, played far enough to build a base
and a power generator, and reported two defects; both are fixed and are
recorded under *Found by running it* below. This is the milestone the
*Suggested order* at the foot of this document was waiting for, and it
immediately paid for itself: the functionality-blob overflow is a heap
overwrite that no amount of reading was going to surface, and neither
crosscheck nor a zero-warning MSVC build had said a word about it.

What the run does **not** yet cover: a full CAM_1A completion, a multiplayer
session (so the corrected `NET_TEMPLATE` wire format still has never carried a
packet), or FMV. [Verification.md](Verification.md) remains the runsheet.

---

## Fixed

### Pointer arithmetic through a 32-bit integer

`Outpost/Stats.cpp` (`statsDealloc`) and `Outpost/Research.cpp`
(`getArtefactComponent`-style lookup) walked arrays of variable-stride structs
by casting the base pointer to `UDWORD`, adding the stride, and casting back.
Both now do byte-pointer arithmetic, which is what they meant and which stays
pointer-width.

### Pointer alignment through a 32-bit integer

`NeuronCore/Script.cpp` aligned a write cursor up to a 4-byte boundary with
`pPos = (UBYTE*)((((UDWORD)pPos) + 3) & ~3)`, at two sites. On x64 that
discards the top half of the address and rebuilds a pointer into unmapped
memory — a guaranteed crash on the first script save. Both now go through
`std::uintptr_t`.

### Treap keys

`NeuronCore/Treap.h`/`.cpp` typed its key `UDWORD`, and `NeuronCore/StrRes.cpp`
keys the string-resource treap on **the string's address**, with
`treapStringCmp` casting the key back to `STRING*` and calling `strcmp` on it.
On x64 every key was truncated on the way in and a half-address was
dereferenced on every comparison. The key was widened to `TREAP_KEY`
(`std::uintptr_t`) as an interim fix, and the module has since been **deleted
outright**: `StrRes` keys an `unordered_map` by the keyword's *value*, so the
address-as-key idiom that caused this is gone rather than widened.

### Hash table keys

`NeuronCore/HashTabl.h`/`.cpp` typed its two keys `int`, and both callers key
on an object address — `NeuronClient/AnimObj.cpp` on the parent object,
`Outpost/Projectile.cpp` on the projectile itself. `HashPJW` casts key 1 back
to `CHAR*` and walks it. Same failure as the treap.

The module has since been deleted outright rather than widened. Both callers are
now a `std::list` of the object itself — `std::list<PROJ_OBJECT>` and
`std::list<ANIM_OBJECT>` — so there is no key to truncate on any platform.

A hash map was the obvious replacement for `AnimObj.cpp`, which really does look
up by `(parent, anim id)`, and it was rejected: `animObj_Update` fires each
animation's done callback mid-iteration, and `droidBurntCallback` adds an
animation from inside that loop. An insert can rehash an `unordered_map`, which
invalidates the loop's iterator; `std::list` invalidates only the iterator to
the element erased. `animObj_Find` is a linear scan as a result, over a
container that holds a few hundred entries at most.

The same rewrite retires the iterator double-advance bug noted in
`Docs/AssetPipeline.md`.

---

## Found by running it (2026-08-26)

### The functionality blob was sized for 32-bit structs

`FUNCTIONALITY` was `UBYTE[40]`, hand-written in 1999 and commented "this is
sizeof(FACTORY) the largest at present". It was exactly that -- on a 32-bit
build. Every functionality struct is allocated as one of these blobs by
`createStructFunc` and cast to its real type, and on x64 they all grew with
their pointers:

| struct | x86 | x64 | |
|---|---|---|---|
| `RES_EXTRACTOR` | 16 | 24 | fits |
| `REARM_PAD` | 16 | 24 | fits |
| `RESEARCH_FACILITY` | 32 | 40 | exactly at the limit |
| `POWER_GEN` | 28 | **48** | overflows by 8 |
| `REPAIR_FACILITY` | 32 | **56** | overflows by 16 |
| `FACTORY` | 40 | **64** | overflows by 24 |

`POWER_GEN::apResExtractors` starts at offset 16 on x64, so
`apResExtractors[3]` occupies bytes 40..47 -- entirely past the end of the
allocation. `memset(p, 0, sizeof(FUNCTIONALITY))` never cleared it, so it was
never null; reading it returned the debug CRT's four `0xFD` fence bytes
followed by the adjacent heap (`0x00044c24fdfdfdfd` as reported), and
`checkForResExtractors` dereferenced that as a `STRUCTURE*`. Writing the slot
corrupted the heap rather than crashing, which is the worse half: every
`FACTORY` was also writing its last 24 bytes -- `psAssemblyPoint`,
`psFormation`, `psCommander`, `secondaryOrder` -- outside its block.

It was **not** a use-after-free, which was the first reading: the slot is
cleared when an extractor dies, by `informPowerGen` and by the `died` check in
`updatePower`, and freed memory would read `0xDD`/`0xFD` in all eight bytes
rather than four. The mixed value is the signature of a read that runs off the
end of a block, not of a dangling pointer.

`FUNCTIONALITY` is now a union of the six structs, so size and alignment both
come from the types. On x86 the union is still exactly 40 bytes and 4-aligned,
so the shipping platform is byte-identical. Nothing else in the tree is a
hand-sized storage blob of this shape.

### The radar dish span 57 times too fast

Not an x64 bug -- it is a Phase 10 units bug, and it would have been just as
wrong on x86 -- but it is the other thing the first run surfaced.
`STRUCTURE::turretRotation` became a float in radians; `structureUpdate`'s
sensor sweep kept building it in degrees, so the renderer read 0..359 degrees
as radians and turned the dish 360/(2*pi) times per three seconds instead of
once. `Move.cpp`'s `SPIN_ANGLE` macros really are degrees and are converted at
the boundary; the `DEG()` sites in `MapDisplay.cpp` and `IntelMap.cpp` are in
code the preprocessor never expands. This was the only live straggler.

---

## Fixed on the second pass (2026-08-26)

### A whole `DROID_TEMPLATE`, pointers and all, went on the network wire

`sendTemplate` in `Outpost/MultiPlay.cpp` did
`memcpy(&m.body[1], pTempl, sizeof(DROID_TEMPLATE))` and `recvTemplate`
memcpy'd the same span back out. `DROID_TEMPLATE` carries two pointers --
`pName` from `STATS_BASE` and `psNext` at the end -- so **both** its size and
the offsets of every field after `pName` move with the pointer width.
Measured, by compiling the struct's field list standalone under both
mingw targets: **`sizeof` is 136 on x86 and 152 on x64, and `aName` starts at
offset 8 against offset 16.** An x86 client and an x64 client could not have
agreed about any field past `ref`, and between two x64 machines the sender's
own addresses arrived as the receiver's `pName` and `psNext`.

Both halves now walk the value fields one at a time (`PackTemplate` /
`UnpackTemplate`, **125 bytes on both platforms**) and neither pointer goes on
the wire; `pName` is repointed at the receiving template's own `aName`. That
last part fixes a bug that was live on x86 too -- the "template already
exists" branch memcpy'd a stack local over the stored template and left
`pName` pointing at the local.

This changes the `NET_TEMPLATE` message layout. Both ends are the same build,
so there is nothing to negotiate, but it is a protocol change.

### `stackPushResult(ST_FEATURE, NULL)` picked the integer overload

Four sites in `Outpost/ScriptFuncs.cpp` -- the "none found" returns of
`scrGetFeature` and `scrEnumStruct` -- pushed a null *object* result as `NULL`
rather than `nullptr`. `stackPushResult` is overloaded on `SDWORD` and
`void*`, and `NULL` is an integer literal, so MSVC silently chose the `SDWORD`
overload. That writes `v.ival`, which on x64 is the low **four** bytes of an
eight-byte union; the high half keeps whatever the stack slot held. The script
then reads the same union as `v.oval` and gets a non-null garbage pointer
where it asked for "nothing found".

On x86 the union is four bytes wide, so the two overloads wrote identical bits
and the bug was invisible. This is the defect class the typed FFI rewrite was
meant to close, surviving because `NULL` routes around the type that was
supposed to fix the store width. `nullptr` makes the `void*` overload the only
viable one on every compiler. The remaining sites in both functions already
said `nullptr`; these four were stragglers.

Found by the new `--x64` crosscheck: GCC's `__null` is pointer-width, so at
64 bits neither overload is a better match and the call is a hard ambiguity
error. x86 stayed 180/180 clean through the same run.

### `/SAFESEH` was set on the x64 configurations

`ImageHasSafeExceptionHandlers=false` was on all four configurations of
`Outpost.vcxproj`. `/SAFESEH` describes the x86 stack-based exception chain
and has nothing to apply to on x64. Removed from the two x64 blocks; the Win32
ones keep it, because `dinput8.lib`'s `dilib1.obj` still carries no handler
table.

### The Release configurations searched a deleted SDK

Both Release blocks put `$(MSBuildThisFileDirectory)..\DX9\Lib` first on
`AdditionalLibraryDirectories`. AGENTS.md §2 says that vendored SDK is gone and
must not be assumed; the directory does not exist, so MSBuild was ignoring it.
It is a trap rather than a bug -- the legacy SDK splits its libraries into
`Lib\x86` and `Lib\x64`, so anyone restoring that tree would have fed 32-bit
import libraries to the x64 link. Removed from both.

---

## Blocker — resolved

### ~~The script VM stores function pointers in 32-bit instruction words~~

**Fixed by the script module rewrite** (`Docs/ScriptRewrite.md`,
`Docs/ScriptLanguage.md`). The instruction stream is now one `ScriptInstr`
record per instruction; `OP_CALL`/`OP_VARCALL` carry table indices resolved
at execution time, so no pointer lives in the stream on any platform. The
save-time pointer-to-index pass had no callers and was deleted outright.

### ~~The instinct FFI truncates every pointer parameter~~

Found during the rewrite, worse than the instruction stream: `stackPopParams`
wrote every popped parameter as a 4-byte store through destinations that at
hundreds of call sites were really `DROID**` or `STRING**`, and
`stackPushResult` carried object pointers in an `SDWORD`. **Fixed**: the FFI
is a typed interface (`ScriptParam`, `Stack.h`) whose store width is fixed at
compile time by the destination's type; every call site across
`ScriptFuncs.cpp`, `ScriptAI.cpp`, `ScriptCB.cpp`, `ScriptObj.cpp` and
`ScriptExtern.cpp` is converted and the varargs form is gone. The sweep also
surfaced and fixed four sites in `ScriptAI.cpp` that parked object pointers
in `SDWORD` locals, the `.vlo` loader passing object pointers through a
`UDWORD` parameter (`eventSetContextVar`, now typed), and
`eventCopyContext` copying context values 32 bits at a time.

---

## Measured

The first x64 build produced **203 unique warnings** (Debug; 205 Release); all
of them are now gone,
which is the only figure in this document that was ever counted rather than
estimated -- and it inverted the audit's expectation. The audit predicted
"mostly C4267, plus C4311/C4312 at the Watch sites". It was the other way
round: 130 pointer truncations against 35 `size_t` narrowings.

The reason is that the audit looked for *pointers parked in integers* and the
build found the reverse -- *integers parked in pointers*, almost all of them
one idiom. `WIDGET::pUserData` is a `void*`, and about half the game's widgets
store a small number in it: a `PACKDWORD_TRI` image triple, a player number, a
list index. The other half store a real object address, so the field cannot be
retyped. `IntDisplay.cpp` alone accounted for 52 warnings, `HCI.cpp` 32,
`MultiMenu.cpp` 19, `MultiInt.cpp` 17.

Three findings were not anticipated at all:

- **`Multibot.cpp:997`, C4789** -- `sendWholeDroid` wrote `asParts[COMP_WEAPON]`
  on a 32-byte stack array whose last valid index is `COMP_CONSTRUCT`,
  corrupting the stack. `Droid.cpp` had the same out-of-bounds index as a read.
  A genuine memory bug, not an x64 one; x64's stricter buffer analysis is
  simply what surfaced it.
- **`IMDLoad.cpp`** -- `_imd_load_bsp` parked each BSP child's file index inside
  the `link[]` pointer itself and resolved it in a second pass (12 warnings).
- **`Stats.cpp:2961` and `Game.cpp:1645/1695`** -- stats-table walks that
  advanced by casting `BASE_STATS*` through `UDWORD`.

All of the above are fixed. `widgPackUserData` / `widgUnpackUserData` in
`Widget.h` carry the widget integers through `uintptr_t`; the BSP indices and
the stats walks no longer put non-pointers in pointers; the `size_t`
narrowings are cast at the point of assignment.

Release surfaces two things Debug does not, because `DEBUG_ASSERT_TEXT` is
`__noop` in release and the optimiser then sees the fall-through.
`widgGetButtonState` and `functionType` both ended on an assert and returned
whatever was in the return register. `functionType` was the dangerous one: its
result indexes `pLoadFunction[]`, so an unrecognised `type` string in the
function stats table called through a garbage function pointer.

Fixing them also turned up one latent crash that had nothing to do with
pointer width. `intOpenPlainForm` read `pUserData` back as a `WIDGET_DISPLAY`
and installed it as the form's paint function whenever it was non-null.
Nothing ever stores a display function there -- on a plain form the field is
the close-animation flag -- so the only value the branch could ever have
installed was `(WIDGET_DISPLAY)1`. It was unreachable solely because
`HandleClosingWindows` deletes the form first.

---

## Watch

### Small integers parked in pointer fields

`Outpost/MultiJoin.cpp` stashes an object *ID* in the target *pointer* field
(`TarRef = (UDWORD)pD->psTarget;`) and resolves it after the object lists are
rebuilt. It is the same shape as the BSP loader, and it is listed here rather
than under Fixed because **the whole block is inside a `/* ... */` comment**
(`MultiJoin.cpp:347`-`581`) and compiles to nothing. If that code is ever
revived, the ID needs its own field.

### A whole struct written to a file, inside `#if 0`

`Outpost/MultiStat.cpp` has the old force-file format still in the tree:
`fwrite(pT, sizeof(DROID_TEMPLATE), 1, ...)` and the matching `fread`, which is
the same defect the network path had. It is behind `#if 0` -- the live format
writes only `multiPlayerID` -- so like the `MultiJoin.cpp` block above it
compiles to nothing. If it is ever revived it needs `PackTemplate`, not a
`memcpy`.

### `#pragma warning(disable:4244)` is tree-wide

`NeuronCore/NeuronCore.h` disables C4244 ("conversion, possible loss of data")
for every translation unit that includes it, which is all of them. On x64 that
class includes `__int64`-to-`int` narrowings, so the "zero warnings" figure
above is measured with one of the relevant diagnostics switched off. AGENTS.md
§4 says not to silence a diagnostic to make a build pass, so this is reported
rather than removed: C4244 also fires on every `float`-to-`int` in the legacy
tree, and nobody has counted how many that is. Sizing it needs an MSVC build
with the pragma commented out -- a measurement, then a decision.

### What is *not* a problem

- **The binary asset formats.** `.gam`, `.bjo`, `.pie`, and the new `.dds`
  texture files are read into POD structs with no pointer members, so their
  layouts do not move with the pointer size. The save-game readers, which did
  serialise richer structures, were deleted outright in an earlier phase.
- **The renderer.** Direct3D 9 has an x64 runtime and import library, the
  vertex structures are all fixed-width floats and `UDWORD` colours, and the
  device is addressed through COM interfaces.
- **Win32 window plumbing.** The tree does not use `SetWindowLong`/
  `GetWindowLong` for pointers — there are no calls at all — so the usual
  `GWLP_USERDATA` trap is absent.

---

## Suggested order

The compile is clean. What is left is everything a compiler cannot tell you:

1. ~~**Run it.**~~ **Done, 2026-08-26.** The boot works, the D3D9 device comes
   up, assets load and a base can be built; the two defects it surfaced are
   under *Found by running it* above. What it has still not covered is a full
   CAM_1A completion, a multiplayer session and FMV, so
   [Verification.md](Verification.md) is still the runsheet and most of it is
   still unticked.

   The 2026-08-17 Win32 play-through de-risked the shared code -- the widget
   user-data round-trip, the BSP loader, the stats-table walks and the
   `size_t` casts are all exercised by a normal game. What it could not speak
   to was the part that differs only at 64 bits, and that is exactly where the
   functionality-blob overflow was hiding.
2. **Make x64 blocking in CI.** It has now been run, so the precondition is
   met. Today
   `.github/workflows/build.yml` sets `continue-on-error` for the x64 legs,
   which was right while it did not build and is now only inertia. The test
   projects are not built by CI at all, on either platform, so the MSTest
   suites in `NeuronCoreTest` never run anywhere; wiring those in is the other
   half of the same job.
3. **Run `tools/crosscheck.py --x64` before pushing**, not just the default
   32-bit pass. The first run of the new flag found the `stackPushResult`
   ambiguity above, which the x86 pass called clean 180/180 in the same
   sitting. It costs one extra `apt-get install g++-mingw-w64-x86-64` in a
   fresh container and about a minute at `-j 8`.
4. **Revisit the `pUserData` design** if the widget layer is ever touched for
   other reasons. The helpers make the round-trip correct and honest, but the
   field still means two different things depending on which widget holds it,
   and there is an unused `UDWORD UserData` beside it that the integer users
   should arguably have been in all along.
