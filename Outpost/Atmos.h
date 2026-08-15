#ifndef _atmos_h
#define _atmos_h
#include "Model.h"
#include "RenderTypes.h"

using ATPART = struct _atmosParticle
{
  UBYTE status;
  UBYTE type;
  UDWORD size;
  PIEVECTORF position;
  PIEVECTORF velocity;
  iIMDShape* imd;
};

using WT_CLASS = enum
{
  WT_RAINING,
  WT_SNOWING,
  WT_NONE
};

extern void atmosInitSystem(void);
extern void atmosUpdateSystem(void);
extern void renderParticle(ATPART* psPart);
extern void atmosDrawParticles(void);
extern void atmosSetWeatherType(WT_CLASS type);
extern WT_CLASS atmosGetWeatherType(void);
using MISTAREA = struct _mistlocale
{
  UBYTE type;
  UBYTE val;
  UBYTE base;
  SBYTE vec;
};

extern MISTAREA* pMistValues;

#endif
