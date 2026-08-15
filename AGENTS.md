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
| File | `PascalCase.cpp` / `.h` | `D3DRender.cpp` |

**This table governs code you write.** The legacy tree does not follow it — `SCREEN_PIXELFORMAT`, `g_psDevice`, `g_bAudioEnabled` and friends are grandfathered, not exemplars. Do not extend those patterns into new code, and do not mass-rename them either (§4).

### The rules behind the table

**R1 — The leading underscore on parameters is deliberate.** It is legal C++: the reserved forms are `_Uppercase`, anything containing `__`, and `_lowercase` **at global scope**. A parameter is never at global scope, so `_fileName` is safe. Never introduce a reserved form — no `_Impl`, no `__helper`, no file-scope `_cache` (use `g_cache` in an anonymous namespace).

**R2 — A type name carries no prefix, and that includes abstract ones.** An interface is `Transport`, not `ITransport`. A base class is not `BaseTransport` or `AbstractTransport`. PascalCase means the name and nothing else — the Hungarian remnants in the legacy tree are not a precedent, and none is to be introduced. Name the concept and let the concrete types say what they are:

```
Transport             ← the concept (the Phase 5 interface)
├── UdpTransport      ← WinSock2-backed, the shipping one
└── LoopbackTransport ← in-process, for tests
```

This also bans `CFoo`, `SFoo`, `EFoo`, `FooBase`, `IFoo`, and `_t` suffixes.

**R3 — Compile-time constants are Constants.** `static constexpr` members and namespace-scope `constexpr`/`inline constexpr` take PascalCase (`MaxDroids`, `TileWidth`, `TextureCacheBytes`). `sm_` is reserved for *mutable* statics, which are rare and must document their thread-safety.

**R4 — Acronyms capitalize as words**: `ImdModel`, `RplStream`, `CdAudioTrack`, `UdpTransport` — never `IMDModel`. Identifiers from an external SDK keep that SDK's spelling (`IDirect3DDevice9`, `D3DFORMAT`, `HRESULT`, `LPDIRECTINPUTDEVICE7`) and are never renamed to fit.

**R5 — Template parameters are PascalCase**: `T`, `Fn`, `BlockBytes`, `Ts...`.

**R6 — Units belong in names; types do not.** `rangeTiles`, `durationTicks`, `speedUnitsPerTick` are encouraged — a world measured in tiles, world units and game ticks makes unit ambiguity a real defect class. Never encode the type: no `iCount`, `pDroid`, `strName`. The legacy Hungarian in the tree (`g_psDevice`, `bAudioEnabled`, `uiRet`) is exactly what this rule bans; it stays where it is and spreads nowhere.

**R7 — A file is named for its primary type**, PascalCase, `.h` / `.cpp` only. `.hpp`, `.cc`, `.inl` are not used; template implementations live in the header. Vendored third-party headers keep whatever name they shipped with (`DX9/Include/`, `EAX.H`) and are never renamed.

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

**Nothing runs it automatically yet.** [`build.yml`](.github/workflows/build.yml) builds Debug and Release for Win32 and runs `tools/check_case.py` (which verifies every `#include` and project entry spells its file with the exact on-disk case — MSVC resolves includes case-insensitively, so a wrong name still builds). There is no clang-tidy step, and the `.vcxproj` files do not enable MSBuild's code analysis. Until one of those changes, §1 is enforced by **review**: check your own diff against the table before handing it back.

**When you do run it, run it on what you wrote, not on the tree.** The legacy code predates every rule here (§1, under the table), so a whole-tree pass reports thousands of grandfathered findings and tells you nothing. Point it at the files your change adds, or filter to your changed lines:

```
clang-tidy --quiet NeuronCore/YourNewFile.cpp -- -I NeuronCore -I DX9/Include -D WIN32 -D _DEBUG
```

Two rules the config cannot express, and that a reviewer therefore has to carry:

- **R2 (type prefixes)** — clang-tidy's `AbstractClassPrefix` can require an *absent* prefix but cannot ban a *present* suffix, so `FooBase` slips through. Grep declaration sites, `using` and `typedef` aliases included, for `\b(class|struct|using)\s+[ICSE][A-Z]` and for trailing `Base`/`Abstract`/`Impl`.
- **R7 (file naming)** — nothing checks that a new file is PascalCase and `.h`/`.cpp`. Look at the filename when you add one, and at the `.vcxproj`/`.filters` entries that must accompany it (§6).

---

## 2. Repository map

| Path | What it is | May you edit it? |
|---|---|---|
| `NeuronCore/` | Engine static library (~85 TUs): platform, D3D9 rendering, input, audio, UI widgets, debug | Yes |
| `Outpost/` | Game executable (~121 TUs): simulation, AI, structures, droids, campaign, multiplayer | Yes |
| `DX9/Include`, `DX9/Lib` | Vendored DirectX 9.0c SDK — deliberately checked in so the build needs no external SDK | **No** |
| `GameData/` | Shipped content (levels, textures, audio, `.rpl` movies) and three third-party DLLs. Binary, authored by tools outside this repo | **No** |
| `Docs/MigrationPlan.md` | The plan and the record of what each phase changed | Yes — see §6 |
| `.clang-format`, `.clang-tidy`, `.editorconfig` | Layout and naming, machine-readable (§1, §4) | Yes — with an owner decision |
| `tools/*.py` | Repository checkers (§3). Several files are empty placeholders; ignore those | Yes |
| `.github/workflows/build.yml` | CI: Debug and Release, Win32 | Yes, carefully |
| `Debug/`, `x64/`, `.vs/`, `*.user` | Build and IDE output | **No — and never commit them** |

Two projects, one dependency edge: `Outpost.vcxproj` links `NeuronCore.lib`, so building the game builds the engine. `Outpost.slnx` is the solution.

---

## 3. Build and verify

**One platform exists: Win32 (x86), toolset v145, `/std:c++latest`.** There is no x64 configuration and no CMake. If a build error tempts you to add a platform, change the toolset, or lower the language standard — stop and report instead.

```powershell
# Build (from the repository root). This builds NeuronCore first.
msbuild Outpost\Outpost.vcxproj /p:Configuration=Debug /p:Platform=Win32 /v:normal /nologo

# Filename-casing gate. MSVC resolves includes case-insensitively, so a wrong
# #include still builds on Windows and only fails on the Linux checkers. Run it.
python tools/check_case.py
```

The executable lands in `Debug\Outpost.exe` (or `Release\`), and it needs `GameData/` beside it at runtime.

**Without MSVC** (Linux container), `tools/crosscheck.py` syntax-checks every translation unit with mingw-w64 against a shadow tree. It is a fast first pass, **not a build**: it cannot link, and MSVC disagrees with GCC in both directions. The Windows CI build is the authority.

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

**R10 — Memory is plain C++.** `new`/`delete`, RAII, and the standard containers. The custom allocators (`Mem.cpp`, `Heap.cpp`, `Block.cpp`) were deleted on purpose; do not reintroduce a pool, a slab or a free-list without an owner decision.

**R11 — No inline assembly.** The last `__asm` block is gone, which is part of what makes the tree portable to the cross-checker. Write the C++ equivalent.

**R12 — Graphics is Direct3D 9 only.** An `IDirect3DDevice9` is owned by `Screen.cpp` and drawn through by `D3DRender.cpp`, `D3DMode.cpp` and `TexMan.cpp`. No DirectDraw, no surfaces, no palettes, no `GetDC`. New COM code uses C++ `device->Method(...)` syntax with RAII lifetimes — `CINTERFACE` is gone and is not coming back.

**R13 — Leave the not-yet-migrated subsystems alone** unless your task *is* that phase: DirectInput 7 (Phase 3), QMixer/DirectSound/CD audio (Phase 4), DirectPlay 4 and Mplayer (Phase 5), WINSTR video (Phase 6). Touching them opportunistically creates conflicts with the phase that will rewrite them.

**R14 — No new third-party dependencies**, and no package manager. The DX9 SDK is vendored for exactly this reason. If you believe something is unavoidable, propose it in your report; do not add it.

**R15 — `namespace Neuron` is for new engine code**, as `Debug.h` does it. Legacy translation units reach it through the `using namespace Neuron;` in `NeuronCore.h`; do not add per-file `using namespace` directives to work around a lookup failure — qualify the name.

---

## 6. Working rules

**Stay in scope.** Do what the task asks. Adjacent legacy code that offends you is not part of the task — note it in your report and move on. Unrequested "while I was in there" changes are the main way this migration acquires regressions it cannot bisect.

**Keep the plan true.** If your change completes, alters or invalidates something [`Docs/MigrationPlan.md`](Docs/MigrationPlan.md) describes, update that document in the same commit. Figures in it are measured, not estimated — if you quote a new one, say how you measured it.

**Prove code is dead before deleting it.** Files here are reached through feature macros the preprocessor never expands in these builds (`QMIXER`, `EDITORWORLD`, `TEST_BED`, `JEREMY`) — `tools/check_case.py` keeps an explicit allow-list of exactly this. Grep the whole tree, including `.vcxproj` and `.filters`, before concluding nothing references a symbol.

**Keep the project files honest.** Adding or removing a source file means editing `NeuronCore.vcxproj`/`Outpost.vcxproj` *and* the matching `.filters`. A file that compiles locally but is missing from the project fails only in CI.

**Commits and PRs.** Branch off `main`; small, focused commits with an imperative subject describing the change, not the process. Both CI configurations (Debug and Release) must be green. Never commit build output, `.vs/` or `.user` files.

---

## 7. Before you hand work back

- [ ] Naming conforms to §1 — including `_` on parameters, `m_` on class state, no `I`/`C`/`Base` prefixes.
- [ ] Only the lines the task required were changed; no reformatting, no drive-by fixes.
- [ ] New or removed files are reflected in the `.vcxproj` **and** `.filters`.
- [ ] `python tools/check_case.py` passes.
- [ ] It builds — Debug at minimum, and say which configurations you actually built.
- [ ] If it touches rendering, input, audio or level loading: it was **run**, not just built.
- [ ] `Docs/MigrationPlan.md` updated if the change moved a phase.
- [ ] Your report states plainly what you verified, what you assumed, and any rule here you had to bend.
