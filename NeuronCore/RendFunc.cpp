#include "pch.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "RendFunc.h"
#include "RendMode.h"
#include "Bug.h"
#include "PiePalette.h"
#include "IvisPatch.h"
#include "PieClip.h"

#ifndef PIETOOL

/***************************************************************************/
/*
 *	Local Variables
 */
/***************************************************************************/

static IMAGEFILE* MouseImageFile;
static UWORD MouseImageID;

/***************************************************************************/
/*
 *	Source
 */
/***************************************************************************/

//*************************************************************************

void iV_SetMousePointer(IMAGEFILE* ImageFile, UWORD ImageID)
{
  DEBUG_ASSERT_TEXT(ImageID < ImageFile->Header.NumImages, "iV_SetMousePointer : Invalid image id");

  MouseImageFile = ImageFile;
  MouseImageID = ImageID;
}

UDWORD iV_GetMouseFrame(void) { return MouseImageID; }

void iV_DrawMousePointer(int x, int y)
{
  iV_DrawImage(MouseImageFile, MouseImageID, x, y);
}

//*************************************************************************

#endif
