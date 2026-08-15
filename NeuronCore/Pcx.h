#ifndef _pcx_
#define _pcx_

#include "IvisDef.h"

namespace Neuron
{
  extern iBool PCXLoad(char* file, iSprite* s, iColour* pal);
  extern iBool PCXLoadMem(int8* pcximge, iSprite* s, iColour* pal);
}

extern BOOL pie_PCXLoadToBuffer(char* file, iSprite* s, iColour* pal);
extern BOOL pie_PCXLoadMemToBuffer(int8* pcximge, iSprite* s, iColour* pal);

#endif /* _pcx_ */
