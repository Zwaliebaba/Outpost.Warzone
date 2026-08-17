/*
 * main.cpp - compile every shipped .slo with the real script compiler.
 *
 * The game tables come from Tables.cpp, which check_scripts.py generates
 * from Outpost/ScriptTabs.cpp; the compiler itself is NeuronCore's, built
 * from source. Nothing here is a copy of production code.
 */
#include "pch.h"

#include "Frame.h"
#include "Script.h"

#include <fstream>
#include <iterator>

extern TYPE_SYMBOL asTypeTable[];
extern FUNC_SYMBOL asFuncTable[];
extern CONST_SYMBOL asConstantTable[];
extern CALLBACK_SYMBOL asCallbackTable[];
extern VAR_SYMBOL asExternTable[];
extern VAR_SYMBOL asObjTable[];
extern TYPE_EQUIV asEquivTable[];

/* Interp.cpp is not built here - it needs the whole event system - so the
   one interpreter entry point the compiler calls stands alone. This is the
   same walk Interp.cpp does over the same table. */
BOOL interpCheckEquiv(INTERP_TYPE to, INTERP_TYPE from)
{
  const bool toRef = (to & VAL_REF) != 0;
  const bool fromRef = (from & VAL_REF) != 0;
  if (toRef != fromRef)
    return FALSE;
  to = static_cast<INTERP_TYPE>(to & ~VAL_REF);
  from = static_cast<INTERP_TYPE>(from & ~VAL_REF);
  if (to == from)
    return TRUE;

  for (UDWORD i = 0; asEquivTable[i].base != 0; i++)
  {
    if (asEquivTable[i].base != to)
      continue;
    for (UDWORD j = 0; j < asEquivTable[i].numEquiv; j++)
    {
      if (asEquivTable[i].aEquivTypes[j] == from)
        return TRUE;
    }
  }
  return FALSE;
}

int main(int argc, char** argv)
{
  scriptSetTypeTab(asTypeTable);
  scriptSetFuncTab(asFuncTable);
  scriptSetConstTab(asConstantTable);
  scriptSetCallbackTab(asCallbackTable);
  scriptSetExternalTab(asExternTable);
  scriptSetObjectTab(asObjTable);

  int compiled = 0;
  int failed = 0;
  for (int i = 1; i < argc; i++)
  {
    std::ifstream file(argv[i], std::ios::binary);
    if (!file)
    {
      std::printf("FAIL %s\n  cannot open\n", argv[i]);
      failed += 1;
      continue;
    }
    const std::vector<char> aData((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    SCRIPT_CODE* psCode = nullptr;
    try
    {
      scriptCompile(reinterpret_cast<UBYTE*>(const_cast<char*>(aData.data())),
                    static_cast<UDWORD>(aData.size()), &psCode, SCR_DEBUGINFO);
      compiled += 1;
      delete psCode; // what scriptFreeCode does; Script.cpp is not built here
    }
    catch (const std::exception& e)
    {
      std::printf("FAIL %s\n  %s\n", argv[i], e.what());
      failed += 1;
    }
  }

  std::printf("compiled %d, failed %d\n", compiled, failed);
  return failed != 0 ? 1 : 0;
}
