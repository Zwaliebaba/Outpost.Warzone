#pragma once

#include "RenderTypes.h"

namespace Neuron
{

/* Load an uncompressed A8R8G8B8 DDS file - the one layout the conversion
 * tool writes, and the layout iBitmap holds in memory, so loading is a
 * validation and a copy.
 *
 * The Load variants size the sprite from the file and allocate its bitmap
 * with new[]; the ToBuffer variants require the sprite's width, height and
 * bitmap to be set already and fail if the file does not match them.
 */
iBool DdsLoad(char* _fileName, iSprite* _sprite);
iBool DdsLoadMem(const int8* _fileData, iSprite* _sprite);
iBool DdsLoadToBuffer(char* _fileName, iSprite* _sprite);
iBool DdsLoadMemToBuffer(const int8* _fileData, iSprite* _sprite);

} // namespace Neuron
