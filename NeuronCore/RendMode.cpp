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

static uint8* _VIDEO_MEM;
static int32 _VIDEO_SIZE;
static iBool _VIDEO_LOCK;

//*************************************************************************

//*************************************************************************
//*** return mode size in bytes
//*
//*
//******

int32 iV_VideoMemorySize(int mode)

{
  int32 size;

  switch (mode)
  {
  case REND_D3D_RGB:
  case REND_D3D_HAL:
  case REND_D3D_REF:
    size = pie_GetVideoBufferWidth() * pie_GetVideoBufferHeight();
    break;
  default:
    size = 0;
  }

  return size;
}

//*************************************************************************
//*** allocate and lock video memory (call only once!)
//*
//*
//******

iBool iV_VideoMemoryLock(int mode)

{
  int32 size;

  if ((size = iV_VideoMemorySize(mode)) == 0)
    return FALSE;

  if ((_VIDEO_MEM = static_cast<uint8*>(iV_HeapAlloc(size))) == nullptr)
    return (0);

  _VIDEO_SIZE = size;
  _VIDEO_LOCK = TRUE;

  iV_DEBUG1("vid[VideoMemoryLock] = locked %dK of video memory\n", size/1024);

  return TRUE;
}

//*************************************************************************
//***
//*
//*
//******

void iV_VideoMemoryFree(void)

{
  if (_VIDEO_LOCK)
  {
    iV_DEBUG0("vid[VideoMemoryFree] = video memory not freed (locked)\n");
    return;
  }

  if (_VIDEO_MEM)
  {
    iV_HeapFree(_VIDEO_MEM, _VIDEO_SIZE);
    _VIDEO_MEM = nullptr;
    _VIDEO_SIZE = 0;
    iV_DEBUG0("vid[VideoMemoryFree] = video memory freed\n");
  }
}

//*************************************************************************
//***
//*
//*
//******

void iV_VideoMemoryUnlock(void)

{
  if (_VIDEO_LOCK)
    _VIDEO_LOCK = FALSE;

  iV_DEBUG0("vid[VideoMemoryUnlock] = video memory unlocked\n");

  iV_VideoMemoryFree();
}

//*************************************************************************
//***
//*
//*
//******

uint8* iV_VideoMemoryAlloc(int mode)

{
  int32 size;

  size = iV_VideoMemorySize(mode);

  if (size == 0)
    return nullptr;

  if (_VIDEO_LOCK)
  {
    if (size <= _VIDEO_SIZE)
      return _VIDEO_MEM;

    iV_DEBUG0("vid[VideoMemoryAlloc] = memory locked with smaller size than required!\n");
    return nullptr;
  }

  if ((_VIDEO_MEM = static_cast<uint8*>(iV_HeapAlloc(size))) == nullptr)
    return nullptr;

  _VIDEO_SIZE = size;

  iV_DEBUG1("vid[VideoMemoryAlloc] = allocated %dK video memory\n", size/1024);

  return _VIDEO_MEM;
}

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
