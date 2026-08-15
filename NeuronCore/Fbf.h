#ifndef _fbf_
#define _fbf_

#include <stdio.h>
#include "PieTypes.h"

#define FBF_OPENFAILED		-1
#define FBF_TOOMANYOPEN		-2
#define FBF_OUTOFMEMORY		-3
#define FBF_UNKNOWNMODE		-4
#define FBF_MODE_R			0
#define FBF_MODE_W			1
#define FBF_SEEK_SET			SEEK_SET
#define FBF_SEEK_END			SEEK_END
#define FBF_DEFAULT_BUFFER	-1

//*************************************************************************

namespace Neuron
{
  extern int FileOpen(char* fname, int mode, int buffersize);
  extern int FileGet(int fd);
  extern void FileClose(int fd);
  extern int FilePut(int fd, int8 c);
  extern int FileSeek(int fd, int where, int seek);
  extern int32 FileSize(char* filename);
  extern int32 FileSizeOpen(int fd);
  extern iBool FileLoad(char* filename, uint8* data);
  extern iBool FileSave(char* filename, uint8* data, int32 size);
}

#endif
