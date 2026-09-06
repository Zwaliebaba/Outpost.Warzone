# x64 readiness

> **Outcome (2026-08-27): x64 is the platform that ships.** Win32 is out of
> CI, which now builds Debug and Release for x64 only, and both block. This
> document keeps its original framing below — it was written as "what stands
> between this tree and an x64 build" and reads as history now. Nothing is
> left under **Watch** but the list of what was checked and found not to be a
> problem; the last four Watch items closed on 2026-09-06.

What stood between this tree and an x64 build, found by auditing for the
hazards a Win32 build cannot report: on Win32 `sizeof(void*) == sizeof(UDWORD)`,
so every pointer-through-integer round trip compiles clean and works. On x64
those same lines truncate a 64-bit pointer to 32 bits and hand back an address
that is not the one that went in.

The x86 CI build's own 343 warnings were cleaned up alongside this audit; that
work is separate and is described in the commit history. Nothing here showed up
as a warning on Win32 — it only appeared once `Platform=x64` was built, and
some of it never warns at all, because a truncating cast is legal C++.

**Both x64 configurations build warning-free**, Debug and Release, zero
warnings and zero errors -- and since 2026-09-06 that figure is measured with
C4244 enabled, which the tree-wide pragma had switched off (see *Fixed on the
fourth pass*). The Win32 configurations are unmaintained.

Status key: **Fixed** — done, and behaviour-identical on Win32.
**Blocker** — must be designed and done before x64 can run.
**Watch** — survives x64 by luck or convention; know it is there.

**State as of 2026-08-17: x64 compiles and links, both configurations, zero
errors.** The script VM, the one item that was a project rather than an edit,
was resolved by the module rewrite ([`ScriptRewrite.md`](Archive/ScriptRewrite.md)).
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

What the run did **not** cover at that point was a full CAM_1A completion, a
multiplayer session (so the corrected `NET_TEMPLATE` wire format had never
carried a packet), or FMV. **2026-09-06: the owner validated that run coverage
and closed it.** [Verification.md](Verification.md) remains the runsheet.

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
`Docs/Archive/AssetPipeline.md`.

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

## Fixed on the third pass (2026-08-27)

### `stackPopType` copied a popped value through its four-byte member

`NeuronCore/Stack.cpp`'s `stackPopType` finished with

```cpp
psVal->v.ival = psTop->v.ival;
```

`INTERP_VAL::v` is a union of `SDWORD ival` and `void* oval`. On Win32 those
are the same width, so copying through `ival` moved **every** type correctly by
accident. On x64 it takes the low four bytes of an eight-byte pointer and
leaves the high half behind.

Every caller is a pop *into a variable* -- `OP_POPGLOBAL` and the two
variable-pop paths in `Interp.cpp`, plus the result pop in `Event.cpp` -- so
the truncation landed in script variables of object type. Those are exactly
the values `scrvAddBasePointer` registers, which is where it surfaced: a live
`BASE_OBJECT` arrived in `scrvUpdateBasePointers` as `0x00000000367f8b70`, and
`psObj->died` read freed memory. The address is its own diagnosis -- a heap
pointer with the high 32 bits zeroed is a pointer that went through a 4-byte
copy.

The fix is the idiom the rest of the VM already uses: `psVal->v = psTop->v;`
copies the whole union whatever the type is, and cannot drift out of step with
a list of which types are pointers. `eventCopyContext` and `eventSetContextVar`
were converted to it by the script rewrite; this is the one site that sweep
missed, because it reads as an int assignment rather than as a value copy.

`NeuronCoreTest`'s `ObjectMembersAndEquality` already covered the defect -- it
assigns an object into a script variable and then writes through it -- but
nothing had ever run the suite. It does now, on both platforms, which is what
turned this from a crash report into a test. `StackPopTypeKeepsTheWholeObjectPointer`
names it directly, since the script-level test fails by dereferencing the
truncated pointer rather than by reporting it.

## Found by running it, second round (2026-09-06)

### The design screen's shadow bars indexed one stats array with another's pointer

Hovering a component on the design screen draws a "shadow" on the body and
power bars: what the design would score with that component fitted.
`intSetTemplateBodyShadowStats` and `intSetTemplatePowerShadowStats` built the
comparison template by copying `sCurrDesign` and writing the hovered stat's
index into one slot -- and on the system tab they chose the slot from **what
the design already carried**, not from the hovered stat. The system tab lists
sensors, ECMs, brains, constructors and repair units together, so a design
with a weapon fitted meant `COMP_WEAPON`, and hovering a sensor ran

```cpp
compTempl.asWeaps[0] = (WEAPON_STATS*)psStats - asWeaponStats;
```

with `psStats` pointing into `asSensorStats`. That is the distance between two
unrelated heap blocks in units of `sizeof(WEAPON_STATS)`, narrowed from
`ptrdiff_t` into the template's `UDWORD` slot. `calcTemplateBody` then
evaluated `asWeaponStats + asWeaps[0]` and faulted (`Droid.cpp:2955` before
this change).

Why it is an x64 defect: on Win32 the narrowing is lossless and the pointer
arithmetic wraps at 32 bits, so a negative difference truncated to `UDWORD`
and added back to the base lands where it started -- near the sensor array,
which is mapped -- and the bar showed a garbage number nobody noticed. On x64
the `UDWORD` is zero-extended to a 64-bit offset, so a negative difference
becomes ~4G entries, times 240 bytes (`sizeof(WEAPON_STATS)` on x64, measured
with the cross-checker), and the read lands a terabyte past the heap. It
reproduces only when the hovered array sits below the weapon array in memory,
which is why the 1999 comment on the function said it "appears to cause a
crash" rather than "crashes". The `asParts` slots are `SDWORD`, so the same
mistake with a sensor design and a hovered ECM still sign-extends and merely
reads the wrong struct; only the weapon slot is unsigned, and only it faults.

The fix takes the slot from the hovered stat's own `ref`, subtracts the base
of the array that ref names, and clears the other system slots the way a
click on the same button does (`SetShadowTemplateComponent` in `Design.cpp`,
with the subtraction asserted in range at the point it becomes an index).
`Droid.cpp`'s `calcTemplate*` family now runs a Debug-only
`CheckTemplateIndices` first, so any other template carrying an index past its
array fails naming the template rather than as an access violation inside the
arithmetic.

One thing this does not explain: the fault was reported with `asWeaponStats`
showing `0x011104376310DCEA`. No 32-bit index added to a canonical user-mode
base reaches a 57-bit value, so if that was the global's own value rather than
the faulting expression's, the global was corrupt as well and needs a hardware
write breakpoint on `&asWeaponStats` after `statsAllocWeapons` to find the
writer. The path above faults on the same line, from the same call stack, with
the global intact.

**Closed 2026-09-06, not reproduced.** The owner's validation run did not see
it again and the fix above explains the fault path in full. The write
breakpoint is the tool if it ever comes back.

---

## Blocker — resolved

### ~~The script VM stores function pointers in 32-bit instruction words~~

**Fixed by the script module rewrite** (`Docs/Archive/ScriptRewrite.md`,
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

## Fixed on the fourth pass (2026-09-06)

The four items that sat under *Watch* until this pass, taken by owner
decision on 2026-09-06.

### `==` on two objects compared only the low 32 bits

`stackBinaryOp` implemented `OP_EQUAL`/`OP_NOTEQUAL` as
`psV1->v.ival == psV2->v.ival`, which on x64 compares the low half of two
object pointers. The objection to fixing it was a linear walk of the type
table on the VM's hot path; there is no walk. `scriptSetTypeTab` now builds a
bitset of the `AT_OBJECT` type ids once, `ScriptTypeIsObject` is one lookup,
and the interpreter compares `oval` when both operands are objects and `ival`
otherwise -- a simple value is written through `ival` and the rest of its
union is indeterminate, so it must not be compared whole.
`ObjectEqualityComparesTheWholePointer` in `NeuronCoreTest` pins it with two
synthetic addresses that agree in the low half.

### The tree-wide `#pragma warning(disable:4244)` is gone

Measured first: with the pragma commented out, the x64 Debug build reported
**52 unique C4244 sites**, 48 in `Outpost` and 4 in `NeuronClient`:

| Conversion | Sites |
|---|---|
| `__int64` to a 32-bit integer | 18 |
| `long` or `WPARAM` to a narrower integer | 3 |
| `float`/`double` to integer, or integer to `float` | 31 |

Every one of the 18 was a pointer difference used as an index --
`psCurr->pStructureType - asStructureStats`, `psResearch - asResearch`,
`psWeapStat - asWeaponStats` -- the exact pattern behind the design-screen
fault above, which the pragma had been hiding at every other site. They now go
through `StatIndex` in `Outpost/Stats.h`, which asserts the pointer is inside
the array it is being indexed against at the point it becomes an index;
`ShadowStatIndex` in `Design.cpp` delegates to it. The float and integer
conversions are cast where they narrow.

The measurement also surfaced three Phase 10 escapes that C4244 had been
reporting into the void, all of them a radian angle meeting integer code:

- `moveBlocked` passed two radian directions to `dirDiff`, a degrees
  function taking `SDWORD`, so both truncated to 0 or +-1 and the block-cancel
  test compared that against `BLOCK_DIR` in radians. It now uses
  `directionDiff`; `dirDiff` had no other caller and is deleted.
- `moveCalcDroidSpeed` clamped the droid's radian pitch against
  `MAX_SPEED_PITCH`, which is 60 degrees. The pitch is converted to degrees
  first.
- `moveUpdateJumpCyborgModel` took its direction as `SDWORD` and was called
  with a `float`; `formationCalcPos` stored a radian direction in an `SDWORD`
  before taking its sine. Both are `float` now.

The pragma is gone from `NeuronCore/NeuronCore.h`; Debug and Release x64 both
build with zero warnings, C4244 included.

### Template indices off the wire are validated

`receiveWholeDroid` now runs `TemplateIndicesValid` (the check
`calcTemplate*` runs in Debug, exported from `Droid.cpp` as a predicate) on the
template it reassembles from the packet, and refuses the droid if any index is
outside its stats array, in Release as well as Debug. Both ends are still the
same build, so a refusal is a bug at the sender; the point is that no index the
game did not compute reaches the arithmetic unchecked.

### The two dead blocks are deleted

The commented-out `multiPlayerRequest` in `Outpost/MultiJoin.cpp` (the object
ID parked in a target pointer) and the `#if 0` force-file format in
`Outpost/MultiStat.cpp` (a whole `DROID_TEMPLATE` through `fwrite`) compiled to
nothing and existed only to be revived wrongly. Git history keeps the text.

---

## Watch

### What is *not* a problem

- **The binary asset formats.** `.gam`, `.bjo`, `.pie`, and the new `.dds`
  texture files are read into POD structs with no pointer members, so their
  layouts do not move with the pointer size. The save-game readers, which did
  serialise richer structures, were deleted outright in an earlier phase.
  **`.nmo`, the model format added in 2026-08, was designed against this
  section** ([NeuronMeshObject.md](NeuronMeshObject.md)): fixed-width fields
  only, no `wchar_t` — which is 2 bytes on Windows and 4 on the mingw
  cross-checkers, and is exactly why the CMO format it derives from could not
  be adopted unchanged — and 31 `static_assert`s on the on-disk struct sizes
  and alignments that are compiled on both targets. A file written by an x86
  build is byte-identical to one written by an x64 build, and the assertions
  are what makes that a checked claim rather than an intention.
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
2. ~~**Make x64 blocking in CI.**~~ **Done, 2026-08-27** (`8f3742a`).
   `.github/workflows/build.yml` builds Debug and Release for x64 only, both
   block, and the three test suites run through `vstest.console` on every
   push.
3. ~~**Run `tools/crosscheck.py --x64` before pushing.**~~ **Moot:** the
   checker's default target is x64 now and `--x86` is the flag for the
   unmaintained 32-bit pass.
4. **`pUserData` -- accepted as it is (owner decision, 2026-09-06).** The
   helpers make the round-trip correct and honest; the field still means two
   different things depending on which widget holds it, and there is an unused
   `UDWORD UserData` beside it that the integer users should arguably have
   been in all along. Not worth the churn on its own; revisit only if the
   widget layer is rewritten.
