#include "pch.h"

#include "Frame.h"
#include "Screen.h"
#include "FrameInt.h"
#include "DXInput.h"

// The direct input object
LPDIRECTINPUT8 psDI = nullptr;

// The direct input mouse object
LPDIRECTINPUTDEVICE8 psDIMouse = nullptr;

// Whether the mouse has been aquired
static BOOL DIMouseAcquired = FALSE;

// Initial mouse scale
#define DINP_MOUSESCALE		2

// mouse coordinates
static SDWORD mickeyX, mickeyY, mickeyScale;

// Initialise the DXInput system
BOOL DInpInitialise(void)
{
  HRESULT hr;

  /* DirectInput 8 asks for the interface by IID rather than handing back a
   * versioned object, and dinput8.lib does not export DirectInputCreate at
   * all. CreateDevice below returns an IDirectInputDevice8 directly, so
   * nothing has to be queried for afterwards.
   */
  hr = DirectInput8Create(hInstance, DIRECTINPUT_VERSION, IID_IDirectInput8, (void**)&psDI, nullptr);
  if (FAILED(hr))
  {
    Neuron::Fatal("DXInpInitialise: couldn't create DI object");
    return FALSE;
  }

  hr = psDI->CreateDevice(GUID_SysMouse, &psDIMouse, nullptr);
  if (FAILED(hr))
  {
    Neuron::Fatal("DXInpInitialise: couldn't create mouse object");
    return FALSE;
  }

  hr = psDIMouse->SetDataFormat(&c_dfDIMouse); //&sDataFormat);
  if (FAILED(hr))
  {
    Neuron::Fatal("DXInpInitialise: couldn't set mouse format");
    return FALSE;
  }

  hr = psDIMouse->SetCooperativeLevel(hWndMain, DISCL_EXCLUSIVE | DISCL_FOREGROUND);
  if (FAILED(hr))
  {
    Neuron::Fatal("DXInpInitialise: couldn't set mouse cooperative level");
    return FALSE;
  }

  DIMouseAcquired = FALSE;
  if (!DInpMouseAcc(DINP_MOUSEACQUIRE))
  {
    Neuron::Fatal("DXInpInitialise: couldn't acquire mouse");
    return FALSE;
  }

  mickeyScale = DINP_MOUSESCALE;
  mickeyX = static_cast<SDWORD>(screenWidth) * mickeyScale / 2;
  mickeyY = static_cast<SDWORD>(screenHeight) * mickeyScale / 2;

  return TRUE;
}

// Shut down the DX input system
void DInpShutDown(void)
{
  RELEASE(psDIMouse);
  RELEASE(psDI);
}

// Aquire or release input for the mouse
BOOL DInpMouseAcc(UDWORD aquireType)
{
  HRESULT hr;

  if (!psDIMouse)
    return FALSE;

  switch (aquireType)
  {
  case DINP_MOUSEACQUIRE:
    if (!DIMouseAcquired)
    {
      hr = psDIMouse->Acquire();
      if (FAILED(hr))
      {
        DEBUG_ASSERT_TEXT(FALSE, "DInpMouseAcc: failed to aquire mouse");
        return FALSE;
      }
      DIMouseAcquired = TRUE;
    }
    break;
  case DINP_MOUSERELEASE:
    if (DIMouseAcquired)
    {
      hr = psDIMouse->Unacquire();
      if (FAILED(hr))
      {
        DEBUG_ASSERT_TEXT(FALSE, "DInpMouseAcc: failed to unaquire mouse");
        return FALSE;
      }
      DIMouseAcquired = FALSE;
    }
    break;
  default: DEBUG_ASSERT_TEXT(FALSE, "DInpMouseAcc: unknown aquire type");
    return FALSE;
  }
  return TRUE;
}

// Get the current state of the mouse
BOOL DInpGetMouseState(SDWORD* pX, SDWORD* pY, SDWORD* pButtons)
{
  HRESULT hr;
  DIMOUSESTATE sState;

  hr = psDIMouse->GetDeviceState(sizeof(DIMOUSESTATE), &sState);
  if (hr == DIERR_INPUTLOST)
  {
    DIMouseAcquired = FALSE;
    DInpMouseAcc(DINP_MOUSEACQUIRE);
    hr = psDIMouse->GetDeviceState(sizeof(DIMOUSESTATE), &sState);
  }
  if (FAILED(hr))
  {
    DEBUG_ASSERT_TEXT(FALSE, "DInpGetMouseState: failed to get mouse state\n{}", DIErrorToString(hr));
    return FALSE;
  }

  // update the mickey coords
  mickeyX += sState.lX;
  if (mickeyX < 0)
    mickeyX = 0;
  if (mickeyX >= static_cast<SDWORD>(screenWidth) * mickeyScale)
    mickeyX = static_cast<SDWORD>(screenWidth) * mickeyScale - 1;
  mickeyY += sState.lY;
  if (mickeyY < 0)
    mickeyY = 0;
  if (mickeyY >= static_cast<SDWORD>(screenHeight) * mickeyScale)
    mickeyY = static_cast<SDWORD>(screenHeight) * mickeyScale - 1;

  *pX = mickeyX / mickeyScale;
  *pY = mickeyY / mickeyScale;
  *pButtons = 0;
  if (sState.rgbButtons[0])
    *pButtons |= DINP_LMB;
  if (sState.rgbButtons[1])
    *pButtons |= DINP_MMB;
  if (sState.rgbButtons[2])
    *pButtons |= DINP_RMB;

  return TRUE;
}

// convert a direct input error to a string
const STRING* DIErrorToString(HRESULT dierror)
{
  switch (dierror)
  {
  case DIERR_INPUTLOST:
    return "Access to the device has been lost.\nIt must be re-acquired.";
    break;
  case DIERR_INVALIDPARAM:
    return "An invalid parameter was passed to the returning function,\n" "or the object was not in a state that admitted the function\n"
      "to be called.";
    break;
  case DIERR_NOTACQUIRED:
    return "The operation cannot be performed unless the device is acquired.";
    break;
  case DIERR_NOTINITIALIZED:
    return "This object has not been initialized";
    break;
  case E_PENDING:
    return "The data necessary to complete this operation is not yet available.";
    break;
  default:
    return "Direct Input error";
    break;
  }
}
