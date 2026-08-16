#include "pch.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "PieState.h"
#include "Screen.h"
#include "RendMode.h"
#include "RendFunc.h"
#include "RenderClip.h"

#include "Render2D.h"

#include "Palette.h"
#include "IvisPatch.h"
#include "TextDraw.h"
#include "BitImage.h"
#include "RenderModel.h"

extern void pie_DrawTextNew(unsigned char* string, int x, int y);
extern SDWORD DisplayXFactor;
#ifndef PIETOOL	// if we are in the pie tool then don't compile any of this

/***************************************************************************/
/*
 *	Global Variables
 */
/***************************************************************************/
/***************************************************************************/
/*
 *	Local Definitions
 */
/***************************************************************************/

#define MAX_IVIS_FONTS	8

using IVIS_FONT = struct
{
  IMAGEFILE* FontFile; // The image data that contains the font.
  //	UWORD FontStartID;		// The image ID of character ASCII 33.
  //	UWORD FontEndID;		// The image ID of last character in the font.
  int FontAbove; // Max pixels above the base line.
  int FontBelow; // Max pixels below the base line.
  int FontLineSize; // Pixel spacing used for new lines.
  int FontSpaceSize; // Pixel spacing used for spaces.
  SWORD FontColourIndex; // The colour index to use.
  UWORD* AsciiTable;
};

/***************************************************************************/
/*
 *	Local Variables
 */
/***************************************************************************/

static SWORD TextColourIndex;
static int NumFonts;
static int ActiveFontID;
static IVIS_FONT iVFonts[MAX_IVIS_FONTS];

/***************************************************************************/
/*
 *	Local ProtoTypes
 */
/***************************************************************************/
void pie_BeginTextRender(SWORD ColourIndex);
void pie_TextRender(IMAGEFILE* ImageFile, UWORD ID, int x, int y);
void pie_TextRender270(IMAGEFILE* ImageFile, UWORD ImageID, int x, int y); //prototype

/***************************************************************************/
/*
 *	Source
 */
/***************************************************************************/

void Neuron::ClearFonts(void)
{
  NumFonts = 0;
  ActiveFontID = -1;
}

// Create a font using an ascii lookup table.
//
// IMAGEFILE *ImageFile		Image file containing the font graphics.
// UWORD *AsciiTable		Array of 256 Ascii to ImageID lookups.
// int SpaceSize			Pixel size of a space.
//
int Neuron::CreateFontIndirect(IMAGEFILE* ImageFile, UWORD* AsciiTable, int SpaceSize)
{
  int Above, Below;
  int Height;
  UWORD Index, c;
  IVIS_FONT* Font;

  assert(NumFonts < MAX_IVIS_FONTS-1);

  Font = &iVFonts[NumFonts];

  Font->FontFile = ImageFile;
  Font->AsciiTable = AsciiTable;
  Font->FontSpaceSize = SpaceSize;
  Font->FontLineSize = 0;
  Font->FontAbove = 0;
  Font->FontBelow = 0;

  // Initialise the font metrics.
  for (c = 0; c < 256; c++)
  {
    Index = AsciiTable[c];
    Above = Neuron::GetImageYOffset(Font->FontFile, Index);
    Below = Above + Neuron::GetImageHeight(Font->FontFile, Index);
    Height = abs(Above) + abs(Below);

    if (Above < Font->FontAbove)
      Font->FontAbove = Above;

    if (Below > Font->FontBelow)
      Font->FontBelow = Below;

    if (Height > Font->FontLineSize)
      Font->FontLineSize = Height;
  }

  ActiveFontID = NumFonts;

  NumFonts++;

  return NumFonts - 1;
}

void Neuron::SetFont(int FontID)
{
  assert(FontID < NumFonts);
  ActiveFontID = FontID;
}

int Neuron::GetTextLineSize(void)
{
  IVIS_FONT* Font = &iVFonts[ActiveFontID];
  return abs(Font->FontAbove) + abs(Font->FontBelow);
}

int Neuron::GetTextAboveBase(void)
{
  IVIS_FONT* Font = &iVFonts[ActiveFontID];
  return Font->FontAbove;
}

int Neuron::GetTextBelowBase(void)
{
  IVIS_FONT* Font = &iVFonts[ActiveFontID];
  return Font->FontBelow;
}

int Neuron::GetTextWidth(unsigned char* String)
{
  int Index;
  int MaxX = 0;
  UWORD ImageID;
  IVIS_FONT* Font = &iVFonts[ActiveFontID];

  while (*String != 0)
  {
    Index = *String;

    if (Index != ASCII_COLOURMODE)
    {
      if (Index != ASCII_SPACE)
      {
        ImageID = Font->AsciiTable[Index];
        MaxX += Neuron::GetImageWidth(Font->FontFile, ImageID) + 1;
      }
      else
        MaxX += Font->FontSpaceSize;
    }

    String++;
  }

  return MaxX;
}

int Neuron::GetCharWidth(unsigned char Char)
{
  int Index;
  UWORD ImageID;
  int Width = 0;
  IVIS_FONT* Font = &iVFonts[ActiveFontID];

  Index = Char;

  if (Index != ASCII_COLOURMODE)
  {
    if (Index != ASCII_SPACE)
    {
      ImageID = Font->AsciiTable[Index];

      Width = Neuron::GetImageWidth(Font->FontFile, ImageID) + 1;
    }
    else
      Width = Font->FontSpaceSize;
  }

  return Width;
}

/*
void Neuron::GetTextExtents(char *String,int *Width,int *y0,int *y1)
{
	int Index;
	int MaxX = 0;
	int MinY = 0;
	int MaxY = 0;
	BOOL FirstChar = TRUE;
	UWORD ImageID;
	IVIS_FONT *Font = &iVFonts[ActiveFontID];

	while (*String!=0) {
		Index = ( *String - '!' );

		if((Index >= 0) && (Index <= Font->FontEndID - Font->FontStartID)) {
			ImageID = Font->FontStartID + Index;

			MaxX += Neuron::GetImageWidth(Font->FontFile,ImageID) + 1;

			if( (Neuron::GetImageHeight(Font->FontFile,ImageID) < MinY) || FirstChar ) {
				MinY = Neuron::GetImageYOffset(Font->FontFile,ImageID);
			}

			if( ((Neuron::GetImageYOffset(Font->FontFile,ImageID) +
				Neuron::GetImageHeight(Font->FontFile,ImageID)) > MaxY) || FirstChar ) {
				MaxY = Neuron::GetImageYOffset(Font->FontFile,ImageID) +
				Neuron::GetImageHeight(Font->FontFile,ImageID);
			}

			FirstChar = FALSE;

		} else if(Index == -1) {
			MaxX += Font->FontSpaceSize;
		}

		String++;
	}

	*Width = MaxX;
	*y0 = MinY;
	*y1 = MaxY;
}
*/

void Neuron::SetTextColour(SWORD Index)
{
  IVIS_FONT* Font = &iVFonts[ActiveFontID];
  Font->FontColourIndex = Index;
}

// --------------------------------------------------------------------------

#if(0)

void pie_TestFormattedText(void)
{
  int ty = 32;

  Neuron::SetTextColour(-1);

  // Start recording text extents.
  pie_StartTextExtents();

  // Draw some left justified text.
  ty = pie_DrawFormattedText((UBYTE*)"Establish a base, then search for a Pre-Collapse structure.", 16, ty, 640 - 32, FTEXT_LEFTJUSTIFY,
                             TRUE);

  // Draw some left justified appended to the last print.
  ty = pie_DrawFormattedText((UBYTE*)"�This st�ru�cture contains technology vital for the success of the Project.", 16, ty, 640 - 32,
                             FTEXT_LEFTJUSTIFYAPPEND,TRUE);

  // Draw some centre justified text.
  ty = pie_DrawFormattedText((UBYTE*)"Establish a base, then search for a Pre-Collapse structure.", 16, ty, 640 - 32, FTEXT_CENTRE,TRUE);

  // Draw some right justified text.
  ty = pie_DrawFormattedText((UBYTE*)"Establish a base, then search for a Pre-Collapse structure.", 16, ty, 640 - 32, FTEXT_RIGHTJUSTIFY,
                             TRUE);

  // Draw a filled box using the text extents.
  pie_FillTextExtents(0, 16, 16, 128, TRUE);
}

#endif

enum
{
  EXTENTS_NONE,
  EXTENTS_START,
  EXTENTS_END
};

static UBYTE FString[256]; // Must do something about these wastefull static arrays.
static UBYTE FWord[256];
static int LastX; // Cursor position after last draw.
static int LastY;
static int LastTWidth; // Pixel width of the last string draw.
static UDWORD FFlags = FTEXTF_SKIP_TRAILING_SPACES | FTEXTF_INSERT_SPACE_ON_APPEND;
static int RecordExtents = EXTENTS_NONE;
static int ExtentsStartX;
static int ExtentsStartY;
static int ExtentsEndX;
static int ExtentsEndY;

void pie_SetFormattedTextFlags(UDWORD Flags) { FFlags = Flags; }

UDWORD pie_GetFormattedTextFlags(void) { return FFlags; }

static RENDERTEXT_CALLBACK Indirect_pie_DrawText = pie_DrawText;

void SetIndirectDrawTextCallback(RENDERTEXT_CALLBACK routine) { Indirect_pie_DrawText = routine; }

RENDERTEXT_CALLBACK GetIndirectDrawTextCallback(void) { return (Indirect_pie_DrawText); }

#define EXTENTS_USEMAXWIDTH (0)
#define EXTENTS_USELASTX (1)

UBYTE ExtentsMode = EXTENTS_USEMAXWIDTH;

void SetExtentsMode_USELAST(void) { ExtentsMode = EXTENTS_USELASTX; }

void SetExtentsMode_USEMAX(void) { ExtentsMode = EXTENTS_USEMAXWIDTH; }

// Draws formatted text with word wrap, long word splitting, embedded
// newlines ( uses @ rather than \n ) and colour mode toggle ( # ) which enables
// or disables font colouring.
//
//	UBYTE *String		The string to display.
//	UDWORD x			x coord of top left of formatted text window.
//	UDWORD y			y coord of top left of formatted text window.
//	UDWORD Width		Width of formatted text window.
//	UDWORD Justify		Justify mode, one of the following:
//							FTEXT_LEFTJUSTIFY
//							FTEXT_CENTRE
//							FTEXT_RIGHTJUSTIFY
//							FTEXT_LEFTJUSTIFYAPPEND
//	BOOL DrawBack		If TRUE then draws transparent box behind text.
//
// NOTE,
//	FTEXT_LEFTJUSTIFYAPPEND should only be used immediatly after calling with FTEXT_LEFTJUSTIFY
//  or FTEXT_LEFTJUSTIFYAPPEND
//
// Returns y coord of next text line.
//
UDWORD pie_DrawFormattedText(UBYTE* String, UDWORD x, UDWORD y, UDWORD Width, UDWORD Justify, BOOL DrawBack)
{
  int i, si, osi;
  int Len = strlen((char*)String);
  int jx = x; // Default to left justify.
  int jy = y;
  int WWidth;
  BOOL GotSpace;
  BOOL NewLine;
  BOOL AddLeadingSpace;
  int t;
  int TWidth;

  si = 0;

  while (si < Len)
  {
    // Remove leading spaces, usefull when doing centre justify.
    if (FFlags & FTEXTF_SKIP_LEADING_SPACES) { while ((si < strlen((char*)String)) && (String[si] == ' ')) { si++; } }

    FString[0] = 0;

    if (Justify == FTEXT_LEFTJUSTIFYAPPEND)
    {
      WWidth = LastTWidth;
      if (FFlags & FTEXTF_INSERT_SPACE_ON_APPEND)
        AddLeadingSpace = TRUE;
    }
    else
    {
      WWidth = 0;
      AddLeadingSpace = FALSE;
    }

    GotSpace = FALSE;
    NewLine = FALSE;

    // Parse through the string, adding words until width is achieved.
    while ((si < strlen((char*)String)) && (WWidth <= Width) && (!NewLine))
    {
      osi = si;

      // Get the next word.
      i = 0;
#ifdef TESTBED
      memset(FWord, 0, 256);
#endif
      if (AddLeadingSpace)
      {
        WWidth += Neuron::GetCharWidth(' ');
        if (WWidth <= Width)
        {
          FWord[i] = ' ';
          i++;
          GotSpace = TRUE;
          AddLeadingSpace = FALSE;
        }
      }

      while ((String[si] != 0) && (String[si] != ' ') && (WWidth <= Width))
      {
        // Check for new line character.
        if (String[si] == ASCII_COLOURMODE)
        {
          // If it's a colour mode toggle char then just add it to the word.
          FWord[i] = String[si];
          i++;
          si++;
        }
        else
        {
          // Update this lines pixel width.
          WWidth += Neuron::GetCharWidth(String[si]);

          // If width ok then add this character to the current word.
          if (WWidth <= Width)
          {
            FWord[i] = String[si];
            i++;
            si++;
          }
        }
      }

      // Don't forget the space.
      if (String[si] == ' ')
      {
        WWidth += Neuron::GetCharWidth(' ');
        if (WWidth <= Width)
        {
          FWord[i] = ' ';
          i++;
          si++;
          GotSpace = TRUE;
        }
      }

      // If we've passed a space and the word goes past the width then rewind
      // to that space and finish this line.
      if (GotSpace)
      {
        if ((WWidth >= Width))
        {
          if (FWord[i - 1] == ' ')
            FWord[i] = 0;
          else
          {
            si = osi;
            break;
          }
        }
      }

      // Terminate the word.
      FWord[i] = 0;

      // And add it to the output string.
      strcat((char*)FString, (char*)FWord);
    }

    // Remove trailing spaces, usefull when doing centre justify.
    if (FFlags & FTEXTF_SKIP_TRAILING_SPACES)
    {
      for (t = strlen((char*)FString) - 1; t >= 0; t--)
      {
        if (FString[t] != ' ')
          break;
        FString[t] = 0;
      }
    }

#ifdef _TESTBED
    // Replace spaces with ~.
    for (t = 0; t < strlen((char*)FString); t++)
    {
      if (FString[t] == ' ')
        FString[t] = '~';
    }
#endif

    TWidth = Neuron::GetTextWidth(FString);

    // Do justify.
    switch (Justify)
    {
    case FTEXT_CENTRE:
      jx = x + (Width - TWidth) / 2;
      break;

    case FTEXT_RIGHTJUSTIFY:

      jx = x + Width - TWidth;
      break;

    case FTEXT_LEFTJUSTIFY:
      jx = x;
      break;

    case FTEXT_LEFTJUSTIFYAPPEND:
      jx = LastX;
      jy = LastY;
      Justify = FTEXT_LEFTJUSTIFY;
      break;
    }

    // draw the text.
    // This is an indirect routine
    if (Indirect_pie_DrawText)
      Indirect_pie_DrawText(FString, jx, jy);

    /* callback type for resload display callback*/
    // remember where we were..
    LastX = jx + TWidth;
    LastY = jy;
    LastTWidth = TWidth;

    if (ExtentsMode == EXTENTS_USELASTX)
    {
      if (RecordExtents == EXTENTS_START)
      {
        // 
        ExtentsStartY = y + Neuron::GetTextAboveBase();
        ExtentsEndY = jy - Neuron::GetTextLineSize() + Neuron::GetTextBelowBase();

        RecordExtents = EXTENTS_END;
        ExtentsStartX = jx;
        ExtentsEndX = LastX;
      }
      else
      {
        if (jx < ExtentsStartX)
          ExtentsStartX = jx;

        if (LastX > ExtentsEndX)
          ExtentsEndX = LastX;
      }
    }

    // and move down a line.
    jy += Neuron::GetTextLineSize();
  }

  if (RecordExtents == EXTENTS_START)
  {
    RecordExtents = EXTENTS_END;

    ExtentsStartY = y + Neuron::GetTextAboveBase();
    ExtentsEndY = jy - Neuron::GetTextLineSize() + Neuron::GetTextBelowBase();

    if (ExtentsMode == EXTENTS_USEMAXWIDTH)
    {
      ExtentsStartX = x; // Was jx, but this broke the console centre justified text background.
      ExtentsEndX = x + Width;
    }
    else
    {
      if (jx < ExtentsStartX)
        ExtentsStartX = jx;
      if (LastX > ExtentsEndX)
        ExtentsEndX = LastX;
    }
  }
  else if (RecordExtents == EXTENTS_END)
  {
    ExtentsEndY = jy - Neuron::GetTextLineSize() + Neuron::GetTextBelowBase();

    if (ExtentsMode == EXTENTS_USEMAXWIDTH)
      ExtentsEndX = x + Width;
  }

  //	// Need to take	FTEXT_LEFTJUSTIFYAPPEND into account here.
  //		// Move y up to the top of the text.
  //
  //		// and draw a transparent box behind the text.
  //		TransBoxFillRGB_psx(x,y, x+Width,
  //							jy-Neuron::GetTextLineSize()+Neuron::GetTextBelowBase(),

  return jy;
}

static SWORD OldTextColourIndex = -1;

void pie_DrawText(unsigned char* string, UDWORD x, UDWORD y)
{
  int Index;
  UWORD ImageID;
  IVIS_FONT* Font = &iVFonts[ActiveFontID];

  /* Colour selection */
  pie_BeginTextRender(Font->FontColourIndex);

  while (*string != 0)
  {
    Index = *string;

    // Toggle colour mode?
    if (Index == ASCII_COLOURMODE)
    {
      if (TextColourIndex >= 0)
      {
        OldTextColourIndex = TextColourIndex;
        TextColourIndex = -1;
      }
      else
      {
        if (OldTextColourIndex >= 0)
          TextColourIndex = OldTextColourIndex;
      }
    }
    else if (Index == ASCII_SPACE)
      x += Font->FontSpaceSize;
    else
    {
      ImageID = Font->AsciiTable[Index];
      pie_TextRender(Font->FontFile, ImageID, x, y);

      x += Neuron::GetImageWidth(Font->FontFile, ImageID) + 1;
    }

    // Don't use this any more, If the text needs to wrap then use
    // pie_DrawFormattedText() defined above.
    /* New bit to make text wrap */
    if (x > (pie_GetVideoBufferWidth() - Font->FontSpaceSize))
    {
      /* Drop it to the next line if we hit screen edge */
      x = 0;
      y += Neuron::GetTextLineSize();
    }
    string++;
  }
}

///* New foreign character capable text printer - AlexM */
//void	pie_DrawText(unsigned char *string, UDWORD x, UDWORD y)
//{
//UWORD		imageID;
//IVIS_FONT	*Font;
//unsigned char	presChar;
//
//	/* Get the present font */
//	Font = &iVFonts[ActiveFontID];
//
//	/* Colour selection */
//	pie_BeginTextRender(Font->FontColourIndex);
//   
//	/* Keep going until NULL terminated */
//	while((presChar = (*string)))
//	{
//		/* Ensure that we skip spaces */
//		if(presChar == 32)
//		{
//			x += Font->FontSpaceSize;
//		}
//		/*	Otherwise we should have a legitimate character. This means
//			that we have to indirect to the correct ImageID as some characters
//			are not used or needed.
//		*/
//		else
//		{
//			/*	Get the image ID - this will select from one of two tables; one
//				for the front end and one for in game. We can start to use an
//				array index if we need more. 
//			*/
//#ifdef WIN32
//			if(Font->bGameFont)
//			{
//				imageID = textJumpTableGame[presChar];	// Presently only for in game
//			}
//			else
//			{
//				imageID = (UWORD)(Font->FontStartID + presChar - '!');
//				// will ultimately be...
//				// imageID = textJumpTableFrontEnd[presChar];
//			}
//#else
//				// 
//				imageID = (UWORD)(Font->FontStartID + presChar - '!');
//#endif
//
//
//			/* Draw the character */
//			pie_TextRender(Font->FontFile,imageID,x,y);
//#ifdef WIN32
// 			x += Neuron::GetImageWidth(Font->FontFile,imageID) + 1;
//#else
// 			x += Neuron::GetImageWidth(Font->FontFile,imageID) + 2;	// legacy?
//#endif																
//			
//		}
//#ifdef WIN32
//		/* New bit to make text wrap */
//		if(x > (pie_GetVideoBufferWidth() - Font->FontSpaceSize) )
//		{
//			/* Drop it to the next line if we hit screen edge */
//			x = 0;
//			y += Neuron::GetTextLineSize();
//		}
//#endif
//		/* Jump along to the next character */
//		string++;
//	}
//}

// --------------------------------------------------------------------------
// OLD text printer below
// --------------------------------------------------------------------------

// NEEDS to be unsigned for foreign chars
/*
void pie_DrawText(unsigned char *String,int XPos,int YPos)
{
	int Index;
	UWORD ImageID;
	IVIS_FONT *Font = &iVFonts[ActiveFontID];


	{
		pie_DrawTextNew(String,XPos,YPos);
		return;
	}
 
		while (*String!=0) {
			Index = ( *String - '!' );

			if((Index >= 0) && (Index <= Font->FontEndID - Font->FontStartID)) 
			{
				ImageID = (UWORD)(Font->FontStartID + Index);
				pie_TextRender(Font->FontFile,ImageID,XPos,YPos);

	#ifdef WIN32
				XPos += Neuron::GetImageWidth(Font->FontFile,ImageID) + 1;
	#else
				XPos += Neuron::GetImageWidth(Font->FontFile,ImageID) + 2;
	#endif
			}
			else if(Index!=-1)	// unknown character code!!!!
			{
				// So force to question mark to designate failure
				Index = '?' - '!';
				ImageID = (UWORD)(Font->FontStartID + Index);

				pie_TextRender(Font->FontFile,ImageID,XPos,YPos);

	#ifdef WIN32
				XPos += Neuron::GetImageWidth(Font->FontFile,ImageID) + 1;
	#else
				XPos += Neuron::GetImageWidth(Font->FontFile,ImageID) + 2;
	#endif
			}
			else if(Index == -1) {
				XPos += Font->FontSpaceSize;
			}

			String++;
		}
}
*/

/* Draw one character into a locked 32 bit back buffer. The subtitles over an
 * FMV sequence are the only thing that draws this way; everything else goes
 * through the textured quads in RenderModel.
 *
 * The position arrives in logical-canvas units like every other coordinate;
 * each glyph pixel becomes a display-scale square of physical pixels, which
 * keeps the subtitles sized and placed with the video frame and the darkened
 * band seq_RenderOneFrame draws under them.
 */
static void pie_RenderCharToBackBuffer(SCREEN_LOCK* psLock, IMAGEFILE* ImageFile, UWORD ID, int x, int y)
{
  IMAGEDEF* Image;
  iBitmap* bmp;
  UDWORD Modulus;
  int w;
  int h;
  int ow;
  int i, j, sy, sx;
  int scale, destRow, destCol;
  UDWORD pixel;
  UDWORD* bp;

  assert(ID < ImageFile->Header.NumImages);
  Image = &ImageFile->ImageDefs[ID];

  Modulus = ImageFile->TexturePages[Image->TPageID].width;
  bmp = ImageFile->TexturePages[Image->TPageID].bmp;

  bmp += static_cast<UDWORD>(Image->Tu) + static_cast<UDWORD>(Image->Tv) * Modulus;

  scale = static_cast<int>(Neuron::DisplayScale());
  x = (x + Image->XOffset) * scale;
  y = (y + Image->YOffset) * scale;
  w = Image->Width;
  h = Image->Height;
  ow = Modulus;

  for (i = 0; i < h; i++)
  {
    for (sy = 0; sy < scale; sy++)
    {
      destRow = y + i * scale + sy;
      if ((destRow < 0) || (destRow >= static_cast<int>(psLock->height)))
        continue;
      bp = (UDWORD*)(psLock->pPixels + psLock->pitch * destRow) + x;
      destCol = 0;
      for (j = 0; j < w; j++)
      {
        /* The page holds packed pixels now; a fully transparent one - the
         * old index 0 - is skipped, which is the same test as before. */
        pixel = bmp[j];
        for (sx = 0; sx < scale; sx++, destCol++)
        {
          if (bmp[j] && (x + destCol >= 0) && (x + destCol < static_cast<int>(psLock->width)))
            bp[destCol] = pixel;
        }
      }
    }
    bmp += ow;
  }
}

void pie_DrawTextToBackBuffer(unsigned char* String, int XPos, int YPos)
{
  int Index;
  UWORD ImageID;
  IVIS_FONT* Font = &iVFonts[ActiveFontID];
  SCREEN_LOCK sLock;

  // We lock the back buffer before drawing the subtitles onto the video
  // frame that was blitted into it.
  if (!screenLockBackBuffer(&sLock))
  {
    Neuron::Fatal("Sequence player text draw buffer lock failed");
    return;
  }

  while (*String != 0)
  {
    Index = *String;
    if (Index != ASCII_SPACE)
    {
      ImageID = Font->AsciiTable[Index];
      pie_RenderCharToBackBuffer(&sLock, Font->FontFile, ImageID, XPos, YPos);
      XPos += Neuron::GetImageWidth(Font->FontFile, ImageID) + 1;
    }
    else
      XPos += Font->FontSpaceSize;

    String++;
  }

  screenUnlockBackBuffer();
}

void pie_DrawText270(unsigned char* String, int XPos, int YPos)
{
  int Index;
  UWORD ImageID;
  IVIS_FONT* Font = &iVFonts[ActiveFontID];

  YPos += Neuron::GetImageWidth(Font->FontFile, Font->AsciiTable[33]) + 1;

  pie_BeginTextRender(Font->FontColourIndex);

  while (*String != 0)
  {
    Index = *String;

    if (Index != ASCII_SPACE)
    {
      ImageID = Font->AsciiTable[Index];
      pie_TextRender270(Font->FontFile, ImageID, XPos, YPos);

      YPos -= (Neuron::GetImageWidth(Font->FontFile, ImageID) + 1);
    }
    else
      YPos -= (Font->FontSpaceSize);
    String++;
  }
}

//
//	if (pie_Hardware())
//
//
//					
//		if((Index >= 0) && (Index <= Font->FontEndID - Font->FontStartID)) 
//
//
////#ifdef WIN32
////#else
////#endif

void pie_BeginTextRender(SWORD ColourIndex)
{
  TextColourIndex = ColourIndex;
  pie_SetRendMode(REND_TEXT);
  pie_SetBilinear(FALSE);
}

void pie_TextRender(IMAGEFILE* ImageFile, UWORD ID, int x, int y)
{
  UDWORD Red;
  UDWORD Green;
  UDWORD Blue;
  UDWORD Alpha = MAX_UB_LIGHT;
  iColour* psPalette;

  if ((TextColourIndex == PIE_TEXT_WHITE) || (TextColourIndex == 255))
  {
    pie_SetColour(MAX_LIGHT);
    pie_SetColourKeyedBlack(TRUE);
    pie_DrawImageFileID(ImageFile, ID, x, y);
  }
  else
  {
    if (TextColourIndex == PIE_TEXT_WHITE)
      pie_SetColour(PIE_TEXT_WHITE_COLOUR);
    else if (TextColourIndex == PIE_TEXT_LIGHTBLUE)
      pie_SetColour(PIE_TEXT_LIGHTBLUE_COLOUR);
    else if (TextColourIndex == PIE_TEXT_DARKBLUE)
      pie_SetColour(PIE_TEXT_DARKBLUE_COLOUR);
    else
    {
      psPalette = pie_GetGamePal();
      Red = psPalette[TextColourIndex].r;
      Green = psPalette[TextColourIndex].g;
      Blue = psPalette[TextColourIndex].b;
      pie_SetColour(((Alpha << 24) | (Red << 16) | (Green << 8) | Blue));
    }
    pie_SetColourKeyedBlack(TRUE);
    pie_DrawImageFileID(ImageFile, ID, x, y);
  }
}

void pie_TextRender270(IMAGEFILE* ImageFile, UWORD ImageID, int x, int y)
{
  UDWORD Red;
  UDWORD Green;
  UDWORD Blue;
  UDWORD Alpha = MAX_UB_LIGHT;
  IMAGEDEF* Image;
  PIEIMAGE pieImage;
  PIERECT dest;
  PIESTYLE rendStyle;
  iColour* psPalette;

  Image = &(ImageFile->ImageDefs[ImageID]);
  //not coloured yet
  if (TextColourIndex == PIE_TEXT_WHITE)
  {
    pie_SetColour(PIE_TEXT_WHITE_COLOUR & 0x80ffffff); //special case semi transparent for rotated text
    pie_SetRendMode(REND_ALPHA_TEXT);
  }
  else if (TextColourIndex == PIE_TEXT_LIGHTBLUE)
    pie_SetColour(PIE_TEXT_LIGHTBLUE_COLOUR);
  else if (TextColourIndex == PIE_TEXT_DARKBLUE)
    pie_SetColour(PIE_TEXT_DARKBLUE_COLOUR);
  else
  {
    psPalette = pie_GetGamePal();
    Red = psPalette[TextColourIndex].r;
    Green = psPalette[TextColourIndex].g;
    Blue = psPalette[TextColourIndex].b;
    pie_SetColour(((Alpha << 24) | (Red << 16) | (Green << 8) | Blue));
  }
  pie_SetColourKeyedBlack(TRUE);
  pieImage.texPage = ImageFile->TPageIDs[Image->TPageID];
  pieImage.tu = Image->Tu;
  pieImage.tv = Image->Tv;
  pieImage.tw = Image->Width;
  pieImage.th = Image->Height;
  dest.x = x + Image->YOffset;
  dest.y = y + Image->XOffset - Image->Width;
  dest.w = Image->Width;
  dest.h = Image->Height;
  pie_DrawImage270(&pieImage, &dest, &rendStyle);
}

#endif
