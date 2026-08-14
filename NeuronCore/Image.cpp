#include "pch.h"

#pragma warning (disable : 4201 4214 4115 4514)
#define WIN32_LEAN_AND_MEAN
#define WIN32_EXTRA_LEAN
#include <windows.h>
#include <ddraw.h>
#pragma warning (default : 4201 4214 4115)

/* Allow frame header files to be singly included */
#define FRAME_LIB_INCLUDE

#include "Types.h"
#include "Image.h"

#define WRITEIMAGES

/* The byte value after which the byte represents a run length in a PCX file */
#define RLE_START 192

/* The size of a PCX palette */
#define PCX_PALETTE_SIZE (256*3)

/* The PCX file header data structure */
/*lint -e754 */
using PCXHEADER = struct _pcxheader
{
  UBYTE manufacturer; // Always 10
  UBYTE version;
  UBYTE encoding; // Always 1 : RLE
  UBYTE bitsPerPixel;
  SWORD x, y; // Upper left of the image
  UWORD width, height; // Image size - have to add one to both of these
  // presumably 0..width = width + 1 bytes ???
  SWORD xResolution, yResolution; // Pixels in x and y direction
  UBYTE aOldPalette[48]; // EGA palette
  UBYTE reserved;
  UBYTE planes; // Number of planes in the image
  SWORD bytesPerLine;
  SWORD paletteType;
  UBYTE aPadding[58];
};

/* Take a memory buffer that contains a PCX file and convert it
 * to an image buffer and a palette buffer.
 * If the returned palette pointer is NULL a true colour PCX has
 * been loaded.  In this case the image data will be 32 bit true colour.
 */
BOOL imageParsePCX(UBYTE* pFileData, // Original file
                   UDWORD fileSize, // File size
                   UDWORD* pWidth, // Image width
                   UDWORD* pHeight, // Image height
                   UBYTE** ppImageData, // Image data from file
                   PALETTEENTRY** ppsPalette) // Palette data from file
{
  PCXHEADER sHeader;
  UBYTE *pSrc, *pDest, *pEndBuffer;
  PALETTEENTRY* psCurrPal;
  SWORD runlen;

  DEBUG_ASSERT_TEXT(fileSize > 0, "Invalid file size");
  DEBUG_ASSERT_TEXT(ppImageData != NULL, "Invalid image data pointer");
  DEBUG_ASSERT_TEXT(ppsPalette != NULL, "Invalide palette data pointer");

  /* Get the header information */
  memcpy(&sHeader, pFileData, sizeof(PCXHEADER));

  /* Check the image format :
     - uses RLE
     - uses 8 bits per pixel
     - uses one colour plane
   */
  if (sHeader.encoding != 1 || sHeader.bitsPerPixel != 8 || sHeader.planes != 1)
  {
    Neuron::Fatal("Unknown PCX format");
    return FALSE;
  }

  *pWidth = sHeader.width + 1;
  *pHeight = sHeader.height + 1;

  /* Allocate a buffer to store the image data */
  *ppImageData = new (std::nothrow) UBYTE[(UDWORD)((*pWidth) * (*pHeight))];
  if (!(*ppImageData))
  {
    Neuron::Fatal("Out of memory");
    return FALSE;
  }

  /* Read in the image data, decompressing */
  pSrc = pFileData + sizeof(PCXHEADER);
  pDest = *ppImageData;
  pEndBuffer = pDest + (*pWidth) * (*pHeight);
  while (pDest < pEndBuffer)
  {
    if (*pSrc < RLE_START)
    {
      /* no run length - put it straight into the buffer */
      *(pDest++) = *(pSrc++);
    }
    else
    {
      /* got an RLE - find its length and read in the actual data */
      runlen = static_cast<SWORD>(*(pSrc++) - RLE_START);

      /* now store the run into the image buffer */
      while ((runlen > 0) && (pDest < pEndBuffer))
      {
        *(pDest++) = *pSrc;
        runlen--;
      }
      pSrc++;

      if (runlen != 0)
      {
        /* The image data is corrupt as it decompresses to a 
           bigger image than specified in the header */
        Neuron::Fatal("Corrupt PCX file data");
        delete[] *ppImageData;
        return FALSE;
      }
    }
  }

  /* Allocate a buffer for the palette */
  *ppsPalette = new (std::nothrow) PALETTEENTRY[256];
  if (!(*ppsPalette))
  {
    Neuron::Fatal("Out of memory");
    delete[] *ppImageData;
    return FALSE;
  }
  memset(*ppsPalette, 0, sizeof(PALETTEENTRY) * 256);

  /* Ensure we are at the right place in the file data to read the palette */
  pSrc = pFileData + fileSize - PCX_PALETTE_SIZE;

  /* Now read in the palette inforamtion */
  pEndBuffer = pSrc + PCX_PALETTE_SIZE;
  psCurrPal = *ppsPalette;
  while (pSrc < pEndBuffer)
  {
    psCurrPal->peRed = *(pSrc++);
    psCurrPal->peGreen = *(pSrc++);
    psCurrPal->peBlue = *(pSrc++);
    psCurrPal++;
  }

  return TRUE;
}

using BMP_FILEHEADER = struct _bmp_fileheader
{
  UDWORD size; // Size in bytes of the file
  UWORD reserved1;
  UWORD reserved2;
  UDWORD offset; // Offset to image data in bytes
};

using BMP_INFOHEADER = struct _bmp_infoheader
{
  UDWORD headerSize; // 40 for windows format, 12 for OS/2
  UDWORD width; // Image width
  UDWORD height; // Image height
  UWORD planes; // Image planes must be 1
  UWORD bitCount; // Bits per pixel, 1,4,8, or 24

  /* This is as far as the OS/2 header goes, the rest is only for windows BMP */
  UDWORD compression; // Compression type
  UDWORD sizeImage; // Size in bytes of compressed image or zero
  UDWORD xPelsPerMeter; // Horizontal resolution in pixels per meter
  UDWORD yPelsPerMeter; // Vertical resolution in pixels per meter
  UDWORD coloursUsed; // Number of colours actually used in the image
  UDWORD coloursImportant; // Number of important colours (for reducing the bit depth)
};

/* Take a memory buffer that contains a BMP file and convert it
 * to an image buffer and a palette buffer.
 * If the returned palette pointer is NULL a true colour BMP has
 * been loaded.  In this case the image data will be 32 bit true colour.
 */
BOOL imageParseBMP(UBYTE* pFileData, // Original file
                   UDWORD fileSize, // File size
                   UDWORD* pWidth, // Image width
                   UDWORD* pHeight, // Image height
                   UBYTE** ppImageData, // Image data from file
                   PALETTEENTRY** ppsPalette) // Palette data from file
{
  BMP_FILEHEADER* psFileHeader;
  BMP_INFOHEADER* psInfoHeader;
  UDWORD paletteEntries, i;
  SDWORD x, y;
  UBYTE* pPalByte;
  UBYTE *pImgDest, *pImgSrc;

  (void)fileSize;

  /* Check that the first two bytes are ASCII "BM" */
  if (*((UWORD*)pFileData) != 0x4d42)
  {
    Neuron::Fatal("Invalid BMP file");
    return FALSE;
  }

  psFileHeader = (BMP_FILEHEADER*)(pFileData + 2);
  psInfoHeader = (BMP_INFOHEADER*)(pFileData + 2 + sizeof(BMP_FILEHEADER));

  if (psInfoHeader->headerSize != 40)
  {
    if (psInfoHeader->headerSize == 12)
      Neuron::Fatal("OS/2 Bitmaps not implemented");
    else
      Neuron::Fatal("Unknown BMP format");
    return FALSE;
  }

  if (psInfoHeader->planes != 1)
  {
    Neuron::Fatal("Unknown BMP format : more than one plane");
    return FALSE;
  }

  *pWidth = psInfoHeader->width;
  *pHeight = psInfoHeader->height;

  /* Read in the palette information if there is any */
  if (psInfoHeader->bitCount <= 8)
  {
    /* Find out the number of entries in the palette */
    if (psInfoHeader->coloursUsed > 0)
      paletteEntries = psInfoHeader->coloursUsed;
    else
    {
      switch (psInfoHeader->bitCount)
      {
      case 1:
        paletteEntries = 2;
        break;
      case 4:
        paletteEntries = 16;
        break;
      case 8:
        paletteEntries = 256;
        break;
      default: Neuron::Fatal("Unknown bit depth for BMP: {}", psInfoHeader->bitCount);
        return FALSE;
        break;
      }
    }

    /* Allocate a palette of a full 256 entries anyway - everything gets
     * converted to 8 bit. 
     */
    *ppsPalette = new (std::nothrow) PALETTEENTRY[256];
    if (*ppsPalette == nullptr)
    {
      Neuron::Fatal("Out of memory");
      return FALSE;
    }

    /* Set it all to zero for those images that use less than 256 entries. */
    memset(*ppsPalette, 0, sizeof(PALETTEENTRY) * 256);

    /* Copy the palette data over */
    pPalByte = pFileData + 2 + sizeof(BMP_FILEHEADER) + sizeof(BMP_INFOHEADER);
    for (i = 0; i < paletteEntries; i++)
    {
      (*ppsPalette)[i].peBlue = *(pPalByte++);
      (*ppsPalette)[i].peGreen = *(pPalByte++);
      (*ppsPalette)[i].peRed = *(pPalByte++);
      (*ppsPalette)[i].peFlags = 0;
      pPalByte++;
    }
  }

  switch (psInfoHeader->bitCount)
  {
  case 1: Neuron::Fatal("1 Bit BMP not implemented");
    delete[] *ppsPalette;
    return FALSE;
    break;
  case 4:
    /* Allocate the memory for the image */
    *ppImageData = new (std::nothrow) UBYTE[(*pWidth) * (*pHeight) /2];
    if (*ppImageData == nullptr)
    {
      Neuron::Fatal("Out of memory");
      delete[] *ppsPalette;
      return FALSE;
    }
    if (psInfoHeader->compression == 0)
    {
      /* No compression on the image - just copy it */
      pImgSrc = pFileData + psFileHeader->offset;
      for (y = (*pHeight) - 1; y >= 0; y--)
      {
        /* BMPs are stored upside down - have to reverse them */
        pImgDest = (*ppImageData) + (*pWidth / 2) * y;

        /* Copy the line over */
        for (x = 0; x < static_cast<SDWORD>(*pWidth / 2); x++)
        {
          BYTE SourceByte, DestByte;

          SourceByte = *(pImgSrc++);
          // Swap nibbles
          DestByte = static_cast<UBYTE>((SourceByte & 0x0f) << 4);
          DestByte = static_cast<UBYTE>(DestByte | ((SourceByte & 0xf0) >> 4));

          *(pImgDest++) = DestByte;
        }
        /* Now skip any padding to the next DWord boundary */
        while (x % 4)
        {
          pImgSrc++;
          x++;
        }
      }
    }
    else
    {
      Neuron::Fatal("Compressed BMP not implemented");
      delete[] *ppsPalette;
      return FALSE;
    }
    break;
  case 8:
    /* Allocate the memory for the image */
    *ppImageData = new (std::nothrow) UBYTE[(*pWidth) * (*pHeight)];
    if (*ppImageData == nullptr)
    {
      Neuron::Fatal("Out of memory");
      delete[] *ppsPalette;
      return FALSE;
    }
    if (psInfoHeader->compression == 0)
    {
      /* No compression on the image - just copy it */
      pImgSrc = pFileData + psFileHeader->offset;
      for (y = (*pHeight) - 1; y >= 0; y--)
      {
        /* BMPs are stored upside down - have to reverse them */
        pImgDest = (*ppImageData) + (*pWidth) * y;

        /* Copy the line over */
        for (x = 0; x < static_cast<SDWORD>(*pWidth); x++)
          *(pImgDest++) = *(pImgSrc++);
        /* Now skip any padding to the next DWord boundary */
        while (x % 4)
        {
          pImgSrc++;
          x++;
        }
      }
    }
    else
    {
      Neuron::Fatal("Compressed BMP not implemented");
      delete[] *ppsPalette;
      return FALSE;
    }
    break;
  case 24: Neuron::Fatal("24 Bit BMP not implemented");
    return FALSE;
    break;
  default: Neuron::Fatal("Unknown bit depth for BMP: {}", psInfoHeader->bitCount);
    return FALSE;
    break;
  }

  return TRUE;
}

#ifdef WRITEIMAGES

#define PALCOUNT (256)

/* Take a memory buffer that contains a image buffer and convert it 
 * to a BMP file. 
 *
 * - NULL palette indicates a 24bit bmp
 */
BOOL imageCreateBMP(UBYTE* pImageData, // Original file
                    PALETTEENTRY* pPaletteData, // Palette data
                    UDWORD Width, // Image width
                    UDWORD Height, // Image height
                    UBYTE** ppBMPFile, // Image data from file
                    UDWORD* fileSize) // Generated BMP File size
{
  BMP_FILEHEADER* psFileHeader;
  BMP_INFOHEADER* psInfoHeader;
  UDWORD BMPSize;
  UBYTE* BMPdata;

  int BitCount;

  int PalEntry, Ycoord;
  UBYTE* DataPointer;
  UBYTE* ImagePointer;

  // If we have no palette then assume that the BMP is 24 bit
  if (pPaletteData != nullptr)
    BitCount = 8;
  else
    BitCount = 24;

  psFileHeader = new (std::nothrow) BMP_FILEHEADER[1];
  if (psFileHeader == nullptr)
    return FALSE;

  psInfoHeader = new (std::nothrow) BMP_INFOHEADER[1];
  if (psInfoHeader == nullptr)
  {
    delete[] psFileHeader;
    psFileHeader = nullptr;
    return FALSE;
  }

  // Calc the number of bytes for this BMP file
  if (BitCount == 8)
    BMPSize = 2 + sizeof(BMP_FILEHEADER) + sizeof(BMP_INFOHEADER) + (PALCOUNT * 4) + (Width * Height);
  else
    BMPSize = 2 + sizeof(BMP_FILEHEADER) + sizeof(BMP_INFOHEADER) + (Width * Height * 3);

  BMPdata = new (std::nothrow) UBYTE[BMPSize];
  if (BMPdata == nullptr) // No mem for BMP file
  {
    delete[] psInfoHeader;
    psInfoHeader = nullptr;
    delete[] psFileHeader;
    psFileHeader = nullptr;
    return FALSE;
  }

  psInfoHeader->headerSize = 40; // Windows format bmp
  psInfoHeader->width = Width;
  psInfoHeader->height = Height;
  psInfoHeader->planes = 1;
  psInfoHeader->bitCount = static_cast<UWORD>(BitCount); // Bits per pixel - only 8 (256colours) is currently supported

  psInfoHeader->compression = 0; // Compression not supported
  psInfoHeader->sizeImage = 0;
  psInfoHeader->xPelsPerMeter = 1; // err...
  psInfoHeader->yPelsPerMeter = 1; // err...
  psInfoHeader->coloursUsed = PALCOUNT;
  psInfoHeader->coloursImportant = PALCOUNT;

  psFileHeader->size = BMPSize;
  psFileHeader->reserved1 = 0;
  psFileHeader->reserved2 = 0;

  psFileHeader->offset = 2 + sizeof(BMP_FILEHEADER) + sizeof(BMP_INFOHEADER);
  if (BitCount == 8)
    psFileHeader->offset += (PALCOUNT * 4);

  // Fill out mem
  *((UWORD*)BMPdata) = 0x4d42;
  memcpy(BMPdata + 2, psFileHeader, sizeof(BMP_FILEHEADER));
  memcpy(BMPdata + 2 + sizeof(BMP_FILEHEADER), psInfoHeader, sizeof(BMP_INFOHEADER));

  memcpy(BMPdata + 2 + sizeof(BMP_FILEHEADER), psInfoHeader, sizeof(BMP_INFOHEADER));
  DataPointer = BMPdata + 2 + sizeof(BMP_FILEHEADER) + sizeof(BMP_INFOHEADER);

  if (BitCount == 8)
  {
    // Copy 'dat palette into 'da memory buffer
    for (PalEntry = 0; PalEntry < PALCOUNT; PalEntry++)
    {
      *(DataPointer++) = pPaletteData[PalEntry].peBlue;
      *(DataPointer++) = pPaletteData[PalEntry].peGreen;
      *(DataPointer++) = pPaletteData[PalEntry].peRed;
      *(DataPointer++) = 0;
    }
  }

  for (Ycoord = Height - 1; Ycoord >= 0; Ycoord--)
  {
    ImagePointer = pImageData + (Ycoord * Width * (BitCount / 8));
    memcpy(DataPointer, ImagePointer, Width * (BitCount / 8));
    DataPointer += (Width * (BitCount / 8));
  }

  *ppBMPFile = BMPdata;
  *fileSize = BMPSize;

  delete[] psInfoHeader;
  psInfoHeader = nullptr;
  delete[] psFileHeader;
  psFileHeader = nullptr;
  return TRUE;
}

#endif
