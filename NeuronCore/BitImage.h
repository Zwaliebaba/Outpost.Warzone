#ifndef __INCLUDED_BITIMAGE__
#define __INCLUDED_BITIMAGE__

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
