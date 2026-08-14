#include "pch.h"
#include "Ivi.h"
#include "Bug.h"

//*************************************************************************

void _debug_create_log(void)

{
  FILE* fp;

  fp = fopen(iV_DEBUG_FILE, "w");
  if (fp)
    fclose(fp);
}

void iV_Debug(char* string, ...)

{
  va_list argptr;
  FILE* fp;

  fp = fopen(iV_DEBUG_FILE, "a");
  if (fp)
  {
    va_start(argptr, string);
    vfprintf(fp, string, argptr);
    va_end(argptr);
    fclose(fp);
  }
}

void iV_DisplayLogFile(void)

{
  FILE* fp;
  int c;

  fp = fopen(iV_DEBUG_FILE, "r");

  if (fp)
  {
    while ((c = getc(fp)) != EOF) { Neuron::DebugTrace("{}",c); }
    fclose(fp);
  }
}
