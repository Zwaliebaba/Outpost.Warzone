# x64 readiness

What stands between this tree and an x64 build, found by auditing for the
hazards a Win32 build cannot report: on Win32 `sizeof(void*) == sizeof(UDWORD)`,
so every pointer-through-integer round trip compiles clean and works. On x64
those same lines truncate a 64-bit pointer to 32 bits and hand back an address
that is not the one that went in.

The x86 CI build's own 343 warnings were cleaned up alongside this audit; that
work is separate and is described in the commit history. The Win32 build now
emits 12. Nothing here showed up as a warning on Win32 — it only appeared once
`Platform=x64` was built, and some of it never warns at all, because a
truncating cast is legal C++.

Status key: **Fixed** — done, and behaviour-identical on Win32.
**Blocker** — must be designed and done before x64 can run.
**Watch** — survives x64 by luck or convention; know it is there.

**State as of 2026-08-17: x64 compiles and links, both configurations, zero
errors.** The script VM, the one item that was a project rather than an edit,
was resolved by the module rewrite ([`ScriptRewrite.md`](ScriptRewrite.md)).
CI builds x64 on every push, non-blocking, and the diagnostics below are
measured from those builds rather than predicted. It has still never been
*run* -- see [Verification.md](Verification.md).

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

The first x64 build produced **203 unique warnings** (Debug; 205 Release),
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
2. **Make x64 blocking in CI** once it has been run at least once. Today
   `.github/workflows/build.yml` sets `continue-on-error` for the x64 legs,
   which was right while it did not build and is now only inertia.
3. **Revisit the `pUserData` design** if the widget layer is ever touched for
   other reasons. The helpers make the round-trip correct and honest, but the
   field still means two different things depending on which widget holds it,
   and there is an unused `UDWORD UserData` beside it that the integer users
   should arguably have been in all along.
