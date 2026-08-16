"""Print what the game sends to OutputDebugString.

Neuron::Fatal calls __debugbreak(), so an assertion under a launcher shows up
only as exit code 0xC0000003 with nothing on screen. The message itself goes to
OutputDebugString, which writes into a shared memory mapping that any one
process at a time may read. This is that reader. Start it before the game:

    python tools/dbg.py

Docs/Verification.md leans on it -- most of the passes there can fail in a way
that is indistinguishable from a rendering fault until you can see the message.

Only one debug listener can run at a time, machine-wide. If DebugView, Visual
Studio's output window, or a second copy of this script already holds the
objects, this one says so and exits rather than silently receiving nothing.

Elevation matters: a listener cannot see output from a process running at a
higher integrity level than itself. Run this the same way you run the game.

This is a transcription of a documented Win32 protocol -- a DBWIN_BUFFER
mapping with DBWIN_BUFFER_READY and DBWIN_DATA_READY gating it -- and it checks
our use of that protocol rather than the protocol itself. It has not been run
on Windows from this repository; there is no Windows in the development
container.
"""

import argparse
import ctypes
import datetime
import sys

BUFFER_SIZE = 4096  # fixed by the protocol: DWORD pid, then char[4092]
PAGE_READWRITE = 0x04
FILE_MAP_READ = 0x0004
SYNCHRONIZE = 0x00100000
EVENT_MODIFY_STATE = 0x0002
WAIT_OBJECT_0 = 0x0
WAIT_TIMEOUT = 0x102
ERROR_ALREADY_EXISTS = 183
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1)


def _kernel32():
    if not sys.platform.startswith("win"):
        sys.exit("dbg.py reads a Win32 shared mapping and only runs on Windows.")

    # A submodule, so plain "import ctypes" does not bind it, and it only
    # imports on Windows -- which is why it lives below the guard above.
    import ctypes.wintypes

    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    wt = ctypes.wintypes

    k32.CreateEventW.argtypes = [wt.LPVOID, wt.BOOL, wt.BOOL, wt.LPCWSTR]
    k32.CreateEventW.restype = wt.HANDLE
    k32.CreateFileMappingW.argtypes = [wt.HANDLE, wt.LPVOID, wt.DWORD, wt.DWORD, wt.DWORD, wt.LPCWSTR]
    k32.CreateFileMappingW.restype = wt.HANDLE
    k32.MapViewOfFile.argtypes = [wt.HANDLE, wt.DWORD, wt.DWORD, wt.DWORD, ctypes.c_size_t]
    k32.MapViewOfFile.restype = wt.LPVOID  # must be pointer-sized, not int
    k32.UnmapViewOfFile.argtypes = [wt.LPCVOID]
    k32.UnmapViewOfFile.restype = wt.BOOL
    k32.WaitForSingleObject.argtypes = [wt.HANDLE, wt.DWORD]
    k32.WaitForSingleObject.restype = wt.DWORD
    k32.SetEvent.argtypes = [wt.HANDLE]
    k32.SetEvent.restype = wt.BOOL
    k32.CloseHandle.argtypes = [wt.HANDLE]
    k32.CloseHandle.restype = wt.BOOL
    return k32


def _create(k32, kind, name, *args):
    """Create a named object, refusing to share it with an existing listener."""
    ctypes.set_last_error(0)
    handle = kind(*args, name)
    err = ctypes.get_last_error()
    if not handle:
        sys.exit(f"could not create {name}: Win32 error {err}")
    if err == ERROR_ALREADY_EXISTS:
        k32.CloseHandle(handle)
        sys.exit(
            f"{name} already exists -- another debug listener is running "
            "(DebugView, a Visual Studio output window, or a second copy of "
            "this script). Only one at a time can receive."
        )
    return handle


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--pid", type=int, help="show only this process id")
    ap.add_argument("--no-time", action="store_true", help="omit timestamps")
    ap.add_argument("--no-pid", action="store_true", help="omit process ids")
    args = ap.parse_args()

    k32 = _kernel32()

    buffer_ready = _create(k32, k32.CreateEventW, "DBWIN_BUFFER_READY", None, False, False)
    data_ready = _create(k32, k32.CreateEventW, "DBWIN_DATA_READY", None, False, False)
    mapping = _create(
        k32, k32.CreateFileMappingW, "DBWIN_BUFFER",
        INVALID_HANDLE_VALUE, None, PAGE_READWRITE, 0, BUFFER_SIZE,
    )

    view = k32.MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, BUFFER_SIZE)
    if not view:
        sys.exit(f"could not map DBWIN_BUFFER: Win32 error {ctypes.get_last_error()}")

    print("listening -- Ctrl-C to stop", file=sys.stderr)
    try:
        while True:
            # Tell the writer the buffer is free, then wait for it to fill.
            k32.SetEvent(buffer_ready)
            waited = k32.WaitForSingleObject(data_ready, 1000)
            if waited == WAIT_TIMEOUT:
                continue
            if waited != WAIT_OBJECT_0:
                sys.exit(f"wait failed: Win32 error {ctypes.get_last_error()}")

            pid = ctypes.c_ulong.from_address(view).value
            raw = ctypes.string_at(view + ctypes.sizeof(ctypes.c_ulong), BUFFER_SIZE - 4)
            text = raw.split(b"\0", 1)[0].decode("mbcs", errors="replace")

            if args.pid is not None and pid != args.pid:
                continue

            prefix = ""
            if not args.no_time:
                prefix += datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3] + " "
            if not args.no_pid:
                prefix += f"[{pid}] "
            # The game emits its own newlines; do not add a second one.
            print(prefix + text.rstrip("\r\n"), flush=True)
    except KeyboardInterrupt:
        pass
    finally:
        k32.UnmapViewOfFile(view)
        for handle in (mapping, data_ready, buffer_ready):
            k32.CloseHandle(handle)


if __name__ == "__main__":
    main()
