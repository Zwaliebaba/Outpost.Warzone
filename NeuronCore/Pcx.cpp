#include "pch.h"
#include "Frame.h"
#include "PieTypes.h"
#include "Fbf.h"
#include "Pcx.h"

#include "IvisPatch.h"

//*************************************************************************

#define PCX_BUFFER_SIZE		65536

//*************************************************************************

using iPCX_header = struct
{
  uint8 manufacturer;
  uint8 version;
  uint8 encoding;
  uint8 bits_per_pixel;
  int16 xmin, ymin;
  int16 xmax, ymax;
  int16 hres, vres;
  uint8 palette16[48];
  uint8 reserved;
  uint8 colour_planes;
  int16 bytes_per_line;
  int16 palette_type;
  uint8 filler[58];
};

//*************************************************************************

static int _PCX_FI;
static int8* _PCX_MI;
static iBool _PCX_MEM;

//*************************************************************************

static int _pcx_read_int8(void)
{
  if (_PCX_MEM)
    return *_PCX_MI++;
  return (Neuron::FileGet(_PCX_FI));
}

//*************************************************************************

static void _load_image(iBitmap* bmp, long size)

{
  int data, num_bytes;
  long count;

  count = 0;

  while (count < size)
  {
    data = _pcx_read_int8();

    if ((data & 0xc0) == 0xc0)
    {
      num_bytes = data & 0x3f;
      data = _pcx_read_int8();
    }
    else
      num_bytes = 1;

    while ((num_bytes-- > 0) && (count++ < size)) { *bmp++ = data; }
  }
}

//*************************************************************************

static void _load_palette(iColour* pal)

{
  int i;

  Neuron::FileSeek(_PCX_FI, -769,FBF_SEEK_END);

  if (_pcx_read_int8() == 0x0C)
  {
    for (i = 0; i < 256; i++)
    {
      pal[i].r = _pcx_read_int8();
      pal[i].g = _pcx_read_int8();
      pal[i].b = _pcx_read_int8();
    }
  }
}

//*************************************************************************

static void _load_palette_mem(iColour* pal)

{
  int i;

  if (_pcx_read_int8() == 0x0C)
  {
    for (i = 0; i < 256; i++)
    {
      pal[i].r = _pcx_read_int8();
      pal[i].g = _pcx_read_int8();
      pal[i].b = _pcx_read_int8();
    }
  }
}

//*************************************************************************
BOOL pie_PCXLoadToBuffer(char* file, iSprite* s, iColour* pal)
{
  iPCX_header header;
  int i;
  long bsize;
  uint8* hp;

  _PCX_MEM = FALSE;


  if ((_PCX_FI = Neuron::FileOpen(file,FBF_MODE_R, 51200)) < 0)
  {
    Neuron::DebugTrace("pcx[PCXLoad] = could not open pcx file {}\n", _PCX_FI);
    return FALSE;
  }

  hp = (uint8*)&header;

  for (i = 0; i < 128; i++)
    *hp++ = _pcx_read_int8();


  if ((header.manufacturer != 10) && (header.version != 5))
  {
    Neuron::FileClose(_PCX_FI);
    return FALSE;
  }

  if (header.bits_per_pixel != 8)
  {
    Neuron::FileClose(_PCX_FI);
    return FALSE;
  }

  if (s->width != (header.xmax - header.xmin) + 1)
  {
    Neuron::FileClose(_PCX_FI);
    return FALSE;
  }

  if (s->height != (header.ymax - header.ymin) + 1)
  {
    Neuron::FileClose(_PCX_FI);
    return FALSE;
  }

  bsize = s->height * s->width;

  _load_image(s->bmp, bsize);

  if (pal)
    _load_palette(pal);

  Neuron::FileClose(_PCX_FI);


  return TRUE;
}

iBool Neuron::PCXLoad(char* file, iSprite* s, iColour* pal)

{
  iPCX_header header;
  int i;
  long bsize;
  uint8* hp;

  _PCX_MEM = FALSE;


  if ((_PCX_FI = Neuron::FileOpen(file,FBF_MODE_R, 51200)) < 0)
  {
    Neuron::DebugTrace("pcx[PCXLoad] = could not open pcx file {}\n", _PCX_FI);
    return FALSE;
  }

  hp = (uint8*)&header;

  for (i = 0; i < 128; i++)
    *hp++ = _pcx_read_int8();


  if ((header.manufacturer != 10) && (header.version != 5))
  {
    Neuron::FileClose(_PCX_FI);
    return FALSE;
  }

  if (header.bits_per_pixel != 8)
  {
    Neuron::FileClose(_PCX_FI);
    return FALSE;
  }

  s->width = (header.xmax - header.xmin) + 1;
  s->height = (header.ymax - header.ymin) + 1;

  bsize = s->height * s->width;

  if ((s->bmp = static_cast<uint8*>(IVIS_HEAP_ALLOC(bsize))) == nullptr)
  {
    Neuron::FileClose(_PCX_FI);
    return FALSE;
  }

  _load_image(s->bmp, bsize);

  if (pal)
  {
    DEBUG_ASSERT_TEXT(FALSE, "warning palette is being loaded for {}",file);
    _load_palette(pal);
  }

  Neuron::FileClose(_PCX_FI);


  return TRUE;
}

iBool Neuron::PCXLoadMem(int8* pcximge, iSprite* s, iColour* pal)

{
  iPCX_header header;
  int i;
  long bsize;
  uint8* hp;

  _PCX_MEM = TRUE;
  _PCX_MI = pcximge;


  hp = (uint8*)&header;

  for (i = 0; i < 128; i++)
    *hp++ = _pcx_read_int8();

  if ((header.manufacturer != 10) && (header.version != 5))
    return FALSE;

  if (header.bits_per_pixel != 8)
    return FALSE;

  s->width = (header.xmax - header.xmin) + 1;
  s->height = (header.ymax - header.ymin) + 1;

  bsize = s->height * s->width;

  if ((s->bmp = static_cast<uint8*>(IVIS_HEAP_ALLOC(bsize))) == nullptr)
    return FALSE;

  _load_image(s->bmp, bsize);

  if (pal)
  {
    DEBUG_ASSERT_TEXT(FALSE, "warning palette is being loaded in Neuron::PCXLoadMem");
    _load_palette_mem(pal);
  }


  return TRUE;
}

BOOL pie_PCXLoadMemToBuffer(int8* pcximge, iSprite* s, iColour* pal)

{
  iPCX_header header;
  int i;
  long bsize;
  uint8* hp;

  _PCX_MEM = TRUE;
  _PCX_MI = pcximge;


  hp = (uint8*)&header;

  for (i = 0; i < 128; i++)
    *hp++ = _pcx_read_int8();

  if ((header.manufacturer != 10) && (header.version != 5))
    return FALSE;

  if (header.bits_per_pixel != 8)
    return FALSE;

  if (s->width != (header.xmax - header.xmin) + 1)
  {
    Neuron::FileClose(_PCX_FI);
    return FALSE;
  }

  if (s->height != (header.ymax - header.ymin) + 1)
  {
    Neuron::FileClose(_PCX_FI);
    return FALSE;
  }

  bsize = s->height * s->width;

  _load_image(s->bmp, bsize);

  if (pal)
  {
    DEBUG_ASSERT_TEXT(FALSE, "warning palette is being loaded in Neuron::PCXLoadMem");
    _load_palette_mem(pal);
  }


  return TRUE;
}

//*************************************************************************
//*************************************************************************
