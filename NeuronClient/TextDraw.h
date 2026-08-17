#ifndef _INCLUDED_TEXTDRAW_
#define _INCLUDED_TEXTDRAW_

#include "BitImage.h"

/* Text colours, packed A8R8G8B8.
 *
 * These were the palette-era sentinels -1/-2/-3, which the text renderer
 * mapped to real colours. Once colour became the packed value those three
 * read as 0xffffffff, 0xfffffffe and 0xfffffffd - all indistinguishably
 * white, so a highlighted menu item looked exactly like an unhighlighted
 * one. They are now the colours they always denoted; the separate
 * PIE_TEXT_*_COLOUR spellings they duplicated are gone.
 *
 * White keeps its meaning as "no tint": modulating by 0xffffffff is
 * identity, so the renderer still uses it as the untinted case. */
#define PIE_TEXT_WHITE				(0xffffffff)
#define PIE_TEXT_LIGHTBLUE			(0xffa0a0ff)
#define PIE_TEXT_DARKBLUE			(0xff6060c0)

namespace Neuron
{
  extern void ClearFonts(void);
  extern void SetFont(int FontID);
  extern int CreateFontIndirect(IMAGEFILE* ImageFile, UWORD* AsciiTable, int SpaceSize);
  extern void GetTextExtents(unsigned char* String, int* Width, int* y0, int* y1);
  extern int GetTextAboveBase(void);
  extern int GetTextBelowBase(void);
  extern int GetTextLineSize(void);
  extern int GetTextWidth(unsigned char* String);
  extern int GetCharWidth(unsigned char Char);
  extern void SetTextColour(UDWORD Colour);
}

#define ASCII_SPACE			(32)
#define ASCII_NEWLINE		('@')
#define ASCII_COLOURMODE	('#')

// Valid values for "Justify" argument of pie_DrawFormattedText().

enum
{
  FTEXT_LEFTJUSTIFY,
  // Left justify.
  FTEXT_CENTRE,
  // Centre justify.
  FTEXT_RIGHTJUSTIFY,
  // Right justify.
  FTEXT_LEFTJUSTIFYAPPEND,
  // Start from end of last print and then left justify.
};

// Valid values for paramaters for pie_SetFormattedTextFlags().

// Skip leading spaces at the start of each line of text. Improves centre justification
// but may result in unwanted word breaks.
#define	FTEXTF_SKIP_LEADING_SPACES		1
// Skip trailing spaces at the end of each line of text, improves centre justification.
#define	FTEXTF_SKIP_TRAILING_SPACES		2
// Inserts a space before the first word in the string, usefull when use FTEXT_LEFTJUSTIFYAPPEND
#define	FTEXTF_INSERT_SPACE_ON_APPEND	4

extern void pie_SetFormattedTextFlags(UDWORD Flags);
extern UDWORD pie_GetFormattedTextFlags(void);
extern void pie_StartTextExtents(void);
extern void pie_FillTextExtents(int BorderThickness, UBYTE r, UBYTE g, UBYTE b, BOOL Alpha);
extern UDWORD pie_DrawFormattedText(UBYTE* String, UDWORD x, UDWORD y, UDWORD Width, UDWORD Justify, BOOL DrawBack);

extern void pie_DrawText(unsigned char* string, UDWORD x, UDWORD y);
extern void pie_DrawTextToBackBuffer(unsigned char* String, int XPos, int YPos);
extern void pie_DrawText270(unsigned char* String, int XPos, int YPos);

void InitClut24(UWORD* InputClut);

using RENDERTEXT_CALLBACK = void(*)(UBYTE* String, UDWORD X, UDWORD Y);
// routines used for textdraw
void SetIndirectDrawTextCallback(RENDERTEXT_CALLBACK routine);
RENDERTEXT_CALLBACK GetIndirectDrawTextCallback(void);

#endif
