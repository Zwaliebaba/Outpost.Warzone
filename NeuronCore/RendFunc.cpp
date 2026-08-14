#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "RendFunc.h"
#include "RendMode.h"
#include "Bug.h"
#include "PiePalette.h"
#include "IvisPatch.h"
#include "Fractions.h"
#include "PieClip.h"


#ifndef PIETOOL




/***************************************************************************/
/*
 *	Local Variables
 */
/***************************************************************************/

UBYTE		aTransTable[256];
UBYTE		aTransTable2[256];		// 2 trans tabels so we can have 2 transparancy colours without slowdown.
UBYTE		aTransTable3[256];		// 3 trans tabels so we can have 3 transparancy colours without slowdown.
UBYTE		aTransTable4[256];		// 4 trans tabels so we can have 4 transparancy colours without slowdown.
/* Set default transparency filter to green pass */
UDWORD		transFilter = TRANS_GREY;
static int	g_mode = REND_UNDEFINED;
static IMAGEFILE *MouseImageFile;
static UWORD MouseImageID;

/***************************************************************************/
/*
 *	Local ProtoTypes
 */
/***************************************************************************/


/* Build a transparency look up table for the interface */
void	pie_BuildTransTable(UDWORD tableNo);

// dummy prototypes for pointer build functions
void (*iV_pBox)(int x0, int y0, int x1, int y1, uint32 colour);
void (*iV_pBoxFill)(int x0, int y0, int x1, int y1, uint32 colour);

//*************************************************************************
//*************************************************************************
//*************************************************************************
//*************************************************************************
/***************************************************************************/
/*
 *	Source
 */
/***************************************************************************/



#if(1) 	//#ifndef PIEPSX		// was #ifdef WIN32

//*************************************************************************

void	SetTransFilter(UDWORD rgb,UDWORD tablenumber)
{
	transFilter = rgb;
	pie_BuildTransTable(tablenumber);			/* Need to recalculate the transparency table */
}

//*************************************************************************

void iV_SetMousePointer(IMAGEFILE *ImageFile,UWORD ImageID)
{
	ASSERT((ImageID < ImageFile->Header.NumImages,"iV_SetMousePointer : Invalid image id"));

	MouseImageFile = ImageFile;
	MouseImageID = ImageID;
}


UDWORD iV_GetMouseFrame(void)
{
	return MouseImageID;
}


void iV_DrawMousePointer(int x,int y)
{
	iV_DrawImage(MouseImageFile,MouseImageID,x,y);
}

// Download buffer in system memory to the display back buffer.
//
/*
void DownloadDisplayBuffer(UBYTE *DisplayBuffer)
{
#ifndef PIEPSX		// was #ifdef WIN32
	UDWORD *Source = (UDWORD*)DisplayBuffer;
	UDWORD *Dest = (UDWORD*) rendSurface.buffer;
	UDWORD Size = rendSurface.size / 4;
	UDWORD i;

	for(i=0; i<Size; i++) {
		*Dest = *Source;
		Source++;
		Dest++;
	}
#endif
}
 */


//*************************************************************************
//
// local functions
//
//*************************************************************************

void	pie_BuildTransTable(UDWORD tableNo)
{
UDWORD	i;
UBYTE	red,green,blue;
iColour* psPalette = pie_GetGamePal();

	// Step through all the palette entries for the currently selected iVPALETTE
	for(i=0; i<256; i++)
	{
	 	switch (transFilter)
		{
		case TINT_BLUE:
			red = (psPalette[i].r * 5) / 8;
			blue = (psPalette[i].b * 7) / 8;
			green = (psPalette[i].g * 5) / 8;
			break;

		case TINT_DEEPBLUE:
			red = (psPalette[i].r * 3) / 8;
			blue = (psPalette[i].b * 5) / 8;
			green = (psPalette[i].g * 3) / 8;
			break;

		case TRANS_GREY:
			red = psPalette[i].r/2;
			blue = psPalette[i].b/2;
			green = psPalette[i].g/2;
			break;

		case TRANS_BLUE:
			red = psPalette[i].r/2;
			blue = psPalette[i].b;
			green = psPalette[i].g/2;
			break;

		case TRANS_BRITE:
			if( ((UDWORD)psPalette[i].r) + 50 >255)
			{
				red = 255;
			}
			else
			{
				red = (psPalette[i].r+50);
			}

			if( ((UDWORD)psPalette[i].b) + 50 >255)
			{
				blue = 255;
			}
			else
			{		
				blue = (psPalette[i].b+50);
			}			
			
			if( ((UDWORD)psPalette[i].g)+50 >255)
			{
				green = 255;
			}
			else
			{
				green = (psPalette[i].g+50);
			}
			
			break;

		default:	
			ASSERT((FALSE,"Invalid transparency filter selection"));
			break;
		}

		if(tableNo == 0)
		{
			aTransTable[i] = pal_GetNearestColour(red,green,blue);
		}
		else if(tableNo == 1)
		{
			aTransTable2[i] = pal_GetNearestColour(red,green,blue);
		}
		else if(tableNo == 2)
		{
			aTransTable3[i] = pal_GetNearestColour(red,green,blue);
		}
		else
		{
			aTransTable4[i] = pal_GetNearestColour(red,green,blue);
		}
	}
}

//*************************************************************************
//*************************************************************************
//*************************************************************************
//*************************************************************************
//*************************************************************************
//*************************************************************************
//*************************************************************************
//*************************************************************************
//*************************************************************************
//*************************************************************************
//*************************************************************************
//*************************************************************************
//*************************************************************************
//*************************************************************************
//*************************************************************************
//*************************************************************************
//*************************************************************************


#endif

#endif
