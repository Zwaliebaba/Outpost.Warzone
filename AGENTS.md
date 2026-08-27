# AGENTS.md — Engineering Rules for *Outpost: Warzone*

Operating instructions for every agent (and human) writing code in this repository. **Read this before generating a single line.**

*Outpost: Warzone* is a 1998 C game being migrated to C++ and Direct3D 9. Most of the tree is legacy code that predates every rule below; the rules govern **what you write**, not what you find.

**What is authoritative, in order:**

1. **This file** — conformance: naming, style, build, and how to work here.
2. **[Docs/MigrationPlan.md](Docs/MigrationPlan.md)** — design: what the target is, which phase is done, what each subsystem became and why. Read the phase your task touches before you start.
3. **The surrounding code** — for anything neither covers, match the file you are editing.

If a rule here conflicts with a habit from another codebase, this file wins. If you think a rule is wrong or your task cannot be done without deviating, **say so in your report — never deviate silently.**

---

## 1. Naming convention (normative — no exceptions)

| Kind | Convention | Example |
|---|---|---|
| Type (class, struct, enum, concept, alias) | `PascalCase` | `ScreenPixelFormat` |
| Function, method | `PascalCase` | `LoadTexturePage()` |
| Member variable | `m_camelCase` | `m_deviceLost` |
| Static member | `sm_camelCase` | `sm_activeDevice` |
| Global | `g_camelCase` | `g_animGlobals`, `g_frameCount` |
| Parameter | `_camelCase` | `_fileName`, `_droidId` |
| Local | `camelCase` | `pageIndex` |
| Constant, enumerator | `PascalCase` | `MaxDroids`, `DeviceLost` |
| Macro | `SCREAMING_SNAKE` | `DEBUG_ASSERT`, `DEBUG_WARNING` |
| Namespace | `PascalCase` | `Neuron` |
| File | `PascalCase.cpp` / `.h` | `Render.cpp` |

**This table governs code you write.** The legacy tree does not follow it — `SCREEN_PIXELFORMAT`, `g_psDevice`, `g_bAudioEnabled` and friends are grandfathered, not exemplars. Do not extend those patterns into new code, and do not mass-rename them either (§4).

### The rules behind the table

**R1 — The leading underscore on parameters is deliberate.** It is legal C++: the reserved forms are `_Uppercase`, anything containing `__`, and `_lowercase` **at global scope**. A parameter is never at global scope, so `_fileName` is safe. Never introduce a reserved form — no `_Impl`, no `__helper`, no file-scope `_cache` (use `g_cache` in an anonymous namespace).

**R2 — A type name carries no prefix, and that includes abstract ones.** An interface is `Transport`, not `ITransport`. A base class is not `BaseTransport` or `AbstractTransport`. PascalCase means the name and nothing else — the Hungarian remnants in the legacy tree are not a precedent, and none is to be introduced. Name the concept and let the concrete types say what they are:

```
Transport             ← the concept
├── UdpTransport      ← a socket-backed one
└── LoopbackTransport ← in-process, for tests
```

This also bans `CFoo`, `SFoo`, `EFoo`, `FooBase`, `IFoo`, and `_t` suffixes.

The tree above is an illustration of the rule, not a description of anything.
Phase 5 shipped as a single `Transport` in [Transport.h](NeuronCore/Transport.h)
with static methods and no hierarchy at all, because there is exactly one
implementation and a base class for one derived class is ceremony. Name the
concept; add the layer when a second thing needs it.

**R3 — Compile-time constants are Constants.** `static constexpr` members and namespace-scope `constexpr`/`inline constexpr` take PascalCase (`MaxDroids`, `TileWidth`, `TextureCacheBytes`). `sm_` is reserved for *mutable* statics, which are rare and must document their thread-safety.

**R4 — Acronyms capitalize as words**: `ImdModel`, `RplStream`, `CdAudioTrack`, `UdpTransport` — never `IMDModel`. Identifiers from an external SDK keep that SDK's spelling (`IDirect3DDevice9`, `D3DFORMAT`, `HRESULT`, `LPDIRECTINPUTDEVICE7`) and are never renamed to fit.

**R5 — Template parameters are PascalCase**: `T`, `Fn`, `BlockBytes`, `Ts...`.

**R6 — Units belong in names; types do not.** `rangeTiles`, `durationTicks`, `speedUnitsPerTick` are encouraged — a world measured in tiles, world units and game ticks makes unit ambiguity a real defect class. Never encode the type: no `iCount`, `pDroid`, `strName`. The legacy Hungarian in the tree (`g_psDevice`, `bAudioEnabled`, `uiRet`) is exactly what this rule bans; it stays where it is and spreads nowhere.

**R7 — A file is named for its primary type**, PascalCase, `.h` / `.cpp` only. `.hpp`, `.cc`, `.inl` are not used; template implementations live in the header. One exception: per-project `pch.h`/`pch.cpp` keep the name MSBuild expects. (The grandfathered lex/yacc spelling — `Script_y.cpp`, `StrRes_l.cpp` and their kin — is gone; **no generated parser code remains in the tree**, and none is to be reintroduced. Formats get hand-written parsers, as `Neuron::Json`, `ScriptLex`/`ScriptComp` and `strresLoad` do.)

**R8 — `m_` marks encapsulated state, not every field.** A `class` with invariants prefixes private members `m_`. A public aggregate — a `Desc` config struct, a wire record, a POD passed to the renderer — uses plain `camelCase` fields so brace initialization reads naturally. This rule extends the table rather than quoting it.

### Worked example — this is the target style

```cpp
// NeuronCore/TexturePage.h
#pragma once
#include <cstdint>
#include <d3d9.h>

namespace Neuron
{

inline constexpr std::uint32_t MaxTexturePages = 64;   // R3: constant → PascalCase

enum class PageFault : std::uint8_t { NotFound, BadFormat, OutOfVideoMemory };

/// One texture page held on the device. R2: no prefix on the type; R8: private state carries m_.
class TexturePage
{
public:
  struct Desc                                          // R8: aggregate → plain fields
  {
    std::uint32_t widthTexels;                         // R6: unit in the name
    std::uint32_t heightTexels;
    D3DFORMAT format;                                  // R4: SDK spelling kept as-is
  };

  [[nodiscard]] static bool Create(IDirect3DDevice9* _device,   // R1: _ on parameters
                                   const Desc& _desc,
                                   TexturePage& _outPage) noexcept;

  [[nodiscard]] bool Restore(IDirect3DDevice9* _device) noexcept;
  [[nodiscard]] std::uint32_t PageId() const noexcept { return m_pageId; }

private:
  IDirect3DTexture9* m_texture = nullptr;
  std::uint32_t m_pageId = 0;
  bool m_deviceLost = false;
};

} // namespace Neuron
```

### Enforcement

[`.clang-tidy`](.clang-tidy) at the repository root is the machine-readable statement of every rule above, and it is the **single source of truth** for the option values — this document does not repeat them, so there is nothing to drift. `readability-identifier-length` is deliberately left off: its defaults reject the domain vocabulary this codebase is built on (`x`, `y`, `_a`, `_b`), and short names here are precise, not lazy.

**Nothing runs it automatically yet.** [`build.yml`](.github/workflows/build.yml) builds Debug and Release for x64, builds and **runs the three CppUnitTest suites** through `vstest.console.exe`, and runs `tools/check_case.py` (which verifies every `#include` and project entry spells its file with the exact on-disk case — MSVC resolves includes case-insensitively, so a wrong name still builds). There is no clang-tidy step, and the `.vcxproj` files do not enable MSBuild's code analysis. Until one of those changes, §1 is enforced by **review**: check your own diff against the table before handing it back.

**When you do run it, run it on what you wrote, not on the tree.** The legacy code predates every rule here (§1, under the table), so a whole-tree pass reports thousands of grandfathered findings and tells you nothing. Point it at the files your change adds, or filter to your changed lines:

```
clang-tidy --quiet NeuronCore/YourNewFile.cpp -- -I NeuronCore -D WIN32 -D _DEBUG
```

Two rules the config cannot express, and that a reviewer therefore has to carry:

- **R2 (type prefixes)** — clang-tidy's `AbstractClassPrefix` can require an *absent* prefix but cannot ban a *present* suffix, so `FooBase` slips through. Grep declaration sites, `using` and `typedef` aliases included, for `\b(class|struct|using)\s+[ICSE][A-Z]` and for trailing `Base`/`Abstract`/`Impl`.
- **R7 (file naming)** — nothing checks that a new file is PascalCase and `.h`/`.cpp`. Look at the filename when you add one, and at the `.vcxproj`/`.filters` entries that must accompany it (§6).

---

## 2. Repository map

| Path | What it is | May you edit it? |
|---|---|---|
| `NeuronCore/` | Engine static library (24 TUs): platform, timing, the resource system and the `Neuron::Json` reader, containers, maths, the script compiler and VM, string resources, networking and transport, and the client/server wire protocol | Yes |
| `NeuronClient/` | Client-side engine static library (45 TUs): the window, D3D9 rendering, IMD models, animations, DirectInput, XAudio2, UI widgets, fonts, images, FMV sequences, the NMO model loader, and the client half of a session | Yes |
| `NeuronServer/` | Server-side engine static library (3 TUs): the server half of a session and one client's replication stream. The simulation itself is still in `Outpost/` | Yes |
| `Outpost/` | Game executable (118 TUs): simulation, AI, structures, droids, campaign, multiplayer | Yes |
| `NeuronCoreTest/`, `NeuronClientTest/`, `NeuronServerTest/` | MSVC CppUnitTest DLLs, one per engine library, each referencing the library it tests and the libraries that library is built on. `NeuronCoreTest` holds the `Neuron::Json`, script compiler/VM, wire-format and protocol suites; the other two hold the client and server halves of a session, and `NeuronClientTest` also holds the NMO loader suite. **CI builds and runs all three** | Yes |
| `GameData/` | Shipped content. The JSON manifests and tables (`datasets.json`, stats, messages, anims, audio configs) are text authored in this repo — `tools/validate_assets.py` must stay green. The binary media (textures, models, `.wav`, `.mp4`, levels) is authored by tools outside this repo | JSON: yes, validated. Binary: **No** |
| `Docs/MigrationPlan.md` | The plan and the record of what each phase changed | Yes — see §6 |
| `.clang-format`, `.clang-tidy`, `.editorconfig` | Layout and naming, machine-readable (§1, §4) | Yes — with an owner decision |
| `tools/*.py` | Repository checkers and content-authoring scripts (§3) | Yes |
| `tools/blender_nmo/` | The Blender import/export add-on for `.nmo`, and `nmo_format.py` — the reference codec the converter and the tests are built on | Yes |
| `.github/workflows/build.yml` | CI: Debug and Release for x64, the three unit-test suites, plus the script corpus. All of it blocks | Yes, carefully |
| `Debug/`, `x64/`, `.vs/`, `*.user` | Build and IDE output | **No — and never commit them** |

**There is no vendored SDK.** `DX9/Include` and `DX9/Lib` held a checked-in DirectX 9.0c
SDK and are gone: the Windows SDK ships `d3d9.h`, `dinput.h`, `d3d9.lib`, `dinput8.lib`
and the rest, so nothing needed them. `NetTest/`, the console harness for the Phase 5
QUIC transport, is gone with them. Do not restore either, and do not add a build step
that assumes they exist.

**Seven projects, and the edges run one way.** `Outpost.slnx` is the solution; its
platforms are `x64` (the one CI builds and gates) and `x86` (unmaintained, see §3).

```
NeuronCore.lib          ← the engine everything else builds on
├── NeuronClient.lib    ← references NeuronCore
├── NeuronServer.lib    ← references NeuronCore
└── Outpost.exe         ← references all three
NeuronCoreTest.dll      ← references NeuronCore; likewise NeuronClientTest,
                          NeuronServerTest against their own libraries
NeuronClientTest.dll    ← also references NeuronServer, for one test (below)
```

**One edge is deliberately not in that shape.** `NeuronClientTest` references
`NeuronServer` as well, so that `ReplicatedWorldTest` can run a `ReplicationWriter`
against a `ReplicaStore` and assert that the client's world is the server's world.
That property belongs to the boundary rather than to either half, the repository
has no home for a test of the boundary itself, and an eighth project for one test
class buys less than it costs. The direction is the cheap one — `NeuronServer` is
three translation units over `NeuronCore`, where the reverse would drag D3D9,
DirectInput and XAudio2 into the server's test. **This is a test-project edge and
not a library one:** the library graph above is unchanged, and nothing in
`NeuronClient` may reach `NeuronServer`.

`NeuronClient` and `NeuronServer` are the destination of the engine split: game-engine
code that is meaningful only to a client (presentation, input, local prediction) or only
to a server (authoritative simulation, session ownership) moves out of `NeuronCore`,
which keeps what both need. **The client half has landed**: presentation, input and
audio moved to `NeuronClient` on 2026-08-16. The server half has begun but is not done:
`NeuronServer` owns the session and its replication stream, and the simulation is still
inside `Outpost.exe` — [Docs/ServerAuthority.md](Docs/ServerAuthority.md) stages D and E
are what move it. **Until a task is that split, do not move a file between them** — see
R13.

---

## 3. Build and verify

**x64 is the platform that ships** (owner decision, 2026-08-27), toolset v145, `/std:c++latest`, and there is no CMake. Win32 is gone from CI: the workflow builds Debug and Release for x64 only, and both **block** — there is no second platform to fall back on and nothing left to excuse a red x64. The 32-bit configurations still exist in the `.vcxproj` files and are not maintained; do not add code that only works at 32 bits, and do not spend a round making them build. [`Docs/X64Readiness.md`](Docs/X64Readiness.md) records how the migration went and what is still on watch; read it before writing anything that puts a pointer in an integer, a struct on a wire, or a struct in a file. If a build error tempts you to change the toolset or lower the language standard — stop and report instead.

```powershell
# Build the game. Its project references pull in NeuronCore, NeuronClient and
# NeuronServer, so this builds all four.
msbuild Outpost\Outpost.vcxproj /p:Configuration=Debug /p:Platform=x64 /v:normal /nologo

# Build everything including the test DLLs.
msbuild Outpost.slnx /p:Configuration=Debug /p:Platform=x64 /v:normal /nologo

# Filename-casing gate. MSVC resolves includes case-insensitively, so a wrong
# #include still builds on Windows and only fails on the Linux checkers. Run it.
python tools/check_case.py
```

Both commands run from the repository root. Use `$(MSBuildThisFileDirectory)` in a
`.vcxproj`, never `$(SolutionDir)`: MSBuild leaves `SolutionDir` empty when it builds a
project file directly, which is what the first command and CI both do.

**The engine libraries build `/permissive-`; the game does not yet.** `NeuronCore`,
`NeuronClient` and `NeuronServer` set `ConformanceMode=true`, `Outpost` sets it to
`false`. Do not switch a project's conformance to make a build pass — fix the code. What
this mostly means in practice is R16.

The executable lands in `Debug\Outpost.exe` (or `Release\`), and it needs `GameData/` beside it at runtime.

**Without MSVC** (Linux container), `tools/crosscheck.py` syntax-checks every translation unit with mingw-w64 against a shadow tree. It is a fast first pass, **not a build**: it cannot link, and MSVC disagrees with GCC in both directions. The Windows CI build is the authority.

```
apt-get install -y g++-mingw-w64-x86-64     # once per container
python3 tools/crosscheck.py -j 8            # x64, which is what CI builds
```

The default is x64, matching CI. `--x86` still exists and checks the unmaintained 32-bit configurations; there is no reason to run it. The width difference is why the platform matters: on Win32 `sizeof(void*) == sizeof(UDWORD)`, so a whole class of defect compiles and *runs* clean at 32 bits and only appears at 64 — `stackPopType` copying an object value through a four-byte union member survived years of shipping x86 that way.

**After touching a simulation file**, run `python tools/crosscheck.py --sim-only`.
It compiles the candidate server-side units with `NeuronClient` taken off the
include path, and it is a **ratchet**: `NeuronCore` must stay at zero failures
and the `Outpost` count must fall, never rise. Reaching for a client header from
simulation code is how the server/client split gets quietly undone — see
[Docs/ServerAuthority.md](Docs/ServerAuthority.md) stage B for the current count
and what is left.

**After touching the model format or its tools**, run the three checks that
keep the two implementations of NMO honest. They are Python and take a second
each, so there is no excuse for skipping them:

```
python tools/nmo_roundtrip_test.py     # the reference codec: byte-exact round trip, 21 rejections
python tools/make_nmo_fixture.py       # regenerate NeuronClientTest/NmoFixture.h; commit if it changes
python tools/pie_to_nmo.py --report    # all 516 shipped models still convert
```

`NmoFixture.h` is **generated** — the golden `.nmo` the C++ tests load, written
by the Python codec so both implementations are tested on the same bytes. Edit
`tools/nmo_fixture.py` and regenerate; never hand-edit the header. The
Blender add-on has its own test (`tools/nmo_blender_test.py`), which needs
Blender's Python and is therefore not part of the routine pass.

**CI runs the C++ half and not the Python half.** `NmoTest.cpp` is inside
`NeuronClientTest`, so the loader is gated like every other suite. Nothing runs
the three commands above, which are what hold the *reference codec* and the
converter to the same specification — so they are yours to run, like
`clang-tidy` in §1. They are three seconds of Python and would sit happily
beside the script corpus if someone is in the workflow anyway.

**After touching the script module**, run `tools/check_scripts.py`. It builds `NeuronCore/ScriptLex.cpp` and `ScriptComp.cpp` from source against the game's real symbol tables and compiles all 59 shipped `.slo` files, then checks the `.vlo` corpus for types the tables do not have. No C++ check can tell you the compiler still accepts the scripts — `int` and `bool` are keywords rather than table entries, and forgetting that built cleanly and rejected 56 of 59 scripts at runtime.

**A green build says nothing about whether the game draws or runs.** For anything touching rendering, input, audio or level loading, launch it:

```
Debug\Outpost.exe -window -game CAM_1A
```

That boots straight into a campaign level — 3D world, HUD, terrain, units, translucent build overlay — without menu input.

`Neuron::Fatal` calls `__debugbreak()`, so a failed assertion under a launcher surfaces only as exit code `0xC0000003` with **no message**. The text goes to `OutputDebugString`; attach a `DBWIN_BUFFER` listener (DebugView, or run under the VS debugger) to turn that exit code back into a diagnosis.

**Report what you actually did.** "Builds clean, not run" and "builds and runs the CAM_1A boot" are different claims. Never imply the second when you only did the first.

---

## 4. Layout and formatting

[`.clang-format`](.clang-format) is the authority for C++ layout: 2-space indent, 140 columns, Allman braces, pointer/reference bound left, includes never reordered. [`.editorconfig`](.editorconfig) covers everything clang-format does not — CRLF, UTF-8, final newline, trailing whitespace, and the non-C++ formats — and repeats the two numbers an editor needs before the first save. **No CI step runs clang-format**, and the tree is not formatted to it, so it describes what you write rather than what you will find.

- **Format only the lines you touch.** No repo-wide reformat, no bulk include reordering, no drive-by renames. A diff whose reviewable content is buried in whitespace churn will be rejected.
- **Match the file you are in.** Legacy headers use `_screen_h`-style guards, `/* */` comments and `UDWORD`-family typedefs. Leave them; do not modernise a file as a side effect of editing three lines of it.
- **New files** follow the §1 worked example: `#pragma once`, PascalCase filename, Allman braces.
- Do not silence a diagnostic with `#pragma warning(disable: ...)` to make a build pass. Fix the cause, or report it.

---

## 5. C++ rules for this codebase

The migration has already made these decisions. Use the replacement that is already in the tree — do not invent a third way, and do not restore what was deliberately removed.

**R9 — Assertions come from [`NeuronCore/Debug.h`](NeuronCore/Debug.h).** `DEBUG_ASSERT`, `DEBUG_ASSERT_TEXT`, `DEBUG_WARNING` — the `_TEXT` forms take a `std::format` string, and all three compile to `__noop` in Release while still type-checking their arguments. Never `#include <assert.h>`, and never resurrect the legacy debug system (removed in Phase 7).

**R10 — Memory is plain C++.** `new`/`delete`, RAII, and the standard containers. The custom allocators (`Mem.cpp`, `Heap.cpp`, `Block.cpp`) were deleted on purpose, and so were the hand-rolled containers that carried their own node pools (`HashTabl`, `Treap`, `PQueue`, `PtrList`); do not reintroduce a pool, a slab or a free-list without an owner decision.

**R11 — No inline assembly.** The last `__asm` block is gone, which is part of what makes the tree portable to the cross-checker. Write the C++ equivalent.

**R12 — Graphics is Direct3D 9 only.** An `IDirect3DDevice9` is owned by `Screen.cpp` and drawn through by `Render.cpp` and `TexMan.cpp`. No DirectDraw, no surfaces, no palettes, no `GetDC`. New COM code uses C++ `device->Method(...)` syntax with RAII lifetimes — `CINTERFACE` is gone and is not coming back.

Phase 8 is removing the `pie_*`/`iV_*` layer that used to sit between the game and that device, so the renderer is mid-move: **do not add a new indirection in front of `Render.cpp`**, and do not reintroduce a second cache of a render state — one cache, owned by the code that makes the device call, is the rule the phase is establishing. `D3DMode.cpp`, `PieState.cpp` and `PieTexture.cpp` have been folded into their neighbours and are not to come back. Stage C2 has renamed the render files: `Render.cpp/.h`, `RenderModel.cpp`, `Render2D.*`, `RenderMatrix.*`, `RenderClip.*` and `Palette.*` are the current names for what used to be `D3DRender`, `PieDraw`, `PieBlitFunc`, `PieMatrix`, `PieClip` and `PiePalette`.

Stage C has also finished the type headers and the `iV_` prefix. `RenderTypes.h` holds the render value types and draw constants, `Model.h` the `iIMDShape` family, `RenderModel.h` the `pie_Draw*` declarations; `BitImage.h` owns the `IMAGEFILE` structures and `RendMode.h` owns `iSurface`. `Ivi.h`, `Ivi.cpp`, `IvisDef.h`, `PieDef.h`, `PieTypes.h` and `Bug.h`/`Bug.cpp` are gone and are not to come back. The `iV_` prefix is now `namespace Neuron` — **do not strip a legacy prefix without first checking the bare name against the Win32 and CRT headers**; eight of the 87 `iV_` names collided, and `iV_HeapAlloc` was a macro that would have hijacked every `kernel32` call. **Headers must include what they use**: the tree relied on hub headers arriving first for years, and C3 fixed ~30 of them.

**R13 — Leave a subsystem mid-migration alone** unless your task *is* that phase. **No legacy subsystem is left**: DirectInput (Phase 3), audio (Phase 4), networking (Phase 5) and FMV video (Phase 6) are all migrated. Stage B6 finished the Phase 6 deletions: `WINSTR.LIB`, `STREAMER.H`, `dsound.lib`, the `GameData` decoder DLLs and `CDSpan.cpp` are gone. The courtesy still applies twice over:

- **The render layer**, while Phase 8 is in flight: if your task is not that phase, do not opportunistically rename or restructure `pie_*`/`iV_*` code.
- **The `NeuronCore` → `NeuronClient`/`NeuronServer` split.** Moving a file between the three libraries changes what links into a server build, so it is a design decision and not a tidy-up. If your task is not the split, leave every file where it is, and note in your report anything you think is on the wrong side.

**R14 — No new third-party dependencies**, and no package manager beyond what is already here. If you believe something is unavoidable, propose it in your report; do not add it. What the build is allowed to depend on is: the Windows SDK, the MSVC standard library, and the two NuGet packages below. Nothing else.

**Two sanctioned exceptions exist, both owner decisions, both arriving through the NuGet restore CI runs before each build. The list is closed: a third needs the same conversation.**

1. **MsQuic** (`Microsoft.Native.Quic.MsQuic.Schannel`), taken by Phase 5. The reasoning is on the record in [Phase5Plan.md](Docs/Phase5Plan.md): the alternative was hand-writing sequencing, acknowledgement, retransmission and ordering for lockstep game commands, where a single reordered packet desynchronises a match silently — and nothing in this repository can test such a protocol. QUIC makes that somebody else's tested code.
2. **C++/WinRT** (`Microsoft.Windows.CppWinRT`), sanctioned for Phase 6. It is a header-only projection with no runtime to redistribute, and what it buys is `winrt::com_ptr` and `winrt::check_hresult` for the Media Foundation COM lifetimes the FMV rewrite introduces — R12 asks for RAII COM ownership and this is the modern spelling of it. **Prefer it over `Microsoft::WRL::ComPtr` in new code**; do not churn existing code to match, and do not reach for the WinRT projection itself (`winrt::Windows::*`) — the sanction covers the COM helpers, not a second UI or async framework.

Neither exception licenses anything beyond the package named. Both are restored per project from a `packages.config` — `NeuronCore` and `Outpost` have one, the other three do not.

**R15 — `namespace Neuron` is for new engine code**, as `Debug.h` does it. Legacy translation units reach it through the `using namespace Neuron;` in `NeuronCore.h`; do not add per-file `using namespace` directives to work around a lookup failure — qualify the name.

**R16 — A string you do not write is `const`.** The engine libraries build `/permissive-`
(§3), which turns on `/Zc:strictStrings`: a literal is `const char[N]` and will not bind
to `char*` or `STRING*`. The fix is const on the signature, never a cast at the call
site — a `const_cast` here is a lie about a literal that lives in a read-only section,
and writing through it is a real crash rather than a theoretical one.

```cpp
void* resGetData(const STRING* _type, const STRING* _id);   // reads, so const
void  scr_error(const char* _message, ...);
const STRING* DXErrorToString(HRESULT _error);              // returns a literal
```

Const propagates: adding it to a signature usually asks for it on the helpers that
signature calls, and that is the change finishing rather than spreading. Adding const to
a parameter is source-compatible for every caller, so it does not break `Outpost` while
that project is still `/permissive`. **Watch for function-pointer typedefs** — a callback
`using`-alias is *not* const-compatible, so a signature reached through one
(`RES_FILELOAD`, `GETSHAPEFUNC`, `FONT_DISPLAY`) must change with its alias or not at all.

---

## 6. Working rules

**Stay in scope.** Do what the task asks. Adjacent legacy code that offends you is not part of the task — note it in your report and move on. Unrequested "while I was in there" changes are the main way this migration acquires regressions it cannot bisect.

**Keep the plan true.** If your change completes, alters or invalidates something [`Docs/MigrationPlan.md`](Docs/MigrationPlan.md) describes, update that document in the same commit. Figures in it are measured, not estimated — if you quote a new one, say how you measured it.

**Prove code is dead before deleting it.** Files here are reached through feature macros the preprocessor never expands in these builds (`QMIXER`, `EDITORWORLD`, `TEST_BED`, `JEREMY`) — `tools/check_case.py` keeps an explicit allow-list of exactly this. Grep the whole tree, including `.vcxproj` and `.filters`, before concluding nothing references a symbol.

**Keep the project files honest.** Adding, removing or *moving* a source file means editing the owning `.vcxproj` **and** its `.filters` — and a move between projects means editing two of each. A file that compiles locally but is missing from the project fails only in CI. `python tools/check_case.py` reads every project's `ClCompile`/`ClInclude`/`None` entries and is the cheapest way to catch a half-done move.

**Commits and PRs.** Branch off `main`; small, focused commits with an imperative subject describing the change, not the process. Both CI configurations (Debug and Release) must be green. Never commit build output, `.vs/` or `.user` files.

---

## 7. Before you hand work back

- [ ] Naming conforms to §1 — including `_` on parameters, `m_` on class state, no `I`/`C`/`Base` prefixes.
- [ ] Only the lines the task required were changed; no reformatting, no drive-by fixes.
- [ ] New, removed or moved files are reflected in the `.vcxproj` **and** `.filters` of every project involved.
- [ ] No project's `ConformanceMode` was changed, and no `const_cast` was added to satisfy R16.
- [ ] `python tools/check_case.py` passes.
- [ ] If it touched the model format or its tools: the three NMO checks in §3 pass, and a regenerated `NmoFixture.h` is committed with the change that caused it.
- [ ] It builds — Debug at minimum, and say which configurations you actually built.
- [ ] If it touches rendering, input, audio or level loading: it was **run**, not just built.
- [ ] `Docs/MigrationPlan.md` updated if the change moved a phase.
- [ ] Your report states plainly what you verified, what you assumed, and any rule here you had to bend.
