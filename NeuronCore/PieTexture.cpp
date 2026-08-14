#include "pch.h"
/***************************************************************************/
/*
 * pieState.c
 *
 * renderer setup and state control routines for 3D rendering
 *
 */
/***************************************************************************/

#include "Frame.h"
#include "PieTexture.h"
#include "PieDef.h"
#include "PieState.h"
#include "DX6TexMan.h"
#include "Tex.h"

/***************************************************************************/
/*
 *	Local Definitions
 */
/***************************************************************************/

using TEXTURE_STATE = struct _textureState
{
  UDWORD lastPageDownloaded;
  UDWORD texPage;
};

/***************************************************************************/
/*
 *	Local Variables
 */
/***************************************************************************/

TEXTURE_STATE textureStates;

/***************************************************************************/
/*
 *	Local ProtoTypes
 */
/***************************************************************************/

/***************************************************************************/
/*
 *	Source
 */
/***************************************************************************/

BOOL pie_Download8bitTexturePage(void* bitmap, UWORD Width, UWORD Height) { return TRUE; }

BOOL pie_Reload8bitTexturePage(void* bitmap, UWORD Width, UWORD Height, SDWORD index) { return dtm_ReLoadTexture(index); }

UDWORD pie_GetLastPageDownloaded(void) { return _TEX_INDEX; }
