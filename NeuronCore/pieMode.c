/***************************************************************************/
/*
 * pieMode.h
 *
 * renderer control for pumpkin library functions.
 *
 */
/***************************************************************************/

#include "frame.h"
#include "piedef.h"
#include "pieState.h"
#include "pieMode.h"
#include "pieMatrix.h"
#include "pieFunc.h"
#include "tex.h"
#include "d3dmode.h"
#include "texd3d.h"
#include "rendmode.h"
#include "Pieclip.h"

/***************************************************************************/
/*
 *	Local Definitions
 */
/***************************************************************************/
#define DIVIDE_TABLE_SIZE		1024
/***************************************************************************/
/*
 *	Local Variables
 */
/***************************************************************************/
	int32		_iVPRIM_DIVTABLE[DIVIDE_TABLE_SIZE];

static SDWORD d3dActive = 0;
static	BOOL	bDither = FALSE;

/***************************************************************************/
/*
 *	Local ProtoTypes
 */
/***************************************************************************/
//okay just this once
extern void GetDXVersion(LPDWORD pdwDXVersion, LPDWORD pdwDXPlatform);

/***************************************************************************/
/*
 *	Source
 */
/***************************************************************************/


BOOL	pie_GetDitherStatus( void )
{
	return(bDither);
}

void	pie_SetDitherStatus( BOOL val )
{
	bDither = val;
}

BOOL pie_CheckForDX6(void)
{
	UDWORD	DXVersion, DXPlatform;

	GetDXVersion(&DXVersion, &DXPlatform);

	return (DXVersion >= 0x600);
}

BOOL pie_Initialise(SDWORD mode)
{
	BOOL r;//result
	int i;

	pie_InitMaths();
	pie_TexInit();

	pie_SetRenderEngine(ENGINE_UNDEFINED);
	rendSurface.usr = REND_UNDEFINED;
	rendSurface.flags = REND_SURFACE_UNDEFINED;
	rendSurface.buffer = NULL;
	rendSurface.size = 0;

	// divtable: first entry == unity to (ie n/0 == 1 !)
	_iVPRIM_DIVTABLE[0] = iV_DIVMULTP;

	for (i=1; i<DIVIDE_TABLE_SIZE; i++)
	{
		_iVPRIM_DIVTABLE[i-0] = MAKEINT ( FRACTdiv(MAKEFRACT(1),MAKEFRACT(i)) *  iV_DIVMULTP);
	}

	pie_MatInit();
	_TEX_INDEX = 0;

	//mode specific initialisation
	if (mode == REND_D3D_REF)
	{
		iV_RenderAssign(REND_D3D_REF,&rendSurface);
		pie_SetRenderEngine(ENGINE_D3D);
		rendSurface.usr = mode;
		r = _mode_D3D_REF();
	}
	else if (mode == REND_D3D_RGB)
	{
		iV_RenderAssign(REND_D3D_RGB,&rendSurface);
		pie_SetRenderEngine(ENGINE_D3D);
		rendSurface.usr = mode;
		r = _mode_D3D_RGB();
	}
	else//REND_D3D_HAL
	{
		mode = REND_D3D_HAL;
		iV_RenderAssign(REND_D3D_HAL,&rendSurface);
		pie_SetRenderEngine(ENGINE_D3D);
		rendSurface.usr = mode;
		r = _mode_D3D_HAL();
	}

	if (r)
	{
		pie_SetDefaultStates();
	}

	if (r)
	{
		iV_RenderAssign(mode,&rendSurface);
		pal_Init();
	}
	else
	{
		
		iV_ShutDown();
		DBERROR(("Initialise videomode failed"));
		return FALSE;
	}
	return TRUE;
}


void pie_ShutDown(void)
{
	_close_D3D();
	pie_SetRenderEngine(ENGINE_UNDEFINED);
}

/***************************************************************************/

void pie_ScreenFlip(CLEAR_MODE clearMode)
{
	pie_D3DRenderForFlip();

	switch (clearMode)
	{
	case CLEAR_OFF:
	case CLEAR_OFF_AND_NO_BUFFER_DOWNLOAD:
		screenFlip(FALSE);
		break;
	case CLEAR_FOG:
		if (pie_GetFogEnabled())
		{
			screen_SetFogColour(pie_GetFogColour());
		}
		else
		{
			screen_SetFogColour(0);
		}
		screenFlip(TRUE);
		break;
	case CLEAR_BLACK:
	default:
		screen_SetFogColour(0);
		screenFlip(TRUE);
		break;
	}
}

/***************************************************************************/

void pie_Clear(UDWORD colour)
{
	(void)colour;
}
/***************************************************************************/

void pie_GlobalRenderBegin(void)
{
	if (d3dActive == 0)
	{
		d3dActive = 1;
		_renderBegin_D3D();
	}
}

void pie_GlobalRenderEnd(BOOL bForceClearToBlack)
{
	(void)bForceClearToBlack;
	if (d3dActive != 0)
	{
		d3dActive = 0;
		_renderEnd_D3D();
	}
}

/***************************************************************************/
UDWORD	pie_GetResScalingFactor( void )
{
UDWORD	resWidth;	//n.b. resolution width implies resolution height...!

	resWidth = pie_GetVideoBufferWidth();
	switch(resWidth)
	{
	case	640:
		return(100);		// game runs in 640, so scale factor is 100 (normal)
		break;
	case	800:
		return(125);
		break;				// as 800 is 125 percent of 640
	case		960:
		return(150);
		break;
	case	    1024:
		return(160);
		break;
	case	    1152:
		return(180);
		break;
	case	    1280:
		return(200);
		break;
	default:
		ASSERT((FALSE,"Unsupported resolution"));
		return(100);		// default to 640
		break;
	}
}
/***************************************************************************/
void pie_LocalRenderBegin(void)
{
}

void pie_LocalRenderEnd(void)
{
}


void pie_RenderSetup(void)
{
}
