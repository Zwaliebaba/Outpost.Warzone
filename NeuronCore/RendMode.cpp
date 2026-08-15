#include "pch.h"
#include "RendMode.h"
#include "RenderClip.h"
#include "RendFunc.h"
#include "IvisPatch.h"

//*************************************************************************
//*************************************************************************

iSurface rendSurface;
iSurface* psRendSurface;

//*************************************************************************

//*************************************************************************
//*** assign the surface the renderer draws through
//******

void Neuron::RenderAssign(iSurface* s) { psRendSurface = s; }

int Neuron::GetDisplayWidth(void) { return rendSurface.width; }

int Neuron::GetDisplayHeight(void) { return rendSurface.height; }
