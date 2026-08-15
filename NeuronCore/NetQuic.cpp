#include "pch.h"
/***************************************************************************/
/*
 * NetQuic.cpp
 *
 * The QUIC side of the transport seam in NetTransport.h, over MsQuic.
 *
 * MsQuic supplies what DirectPlay supplied and what a hand-written protocol
 * would otherwise have had to: reliable ordered streams, unreliable datagrams,
 * connection lifecycle and idle timeout, and TLS 1.3 on every byte. What is
 * written here is the session and player layer above that, and -- elsewhere --
 * LAN discovery, which QUIC has no notion of.
 *
 * This file is the beginning of that. It currently brings the library up and
 * takes it down, which is enough to prove the parts nothing else can check
 * from Linux: that the NuGet package restores, that its Win32 x86 import
 * library is what gets linked, and that msquic.dll is copied beside the
 * executable.
 */
/***************************************************************************/

#include <winsock2.h>
#include <windows.h>

/* msquic_winuser.h writes `#if DEBUG` to pick the debug layout of QUIC_SQE.
 * Debug.h -- reached from pch.h long before this line -- defines DEBUG with no
 * value, which turns that into `#if` with no expression and is a preprocessor
 * error rather than a wrong answer. Undefining it for the include is also the
 * correct value: the msquic.dll the package ships is a release build, so the
 * layout it was compiled with is the one `#if DEBUG` selects when DEBUG is
 * absent.
 */
#pragma push_macro("DEBUG")
#undef DEBUG
#include <msquic.h>
#pragma pop_macro("DEBUG")

#include "Frame.h"
#include "NetTransport.h"

/***************************************************************************/

/* MsQuic hands out its entire API as a table of function pointers, so this is
 * the only global the rest of the file needs.
 */
static const QUIC_API_TABLE* g_pMsQuic = nullptr;
static HQUIC g_hRegistration = nullptr;

/* Named so it is recognisable in MsQuic's own tracing, which is otherwise a
 * sea of anonymous registrations.
 */
static const QUIC_REGISTRATION_CONFIG g_sRegConfig = {"Outpost", QUIC_EXECUTION_PROFILE_LOW_LATENCY};

/***************************************************************************/

BOOL netquic_Startup(void)
{
  QUIC_STATUS status;

  if (g_pMsQuic != nullptr)
    return TRUE;

  status = MsQuicOpen2(&g_pMsQuic);
  if (QUIC_FAILED(status))
  {
    Neuron::DebugTrace("netquic_Startup: MsQuicOpen2 failed, 0x{:08x}\n", static_cast<UDWORD>(status));
    g_pMsQuic = nullptr;
    return FALSE;
  }

  /* LOW_LATENCY rather than the default: this carries lockstep game commands,
   * where a late packet is worse than a small one.
   */
  status = g_pMsQuic->RegistrationOpen(&g_sRegConfig, &g_hRegistration);
  if (QUIC_FAILED(status))
  {
    Neuron::DebugTrace("netquic_Startup: RegistrationOpen failed, 0x{:08x}\n", static_cast<UDWORD>(status));
    MsQuicClose(g_pMsQuic);
    g_pMsQuic = nullptr;
    g_hRegistration = nullptr;
    return FALSE;
  }

  return TRUE;
}

/***************************************************************************/

void netquic_Shutdown(void)
{
  if (g_hRegistration != nullptr)
  {
    /* Closing the registration waits for every connection under it to finish,
     * so nothing can still be in a callback once this returns.
     */
    g_pMsQuic->RegistrationClose(g_hRegistration);
    g_hRegistration = nullptr;
  }

  if (g_pMsQuic != nullptr)
  {
    MsQuicClose(g_pMsQuic);
    g_pMsQuic = nullptr;
  }
}

/***************************************************************************/

BOOL netquic_Available(void) { return g_pMsQuic != nullptr ? TRUE : FALSE; }

/***************************************************************************/
