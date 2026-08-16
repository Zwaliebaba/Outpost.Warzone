#ifndef _tex_
#define _tex_
#include <d3d9.h>
#include "RenderTypes.h"


//*************************************************************************

/* One page per device texture. The two used to disagree - the name table
 * allowed 48 while the device only ever had 32 - so a page past 32 was
 * written into the table and then refused by the texture manager.
 */
#define TEX_MAX		32

//*************************************************************************

#define TEXTEX(i)		((iTexture *) (&_TEX_PAGE[(i)].tex))
#define TEXWIDTH(i)	(_TEX_PAGE[(i)].tex.width)
#define TEXHEIGHT(i) (_TEX_PAGE[(i)].tex.height)
#define TEXNAME(i)	((char *) (&_TEX_PAGE[(i)].name))
#define TEXTYPE(i)	(_TEX_PAGE[(i)].type)

//*************************************************************************

using iTexPage = struct
{
  iTexture tex;
  LPDIRECT3DTEXTURE9 psTexture; // the device texture this page draws from
  uint8 type;
  char name[80];
  int bResource; // Was page provided by resource handler?
};

//*************************************************************************
extern int _TEX_INDEX;
extern iTexPage _TEX_PAGE[TEX_MAX];

//*************************************************************************

namespace Neuron
{
  extern int TexLoad(char* path, char* filename, int type, iBool palkeep, iBool bColourKeyed);
  extern int TexLoadNew(char* path, char* filename, int type, iBool palkeep, iBool bColourKeyed);
}
extern int pie_ReloadTexPage(char* filename, UBYTE* pBuffer);
extern int pie_AddBMPtoTexPages(iSprite* s, char* filename, int type, iBool bColourKeyed, iBool bResource);
extern void pie_TexInit(void);
extern UDWORD pie_GetLastPageDownloaded(void);

//*************************************************************************

extern void pie_TexShutDown(void);

namespace Neuron
{
  extern BOOL TexSizeIsLegal(UDWORD Width, UDWORD Height);
  extern BOOL IsPower2(UDWORD Value);
}

BOOL GenerateTEXPAGE(char* Filename, RECT* VramArea, UDWORD Mode, UWORD Clut);
BOOL FindTextureNumber(UDWORD TexNum, int* TexPage);

#endif
