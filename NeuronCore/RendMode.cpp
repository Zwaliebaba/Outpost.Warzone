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
//***
//*
//*
//******

iSurface* iV_SurfaceCreate(uint32 flags, int width, int height, int xp, int yp, uint8* buffer)
{
  iSurface* s;
  int i;

  assert(buffer!=NULL); // on playstation this MUST be null

  if ((s = static_cast<iSurface*>(iV_HeapAlloc(sizeof(iSurface)))) == nullptr)
    return nullptr;

  s->flags = flags;
  s->xcentre = width >> 1;
  s->ycentre = height >> 1;
  s->xpshift = xp;
  s->ypshift = yp;
  s->width = width;
  s->height = height;
  s->size = width * height;
  s->buffer = buffer;
  for (i = 0; i < iV_SCANTABLE_MAX; i++)
    s->scantable[i] = i * width;
  s->clip.left = 0;
  s->clip.right = width - 1;
  s->clip.top = 0;
  s->clip.bottom = height - 1;

  iV_DEBUG2("vid[SurfaceCreate] = created surface width %d, height %d\n", width, height);

  return s;
}

// user must free s->buffer before calling
void iV_SurfaceDestroy(iSurface* s)

{
  // if renderer assigned to surface
  if (psRendSurface == s)
    psRendSurface = nullptr;

  if (s)
  iV_HeapFree(s, sizeof(iSurface));
}

//*************************************************************************
//*** assign the surface the renderer draws through
//*
//* params	mode	= render mode (screen/user) see iV_MODE_...
//*
//******

void iV_RenderAssign(int mode, iSurface* s)
{
  (void)mode;
  psRendSurface = s;
}

int iV_GetDisplayWidth(void) { return rendSurface.width; }

int iV_GetDisplayHeight(void) { return rendSurface.height; }
