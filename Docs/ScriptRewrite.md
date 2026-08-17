# Script module rewrite

Owner instruction (2026-08-17): rewrite the scripting module with no lex/yacc —
native C++ only — and make it x64-clean. The compiled form of a script has no
backward-compatibility constraint ("no issue to change that"), so the
instruction encoding is free. Wider modernisation of the module is in scope.

The one thing that is **not** free is the language: 59 `.slo` scripts and 123
`.vlo` value files ship in `GameData` (732 KB of source), and every one of
them must keep compiling to the same behaviour. The language surface is the
contract; everything behind the lexer is replaceable.

> **Delivered (2026-08-17).** All six stages of §5 landed, one commit each,
> and both Win32 CI configurations are green. **10,907 lines of generated
> parser code are gone and no generated parser remains anywhere in the
> tree** — the build's warning count fell from 343 to 12, and the script VM
> moved from the sole x64 **Blocker** to Fixed. What each stage changed is
> recorded per stage in §5 and in the commit history; the language the new
> compiler accepts is specified in
> [`ScriptLanguage.md`](ScriptLanguage.md).
>
> **Not yet done:** the corpus acceptance test. The game compiles every
> shipped script at startup, so booting a campaign level and a skirmish
> match is what proves the rewrite semantically — CI only proves it
> compiles.

---

## 1. What the module is

Scripts drive the campaign, the tutorial, the skirmish AIs and multiplayer
rules. A `.slo` file declares variables, triggers and events; a `.vlo` file
binds a compiled script to a context and pours values into its globals
(`script "skirmishAI.slo" run { player INT 1 ... }`). Both compile at load
time — there is no offline compile step and no compiled data on disk. The
event system then ticks: triggers test on schedule, fire their events, events
run through a stack-based interpreter that calls back into ~400 registered C
functions ("instinct functions").

| Part | Files | Lines | Fate |
|---|---|---|---|
| `.slo` compiler (generated, MKS yacc/lex) | `NeuronCore/Script_y.cpp` `.h`, `Script_l.cpp` | 6,346 | delete, replace native |
| `.vlo` parser (generated) | `Outpost/ScriptVals_y.cpp` `.h`, `ScriptVals_l.cpp` | 2,688 | delete, replace native |
| String-resource parser (generated) | `NeuronCore/StrRes_y.cpp` `.h`, `StrRes_l.cpp`, `StrResLY.h` | 1,873 | decision §6 |
| Compiler interface / symbol types | `NeuronCore/Parse.h` | 293 | shrinks |
| Interpreter | `NeuronCore/Interp.cpp` `.h` | 813 | rewritten onto new encoding |
| Operand stack + instinct FFI | `NeuronCore/Stack.cpp` `.h` | 714 | FFI made pointer-width-correct |
| Event/trigger scheduler | `NeuronCore/Event.cpp` `.h` | 1,222 | containers modernised, semantics kept |
| Library glue + dead serialisation | `NeuronCore/Script.cpp` `.h` | 913 | ~630 dead lines deleted |
| Disassembler | `NeuronCore/CodePrint.cpp` `.h` | 505 | rewritten onto new encoding |
| Game tables (types, funcs, externs, objvars, consts, callbacks) | `Outpost/ScriptTabs.*` | 870 | unchanged (cleanups only) |
| Instinct functions | `Outpost/ScriptFuncs.*`, `ScriptAI.*`, `ScriptCB.*`, `ScriptObj.*`, `ScriptExtern.*` | 8,933 | unchanged except FFI call-site sweep |
| `.vlo` runtime (contexts, base pointers, groups) | `Outpost/ScriptVals.cpp` `.h` | 532 | kept; load entry rewritten |

Total generated parser code deleted: **10,907 lines** (plus their 44 CI
warnings, the only warnings left in the build).

## 2. What is wrong with it today

### x64 hazards — why "make it x64" is a rewrite, not a patch

1. **Function pointers live inside 32-bit code words.** The instruction
   stream is `UDWORD*`. `OP_CALL` stores a `SCRIPT_FUNC` in the word after
   the opcode; `Interp.cpp:418` executes `scriptFunc = (SCRIPT_FUNC)*(ip+1)`.
   `OP_VARCALL` does the same for variable accessors. 8-byte pointers do not
   fit in 4-byte slots.
2. **String literals and object constants too.** `PUT_STRING`/`PUT_FUNC` in
   `Script_y.cpp` write `STRING*` and `CONST_SYMBOL::oval` pointers into code
   words at compile time.
3. **The instinct FFI truncates every pointer parameter.**
   `stackPopParams(...)` (`Stack.cpp:158`) writes each popped parameter as
   `*pData = (UDWORD)psVal->v.ival` — a 4-byte store through a destination
   that at hundreds of the 144 call sites in `ScriptFuncs.cpp` alone is
   really a `DROID**`, `STRUCTURE**` or `STRING**`. On x64 the top half of
   the pointer variable is left as garbage. `stackPushResult(type, SDWORD)`
   truncates on the way out, at 125 sites. This hazard was **not** in
   `Docs/X64Readiness.md`; it is recorded here.
4. **`.vlo` initialisation truncates objects.** `eventSetContextVar(...,
   UDWORD data)` receives `(UDWORD)psTemplate`, `(UDWORD)psViewData`,
   `(UDWORD)pString`, … at ~10 sites in `ScriptVals_y.cpp`.

### Structural debt

- **The lexer consults seven symbol tables to classify identifiers**
  (`Script_l.cpp:906-933`) — the classic yacc feedback hack. A
  recursive-descent parser resolves identifiers in context and needs no such
  loop.
- **Dead serialisation.** `scriptSaveProg`/`scriptLoadProg`
  (`Script.cpp:87-663`) have no callers — save/load was deleted in an
  earlier phase. `scriptGetVarIndex` likewise. This includes the whole
  save-time pointer→index pass listed in `X64Readiness.md`.
- **`NOSCRIPT`** is defined nowhere; its conditional blocks are noise.
- **Hand-rolled allocation everywhere.** Context globals are chunked linked
  lists (`VAL_CHUNK`) walked linearly on *every variable access*
  (`interpGetVarData`); the event system carries pool-tuning knobs
  (`EVENT_INIT`) that ScriptTabs must fill in.
- A latent bug the rewrite retires: `hashTable`-style shared-cursor
  iteration is gone already, but `anim_GetFrame3D`-class signed tests and
  the like are noted in `AssetPipeline.md` Appendix B; the script items in
  that list fall out with the new compiler.

## 3. The language as actually shipped

Measured over all 59 `.slo` files (comments and strings stripped):

- **Keywords used:** `public private trigger event wait every inactive init
  ref while if else pause and or not true false TRUE FALSE` — and exactly
  one uppercase `AND` (`genexp.slo:1554`), so word-operators are accepted
  case-insensitively by the old lexer. New lexer: keywords are
  case-insensitive. 
- **Keywords in the grammar but used by nothing:** `exit` (the statement —
  `OP_EXIT` never emitted from data), script-defined functions (`FUNCTION`
  token, `scriptStartFunctionDef` machinery — no `.slo` defines one), and
  trigger `link` declarations.
- **Operators used:** `= == != >= <= > < + - * / . [ ] ( ) { } ; ,` and
  unary minus. No modulo, no bitwise ops.
- Literals: decimal integers, `"strings"`, booleans. Comments: `//` and
  `/* */`.
- `.vlo` grammar is four constructs: `script "name.slo"`, `run`/`store`
  blocks, `var TYPE value` lines (value = int, bool, string, or quoted
  identifier resolved per type), and `array[i]` element assignment.

The compiler's type system (typed variables, `TYPE_EQUIV` equivalence table,
`VAL_REF` reference parameters, typed instinct signatures) is load-bearing
and keeps identical semantics.

## 4. Target design

### 4.1 Instruction encoding: fixed-size records, not packed words

The old form packs opcode+operand into a `UDWORD` (`op << 24 | data`) and
appends raw pointers as extra words. The replacement is one struct per
instruction:

```cpp
struct ScriptInstr
{
  OPCODE op;         // OP_PUSH .. OP_PAUSE (secondary maths ops stay data)
  INTERP_TYPE type;  // operand type for PUSH/PUSHREF, 0 otherwise
  SDWORD data;       // small operand: var index, jump delta (in instructions),
                     // secondary op, pause time, array base|dims
  union
  {
    SDWORD ival;     // PUSH immediate int/bool
    UDWORD index;    // CALL: slot in asScrInstinctTab
                     // VARCALL: table|get/set|slot, packed
                     // PUSH string: slot in the code's string table
    void* oval;      // PUSH object constant (NULLOBJECT etc.)
  } arg;
};
```

- **Calls go by index, not address.** `OP_CALL` carries the instinct table
  slot; the interpreter does `asScrInstinctTab[i].pFunc()`. `OP_VARCALL`
  carries which table (extern/objvar), get-or-set, and slot. No pointer in
  the stream, so nothing to truncate and the disassembler can print names.
- **String literals are interned** in a `std::vector<std::string>` owned by
  `SCRIPT_CODE`; `OP_PUSH VAL_STRING` carries the slot. (Today's compiler
  leaks a `strdup` per literal into the code stream.)
- **Jumps count instructions,** same relative-delta scheme as today, now
  over uniform records — no `aOpSize[]` table, no variable-length decode.
  This is the "variable size code is fine" freedom spent on *uniform* size:
  records beat byte-variable encoding because jump fixups, bounds checks and
  disassembly all become index arithmetic. Memory is a non-issue at this
  corpus size.
- `SCRIPT_CODE` becomes RAII: `std::vector<ScriptInstr>` code,
  `std::vector` trigger/event tables, string table, debug info;
  `scriptFreeCode` shrinks to `delete`.

### 4.2 Native compiler (replaces `Script_l.cpp`/`Script_y.cpp`)

Hand-written, in `NeuronCore`, following the house precedent (`Json.cpp` was
the R14 answer for parsers — own the small thing):

- **`ScriptLex.cpp/.h`** — a plain buffer lexer: identifiers, keywords
  (case-insensitive), integers, strings, the operator set, both comment
  forms, line/column tracking. No symbol-table feedback: it returns IDENT
  and lets the parser resolve.
- **`ScriptComp.cpp`** — recursive-descent parser + code generator.
  Declarations, triggers, events; statements (assign, call, if/else, while,
  pause, exit); precedence-climbing expressions; identical type checking
  through `interpCheckEquiv` and the same `TYPE_EQUIV` table. Emits
  `ScriptInstr` vectors with backpatched jumps, assembles the final
  `SCRIPT_CODE`. Entry point stays `scriptCompile(pData, size, &psProg,
  debugType)` so `Data.cpp` does not change.
- **Errors** carry file line/column and what was expected; compile failure
  remains fatal at the `dataScriptLoad` boundary, as today.
- `Parse.h` shrinks to the game-table ABI (`TYPE_SYMBOL`, `VAR_SYMBOL`,
  `CONST_SYMBOL`, `FUNC_SYMBOL`, `CALLBACK_SYMBOL`) — those structs are how
  `ScriptTabs.cpp` hands the game to the compiler and they stay
  source-compatible, minus the `NOSCRIPT` conditionals. Script-defined
  functions (kept per §6 Q2) live in compiler-internal symbols, not in the
  game-table ABI; the never-defined `scriptStartFunctionDef` /
  `scriptSetParameters` / `scriptSetCode` declarations go.

### 4.3 Native `.vlo` parser (replaces `ScriptVals_l/_y`)

A ~300-line recursive-descent parser in `Outpost/ScriptVals.cpp` behind the
existing `scrvLoad(pData, size)` entry. `eventSetContextVar` changes
signature to take an `INTERP_VAL` (typed, pointer-safe), fixing hazard §2.4
at the same time. The context/base-pointer/group runtime in `ScriptVals.cpp`
is untouched.

### 4.4 FFI made pointer-width-correct — typed interface

The varargs `stackPopParams(SDWORD numParams, ...)` and the `SDWORD`-slot
`stackPushResult` are replaced outright (owner decision, §6 Q3) with the
typed `ScriptParam` API sketched there. The destination's store width is
fixed at compile time by which constructor the call site selects — 32-bit
scalars keep 32-bit stores, object/string/ref destinations get
pointer-wide stores — and the runtime type check against the script's
declared types stays exactly as today via `interpCheckEquiv`. Every call
site across the five instinct-function files converts mechanically; the
old varargs form is deleted so no unconverted site can survive the build.

### 4.5 Event system modernisation (semantics frozen)

- Context globals: `VAL_CHUNK` chains → `std::vector<INTERP_VAL>` per
  context; `interpGetVarData` becomes indexing. **Done.** Every variable
  access was a linked-list walk; it is now an index into storage that
  never moves, so `OP_PUSHREF` values and the base-pointer registry stay
  valid as before.
- `EVENT_INIT` pool tuning disappears; `scriptInitialise()` loses its
  parameter. **Done.**
- Trigger and context lists: **left as the intrusive lists they are**, a
  deliberate narrowing recorded at implementation time. They are correct
  and self-contained, and a `std::list` conversion would rework the one
  piece of the module where behaviour is subtlest — the time-ordered
  insert decides firing order between same-tick triggers, and the
  deferred `psAddedTriggers` handling exists precisely because the lists
  mutate mid-iteration — to change nothing but allocation style.
- Value create/release hooks (`eventAddValueCreate`/`Release`) unchanged —
  the group/base-pointer machinery depends on them.

### 4.6 What does not change

The `.slo`/`.vlo` languages and their semantics; trigger scheduling and tick
rate; the instinct function set and signatures; the callback set; type
equivalence rules; `SCRIPT_CONTEXT` per-context global copies;
`calcCheatHash` (it hashes *source*, which is unchanged, so multiplayer
script hashing is unaffected); the resource types `SCRIPT`/`SCRIPTVAL` and
load order; `INTERP_MAXINSTRUCTIONS` runaway guard; the recursion guard in
`interpRunScript`.

### 4.7 Tests

`NeuronCoreTest` (MSVC CppUnitTestFramework, the `JsonTest.cpp` precedent)
gains:

- **Lexer tests** — token streams for each construct, both comment forms,
  case-insensitive keywords, error positions.
- **Compiler tests** — register a miniature type/function/constant table,
  compile snippets, assert against the disassembler's text (golden strings)
  and against compile-errors for ill-typed input.
- **Interpreter tests** — run compiled snippets end to end: arithmetic,
  if/while control flow, instinct calls that record their arguments,
  `VAL_REF` out-params, pause rescheduling.

The full-corpus proof stays where it is authoritative: the game compiles
every shipped script at startup, so the MSVC CI build plus a boot is the
acceptance test. `tools/crosscheck.py` gates syntax portability of all new
code on every stage.

## 5. Staging

Each stage compiles clean through `check_case` + full `crosscheck`, is
committed separately, and leaves the game runnable. **All six are done**;
the notes below are what was planned, and the deltas from plan are called
out where they occurred.

1. **Delete the dead weight.** `scriptSaveProg`/`scriptLoadProg`/
   `scriptGetVarIndex` + `BINARY_HDR` + the `FUNC_*` index-flag defines
   (~630 lines of `Script.cpp`), every `#ifdef NOSCRIPT`, the `VAL_FLOAT`/
   `OP_FUNC` comment fossils. No behaviour change.
2. **New encoding + native `.slo` compiler + interpreter + disassembler.**
   `ScriptInstr`, `ScriptLex`, `ScriptComp`; `Interp.cpp` loop over
   records; `CodePrint.cpp` over records; delete `Script_l.cpp`,
   `Script_y.cpp`, `Script_y.h`. The big stage, one commit, because the
   encoding change makes old-parser/new-interpreter mixtures meaningless.
3. **Native `.vlo` parser**; `eventSetContextVar` → `INTERP_VAL`; delete
   `ScriptVals_l.cpp`, `ScriptVals_y.cpp`, `ScriptVals_y.h`.
4. **FFI typed interface** (per §6 Q3). New `ScriptParam` API in
   `Stack.h`; every `stackPopParams`/`stackPushResult` site in
   `ScriptFuncs.cpp`, `ScriptAI.cpp`, `ScriptCB.cpp`, `ScriptObj.cpp`,
   `ScriptExtern.cpp` converted; the varargs form deleted; audit every
   `VAL_REF` pop. Update `X64Readiness.md`: script VM moves Blocker →
   Fixed, FFI hazard recorded → Fixed.
5. **Event/context containers** (§4.5). Behaviour-identical; the diff is
   allocation and iteration only.
6. **String-resource parser** (per §6 Q1): native replacement for
   `StrRes_l/_y` — the format is `IDENT "string"` pairs with C comments —
   and lex/yacc output is extinct in the tree.

**Bugs the rewrite fixed on the way through**, none of them the point of a
stage, all of them found by the work:

- `eventCopyContext` copied context values through the 32-bit `ival` union
  member, truncating every object pointer in a copied context on x64.
- Four sites in `ScriptAI.cpp` (`scrSkCanBuildTemplate`, `skDoResearch`,
  `scrSkGetFactoryCapacity`, the skirmish template store) popped object
  pointers into `SDWORD` locals and cast back.
- Trigger labels never reached the debug info, so `eventGetTriggerID`
  printed `NOT FOUND` for every code trigger it was asked to name.
- The `.vlo` loader passed object pointers through a `UDWORD` parameter
  (`eventSetContextVar`).

Stage 2 is the risk mass. Mitigations: corpus keyword/operator measurement
above bounds the lexer; the type checker reuses the same equivalence table
and the same strictness; unit tests pin the opcode semantics; and any
compile failure at boot names file, line and column.

## 6. Decisions (owner, 2026-08-17)

1. **String-resource parser: in scope.** Stage 6 replaces `StrRes_l`/
   `StrRes_y` with a native parser; generated parser code becomes extinct in
   the tree.
2. **Script-defined functions: kept.** The new compiler implements
   `function` blocks as a working feature. Note what this means: the old
   machinery was never finished. The definition helpers (`
   scriptStartFunctionDef`, `scriptSetParameters`, `scriptSetCode`) are
   declared in `Parse.h` and defined nowhere — the generated parser links,
   so no live production references them; a `function` definition cannot
   have compiled. At a call site, `scriptCodeFunction` emits the
   parameter pushes and then, for a script-defined callee, emits no call
   at all — the `OP_FUNC` path is commented out (`Script_y.cpp:784`). So
   there is no legacy behaviour to preserve, only a keyword to honour.
   The new compiler defines the semantics cleanly (a call opcode carrying
   a function index, parameters on the operand stack, typed returns) and,
   since no shipped script exercises the feature, the `NeuronCoreTest`
   suite is its only harness; those tests are part of stage 2's
   definition of done.
3. **FFI: full typed interface.** `stackPopParams`'s varargs contract (and
   `stackPushResult`'s `SDWORD` slot) are replaced outright with a typed
   API, and every call site in `ScriptFuncs.cpp`, `ScriptAI.cpp`,
   `ScriptCB.cpp`, `ScriptObj.cpp` and `ScriptExtern.cpp` is converted —
   ~144 pop and ~125 push sites across ~170 functions. Sketch:

   ```cpp
   struct ScriptParam   // one destination, width fixed at compile time
   {
     ScriptParam(INTERP_TYPE _type, SDWORD* _dest);   // 32-bit scalars
     ScriptParam(INTERP_TYPE _type, UDWORD* _dest);
     template <typename T>
     ScriptParam(INTERP_TYPE _type, T** _dest);       // objects, strings, refs
     ...
   };
   BOOL stackPopParams(std::initializer_list<ScriptParam> _params);

   BOOL stackPushResult(INTERP_TYPE _type, SDWORD _value);   // scalars
   BOOL stackPushResult(INTERP_TYPE _type, void* _value);    // objects
   ```

   The runtime type check against the script's declared types stays exactly
   as today (`interpCheckEquiv`); what the compiler now guarantees is that
   the *store width* matches the destination variable, which is the x64
   failure class. A call site changes shape mechanically:
   `stackPopParams(2, VAL_INT, &x, ST_DROID, &psDroid)` becomes
   `stackPopParams({{VAL_INT, &x}, {ST_DROID, &psDroid}})`.
