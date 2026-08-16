/*
 * DXError.h
 *
 * Convert a DirectX error code to an error string.
 *
 */
#ifndef _dxerror_h
#define _dxerror_h

#pragma warning (disable : 4201 4214 4115 4514)
#include <windows.h>
#include <d3d9.h>
#pragma warning (default : 4201 4214 4115)

#include "Types.h"

/* Turn a Direct3D 9 (or general COM) error code into a string */
extern const STRING* DXErrorToString(HRESULT error);

#endif
