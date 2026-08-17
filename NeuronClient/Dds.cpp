#include "pch.h"

/*
 * Dds.cpp
 *
 * The texture file loader: uncompressed A8R8G8B8 DDS.
 *
 * This replaced Pcx.cpp when the palette was removed. The PCX loader decoded
 * 8 bit indices and expanded them through the game palette; the conversion
 * tool (tools/convert_pcx_to_dds.py) baked that same expansion into the
 * files, so a DDS file's pixels are byte for byte what the game holds in
 * memory - the old "index 0 / black is transparent" convention arrives as an
 * alpha of zero. Loading is a header validation and a copy.
 */

#include "Frame.h"
#include "RenderTypes.h"
#include "Fbf.h"
#include "Dds.h"

namespace
{

/* The file layout: the FourCC magic, then this 124 byte header. Fields the
 * loader does not validate are named for the record. */
struct DdsPixelFormat
{
  UDWORD size; // always 32
  UDWORD flags; // DDPF_RGB | DDPF_ALPHAPIXELS
  UDWORD fourCC;
  UDWORD rgbBitCount; // always 32
  UDWORD rBitMask; // 0x00ff0000
  UDWORD gBitMask; // 0x0000ff00
  UDWORD bBitMask; // 0x000000ff
  UDWORD aBitMask; // 0xff000000
};

struct DdsHeader
{
  UDWORD size; // always 124
  UDWORD flags;
  UDWORD height;
  UDWORD width;
  UDWORD pitchOrLinearSize;
  UDWORD depth;
  UDWORD mipMapCount;
  UDWORD reserved1[11];
  DdsPixelFormat pixelFormat;
  UDWORD caps;
  UDWORD caps2;
  UDWORD caps3;
  UDWORD caps4;
  UDWORD reserved2;
};

inline constexpr UDWORD DdsMagic = 0x20534444; // 'DDS ' little-endian
inline constexpr UDWORD DdsPixelFormatRgba = 0x41; // DDPF_RGB | DDPF_ALPHAPIXELS
inline constexpr int DdsFileHeaderBytes = 4 + sizeof(DdsHeader);

/* Check the header describes the one layout the game reads */
bool HeaderIsUsable(const DdsHeader& _header)
{
  if (_header.size != sizeof(DdsHeader))
    return false;
  if (_header.width == 0 || _header.height == 0)
    return false;
  if (_header.pixelFormat.size != sizeof(DdsPixelFormat))
    return false;
  if ((_header.pixelFormat.flags & DdsPixelFormatRgba) != DdsPixelFormatRgba)
    return false;
  if (_header.pixelFormat.rgbBitCount != 32)
    return false;
  if (_header.pixelFormat.rBitMask != 0x00ff0000 || _header.pixelFormat.gBitMask != 0x0000ff00 ||
    _header.pixelFormat.bBitMask != 0x000000ff || _header.pixelFormat.aBitMask != 0xff000000)
    return false;
  return true;
}

/* Read the magic and header through the buffered file layer */
bool ReadFileHeader(int _fd, DdsHeader& _header)
{
  UBYTE aRaw[DdsFileHeaderBytes];
  int i, byteRead;

  for (i = 0; i < DdsFileHeaderBytes; i++)
  {
    byteRead = Neuron::FileGet(_fd);
    if (byteRead < 0)
      return false;
    aRaw[i] = static_cast<UBYTE>(byteRead);
  }

  if (*reinterpret_cast<UDWORD*>(aRaw) != DdsMagic)
    return false;

  memcpy(&_header, aRaw + 4, sizeof(DdsHeader));
  return HeaderIsUsable(_header);
}

/* Read width x height packed pixels through the buffered file layer */
bool ReadFilePixels(int _fd, iBitmap* _bmp, long _count)
{
  long i;
  int b0, b1, b2, b3;

  for (i = 0; i < _count; i++)
  {
    b0 = Neuron::FileGet(_fd);
    b1 = Neuron::FileGet(_fd);
    b2 = Neuron::FileGet(_fd);
    b3 = Neuron::FileGet(_fd);
    if (b0 < 0 || b1 < 0 || b2 < 0 || b3 < 0)
      return false;
    _bmp[i] = static_cast<iBitmap>(b0 & 0xff) | (static_cast<iBitmap>(b1 & 0xff) << 8) |
      (static_cast<iBitmap>(b2 & 0xff) << 16) | (static_cast<iBitmap>(b3 & 0xff) << 24);
  }

  return true;
}

bool ReadMemHeader(const int8* _fileData, DdsHeader& _header)
{
  if (*reinterpret_cast<const UDWORD*>(_fileData) != DdsMagic)
    return false;

  memcpy(&_header, _fileData + 4, sizeof(DdsHeader));
  return HeaderIsUsable(_header);
}

} // namespace

namespace Neuron
{

iBool DdsLoad(char* _fileName, iSprite* _sprite)
{
  DdsHeader header;
  int fd;
  long pixelCount;

  if ((fd = Neuron::FileOpen(_fileName,FBF_MODE_R, 51200)) < 0)
  {
    Neuron::DebugTrace("DdsLoad: could not open {}\n", _fileName);
    return FALSE;
  }

  if (!ReadFileHeader(fd, header))
  {
    Neuron::DebugTrace("DdsLoad: {} is not an uncompressed A8R8G8B8 DDS\n", _fileName);
    Neuron::FileClose(fd);
    return FALSE;
  }

  _sprite->width = static_cast<int>(header.width);
  _sprite->height = static_cast<int>(header.height);
  pixelCount = static_cast<long>(header.width) * static_cast<long>(header.height);

  if ((_sprite->bmp = new (std::nothrow) iBitmap[pixelCount]) == nullptr)
  {
    Neuron::FileClose(fd);
    return FALSE;
  }

  if (!ReadFilePixels(fd, _sprite->bmp, pixelCount))
  {
    Neuron::DebugTrace("DdsLoad: {} ends before its pixels do\n", _fileName);
    delete[] _sprite->bmp;
    _sprite->bmp = nullptr;
    Neuron::FileClose(fd);
    return FALSE;
  }

  Neuron::FileClose(fd);
  return TRUE;
}

iBool DdsLoadToBuffer(char* _fileName, iSprite* _sprite)
{
  DdsHeader header;
  int fd;

  if ((fd = Neuron::FileOpen(_fileName,FBF_MODE_R, 51200)) < 0)
  {
    Neuron::DebugTrace("DdsLoadToBuffer: could not open {}\n", _fileName);
    return FALSE;
  }

  if (!ReadFileHeader(fd, header))
  {
    Neuron::DebugTrace("DdsLoadToBuffer: {} is not an uncompressed A8R8G8B8 DDS\n", _fileName);
    Neuron::FileClose(fd);
    return FALSE;
  }

  if (_sprite->width != static_cast<int>(header.width) || _sprite->height != static_cast<int>(header.height))
  {
    Neuron::DebugTrace("DdsLoadToBuffer: {} is {}x{}, wanted {}x{}\n", _fileName, header.width, header.height, _sprite->width,
                       _sprite->height);
    Neuron::FileClose(fd);
    return FALSE;
  }

  if (!ReadFilePixels(fd, _sprite->bmp, static_cast<long>(header.width) * static_cast<long>(header.height)))
  {
    Neuron::DebugTrace("DdsLoadToBuffer: {} ends before its pixels do\n", _fileName);
    Neuron::FileClose(fd);
    return FALSE;
  }

  Neuron::FileClose(fd);
  return TRUE;
}

iBool DdsLoadMem(const int8* _fileData, iSprite* _sprite)
{
  DdsHeader header;
  long pixelCount;

  if (!ReadMemHeader(_fileData, header))
  {
    Neuron::DebugTrace("DdsLoadMem: not an uncompressed A8R8G8B8 DDS\n");
    return FALSE;
  }

  _sprite->width = static_cast<int>(header.width);
  _sprite->height = static_cast<int>(header.height);
  pixelCount = static_cast<long>(header.width) * static_cast<long>(header.height);

  if ((_sprite->bmp = new (std::nothrow) iBitmap[pixelCount]) == nullptr)
    return FALSE;

  memcpy(_sprite->bmp, _fileData + DdsFileHeaderBytes, pixelCount * sizeof(iBitmap));
  return TRUE;
}

iBool DdsLoadMemToBuffer(const int8* _fileData, iSprite* _sprite)
{
  DdsHeader header;

  if (!ReadMemHeader(_fileData, header))
  {
    Neuron::DebugTrace("DdsLoadMemToBuffer: not an uncompressed A8R8G8B8 DDS\n");
    return FALSE;
  }

  if (_sprite->width != static_cast<int>(header.width) || _sprite->height != static_cast<int>(header.height))
  {
    Neuron::DebugTrace("DdsLoadMemToBuffer: file is {}x{}, wanted {}x{}\n", header.width, header.height, _sprite->width, _sprite->height);
    return FALSE;
  }

  memcpy(_sprite->bmp, _fileData + DdsFileHeaderBytes, static_cast<long>(header.width) * static_cast<long>(header.height) *
    sizeof(iBitmap));
  return TRUE;
}

} // namespace Neuron
