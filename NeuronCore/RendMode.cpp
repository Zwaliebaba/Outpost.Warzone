#include "pch.h"
#include "RendMode.h"
#include "PieClip.h"
#include "RendFunc.h"
#include "Bug.h"
#include "IvisPatch.h"

//*************************************************************************
//*************************************************************************

iSurface rendSurface;
iSurface* psRendSurface;

//*************************************************************************

//*************************************************************************
//*** assign the surface the renderer draws through
//******

void iV_RenderAssign(iSurface* s) { psRendSurface = s; }

int iV_GetDisplayWidth(void) { return rendSurface.width; }

int iV_GetDisplayHeight(void) { return rendSurface.height; }
