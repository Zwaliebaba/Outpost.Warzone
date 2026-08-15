#include "pch.h"
#include "RendMode.h"
#include "Bug.h"
#include "Palette.h"
#include "Pcx.h"
#include "Tex.h"
#include "IvisPatch.h"

#include "BitImage.h"

static BOOL LoadTextureFile(char* FileName, iSprite* TPage, int* TPageID);

UWORD iV_GetImageWidth(IMAGEFILE* ImageFile, UWORD ID)
{
  assert(ID < ImageFile->Header.NumImages);
  return ImageFile->ImageDefs[ID].Width;
}

UWORD iV_GetImageHeight(IMAGEFILE* ImageFile, UWORD ID)
{
  assert(ID < ImageFile->Header.NumImages);
  return ImageFile->ImageDefs[ID].Height;
}

// Get image width with no coordinate conversion.
//
UWORD iV_GetImageWidthNoCC(IMAGEFILE* ImageFile, UWORD ID)
{
  assert(ID < ImageFile->Header.NumImages);
  return ImageFile->ImageDefs[ID].Width;
}

// Get image height with no coordinate conversion.
//
UWORD iV_GetImageHeightNoCC(IMAGEFILE* ImageFile, UWORD ID)
{
  assert(ID < ImageFile->Header.NumImages);
  return ImageFile->ImageDefs[ID].Height;
}

SWORD iV_GetImageXOffset(IMAGEFILE* ImageFile, UWORD ID)
{
  assert(ID < ImageFile->Header.NumImages);
  return ImageFile->ImageDefs[ID].XOffset;
}

SWORD iV_GetImageYOffset(IMAGEFILE* ImageFile, UWORD ID)
{
  assert(ID < ImageFile->Header.NumImages);
  return ImageFile->ImageDefs[ID].YOffset;
}

UWORD iV_GetImageCenterX(IMAGEFILE* ImageFile, UWORD ID)
{
  assert(ID < ImageFile->Header.NumImages);
  return ImageFile->ImageDefs[ID].XOffset + ImageFile->ImageDefs[ID].Width / 2;
}

UWORD iV_GetImageCenterY(IMAGEFILE* ImageFile, UWORD ID)
{
  assert(ID < ImageFile->Header.NumImages);
  return ImageFile->ImageDefs[ID].YOffset + ImageFile->ImageDefs[ID].Height / 2;
}

IMAGEFILE* iV_LoadImageFile(UBYTE* FileData, UDWORD FileSize)
{
  UBYTE* Ptr;
  IMAGEHEADER* Header;
  IMAGEFILE* ImageFile;
  IMAGEDEF* ImageDef;

  int i;

  Ptr = FileData;

  Header = (IMAGEHEADER*)Ptr;
  Ptr += sizeof(IMAGEHEADER);

  ImageFile = new (std::nothrow) IMAGEFILE[1];
  if (ImageFile == nullptr)
  {
    Neuron::Fatal("Out of memory");
    return nullptr;
  }

  ImageFile->TexturePages = new (std::nothrow) iSprite[Header->NumTPages];
  if (ImageFile->TexturePages == nullptr)
  {
    Neuron::Fatal("Out of memory");
    return nullptr;
  }

  ImageFile->ImageDefs = new (std::nothrow) IMAGEDEF[Header->NumImages];
  if (ImageFile->ImageDefs == nullptr)
  {
    Neuron::Fatal("Out of memory");
    return nullptr;
  }

  ImageFile->Header = *Header;

  // Load the texture pages.
  for (i = 0; i < Header->NumTPages; i++)
    LoadTextureFile((char*)Header->TPageFiles[i], &ImageFile->TexturePages[i], (int*)&ImageFile->TPageIDs[i]);

  ImageDef = (IMAGEDEF*)Ptr;
  for (i = 0; i < Header->NumImages; i++)
  {
    ImageFile->ImageDefs[i] = *ImageDef;
    if ((ImageDef->Width <= 0) || (ImageDef->Height <= 0))
    {
      Neuron::Fatal("Illegal image size");
      return nullptr;
    }
    ImageDef++;
  }

  return ImageFile;
}

void iV_FreeImageFile(IMAGEFILE* ImageFile)
{
  delete[] ImageFile->TexturePages;
  ImageFile->TexturePages = nullptr;
  delete[] ImageFile->ImageDefs;
  ImageFile->ImageDefs = nullptr;
  delete[] ImageFile;
  ImageFile = nullptr;
}

static BOOL LoadTextureFile(char* FileName, iSprite* pSprite, int* texPageID)
{
  SDWORD i;

  Neuron::DebugTrace("ltf) {}\n",FileName);

  if (!resPresent("IMGPAGE", FileName))
  {
    if (!iV_PCXLoad(FileName, pSprite, nullptr))
    {
      Neuron::Fatal("Unable to load texture file : {}",FileName);
      return FALSE;
    }
  }
  else
    *pSprite = *static_cast<iSprite*>(resGetData("IMGPAGE", FileName));

  /* Back to beginning */
  i = 0;
  /* Have we already loaded this one then? */
  while (i < _TEX_INDEX)
  {
    if (stricmp(FileName, _TEX_PAGE[i].name) == 0)
    {
      *texPageID = i;
      return TRUE;
    }
    i++;
  }
#ifdef PIETOOL
  *texPageID = NULL;
#else
  *texPageID = pie_AddBMPtoTexPages(pSprite, FileName, 1, TRUE, TRUE);
#endif
  return TRUE;
}
