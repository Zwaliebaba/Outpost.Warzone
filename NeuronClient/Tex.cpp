#include "pch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "Frame.h"
#include "RenderTypes.h"
#include "PieState.h"
#include "TexMan.h"
#include "Tex.h"
#include "TextureCache.h"
#include "RendMode.h"
#include "IvisPatch.h"
#include "Render.h"

//*************************************************************************

iTexPage _TEX_PAGE[TEX_MAX];

//*************************************************************************

int _TEX_INDEX;

//*************************************************************************

static int _tex_get_top_bit(uint32 n)

{
  int i;
  uint32 mask = 0x80000000;

  for (i = 31; (n & mask) == 0; mask >>= 1, i--);

  return i;
}

//*************************************************************************
//*** load texture file or return index if already loaded
//*
//* params	filename = texture file name
//*			type		= page type, stored and never read
//*
//* returns	-1			= error
//*			else		= texture slot
//*
//******

/*
	Alex's shiny new texture loader. Will first check to see if the filename
	you're trying to load already resides in either memory (software) or in
	3dfx texture memory (3dfx). If it does, it'll send back the page number. If
	not, then it'll load it in for you. Also, it'll try and get it from the resource
	handler. If the resource handler doesn't know about this file then, the old
	texture load function is called. Should still work on PSX as the resource stuff isn't in
	there yet, so it'll default through to the old version 
*/

int pie_AddBMPtoTexPages(iSprite* s, char* filename, int type, iBool bColourKeyed, iBool bResource)
{
  int i;
  /* Get next available texture page */
  i = _TEX_INDEX;
  /* Have we used up too many? */
  if (_TEX_INDEX >= TEX_MAX)
  {
    /* Named `buffer` here, which is a local of Neuron::TexLoad and not in scope in
     * this function. It never failed to compile because iV_DEBUG1 expanded to
     * nothing; `filename` is this function's equivalent. */
    Neuron::DebugTrace("tex[TexLoad] = too many texture pages '{}'\n", filename);
    return -1;
  }

  /* Stick the name into the tex page structures */
  strcpy(_TEX_PAGE[i].name, filename);

  /* Store away all the info */
  /* DID come from a resource */
  _TEX_PAGE[i].bResource = bResource;
  // Default values
  _TEX_PAGE[i].tex.bmp = nullptr;
  _TEX_PAGE[i].tex.width = 256;
  _TEX_PAGE[i].tex.height = 256;
  _TEX_PAGE[i].tex.xshift = 0;

  if (s != nullptr)
  {
    _TEX_PAGE[i].tex.bmp = s->bmp;
    _TEX_PAGE[i].tex.width = s->width;
    _TEX_PAGE[i].tex.height = s->height;
    _TEX_PAGE[i].tex.xshift = _tex_get_top_bit(s->width);
  }
  _TEX_PAGE[i].tex.bColourKeyed = bColourKeyed;
  _TEX_PAGE[i].type = type;

  /* set pie texture pointer */
  if (dtm_LoadTexSurface(&_TEX_PAGE[i].tex, i) == FALSE)
    return -1;

  /* Send back the texpage number so we can store it in the IMD */

  _TEX_INDEX++;

  return (i);
}

/* Points an existing page at different pixels and re-uploads it, keeping
 * its slot so the texpage indices already baked into loaded models stay
 * right. This is what a tileset switch does to the pages it changes; it
 * replaces pie_ReloadTexPage, which decoded into the previous page's buffer
 * because the pixels belonged to a resource that was about to be freed.
 */
BOOL pie_RefillTexPage(SDWORD index, iSprite* s)
{
  if (index < 0 || index >= _TEX_INDEX || s == nullptr)
    return FALSE;

  _TEX_PAGE[index].tex.bmp = s->bmp;
  _TEX_PAGE[index].tex.width = s->width;
  _TEX_PAGE[index].tex.height = s->height;
  _TEX_PAGE[index].tex.xshift = _tex_get_top_bit(s->width);

  return dtm_LoadTexSurface(&_TEX_PAGE[index].tex, index);
}

/* The page a model's TEXTURE directive names, by the page-NN id the loader
 * reduces that name to. The lookup, the decode and the slot all belong to
 * TextureCache now; this stays because IMDLoad calls it and the arguments
 * it passes - a search path, a palette flag, the model's own idea of the
 * page type - stopped meaning anything when the pages became data.
 */
int Neuron::TexLoadNew(char* path, char* filename, int type, iBool palkeep, iBool bColourKeyed)
{
  (void)path;
  (void)type;
  (void)palkeep;
  (void)bColourKeyed;

  // If we are in the BSP or PIEBIN tool, then just added it into the array and exit
#ifdef PIETOOL
  return pie_AddBMPtoTexPages(NULL, filename, type, bColourKeyed, TRUE);
#endif

  DEBUG_ASSERT_TEXT(strlen(filename)<MAX_FILE_PATH, "Texture file path too long");

  return Neuron::TextureCache::PageIndex(filename);
}

// Routine to generate TEXpages 
//
// Called from the resource loading stuff
//
//
BOOL GenerateTEXPAGE(char* Filename, RECT* VramArea, UDWORD Mode, UWORD Clut) { return (TRUE); }

SBYTE GetTextureNumber(char* Name)
{
  char Letter, NextLetter;
  // find first digit
  SBYTE Num = -1;

  do
  {
    Letter = *Name++;
    if ((Letter >= '0') && (Letter <= '9'))
    {
      NextLetter = *Name++;
      if ((NextLetter >= '0') && (NextLetter <= '9'))
        Num = ((Letter - '0') * 10) + (NextLetter - '0'); // 2 digit number
      else // single digit number
        Num = Letter - '0';
      break;
    }
  }
  while (Letter != 0);

  return (Num);
}

/*
	Alex - fixed this so it doesn't try to free up the memory if it got the page from resource
	handler - this is because the resource handler will deal with freeing it, and in all probability
	will have already done so by the time this is called, thus avoiding an 'already freed' moan.
*/
void pie_TexShutDown(void)

{
  int i, j;

  i = 0;
  j = 0;

  while (i < _TEX_INDEX)
  {
    /*	Only free up the ones that were NOT allocated through resource handler cos they'll already
      be free */
    if (_TEX_PAGE[i].bResource == FALSE)
    {
      if (_TEX_PAGE[i].tex.bmp)
      {
        j++;
        delete[] _TEX_PAGE[i].tex.bmp;
        _TEX_PAGE[i].tex.bmp = nullptr;
      }
    }
    i++;
  }

  Neuron::DebugTrace("pie_TexShutDown successful - freed {} texture pages\n",j);
}

/* The number of texture pages loaded so far. Texture.cpp asks for this to
 * work out where the level's own pages start.
 */
UDWORD pie_GetLastPageDownloaded(void) { return _TEX_INDEX; }

void pie_TexInit(void)
{
  int i;

  i = 0;

  while (i < _TEX_INDEX)
  {
    _TEX_PAGE[i].tex.bmp = nullptr;
    _TEX_PAGE[i].tex.width = 0;
    _TEX_PAGE[i].tex.height = 0;
    _TEX_PAGE[i].tex.xshift = 0;
    i++;
  }
}

// Check that a texture is  <= 256x256 and 2^n x 2^n in size.
//
BOOL Neuron::TexSizeIsLegal(UDWORD Width, UDWORD Height)
{
  if ((Width > 256) || (Height > 256))
    return FALSE;

  if (!Neuron::IsPower2(Width))
    return FALSE;

  //  For now don't limit height to 2^n.
  if (!Neuron::IsPower2(Height))
    return FALSE;

  return TRUE;
}

// Return TRUE if the given value is 2^n.
//
BOOL Neuron::IsPower2(UDWORD Value)
{
  int Bits = 0;

  while (Value)
  {
    if (Value & 1)
      Bits++;
    Value = Value >> 1;
  }

  if (Bits != 1)
    return FALSE;

  return TRUE;
}
