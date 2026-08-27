/* Forwards the tree's `#include <Windows.h>` to mingw's lower-case header, and
 * restores the identifiers MSVC's copy takes that mingw's does not.
 *
 * MSVC reaches rpcndr.h through rpc.h and defines `small` as `char` for every
 * translation unit; mingw ships the same line guarded by `#ifdef RC_INVOKED`,
 * so under this harness it never fires. That one difference hid a real defect:
 * NetWireTest declared a `char small[8]`, which the cross-check called clean
 * and MSVC rejected with "'char' followed by 'char' is illegal".
 *
 * So the divergence is stated here rather than left to be found on the build
 * agent. Nothing in the tree uses these as identifiers -- MSVC would already
 * be rejecting it if anything did -- so adding them costs nothing and closes
 * the class.
 *
 * Like the other files under tools/stubs this is a transcription of somebody
 * else's headers: it checks our use of them, not themselves.
 */

#pragma once

#include <windows.h>

/* rpcndr.h, as MSVC expands it. */
#ifndef small
#define small char
#endif
