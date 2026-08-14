#include <stdio.h>
#include <stdlib.h>
#include "RendMode.h"
#include "PieClip.h"
#include "D3DMode.h"
#include "RendFunc.h"
#include "Bug.h"
#include "PiePalette.h"
#include "IvisPatch.h"

//*************************************************************************
//*************************************************************************

void (*iV_VSync)(void);
//*************************************************************************

iSurface rendSurface;
iSurface* psRendSurface;
static int g_mode = REND_UNDEFINED;

//*************************************************************************

static uint8* _VIDEO_MEM;
static int32 _VIDEO_SIZE;
static iBool _VIDEO_LOCK;

//*************************************************************************
//temporary definition
//void (*iV_pPolygon)(int npoints, iVertex *vrt, iTexture *tex, uint32 type); 
//void (*iV_pTriangle)(iVertex *vrt, iTexture *tex, uint32 type);
void (*iV_ppBitmapColourTrans)(iBitmap* bmp, int x, int y, int w, int h, int ow, int ColourIndex);
//void (*iV_DrawStretchImage)(IMAGEFILE *ImageFile,UWORD ID,int x,int y,int Width,int Height);
//*************************************************************************

//*************************************************************************
//*** return mode size in bytes
//*
//*
//******

static BOOL bHas3DNow;
BOOL weHave3DNow(void) { return (bHas3DNow); }

#define	CPUID	__asm _emit 0x0f __asm _emit 0xa2

BOOL cpuHas3DNow(void)
{
  BOOL b3DNow;
  /* As of yet - we haven't found an AMD 3DNow! equipped processor */
  b3DNow = FALSE;

  _asm
    {
    push ebx
    push edx
    push ecx
    pushfd
    pushfd
    pop eax
    mov edx,eax
    xor eax,0200000h
    push eax
    popfd
    pushfd
    pop eax
    cmp eax,edx
    // quit if the processor has no cpu id query instructions
    jz has_no_3dnow
    // Otherwise, establish what kind of CPU we have found
    xor eax,eax
    // issue the cpuid instruction
    CPUID
    // Now we need to check for an AMD processor - the id is authenticAMD
    cmp ebx,068747541h // htuA
    jnz has_no_3dnow
    cmp edx,069746e65h // itne
    jnz has_no_3dnow
    cmp ecx,0444d4163h // DMAc
    jnz has_no_3dnow
    // At this point we could check for other vendors that support AMD technology and 3DNow, but....
    mov eax,080000000h
    CPUID
    test eax,eax
    jz has_no_3dnow
    mov eax,080000001h
    CPUID
    test edx,080000000h // we have 3DNow!
    jz has_no_3dnow
    // Need to send back that we have 3D now support
    mov eax,1 // we have it
    jmp has_3d_now
    has_no_3dnow:
    mov eax,0
    has_3d_now:
    mov b3DNow,eax
    popfd
    pop ecx
    pop edx
    pop ebx
    }
  return (b3DNow);
}

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
//*** assign renderer
//*
//* params	mode	= render mode (screen/user) see iV_MODE_...
//*
//******

void rend_Assign(int mode, iSurface* s)
{
  iV_RenderAssign(mode, s);

  /* Need to look into this - won't the unwanted called still set render surface? */
  psRendSurface = s;
}

// pre VideoOpen
void rend_AssignScreen(void) { iV_RenderAssign(rendSurface.usr, &rendSurface); }

int iV_GetDisplayWidth(void) { return rendSurface.width; }

int iV_GetDisplayHeight(void) { return rendSurface.height; }

//*************************************************************************
//
// function pointers for render assign
//
//*************************************************************************

void (*iV_pLine)(int x0, int y0, int x1, int y1, uint32 colour);
void (*iV_TransTriangle)(iVertex* vrt);

char* (*iV_ScreenDumpToDisk)(void);

void (*iV_ppBitmap)(iBitmap* bmp, int x, int y, int w, int h, int ow);
void (*iV_ppBitmapTrans)(iBitmap* bmp, int x, int y, int w, int h, int ow);

void (*iV_SetTransFilter)(UDWORD rgb, UDWORD tablenumber);

void (*iV_UniBitmapDepth)(int texPage, int u, int v, int srcWidth, int srcHeight, int x, int y, int destWidth, int destHeight,
                          unsigned char brightness, int depth);

void (*iV_SetTransImds)(BOOL trans);

//mapdisplay

void (*iV_tgTriangle)(iVertex* vrt, iTexture* tex);
void (*iV_tgPolygon)(int num, iVertex* vrt, iTexture* tex);

//design

//text

//*************************************************************************
//
// function pointers for render assign
//
//*************************************************************************

#ifndef PIETOOL
void iV_RenderAssign(int mode, iSurface* s)
{
  /* Need to look into this - won't the unwanted called still set render surface? */
  psRendSurface = s;

  bHas3DNow = cpuHas3DNow(); // do some funky stuff to see if we have an AMD

  g_mode = mode;

  switch (mode)
  {
  case REND_D3D_RGB:
  case REND_D3D_HAL:
  case REND_D3D_REF:
    iV_VSync = _vsync_D3D;
    iV_pLine = _dummyFunc2_D3D;
    iV_pBox = _dummyFunc2_D3D;
    iV_pBoxFill = _dummyFunc2_D3D;
    iV_ppBitmap = _dummyFunc5_D3D;
    iV_ppBitmapColourTrans = _dummyFunc6_D3D;
    iV_ppBitmapTrans = _dummyFunc5_D3D;
    iV_SetTransFilter = SetTransFilter_D3D;
    break;

  default: iV_DEBUG1("vid[RenderAssign] = unsupported render mode %d\n", mode);
    break;
  }

  iV_DEBUG0("vid[RenderAssign] = assigned renderer :\n");
  iV_DEBUG5("usr %d\nflags %x\nxcentre, ycentre %d\nbuffer %p\n", s->usr, s->flags, s->xcentre, s->ycentre, s->buffer);
}
#endif	// don't want this function at all if we have PIETOOL defined
