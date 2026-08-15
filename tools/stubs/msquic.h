/* Cross-check shim for msquic.h. NOT part of the build.
 *
 * MsQuic reaches the real build through the Microsoft.Native.Quic.MsQuic.Schannel
 * NuGet package, which supplies its own headers; MSVC never sees this file.
 * mingw-w64 has no msquic.h at all, so without something here the QUIC transport
 * would be the one file the cross-check could not look at.
 *
 * Unlike tools/stubs/x3daudio.h, this is NOT a transcription. msquic_upstream.h
 * and msquic_winuser.h beside it are the upstream headers verbatim, pinned to
 * the same package version the build restores (2.6.0), so what is checked here
 * is the genuine API and a wrong signature is a real error rather than agreement
 * with my own copy of it.
 *
 * Only two things stand between those headers and GCC, and both are gaps in
 * mingw-w64 rather than anything of MsQuic's:
 *
 *   - a handful of SAL annotations that mingw's sal.h does not define;
 *   - RtlIpv6StringToAddressA, which mstcpip.h declares on Windows and mingw
 *     does not, used by an inline helper in msquic_winuser.h.
 *
 * Keep the version here in step with packages.config. CI, compiling against
 * the NuGet headers, remains the authority.
 */

#ifndef _MSQUIC_CROSSCHECK_SHIM_H_
#define _MSQUIC_CROSSCHECK_SHIM_H_

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#ifndef _Pre_defensive_
#define _Pre_defensive_
#endif
#ifndef _Post_writable_byte_size_
#define _Post_writable_byte_size_(x)
#endif

extern "C" {
LONG NTAPI RtlIpv6StringToAddressA(PCSTR, PCSTR*, struct in6_addr*);
}

#include "msquic_upstream.h"

#endif	// _MSQUIC_CROSSCHECK_SHIM_H_
