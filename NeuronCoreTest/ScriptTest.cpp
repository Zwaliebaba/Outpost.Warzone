#include "pch.h"
#include "CppUnitTest.h"

#include "Script.h"

#include <cstring>
#include <stdexcept>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

/* The compiler, interpreter and event system under a miniature set of game
   tables.  Script-defined functions have no shipped script exercising
   them, so this suite is their only harness (Docs/ScriptRewrite.md 6.2).

   The instinct FFI here still pops object values through UDWORD
   destinations, matching ScriptFuncs.cpp today; stage 4 of the rewrite
   replaces both together. */
namespace NeuronCoreTest
{
namespace
{
enum
{
  TT_OBJ = VAL_USERTYPESTART,
  // an AT_OBJECT user type
  TT_SIMPLE,
  // an AT_SIMPLE user type
};

struct TestObj
{
  SDWORD health;
};

TestObj g_sObjA;
TestObj g_sObjB;

SDWORD g_lastIntArg;
SDWORD g_externValue;
SDWORD g_callCount;

BOOL TestRecordInt(void)
{
  SDWORD val;
  if (!stackPopParams({{VAL_INT, &val}}))
    return FALSE;
  g_lastIntArg = val;
  g_callCount += 1;
  return TRUE;
}

BOOL TestAddTwo(void)
{
  SDWORD a, b;
  if (!stackPopParams({{VAL_INT, &a}, {VAL_INT, &b}}))
    return FALSE;
  return stackPushResult(VAL_INT, a + b);
}

BOOL TestIsPositive(void)
{
  SDWORD val;
  if (!stackPopParams({{VAL_INT, &val}}))
    return FALSE;
  return stackPushResult(VAL_BOOL, val > 0);
}

BOOL TestWriteRef(void)
{
  SDWORD *pDest, val;
  if (!stackPopParams({{VAL_INT, &val}, {VAL_INT | VAL_REF, &pDest}}))
    return FALSE;
  *pDest = val;
  return TRUE;
}

BOOL TestGetObjA(void)
{
  return stackPushResult(TT_OBJ, &g_sObjA);
}

BOOL TestGetObjB(void)
{
  return stackPushResult(TT_OBJ, &g_sObjB);
}

/* Object member accessors.  The compiler emits: value, object, set - so
   set pops the object first, then the value. */
BOOL TestObjHealthGet(UDWORD index)
{
  TestObj* psObj;
  (void)index;
  if (!stackPopParams({{TT_OBJ, &psObj}}))
    return FALSE;
  return stackPushResult(VAL_INT, psObj->health);
}

BOOL TestObjHealthSet(UDWORD index)
{
  TestObj* psObj;
  SDWORD val;
  (void)index;
  if (!stackPopParams({{VAL_INT, &val}, {TT_OBJ, &psObj}}))
    return FALSE;
  psObj->health = val;
  return TRUE;
}

BOOL TestExternGet(UDWORD index)
{
  (void)index;
  return stackPushResult(VAL_INT, g_externValue);
}

BOOL TestExternSet(UDWORD index)
{
  (void)index;
  return stackPopParams({{VAL_INT, &g_externValue}}) ? TRUE : FALSE;
}

BOOL TestCallback(void)
{
  return stackPushResult(VAL_BOOL, TRUE);
}

TYPE_SYMBOL asTestTypes[] = {
  {TT_OBJ, AT_OBJECT, "TESTOBJ"},
  {TT_SIMPLE, AT_SIMPLE, "TESTSIMPLE"},
  {0, 0, nullptr},
};

FUNC_SYMBOL asTestFuncs[] = {
  {"recordInt", TestRecordInt, VAL_VOID, 1, {VAL_INT}},
  {"addTwo", TestAddTwo, VAL_INT, 2, {VAL_INT, VAL_INT}},
  {"isPositive", TestIsPositive, VAL_BOOL, 1, {VAL_INT}},
  {"writeRef", TestWriteRef, VAL_VOID, 2, {VAL_INT, VAL_INT | VAL_REF}},
  {"getObjA", TestGetObjA, TT_OBJ, 0, {VAL_VOID}},
  {"getObjB", TestGetObjB, TT_OBJ, 0, {VAL_VOID}},
  {nullptr, nullptr, VAL_VOID, 0, {VAL_VOID}},
};

VAR_SYMBOL asTestObjVars[] = {
  {"health", VAL_INT, ST_OBJECT, TT_OBJ, 0, TestObjHealthGet, TestObjHealthSet},
  {nullptr, VAL_VOID, ST_OBJECT, VAL_VOID, 0, nullptr, nullptr},
};

VAR_SYMBOL asTestExterns[] = {
  {"externCounter", VAL_INT, ST_EXTERN, 0, 0, TestExternGet, TestExternSet},
  {nullptr, VAL_VOID, ST_EXTERN, VAL_VOID, 0, nullptr, nullptr},
};

CONST_SYMBOL asTestConsts[] = {
  {"ANSWER", VAL_INT, 0, 42, nullptr},
  {"NULLTESTOBJ", TT_OBJ, 0, 0, nullptr},
  {nullptr, VAL_VOID, 0, 0, nullptr},
};

CALLBACK_SYMBOL asTestCallbacks[] = {
  {"CALL_TESTEVENT", TR_CALLBACKSTART, nullptr, 0, {VAL_VOID}},
  {"CALL_TESTPARAM", TR_CALLBACKSTART + 1, TestCallback, 1, {VAL_INT}},
  {nullptr, 0, nullptr, 0, {VAL_VOID}},
};

SCRIPT_CODE* Compile(const char* pSource)
{
  SCRIPT_CODE* psProg = nullptr;
  const BOOL ok = scriptCompile(
    reinterpret_cast<UBYTE*>(const_cast<char*>(pSource)),
    static_cast<UDWORD>(strlen(pSource)), &psProg, SCR_DEBUGINFO);
  Assert::IsTrue(ok == TRUE, L"scriptCompile failed");
  Assert::IsNotNull(psProg);
  return psProg;
}

/* Compile expecting rejection: Neuron::Fatal throws std::runtime_error */
void AssertRejects(const char* pSource)
{
  SCRIPT_CODE* psProg = nullptr;
  bool thrown = false;
  try
  {
    scriptCompile(reinterpret_cast<UBYTE*>(const_cast<char*>(pSource)),
                  static_cast<UDWORD>(strlen(pSource)), &psProg, SCR_DEBUGINFO);
  }
  catch (const std::runtime_error&)
  {
    thrown = true;
  }
  Assert::IsTrue(thrown, L"expected the script to be rejected");
}

SDWORD GlobalInt(SCRIPT_CONTEXT* psContext, UDWORD index)
{
  INTERP_VAL* psVal = nullptr;
  Assert::IsTrue(eventGetContextVal(psContext, index, &psVal) == TRUE);
  return psVal->v.ival;
}

/* Compile, make a context, run event `event`, return the context */
SCRIPT_CONTEXT* RunEvent(SCRIPT_CODE* psProg, UDWORD event = 0)
{
  SCRIPT_CONTEXT* psContext = nullptr;
  Assert::IsTrue(eventNewContext(psProg, CR_NORELEASE, &psContext) == TRUE);
  Assert::IsTrue(interpRunScript(psContext, IRT_EVENT, event, 0) == TRUE, L"event failed to run");
  return psContext;
}
} // namespace

TEST_CLASS(ScriptTest)
{
public:
  TEST_METHOD_INITIALIZE(SetUp)
  {
    scriptSetTypeTab(asTestTypes);
    scriptSetFuncTab(asTestFuncs);
    scriptSetObjectTab(asTestObjVars);
    scriptSetExternalTab(asTestExterns);
    scriptSetConstTab(asTestConsts);
    scriptSetCallbackTab(asTestCallbacks);
    scriptSetTypeEquiv(nullptr);

    Assert::IsTrue(scriptInitialise() == TRUE);

    g_lastIntArg = 0;
    g_externValue = 0;
    g_callCount = 0;
    g_sObjA.health = 0;
    g_sObjB.health = 0;
  }

  TEST_METHOD_CLEANUP(TearDown)
  {
    scriptShutDown();
  }

  TEST_METHOD(ArithmeticAndGlobals)
  {
    SCRIPT_CODE* psProg = Compile(
      "public int a, b;\n"
      "event doIt(inactive) { a = 2 + 3 * 4; b = (a - 4) / 5; }\n");
    SCRIPT_CONTEXT* psContext = RunEvent(psProg);
    Assert::AreEqual(14, GlobalInt(psContext, 0));
    Assert::AreEqual(2, GlobalInt(psContext, 1));
    eventRemoveContext(psContext);
    scriptFreeCode(psProg);
  }

  TEST_METHOD(IfElseChainsAndWhile)
  {
    SCRIPT_CODE* psProg = Compile(
      "private int n, sum;\n"
      "event doIt(inactive)\n"
      "{\n"
      "  n = 5; sum = 0;\n"
      "  while (n > 0) { sum = sum + n; n = n - 1; }\n"
      "  if (sum == 15) { sum = sum * 2; }\n"
      "  else if (sum == 0) { sum = 0 - 1; }\n"
      "  else { sum = 0 - 2; }\n"
      "}\n");
    SCRIPT_CONTEXT* psContext = RunEvent(psProg);
    Assert::AreEqual(30, GlobalInt(psContext, 1));
    eventRemoveContext(psContext);
    scriptFreeCode(psProg);
  }

  TEST_METHOD(CaseInsensitiveKeywordsAndConstants)
  {
    SCRIPT_CODE* psProg = Compile(
      "public int x;\n"
      "public BOOL flag;\n"
      "event doIt(inactive) { IF (TRUE AND NOT FALSE) { x = ANSWER; flag = true; } }\n");
    SCRIPT_CONTEXT* psContext = RunEvent(psProg);
    Assert::AreEqual(42, GlobalInt(psContext, 0));
    Assert::AreEqual(1, GlobalInt(psContext, 1));
    eventRemoveContext(psContext);
    scriptFreeCode(psProg);
  }

  TEST_METHOD(InstinctCallsRefParamsAndExterns)
  {
    SCRIPT_CODE* psProg = Compile(
      "public int got;\n"
      "event doIt(inactive)\n"
      "{\n"
      "  recordInt(addTwo(20, 3));\n"
      "  writeRef(7, ref got);\n"
      "  externCounter = got + addTwo(1, 2);\n"
      "  got = externCounter + got;\n"
      "}\n");
    SCRIPT_CONTEXT* psContext = RunEvent(psProg);
    Assert::AreEqual(23, g_lastIntArg);
    Assert::AreEqual(10, g_externValue);
    Assert::AreEqual(17, GlobalInt(psContext, 0));
    eventRemoveContext(psContext);
    scriptFreeCode(psProg);
  }

  TEST_METHOD(ArraysAreRowMajor)
  {
    SCRIPT_CODE* psProg = Compile(
      "public int grid[3][4];\n"
      "public int i, j, flat;\n"
      "event doIt(inactive)\n"
      "{\n"
      "  i = 0;\n"
      "  while (i < 3)\n"
      "  {\n"
      "    j = 0;\n"
      "    while (j < 4) { grid[i][j] = i * 10 + j; j = j + 1; }\n"
      "    i = i + 1;\n"
      "  }\n"
      "  flat = grid[2][3];\n"
      "}\n");
    SCRIPT_CONTEXT* psContext = RunEvent(psProg);
    Assert::AreEqual(23, GlobalInt(psContext, 2));
    /* dimension 0 outermost: grid[2][3] is slot numGlobals + 2*4 + 3 */
    INTERP_VAL* psVal = nullptr;
    Assert::IsTrue(eventGetContextVal(psContext, 3 + 2 * 4 + 3, &psVal) == TRUE);
    Assert::AreEqual(23, psVal->v.ival);
    eventRemoveContext(psContext);
    scriptFreeCode(psProg);
  }

  TEST_METHOD(ObjectMembersAndEquality)
  {
    SCRIPT_CODE* psProg = Compile(
      "public TESTOBJ it, other;\n"
      "public int hp;\n"
      "public BOOL same;\n"
      "event doIt(inactive)\n"
      "{\n"
      "  it = getObjA();\n"
      "  other = getObjB();\n"
      "  it.health = 250;\n"
      "  hp = it.health;\n"
      "  same = it == other;\n"
      "  other = it;\n"
      "  same = same or it == other;\n"
      "}\n");
    SCRIPT_CONTEXT* psContext = RunEvent(psProg);
    Assert::AreEqual(250, g_sObjA.health);
    Assert::AreEqual(250, GlobalInt(psContext, 2));
    Assert::AreEqual(1, GlobalInt(psContext, 3));
    eventRemoveContext(psContext);
    scriptFreeCode(psProg);
  }

  TEST_METHOD(TriggersCompileToTheRightMetadata)
  {
    SCRIPT_CODE* psProg = Compile(
      "public int x;\n"
      "trigger everyTrig(every, 100);\n"
      "trigger waitTrig(wait, 50);\n"
      "trigger initTrig(init);\n"
      "trigger codeTrig(x > 3, 20);\n"
      "trigger cbTrig(CALL_TESTEVENT);\n"
      "event doIt(everyTrig) { x = 1; }\n"
      "event doWait(waitTrig) { x = 2; }\n"
      "event doInit(initTrig) { x = 3; }\n"
      "event doCode(codeTrig) { x = 4; }\n"
      "event doCb(cbTrig) { x = 5; }\n"
      "event doInline(every, 7) { x = 6; }\n");
    Assert::AreEqual(static_cast<UWORD>(6), psProg->numTriggers); // 5 + 1 inline
    Assert::AreEqual(static_cast<UWORD>(6), psProg->numEvents);
    Assert::AreEqual(static_cast<UWORD>(TR_EVERY), psProg->psTriggerData[0].type);
    Assert::AreEqual(100u, psProg->psTriggerData[0].time);
    Assert::AreEqual(static_cast<UWORD>(TR_WAIT), psProg->psTriggerData[1].type);
    Assert::AreEqual(static_cast<UWORD>(TR_INIT), psProg->psTriggerData[2].type);
    Assert::AreEqual(static_cast<UWORD>(TR_CODE), psProg->psTriggerData[3].type);
    Assert::AreEqual(static_cast<UWORD>(1), psProg->psTriggerData[3].code);
    Assert::AreEqual(static_cast<UWORD>(TR_CALLBACKSTART), psProg->psTriggerData[4].type);
    Assert::AreEqual(static_cast<UWORD>(TR_EVERY), psProg->psTriggerData[5].type);
    Assert::AreEqual(static_cast<SWORD>(5), psProg->pEventLinks[5]);
    /* the code trigger's body leaves its BOOL on the stack */
    SCRIPT_CONTEXT* psContext = nullptr;
    Assert::IsTrue(eventNewContext(psProg, CR_NORELEASE, &psContext) == TRUE);
    Assert::IsTrue(interpRunScript(psContext, IRT_TRIGGER, 3, 0) == TRUE);
    INTERP_VAL sVal;
    sVal.type = VAL_BOOL;
    Assert::IsTrue(stackPopType(&sVal) == TRUE);
    Assert::AreEqual(0, sVal.v.ival); // x is 0, so x > 3 is false
    eventRemoveContext(psContext);
    scriptFreeCode(psProg);
  }

  TEST_METHOD(ForwardEventDeclarationsLink)
  {
    SCRIPT_CODE* psProg = Compile(
      "public int x;\n"
      "public EVENT which;\n"
      "event later;\n"
      "event first(inactive) { which = later; x = 1; }\n"
      "event later(inactive) { x = 2; }\n");
    SCRIPT_CONTEXT* psContext = RunEvent(psProg, 0); // 'later' declared first: index 0
    Assert::AreEqual(2, GlobalInt(psContext, 0));
    eventRemoveContext(psContext);
    scriptFreeCode(psProg);
  }

  TEST_METHOD(ScriptFunctionsWorkEndToEnd)
  {
    SCRIPT_CODE* psProg = Compile(
      "public int total;\n"
      "function int clampAdd(int a, int b)\n"
      "{\n"
      "  if (a + b > 100) { return 100; }\n"
      "  return a + b;\n"
      "}\n"
      "function void bump(int by)\n"
      "{\n"
      "  total = clampAdd(total, by);\n"
      "}\n"
      "event doIt(inactive)\n"
      "{\n"
      "  total = 0;\n"
      "  bump(30);\n"
      "  bump(50);\n"
      "  bump(40);\n"
      "}\n");
    Assert::AreEqual(static_cast<UWORD>(2), psProg->numFuncs);
    SCRIPT_CONTEXT* psContext = RunEvent(psProg);
    Assert::AreEqual(100, GlobalInt(psContext, 0));
    eventRemoveContext(psContext);
    scriptFreeCode(psProg);
  }

  TEST_METHOD(ScriptFunctionRecursionIsRefused)
  {
    SCRIPT_CODE* psProg = Compile(
      "public int x;\n"
      "function int loop(int n) { return loop(n); }\n"
      "event doIt(inactive) { x = loop(1); }\n");
    SCRIPT_CONTEXT* psContext = nullptr;
    Assert::IsTrue(eventNewContext(psProg, CR_NORELEASE, &psContext) == TRUE);
    Assert::IsTrue(interpRunScript(psContext, IRT_EVENT, 0, 0) == FALSE, L"recursion must fail at runtime");
    eventRemoveContext(psContext);
    scriptFreeCode(psProg);
  }

  TEST_METHOD(RejectsWhatTheOldGrammarRejected)
  {
    // type mismatch in assignment
    AssertRejects("public int a;\npublic BOOL b;\nevent e(inactive) { a = b; }\n");
    // arithmetic on bools
    AssertRejects("public BOOL b;\nevent e(inactive) { b = true + false; }\n");
    // unknown identifier
    AssertRejects("event e(inactive) { nosuchthing = 1; }\n");
    // wrong arity
    AssertRejects("event e(inactive) { recordInt(1, 2); }\n");
    // string literal in script code has no production
    AssertRejects("public int a;\nevent e(inactive) { recordInt(\"hi\"); }\n");
    // sections out of order: a trigger after an event
    AssertRejects("event e(inactive) { }\ntrigger t(every, 5);\n");
    // event declared but never defined
    AssertRejects("event ghost;\nevent e(inactive) { }\n");
  }

  TEST_METHOD(RejectsBadFunctions)
  {
    // return outside a function
    AssertRejects("event e(inactive) { return; }\n");
    // pause inside a function
    AssertRejects("function void f() { pause(10); }\nevent e(inactive) { }\n");
    // non-void function not ending in return
    AssertRejects("function int f() { recordInt(1); }\nevent e(inactive) { }\n");
    // returning a value from a void function
    AssertRejects("function void f() { return 3; }\nevent e(inactive) { }\n");
  }

  TEST_METHOD(ExitStatementLeavesTheBody)
  {
    SCRIPT_CODE* psProg = Compile(
      "public int x;\n"
      "event doIt(inactive) { x = 1; exit; x = 2; }\n");
    SCRIPT_CONTEXT* psContext = RunEvent(psProg);
    Assert::AreEqual(1, GlobalInt(psContext, 0));
    eventRemoveContext(psContext);
    scriptFreeCode(psProg);
  }

  TEST_METHOD(CommentsAndFormatsLex)
  {
    SCRIPT_CODE* psProg = Compile(
      "// line comment\n"
      "/* block\n   comment */\n"
      "public int x; // trailing\n"
      "event doIt(inactive) { x = /* inline */ 3; }\n");
    SCRIPT_CONTEXT* psContext = RunEvent(psProg);
    Assert::AreEqual(3, GlobalInt(psContext, 0));
    eventRemoveContext(psContext);
    scriptFreeCode(psProg);
  }
};
} // namespace NeuronCoreTest
