#include "pch.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "RendFunc.h"
#include "RendMode.h"
#include "Palette.h"
#include "IvisPatch.h"
#include "RenderClip.h"

#ifndef PIETOOL

/***************************************************************************/
/*
 *	Local Variables
 */
/***************************************************************************/

/* Read back by the EXTID_CURSOR script variable through Neuron::GetMouseFrame.
 * SetMousePointer, which was the only thing that ever wrote it, had no
 * callers, so this has read zero for a long time. The script binding is
 * compiled into shipped .slo files, so the getter stays.
 */
static UWORD MouseImageID;

/***************************************************************************/
/*
 *	Source
 */
/***************************************************************************/

//*************************************************************************

UDWORD Neuron::GetMouseFrame(void) { return MouseImageID; }

//*************************************************************************

#endif
