#ifndef __INCLUDED_BITIMAGE__
#define __INCLUDED_BITIMAGE__

#include "Frame.h"
#include "RenderTypes.h"

//*************************************************************************
//
// immitmap image structures
//
//*************************************************************************

using CLUTHEADER = struct
{
  UBYTE Type[4];
  UWORD Version;
  UWORD ClutSize;
  UWORD NumCluts;
  UWORD Pad;
};

using IMAGEHEADER = struct
{
  UBYTE Type[4];
  UWORD Version;
  UWORD NumImages;
  UWORD BitDepth;
  UWORD NumTPages;
  UBYTE TPageFiles[16][16];
  UBYTE PalFile[16];
};

using IMAGEDEF = struct
{
  //	UDWORD HashValue
  UWORD TPageID;
  UWORD PalID;
  UWORD Tu, Tv;
  UWORD Width;
  UWORD Height;
  SWORD XOffset;
  SWORD YOffset;
};

using IMAGEFILE = struct
{
  IMAGEHEADER Header;
  iSprite* TexturePages;
  UWORD NumCluts;
  UWORD TPageIDs[16];
  UWORD ClutIDs[48];
  IMAGEDEF* ImageDefs;
};

//*************************************************************************

using CLUTLIST = struct
{
  UWORD NumCluts;
  UWORD* ClutIDs;
};

using CLUTCALLBACK = void(*)(UWORD* clut);

namespace Neuron
{
  UWORD GetImageWidth(IMAGEFILE* ImageFile, UWORD ID);
  UWORD GetImageHeight(IMAGEFILE* ImageFile, UWORD ID);
  UWORD GetImageWidthNoCC(IMAGEFILE* ImageFile, UWORD ID);
  UWORD GetImageHeightNoCC(IMAGEFILE* ImageFile, UWORD ID);
  SWORD GetImageXOffset(IMAGEFILE* ImageFile, UWORD ID);
  SWORD GetImageYOffset(IMAGEFILE* ImageFile, UWORD ID);
  UWORD GetImageCenterX(IMAGEFILE* ImageFile, UWORD ID);
  UWORD GetImageCenterY(IMAGEFILE* ImageFile, UWORD ID);

  IMAGEFILE* LoadImageFile(UBYTE* FileData, UDWORD FileSize);
  void FreeImageFile(IMAGEFILE* ImageFile);
}

#endif
