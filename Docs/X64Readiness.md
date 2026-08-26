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
measured from those builds rather than predicted. It has still never been
*run* -- see [Verification.md](Verification.md).

**2026-08-26.** `tools/crosscheck.py` grew an `--x64` flag, and the four
findings under *Fixed on the second pass* below came out of it and out of a
re-audit of what crosses a process boundary. The harness had only ever run
`i686-w64-mingw32-g++`, so the Linux pre-CI gate was blind to exactly the
class of defect this document exists for. All four crosscheck configurations
-- x86 and x64, debug and release -- are now 180/180 clean.

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

1. **Run it.** A clean x64 compile proves nothing about the D3D9 device, the
   MsQuic import or asset loading. The CAM_1A boot is the test, and
   [Verification.md](Verification.md) is the runsheet.

   Win32 *has* been played with all of the changes below in it (2026-08-17),
   which de-risks the shared code: the widget user-data round-trip, the BSP
   loader, the stats-table walks and the `size_t` casts are all exercised by a
   normal game and none of them misbehaved. What that run cannot speak to is
   the part that only differs at 64 bits — whether a pointer survives the
   places this document was written about — so x64 still needs its own boot.
2. **Make x64 blocking in CI** once it has been run at least once. Today
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
