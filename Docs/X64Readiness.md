# x64 readiness

What stands between this tree and an x64 build, found by auditing for the
hazards a Win32 build cannot report: on Win32 `sizeof(void*) == sizeof(UDWORD)`,
so every pointer-through-integer round trip compiles clean and works. On x64
those same lines truncate a 64-bit pointer to 32 bits and hand back an address
that is not the one that went in.

The x86 CI build's own 343 warnings were cleaned up alongside this audit; that
work is separate and is described in the commit history. Nothing here shows up
as a warning on Win32 — it only appears when `Platform=x64` is first built, and
some of it will not appear even then, because a truncating cast is legal C++.

Status key: **Fixed** — done, and behaviour-identical on Win32.
**Blocker** — must be designed and done before x64 can run.
**Watch** — survives x64 by luck or convention; know it is there.

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
dereferenced on every comparison. The key is now `TREAP_KEY`
(`std::uintptr_t`); the five casts in `StrRes.cpp` follow it.

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

## Watch

### Small integers parked in pointer fields

Two idioms store a number where a pointer lives and read it back with a
narrowing cast. Both survive x64 — the stored value is small, so the truncated
high half is zero — but neither is defensible and both will warn (C4311/C4312).

- `Outpost/MultiInt.cpp`: `i = (int)psWidget->pUserData;` and the packed
  variants, at four sites. The widget layer's `pUserData` is `void*` and the
  front end uses it as an integer field.
- `Outpost/MultiJoin.cpp`: `TarRef = (UDWORD)pD->psTarget;` then
  `pD->psTarget = IdToPointer(TarRef, ANYPLAYER)`. The multiplayer join path
  stashes an object *ID* in the target *pointer* field and resolves it after
  the object lists are rebuilt. It works, but the field's type is a lie for
  the duration.

### `strlen` and `sizeof` into 32-bit locals

`size_t` is 64-bit on x64, so `int Len = strlen(...)` becomes C4267. Present in
`NeuronCore/Frame.cpp`, `NeuronClient/TextDraw.cpp`, `NeuronClient/Anim.cpp`,
`NeuronClient/AnimObj.cpp` among others. Harmless for the string lengths this
game handles — none approach 2 GB — but it is noise that will bury real
findings in the first x64 build, so it is worth a mechanical pass.

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

1. Add the x64 configuration and build it. Expect a wall of C4267/C4311/C4312;
   the four **Fixed** items above are already out of the way, so what remains
   should be the **Watch** list plus the script VM.
2. Do the script VM (option 1). Nothing runs until it is done.
3. Sweep the `size_t` narrowings.
4. Retype `pUserData` and the join-path target field, or wrap both in named
   helpers that make the intent explicit.
