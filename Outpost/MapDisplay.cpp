#include "pch.h"
#include <directxmath.h>
/*
	MapDisplay - Renders the world view necessary for the intelligence map
	Alex McLean, Pumpkin Studios, EIDOS Interactive, 1997

	Makes heavy use of the functions available in display3d.c. Could have
	messed about with display3d.c to make to world render dual purpose, but
	it's neater as a separate file, as the intelligence map has special requirements
	and overlays and needs to render to a specified buffer for later use.
*/

/* ----------------------------------------------------------------------------------------- */
/* Included files */
#include "stdio.h"

/* Includes direct access to render library */
#include "Model.h"
#include "BitImage.h"
#include "RenderTypes.h"
#include "PieState.h"
#include "PieMode.h"
#include "RenderMatrix.h"
#include "RendMode.h"

#include "Map.h"
#include "MapDisplay.h"
#include "Component.h"
#include "Disp2D.h"
#include "Display3D.h"
#include "HCI.h"
#include "IntelMap.h"
#include "IntImage.h"
#include "Texture.h"
#include "IntDisplay.h"

extern UWORD ButXPos; // From intDisplay.c
extern UWORD ButYPos;
extern UWORD ButWidth, ButHeight;
extern BOOL godMode;

#define ROTATE_ANGLE	5

/* ----------------------------------------------------------------------------------------- */
/* Function prototypes */

/*	Sets up the intelligence map by allocating the necessary memory and assigning world
	variables for the renderer to work with */
/* Draws the intelligence map to the already setup buffer */

/* Frees up the memory we've used */

/* Draw a tile on the grid */
//void		drawMapTile				(SDWORD i, SDWORD j);//line draw nolonger used

/* Textured tile draw */

/* Clears the map buffer prior to drawing in it */
//clear text message background with gray fill

/*fills the map buffer with intelColours prior to drawing in it*/

//only used in software
/*fills the map buffer with a bitmap prior to drawing in it*/
static void fillMapBufferWithBitmap(iSurface* surface);


//fill the intelColours array with the colours used for the background
/* ----------------------------------------------------------------------------------------- */

static iTexture texturePage = {6, 64, 64, nullptr};

/*Flag to switch code for bucket sorting in renderFeatures etc
  for the renderMapToBuffer code */
/*This is no longer used but may be useful for testing so I've left it in - maybe
get rid of it eventually? - AB 1/4/98*/
BOOL doBucket = TRUE;

#define MAX_INTEL_SHADES		20

//colours used to 'paint' the background of 3D view
UDWORD intelColours[MAX_INTEL_SHADES];

/* unused
void	drawMapTile(SDWORD i, SDWORD j)
{
#ifdef PSX
		SetOTIndex_PSX(OT2D_EXTREMEBACK);
		DBPRINTF(("drawMapTile called\n");
#endif

		 pie_Line(tileScreenCoords[i+0][j+0].x,tileScreenCoords[i+0][j+0].y,
    	 		tileScreenCoords[i+0][j+1].x,tileScreenCoords[i+0][j+1].y,255);
    	 pie_Line(tileScreenCoords[i+0][j+1].x,tileScreenCoords[i+0][j+1].y,
		 		tileScreenCoords[i+1][j+1].x,tileScreenCoords[i+1][j+1].y,255);
    	 pie_Line(tileScreenCoords[i+1][j+1].x,tileScreenCoords[i+1][j+1].y,
    	 		tileScreenCoords[i+1][j+0].x,tileScreenCoords[i+1][j+0].y,255);
    	 pie_Line(tileScreenCoords[i+1][j+0].x,tileScreenCoords[i+1][j+0].y,
    	 		tileScreenCoords[i+0][j+0].x,tileScreenCoords[i+0][j+0].y,255); 
}
*/

/* Clears the map buffer prior to drawing in it */
/*void	clearMapBuffer(iSurface *surface)
{
	UDWORD		surfaceWidth, extraWidth, height, width;
	UDWORD		*toClear;
#ifdef WIN32
	toClear = (UDWORD *)surface->buffer;
	//make sure width is multiple of 4
	surfaceWidth = surface->width & 0xfffc;
	if (surfaceWidth < (UDWORD) surface->width)
	{
		surfaceWidth += 4;
	}
	extraWidth = (MSG_BUFFER_WIDTH - surfaceWidth)/4;

	for (height = 0; height < (UDWORD)(surface->height); height++)
	{
		for (width=0; width < surfaceWidth/4; width++)
		{
			*toClear++ = (UDWORD)0;
		}
		toClear += extraWidth;
	}

#endif
}
*/
/*fills the map buffer with intelColours prior to drawing in it*/
/*void	fillMapBuffer(iSurface *surface)
{
#ifdef PSX
	DBPRINTF(("fillMapBuffer not defined on psx\n");
#else
	UBYTE		*toFill;
	UDWORD		width, height, extraWidth;

	toFill = surface->buffer;
	extraWidth = MSG_BUFFER_WIDTH - surface->width;
	for (height = 0; height < (UDWORD)(surface->height); height++)
	{
		for (width=0; width < (UDWORD)(surface->width); width++)
		{
			*toFill++ = (UBYTE)intelColours[(MAX_INTEL_SHADES-1) * 
				height/surface->height];
		}
		toFill += extraWidth;
	}
#endif
}*/

//only used in software
/*fills the map buffer with a bitmap*/
void fillMapBufferWithBitmap(iSurface* surface)
{
  UBYTE* toFill;
  UDWORD x, y, extraWidth, surfaceWidth, surfaceHeight, bitmapWidth, bitmapHeight, xSource, ySource, x0, y0;
  iBitmap* pBitmapBuffer;
  IMAGEDEF* pImageDef;
  UDWORD Modulus;

  toFill = surface->buffer;
  extraWidth = MSG_BUFFER_WIDTH - surface->width;

  pImageDef = &IntImages->ImageDefs[IMAGE_BUT0_UP];
  Modulus = IntImages->TexturePages[pImageDef->TPageID].width;

  pBitmapBuffer = IntImages->TexturePages[pImageDef->TPageID].bmp;
  x0 = static_cast<UDWORD>(pImageDef->Tu) + 5;
  y0 = static_cast<UDWORD>(pImageDef->Tv) + 5;

  bitmapWidth = pImageDef->Width - 10;
  bitmapHeight = pImageDef->Height - 10;
  surfaceWidth = static_cast<UDWORD>(surface->width);
  surfaceHeight = static_cast<UDWORD>(surface->height);

  for (y = 0; y < surfaceHeight; y++)
  {
    for (x = 0; x < surfaceWidth; x++)
    {
      //get the source x/y for this destination
      xSource = x * bitmapWidth / surfaceWidth;
      ySource = y * bitmapHeight / surfaceHeight;

      *toFill++ = pBitmapBuffer[x0 + xSource + (y0 + ySource) * Modulus];
    }
    toFill += extraWidth;
  }
}

//clear text message background with gray fill
/*void clearIntelText(iSurface *surface)
{
#ifdef PSX
	DBPRINTF(("clearIntelText not defined on psx\n");
#else
	UBYTE		*toFill;
	UDWORD		width, height, extraWidth;

	toFill = surface->buffer;
	toFill += (MSG_BUFFER_WIDTH * surface->height);
	extraWidth = MSG_BUFFER_WIDTH - surface->width;

	for (height = 0; height < INTMAP_TEXTWINDOWHEIGHT; height++)
	{
		for (width=0; width < (UDWORD)(surface->width); width++)
		{
			*toFill++ = 224;
		}
		toFill += extraWidth;
	}
#endif
}
*/
/* renders up to two IMDs into the surface - used by message display in Intelligence Map 
THIS HAS BEEN REPLACED BY renderResearchToBuffer()*/
/*void renderIMDToBuffer(iSurface *pSurface, iIMDShape *pIMD, iIMDShape *pIMD2,
					   UDWORD WindowX,UDWORD WindowY,UDWORD OriginX,UDWORD OriginY)
{
	static UDWORD angle = 0;
	UNUSEDPARAMETER(OriginX);
	UNUSEDPARAMETER(OriginY);
	UNUSEDPARAMETER(WindowX);
	UNUSEDPARAMETER(WindowY);

	if(!pie_Hardware())
	{
		 //Ensure all rendering is done to our bitmap and not to back or primary buffer
   		Neuron::RenderAssign(MODE_SURFACE,pSurface);
	}

	// Empty the buffer 
	//fill with the intelColours set up at the beginning
	//fill with IMAGE_BUT0 graphic
	if (!pie_Hardware())
	{
		fillMapBufferWithBitmap(pSurface);
	}

	// Set identity (present) context
	pie_MatBegin();

	if (pie_Hardware())
	{
		Neuron::SetGeometricOffset(OriginX+10,OriginY+10);
	}
	else
	{
		Neuron::SetGeometricOffset(pSurface->width/2,pSurface->height/2);
	}

	// shift back
	pie_TRANSLATE(0,0,BUTTON_DEPTH);
	scaleMatrix(RESEARCH_COMPONENT_SCALE);

	// Pitch down a bit 
	pie_MatRotX(DEG(-30));

	// Rotate round
	angle += ROTATE_ANGLE;
	if (angle > 360)
	{
		angle -= 360;
	}
	pie_MatRotY(DEG(angle));

	//draw the imds
	if (pIMD2)
	{
		pie_Draw3DShape(pIMD2, 0, 0, pie_MAX_BRIGHT_LEVEL, 0, pie_BUTTON, 0);

	}
	pie_Draw3DShape(pIMD, 0, 0, pie_MAX_BRIGHT_LEVEL, 0, pie_BUTTON, 0);

	// close matrix context
	pie_MatEnd();


	if (!pie_Hardware())
	{
		// Tell renderer we're back to back buffer 
		Neuron::RenderAssign(MODE_4101,&rendSurface);
	}
}*/

/* renders the Research IMDs into the surface - used by message display in 
Intelligence Map */
void renderResearchToBuffer(RESEARCH* psResearch, UDWORD OriginX, UDWORD OriginY)
{
  static UDWORD angle = 0;

  BASE_STATS* psResGraphic;
  UDWORD compID, IMDType;
  iVector Rotation, Position;
  UDWORD basePlateSize, Radius;
  SDWORD scale;

  // Set identity (present) context
  Neuron::MatrixPush();

  Neuron::SetGeometricOffset(OriginX + 10, OriginY + 10);

  // Pitch down a bit 

  // Rotate round
  angle += ROTATE_ANGLE;
  if (angle > 360)
    angle -= 360;

  Position.x = 0;
  Position.y = 0;
  Position.z = BUTTON_DEPTH;

  // Rotate round
  Rotation.x = -30;
  Rotation.y = angle;
  Rotation.z = 0;

  //draw the IMD for the research
  if (psResearch->psStat)
  {
    //we have a Stat associated with this research topic
    if (StatIsStructure(psResearch->psStat))
    {
      //this defines how the button is drawn
      IMDType = IMDTYPE_STRUCTURESTAT;
      psResGraphic = psResearch->psStat;
      //set up the scale
      basePlateSize = getStructureStatSize((STRUCTURE_STATS*)psResearch->psStat);
      if (basePlateSize == 1)
      {
        scale = RESEARCH_COMPONENT_SCALE / 2;
        /*HACK HACK HACK! 
        if its a 'tall thin (ie tower)' structure stat with something on 
        the top - offset the position to show the object on top*/
        if (((STRUCTURE_STATS*)psResearch->psStat)->pIMD->nconnectors AND getStructureStatHeight((STRUCTURE_STATS*)psResearch->psStat) >
          TOWER_HEIGHT)
          Position.y -= 30;
      }
      else if (basePlateSize == 2)
        scale = RESEARCH_COMPONENT_SCALE / 4;
      else
        scale = RESEARCH_COMPONENT_SCALE / 5;
    }
    else
    {
      compID = StatIsComponent(psResearch->psStat);
      if (compID != COMP_UNKNOWN)
      {
        //this defines how the button is drawn
        IMDType = IMDTYPE_COMPONENT;
        psResGraphic = psResearch->psStat;
        scale = RESEARCH_COMPONENT_SCALE;
      }
      else
      {
        DEBUG_ASSERT_TEXT(FALSE, "intDisplayMessageButton: invalid stat");
        IMDType = IMDTYPE_RESEARCH;
        psResGraphic = (BASE_STATS*)psResearch;
      }
    }
  }
  else
  {
    //no Stat for this research topic so use the research topic to define what is drawn
    psResGraphic = (BASE_STATS*)psResearch;
    IMDType = IMDTYPE_RESEARCH;
  }

  //scale the research according to size of IMD
  if (IMDType == IMDTYPE_RESEARCH)
  {
    Radius = getResearchRadius(psResGraphic);
    if (Radius <= 100)
      scale = RESEARCH_COMPONENT_SCALE / 2;
    else if (Radius <= 128)
      scale = RESEARCH_COMPONENT_SCALE / 3;
    else if (Radius <= 256)
      scale = RESEARCH_COMPONENT_SCALE / 4;
    else
      scale = RESEARCH_COMPONENT_SCALE / 5;
  }

  /* display the IMDs */
  if (IMDType == IMDTYPE_COMPONENT)
    displayComponentButton(psResGraphic, &Rotation, &Position,TRUE, scale);
  else if (IMDType == IMDTYPE_RESEARCH)
    displayResearchButton(psResGraphic, &Rotation, &Position,TRUE, scale);
  else if (IMDType == IMDTYPE_STRUCTURESTAT)
  {
    displayStructureStatButton((STRUCTURE_STATS*)psResGraphic, selectedPlayer, &Rotation, &Position,TRUE, scale);
  }
  else
    DEBUG_ASSERT_TEXT(FALSE, "renderResearchToBuffer: Unknown PIEType");

  // close matrix context
  Neuron::MatrixPop();
}
