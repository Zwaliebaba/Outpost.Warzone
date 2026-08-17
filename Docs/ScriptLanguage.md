# The script language

The reference for the `.slo`/`.vlo` language the native compiler implements.
Recovered from the generated parser this compiler replaced (the `.y`/`.l`
sources never shipped; the MKS-yacc output carried one rule comment per
semantic action, and the action-less list/empty rules were reconstructed from
the 59-file corpus, which also pinned every judgement call noted below).
Where this document and the shipped scripts disagree, the scripts win.

## 1. Lexical rules

- **Comments**: `//` to end of line, `/* ... */` (non-nesting). Whitespace
  separates tokens and is otherwise ignored.
- **Keywords, case-insensitive**: `trigger event wait every inactive init
  initialise link ref while if else exit pause and or not true false
  function return void int bool public private`. The corpus uses `and`/`AND`,
  `true`/`TRUE`, `false`/`FALSE`; no identifier in the corpus collides with
  any keyword under case folding. `public`/`private` are the two STORAGE
  keywords. `init` and `initialise` are the same token. `function`,
  `return`, `void` are new with the native compiler (the old grammar
  reserved `FUNCTION` but had no production for it); all three are unused
  as identifiers by the corpus.
- **`int` and `bool` are keywords, not table entries.** The game's
  `asTypeTable` holds only the *user* types, so nothing resolves `int` or
  `bool` by lookup; the lexer has always produced them directly, and both
  the `.slo` and `.vlo` parsers dispatch on the token. The corpus depends on
  this heavily — `int` and `INT` together appear over 7,400 times in the
  value files alone — and a parser that omits it rejects almost every
  shipped script. `tools/check_scripts.py` guards against exactly that.
- **Identifiers**: `[A-Za-z_][A-Za-z0-9_]*`, case-sensitive. Classified by
  the parser (not the lexer) against, in order: the object-variable table
  (only inside a `.` access), the external-variable table, script-declared
  variables/arrays, user types, instinct functions, constants, callbacks,
  script triggers, script events, script functions.
- **Integers**: decimal digits only. No hex, no suffixes. Negation is the
  unary operator, except the two INTEGER-token positions (array bound,
  pause time) where the old lexer never saw a sign either.
- **Strings**: `"..."` (no escapes) lex as a token but no `.slo` grammar
  production accepts one — a string literal in script code is an error,
  exactly as before (corpus: zero occurrences). `.vlo` uses them.
- **Operators/punctuation**: `= == != >= <= > < + - * / . , ; ( ) { } [ ]`.

## 2. Grammar

Sections are strictly ordered (corpus-verified: no file interleaves them).

```
script        := header? var_list function_list? trigger_list event_list
header        := ( "link" type_name ";" )*     // parsed, no effect (old action empty)
var_list      := ( var_decl ";" )*
var_decl      := storage type_name var_ident ( "," var_ident )*
storage       := "public" | "private"
type_name     := "int" | "bool" | TYPE | "trigger" | "event"
                                               // TYPE = entry in the game type table;
                                               // int/bool are keywords (see §1)
var_ident     := IDENT | IDENT ( "[" INTEGER "]" )+     // 1..4 dims, 1..254 elements

function_list := function_def*                 // new; see §5
function_def  := "function" ( "void" | type_name ) IDENT
                 "(" ( type_name IDENT ( "," type_name IDENT )* )? ")"
                 "{" statement* "}"

trigger_list  := trigger_decl*
trigger_decl  := "trigger" IDENT "(" trigger_sub ")" ";"
trigger_sub   := boolexp "," INTEGER           // TR_CODE, test interval
               | "wait" "," INTEGER            // TR_WAIT
               | "every" "," INTEGER           // TR_EVERY
               | "init"                        // TR_INIT
               | CALLBACK                      // parameterless callback
               | CALLBACK "," param_list      // callback with params

event_list    := ( event_fwd | event_def )*
event_fwd     := "event" IDENT ";"             // forward declaration
event_def     := "event" NAME "(" event_link ")" "{" statement* "}"
                 // NAME: new IDENT or a forward-declared event
event_link    := TRIGSYM                       // a declared trigger
               | trigger_sub                   // inline anonymous trigger
               | "inactive"                    // no trigger (link -1)

statement     := assignment ";" | call ";" | if_stmt | while_stmt
               | "exit" ";" | "pause" "(" INTEGER ")" ";"
               | "return" expr? ";"            // new, function bodies only
assignment    := lvalue "=" expr
lvalue        := VARIABLE | objexp "." MEMBER | ARRAY ( "[" expr "]" )+
if_stmt       := "if" "(" boolexp ")" "{" statement* "}"
                 ( "else" if_stmt | "else" "{" statement* "}" )?
while_stmt    := "while" "(" boolexp ")" "{" statement* "}"
call          := FUNC "(" param_list ")"       // instinct or script function
param_list    := ( param ( "," param )* )?
param         := expr | "ref" VARIABLE         // ref: script globals only
```

Expression precedence, loosest to tightest (all binary ops left-assoc):
`or` < `and` < `== !=` < `>= <= > <` < `+ -` < `* /` < unary `- not` <
postfix `. MEMBER` and `[expr]` < atoms (INTEGER, TRUE/FALSE, variable,
constant, call, `(expr)`, trigger name, event name, `inactive`).

## 3. Types and checking

- Base types `INT`, `BOOL`, plus `trigger`, `event` (indices at runtime),
  plus the game's user types (`asTypeTable`), each `AT_SIMPLE` (32-bit
  value) or `AT_OBJECT` (pointer). Equivalence is `interpCheckEquiv`: exact
  match or an entry in the game's `TYPE_EQUIV` table; `REF`-ness must match.
- Arithmetic `+ - * /` and orderings `>= <= > <`: INT operands, INT/BOOL
  result. `and or not`: BOOL. `== !=`: INT×INT, BOOL×BOOL, or
  user/object×user/object under `interpCheckEquiv` (error otherwise).
- Assignment: value type must `interpCheckEquiv` the variable's type.
- Calls: arity must match exactly; each argument checks against the
  declared parameter type; `ref` arguments carry `type|VAL_REF`.
  A non-void call in statement position gets its result popped.
- Triggers: the boolexp form must be BOOL. Callback params check against
  the callback's declared parameter list (min 1 when parameterised).
- Object member access `obj.member`: the base must be an `AT_OBJECT`
  expression; the member resolves in the object-variable table filtered by
  `interpCheckEquiv(member.objType, baseType)`; members are `ST_OBJECT`
  storage with get/set accessor functions.
- External variables (`ST_EXTERN`): reads need a get, writes a set
  function; cannot be `ref` parameters. Script globals are
  `ST_PUBLIC`/`ST_PRIVATE` (identical semantics at runtime).

## 4. Execution model and code generation

One `SCRIPT_CODE` per file: all trigger bodies (declaration order), then
all event bodies, in one instruction array; `pTriggerTab`/`pEventTab` hold
`n+1` offsets so body `i` spans `[tab[i], tab[i+1])`. `pEventLinks[i]` is
the event's trigger index or -1. `TRIGGER_DATA` is `{type, has-code,
time}`. Anonymous inline triggers append at declaration point (an event
defined with an inline trigger links to `numTriggers-1`).

- A body runs until `ip` reaches its end — there is **no** trailing EXIT
  emitted; `exit` (OP_EXIT) jumps straight to the end from anywhere.
- A TR_CODE trigger body is its boolexp: it **leaves one BOOL on the
  stack**, which the event system pops as the fired flag. A
  callback-with-params trigger body pushes its params and CALLs the
  callback, whose C function leaves the BOOL.
- `pause(n)`: only legal with an empty stack (asserted); reschedules the
  current event at the instruction after the pause via a TR_PAUSE trigger
  with interval `n`, then exits the body. On firing, the event re-enters
  at that stored offset (`interpRunScript(IRT_EVENT, event, offset)`).
- Old-encoding shapes preserved per construct (operand order is contract,
  the encoding itself is new — §6):
  - read var: PUSHGLOBAL idx | VARCALL(get) for externs;
    write: value-code, then POPGLOBAL idx | VARCALL(set).
  - member read: object-code, VARCALL member-get(index).
    member write: **value-code first**, then object-code, then
    VARCALL member-set(index) — the accessor pops object then value.
  - array element: index expressions left-to-right, then
    PUSH/POPARRAYGLOBAL. The interpreter pops indices last-dimension-first
    and addresses row-major, dimension 0 outermost:
    `offset = (((i0)*e1 + i1)*e2 + ...) + base`, bounds-checked per
    dimension.
  - call: params left-to-right, then CALL; statement-position non-void
    adds POP.
  - `ref x`: PUSHREF carrying `type|VAL_REF` and the global's index — the
    stack value points at the variable's 32-bit slot.
  - if: cond, JUMPFALSE past body(+jump), body, JUMP-to-end (each clause's
    end-jump patched to the end of the whole if/else chain).
  - while: cond, JUMPFALSE +body+2, body, JUMP back to cond.
  - constants: PUSH with the constant's type and value (object constants
    push a pointer).
  - trigger/event names in expressions: PUSH VAL_TRIGGER/VAL_EVENT with
    the index; `inactive` pushes VAL_TRIGGER -1.
- Runtime guards kept: `INTERP_MAXINSTRUCTIONS` (100,000) per body; jump
  targets bounds-checked; no recursive interpreter entry (a callback
  firing mid-script is an error, as before).

## 5. Script-defined functions (new)

`function` blocks are new working behaviour: the old tree reserved the
keyword but had no grammar rule, no definition machinery, and the call
path emitted nothing (see `Docs/ScriptRewrite.md` §6.2).

- Defined between the variable and trigger sections; usable everywhere an
  instinct function of the same type is, **after** definition.
- Parameters are typed and become hidden per-context global slots
  (allocated after the script's declared globals and arrays); a call pops
  the arguments into those slots (right-to-left, matching push order),
  pushes the return site on the interpreter's call stack, and jumps
  (OP_SCRIPTCALL, function index). `return expr` type-checks against the
  declared return type, leaves the value on the stack and returns
  (OP_SCRIPTRET); `return;` and falling off the end are legal for `void`.
  A non-void function must return through every path ending; falling off
  the end of a non-void function is a compile error.
- **Recursion is rejected at runtime** (the call stack refuses a function
  already on it): parameters are fixed slots, so re-entry would alias
  them. Function bodies live in the same instruction array after the
  events, indexed by a function table with `n+1` offsets.
- `ref` to a parameter works (it is a global slot). `pause` inside a
  function is rejected at compile time (the event system cannot resume a
  call stack).

## 6. The instruction encoding

One record per instruction (`ScriptInstr` in `Interp.h`): `{op, type,
data, arg}` where `arg` is a union of `SDWORD ival` (PUSH immediate),
`void* oval` (PUSH object constant) and `UDWORD func` (packed callee).
No pointer is ever stored in a 32-bit slot; jump deltas count
instructions; `aOpSize`/`OPCODE_SHIFT`/array masks are gone (array ops
carry base in `data`, dimensions in `type`).

`func` packing: bit 31 = table (0 instinct, 1 callback) for OP_CALL;
bit 31 = table (0 extern, 1 objvar), bit 30 = set-not-get for OP_VARCALL,
low bits the slot; OP_SCRIPTCALL's `func` is the script function index and
`data` its arity. The interpreter resolves through
`asScrInstinctTab`/`asScrCallbackTab`/`asScrExternalTab`/
`asScrObjectVarTab` at execution time, so the disassembler can print real
names and nothing survives in memory that a table reload would break.

## 7. Implementation notes (native compiler)

Decisions that bind the implementation, in addition to §6:

- **Files**: `NeuronCore/ScriptLex.h/.cpp` (token enum, lexer — shared with
  the stage-3 `.vlo` parser), `NeuronCore/ScriptComp.cpp` (parser +
  codegen + the `scriptSet*Tab` setters, table globals and
  `scriptLookUpType`, which move here from the deleted `Script_y.cpp`).
  New code in `namespace Neuron` (R15); the legacy entry points
  (`scriptCompile`, `cpPrintProgram`) keep their global names.
- **`SCRIPT_CODE`** keeps its member *names* but the members become
  `std::vector` (indexing sites compile unchanged; `psDebug == nullptr`
  checks become `.empty()`, `debugEntries` becomes `psDebug.size()`).
  New members for functions: `pFuncTab` (n+1 offsets), `psFuncData`
  (`{firstParamSlot, numParams, returnType}`), `aParamTypes` (types of
  all param slots). Context slot layout: declared globals
  `[0, numGlobals)`, array elements `[numGlobals, numGlobals+arraySize)`,
  function parameters after that. Parameter slots are **excluded** from
  the create/release value hooks (they are call-scoped temporaries; the
  hooks would leak or double-free them).
- **Interpreter**: a frame stack (`{returnIp, frameStart, frameEnd}`)
  serves OP_SCRIPTCALL/OP_SCRIPTRET; jump bounds check against the
  current frame; recursion is refused by scanning the frame stack for the
  callee. `INTERP_MAXINSTRUCTIONS` counts across frames per
  `interpRunScript` call.
- **Table ABI shrink**: `VAR_SYMBOL` keeps exactly the seven fields the
  static game-table initialisers set (`pIdent, type, storage, objType,
  index, get, set`); `FUNC_SYMBOL` keeps five (`pIdent, pFunc, type,
  numParams, aParams`); compiler-internal symbols get their own private
  types. `CONST_SYMBOL`/`CALLBACK_SYMBOL`/`TYPE_SYMBOL` unchanged.
- **Errors**: compile errors report file/line/column and expected-vs-got
  through `Neuron::Fatal`, which throws — `NeuronCoreTest` catches
  `std::runtime_error` to assert on rejected input; the game keeps its
  fatal-at-load behaviour.
- **Debug info**: always generated (the `.vlo` loader resolves variable
  names through `psVarDebug`). Each trigger and event body now gets a
  labelled entry at its first instruction — the old parser's trigger
  labels were lost to a buffer-management bug, so `eventGetTriggerID`
  printed "NOT FOUND" for code triggers; now it finds them.
- The old `OP_JUMPTRUE` was never emitted by the old compiler and is not
  by the new one; the interpreter still executes it for completeness.

## 8. `.vlo` value files (stage 3)

```
vlo      := entry*
entry    := "script" QTEXT ( "run" "{" binding* "}" | "store" QTEXT )
binding  := IDENT ( "[" INTEGER "]" )* type_name value
value    := INTEGER | "-" INTEGER | TRUE/FALSE | QTEXT
```
`run` compiles-and-runs against the named script now; `store` registers
the context under an id for later (`scrvAddContext`) and takes no binding
block. Only the `run` form appears in the corpus — all 123 shipped value
files use it — so `store` is carried forward from the old grammar
untested by data.

`type_name` is the same production as §2, and value files lean on the
`int`/`bool` keywords hard: `int`/`INT` appear 7,443 times and
`bool`/`BOOL` 79 times across the corpus, against 5,107 occurrences of
all the user types put together.

Values resolve per type (stats by name, level names, messages, strings);
object values install through `eventSetContextVar` typed as `INTERP_VAL`.
Variable names resolve through the script's always-generated variable
debug records.
