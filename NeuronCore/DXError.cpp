#include "pch.h"

#pragma warning (disable : 4201 4214 4115 4514)
#define WIN32_LEAN_AND_MEAN
#define WIN32_EXTRA_LEAN
#include <windows.h>
#include <d3d9.h>
#pragma warning (default : 4201 4214 4115)

/* Allow frame header files to be singly included */
#define FRAME_LIB_INCLUDE

#include "DXError.h"
#include "Types.h"

/*
 * DXErrorToString
 *
 * Convert a Direct3D 9 error - or one of the general COM results D3D can
 * return - to a string.
 *
 * The Direct3D 9 error set is far smaller than the DirectDraw one this
 * replaced, because most of what DirectDraw could fail at (surface
 * management, cooperative levels, colour keys, palettes) no longer exists.
 */
STRING* DXErrorToString(HRESULT error)
{
  switch (error)
  {
  case D3D_OK:
    return "No error.";

  /* Device state */
  case D3DERR_DEVICELOST:
    return "The device has been lost but cannot be reset at this time.";
  case D3DERR_DEVICENOTRESET:
    return "The device has been lost and can be reset.";
  case D3DERR_DRIVERINTERNALERROR:
    return "Internal driver error. The application should shut down.";
  case D3DERR_NOTAVAILABLE:
    return "This device does not support the queried technique.";
  case D3DERR_OUTOFVIDEOMEMORY:
    return "Direct3D does not have enough display memory to perform the operation.";

  /* Creation and capability */
  case D3DERR_INVALIDCALL:
    return "The method call is invalid - a parameter may have an invalid value.";
  case D3DERR_INVALIDDEVICE:
    return "The requested device type is not valid.";
  case D3DERR_NOTFOUND:
    return "The requested item was not found.";
  case D3DERR_MOREDATA:
    return "There is more data available than the specified buffer can hold.";
  case D3DERR_WRONGTEXTUREFORMAT:
    return "The pixel format of the texture surface is not valid.";
  case D3DERR_CONFLICTINGRENDERSTATE:
    return "The currently set render states cannot be used together.";
  case D3DERR_CONFLICTINGTEXTUREFILTER:
    return "The current texture filters cannot be used together.";
  case D3DERR_CONFLICTINGTEXTUREPALETTE:
    return "The current textures cannot be used simultaneously.";
  case D3DERR_UNSUPPORTEDALPHAARG:
    return "The device does not support this alpha argument.";
  case D3DERR_UNSUPPORTEDALPHAOPERATION:
    return "The device does not support this alpha operation.";
  case D3DERR_UNSUPPORTEDCOLORARG:
    return "The device does not support this colour argument.";
  case D3DERR_UNSUPPORTEDCOLOROPERATION:
    return "The device does not support this colour operation.";
  case D3DERR_UNSUPPORTEDFACTORVALUE:
    return "The device does not support the specified texture factor value.";
  case D3DERR_UNSUPPORTEDTEXTUREFILTER:
    return "The device does not support the specified texture filter.";
  case D3DERR_TOOMANYOPERATIONS:
    return "The application is requesting more texture filtering operations than the device supports.";
  case D3DERR_WASSTILLDRAWING:
    return "The previous blit is incomplete.";
  case D3DERR_DRIVERINVALIDCALL:
    return "The driver rejected the call.";

  /* Results D3D9 shares with the rest of COM */
  case D3DOK_NOAUTOGEN:
    return "The call succeeded, but no mipmaps were generated.";
  case E_OUTOFMEMORY:
    return "Direct3D could not allocate sufficient memory to complete the call.";
  case E_INVALIDARG:
    return "An invalid parameter was passed to the method.";
  case E_NOINTERFACE:
    return "The requested interface is not available.";
  case E_FAIL:
    return "Unspecified failure.";
  case E_NOTIMPL:
    return "The method is not implemented.";

  default:
    return "Unrecognized error value.";
  }
}
