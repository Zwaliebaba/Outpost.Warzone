#!/usr/bin/env python3
"""Syntax-check every translation unit with mingw-w64 against a shadow tree.

There is no MSVC in the Linux container, so this is the fast first pass: it
catches the portable C++ errors, which is most of them. It is not a build --
it cannot link, and MSVC disagrees with GCC in both directions. The Windows
CI build remains the authority.

The shadow neutralises the things GCC cannot process: includes whose case
does not match the real filename, and the MSVC-only headers NeuronCore.h
pulls in but never uses.

Usage:  tools/crosscheck.py [-j N] [--release] [--x86] [--sim-only] [file.cpp ...]

The default target is x64, which is what CI builds and gates. --x86 checks
with the 32-bit mingw instead; those configurations are unmaintained since
Win32 left CI on 2026-08-27, so there is normally no reason to ask.

The width is why the target matters: at 32 bits sizeof(void*) ==
sizeof(UDWORD), so every pointer-through-integer round trip compiles clean
and an x86 pass cannot see it. That is the whole subject of
Docs/X64Readiness.md.

--sim-only checks the candidate server-side units with the NeuronClient
include directory taken away, which is the standing gate behind
Docs/ServerAuthority.md stage B. It is a ratchet rather than a pass/fail:
NeuronCore must stay at zero and the Outpost count must fall, never rise.
"""
import argparse, os, re, shutil, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor
from functools import lru_cache, partial

ROOT = os.path.dirname(os.path.abspath(os.path.dirname(__file__)))
# One per target. MSVC defines WIN32 on both platforms (every .vcxproj
# configuration carries it), so DEFS below needs no per-target arm.
CXX = {'x86': 'i686-w64-mingw32-g++', 'x64': 'x86_64-w64-mingw32-g++'}

# CINTERFACE is deliberately absent. It used to be here because the legacy
# DirectX files called COM through lpVtbl, but nothing in the tree defines it
# any more -- the projects never did, and the D3D9 work moved the call sites to
# C++ syntax. Defining it here made seven units fail against a build that is
# green, which is the harness lying rather than finding anything.
#
# The debug macro is not here: --release swaps _DEBUG for NDEBUG, and the two
# configurations do not compile the same code - DEBUG gates blocks all over the
# tree - so a clean debug run says nothing about the release branches and vice
# versa. main() appends whichever one is in force.
DEFS = ['WIN32',
        '_CRT_SECURE_NO_WARNINGS', '_CRT_NONSTDC_NO_DEPRECATE',
        'DIRECTINPUT_VERSION=0x0800', '__STDC__=1',
        # an MSVC intrinsic, and the release half of Debug.h is built on it
        '__noop(...)=((void)0)']

SKIP = re.compile(r'^(GameData|Docs|tools|packages)/')

# The projects mingw can syntax-check. NeuronCoreTest is deliberately absent:
# it is built on MSVC's CppUnitTest framework, which does not ship for mingw
# and is not worth stubbing. DX9/Include held the vendored DirectX SDK headers
# and NetTest was the console harness; both are gone from the tree.
PROJECTS = ('NeuronCore', 'Outpost', 'NeuronClient', 'NeuronServer')

# --sim-only: the candidate server-side translation units, checked with the
# NeuronClient include directory taken away. A unit that still compiles reaches
# nothing presentational, even transitively, so it could link into a headless
# server today; a unit that fails names its own coupling in the error.
#
# This is the standing proof behind Docs/ServerAuthority.md stage B, and it is a
# ratchet rather than a pass/fail: the whole of NeuronCore has to stay clean,
# and the Outpost count below has to fall, never rise. The remainder is not
# noise -- it is the measured size of the simulation's real calls into
# presentation, which is the work stage D's message planes absorb.
#
# NeuronCore is checked in this mode too and is expected to stay at zero: it is
# the half both sides share, so a client header reaching it is a layering
# defect rather than a coupling to unpick.
SIM_UNITS = (
    # the world and the things in it
    'Droid.cpp', 'Structure.cpp', 'Feature.cpp', 'Objects.cpp', 'ObjMem.cpp',
    'Combat.cpp', 'Projectile.cpp', 'Move.cpp', 'Action.cpp', 'Order.cpp',
    'Group.cpp', 'Formation.cpp', 'CmdDroid.cpp', 'Target.cpp', 'Player.cpp',
    # pathfinding
    'FPath.cpp', 'AStar.cpp', 'GatewayRoute.cpp', 'OptimisePath.cpp',
    'Findpath.cpp', 'LOSRoute.cpp', 'Gateway.cpp', 'GatewaySup.cpp',
    # the map and what can be seen of it
    'Map.cpp', 'MapGrid.cpp', 'Cluster.cpp', 'Visibility.cpp',
    # economy, tech and rules
    'Power.cpp', 'PowerCrypt.cpp', 'Research.cpp', 'Function.cpp',
    'Stats.cpp', 'StatsTable.cpp', 'Difficulty.cpp',
    # mission and campaign state
    'Mission.cpp', 'Scores.cpp',
    # the AI players, which are simulation and not any client's business
    'AI.cpp', 'ScriptAI.cpp',
)


@lru_cache(maxsize=None)
def sibling_includes(proj):
    """The sibling projects proj compiles against, read from its .vcxproj.

    Taken from AdditionalIncludeDirectories rather than hardcoded, so a file
    moving between projects does not silently take the harness out of step
    with MSVC -- which is exactly what happened when the client layer moved
    from NeuronCore to NeuronClient.

    Cached: the include rewrite asks this once per #include line, which is
    thousands of times per run against four possible answers.
    """
    path = os.path.join(ROOT, proj, f'{proj}.vcxproj')
    try:
        with open(path, encoding='utf-8') as fh:
            text = fh.read()
    except OSError:
        return ()
    found = []
    for block in re.findall(r'<AdditionalIncludeDirectories>([^<]*)<', text):
        for entry in block.split(';'):
            name = entry.strip().replace('\\', '/').rstrip('/').rsplit('/', 1)[-1]
            if name in PROJECTS and name != proj and name not in found:
                found.append(name)
    return tuple(found)

# MSVC-only headers that ship with the Concurrency Runtime. NeuronCore.h
# includes them but nothing uses them, so an empty stub is enough for GCC.
STUBS = ['concurrent_queue.h', 'concurrent_unordered_map.h', 'mdspan',
         'restrictederrorinfo.h', 'stacktrace']

# Headers the tree really uses that mingw-w64 does not ship, so an empty stub
# will not do. These are hand-written declarations, checked in under
# tools/stubs and copied over the generated ones. They are a transcription of
# somebody else's API: they catch mistakes in our use of it, not mistakes in
# themselves. Each says so at the top.
STUBDIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'stubs')

# Angled or quoted, for harvesting every name the tree includes; quoted only
# and in three groups, for rewriting one to the real spelling on disk.
ANY_INCLUDE = re.compile(r'#\s*include\s*[<"]([^">]+)[">]')
QUOTED_INCLUDE = re.compile(r'(#\s*include\s*")([^"]+)(")')


def sources():
    out = []
    for proj in PROJECTS:
        for f in sorted(os.listdir(os.path.join(ROOT, proj))):
            if f.endswith('.cpp'):
                out.append(f'{proj}/{f}')
    return out


def build_shadow(dst):
    """Copy the tree, then apply the neutralisations.

    Source text is read exactly once: the same in-memory copy supplies the
    include names the stub generator needs and is the thing the case rewrite
    edits, so it lands in the shadow by being written rather than by being
    copied and then patched in place.
    """
    real = {}       # proj -> {lowercased name: real name}
    text = {}       # (proj, name) -> source text
    included = set()

    for proj in PROJECTS:
        src = os.path.join(ROOT, proj)
        if not os.path.isdir(src):
            continue
        os.makedirs(os.path.join(dst, proj), exist_ok=True)
        for f in sorted(os.listdir(src)):
            p = os.path.join(src, f)
            if not os.path.isfile(p):
                continue
            real.setdefault(proj, {})[f.lower()] = f
            if f.endswith(('.cpp', '.h')):
                # newline='' both ways, so the round trip through the rewrite
                # is byte-for-byte and the shadow keeps the tree's CRLFs
                with open(p, encoding='utf-8', errors='surrogateescape',
                          newline='') as fh:
                    t = fh.read()
                text[proj, f] = t
                included.update(m.group(1) for m in ANY_INCLUDE.finditer(t))
            else:
                # nothing reads these, but the .vcxproj sitting beside the
                # sources is what makes the shadow browsable when it breaks
                shutil.copy2(p, os.path.join(dst, proj, f))

    stubdir = os.path.join(dst, 'stubs')
    os.makedirs(stubdir, exist_ok=True)
    for s in STUBS:
        with open(os.path.join(stubdir, s), 'w') as fh:
            fh.write('#pragma once\n')

    # mingw ships the system headers in lower case and the shadow lives on a
    # case-sensitive filesystem, so "Windows.h" has to be forwarded by hand.
    known = {n for m in real.values() for n in m}
    for name in sorted({n.rsplit('/', 1)[-1] for n in included}):
        if name.lower() != name and name.lower() not in known:
            with open(os.path.join(stubdir, name), 'w') as fh:
                fh.write(f'#pragma once\n#include <{name.lower()}>\n')

    # Written last so a real stub wins over anything generated above. Walked
    # rather than listed because some stubs sit in a directory the include
    # names -- <winrt/base.h> has to arrive as stubs/winrt/base.h.
    if os.path.isdir(STUBDIR):
        for root, _, files in os.walk(STUBDIR):
            rel = os.path.relpath(root, STUBDIR)
            out = stubdir if rel == '.' else os.path.join(stubdir, rel)
            os.makedirs(out, exist_ok=True)
            for f in sorted(files):
                shutil.copy2(os.path.join(root, f), os.path.join(out, f))

    # Include case: MSVC resolves case-insensitively, the shadow is on a
    # case-sensitive filesystem, so rewrite to the real name.
    for (proj, f), t in text.items():
        where = (proj, *sibling_includes(proj))

        def fix(m, where=where):
            name = m.group(2)
            for w in where:
                hit = real.get(w, {}).get(name.lower())
                if hit and hit != name:
                    return m.group(1) + hit + m.group(3)
            return m.group(0)

        with open(os.path.join(dst, proj, f), 'w',
                  encoding='utf-8', errors='surrogateescape', newline='') as fh:
            fh.write(QUOTED_INCLUDE.sub(fix, t))


# -fpermissive is needed: this codebase names anonymous types with
# `using X = enum {...}`, which MSVC accepts and GCC calls "declared using
# unnamed type", and 81 units use it. But -fpermissive also downgrades genuine
# type errors to warnings -- which is how a batch of NETPLAYERID-for-DPID
# conversions passed here and failed on MSVC.
#
# So the diagnostics that -fpermissive would otherwise swallow are matched back
# out of the run. Keep this list to things MSVC really rejects. They appear as
# "warning: invalid conversion ... [-fpermissive]" rather than as errors. The
# unnamed-type diagnostic carries the same tag and is deliberately not here.
PERMISSIVE_HIDES = re.compile(r'(invalid conversion|cannot convert).*\[-fpermissive\]')


def check(cmds, ours, shadow, rel):
    """Syntax-check one unit.

    One compiler run, not two. This used to compile everything twice -- once
    quiet for the exit status, then again loud to recover what -fpermissive
    downgraded -- but -w only suppresses printing and never changes the exit
    status, so a single loud run carries both signals. Dropping the quiet pass
    halves the wall clock of a green run.
    """
    r = subprocess.run(cmds[rel.split('/')[0]] + [os.path.join(shadow, rel)],
                       capture_output=True, text=True)
    if r.returncode != 0:
        return rel, r.returncode, r.stderr

    # Only our own files are judged. The vendored msquic headers trip this on
    # a mingw-vs-SDK signature difference in RtlIpv4StringToAddressA, which is
    # a gap in mingw rather than anything MSVC would reject.
    hidden = [l for l in r.stderr.splitlines()
              if PERMISSIVE_HIDES.search(l) and l.startswith(ours)]
    if hidden:
        return rel, 1, '\n'.join(hidden)

    return rel, 0, ''


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    # -j used to be hand-parsed, which quietly mistook the N of "-j 8" for a
    # filename and left the job count at its default.
    ap.add_argument('-j', '--jobs', type=int, metavar='N', default=os.cpu_count() or 8,
                    help='parallel compiler invocations (default: %(default)s)')
    ap.add_argument('--release', action='store_true',
                    help='define NDEBUG instead of _DEBUG')
    ap.add_argument('--x86', action='store_true',
                    help='check with the 32-bit mingw (default: x64, which is what CI builds)')
    ap.add_argument('--sim-only', action='store_true',
                    help='check the candidate server units with NeuronClient removed '
                         'from the include path (Docs/ServerAuthority.md stage B)')
    ap.add_argument('files', nargs='*',
                    help='translation units to check (default: all of them)')
    opt = ap.parse_args()

    cxx = CXX['x86' if opt.x86 else 'x64']
    if shutil.which(cxx) is None:
        print(f'{cxx} not found', file=sys.stderr)
        return 2

    files = [f.replace('\\', '/') for f in opt.files] or sources()
    if opt.sim_only:
        keep = {f'Outpost/{u}' for u in SIM_UNITS}
        files = [f for f in files
                 if f in keep or f.split('/')[0] == 'NeuronCore']
    files = [f for f in files if not SKIP.match(f)]
    outside = [f for f in files if f.split('/')[0] not in PROJECTS]
    if outside:
        print('not in a project mingw can check, skipping: ' + ', '.join(outside),
              file=sys.stderr)
        files = [f for f in files if f.split('/')[0] in PROJECTS]

    shadow = tempfile.mkdtemp(prefix='xcheck-')
    try:
        build_shadow(shadow)

        # Built once per project rather than once per unit: the flags depend on
        # nothing but the project the unit lives in.
        #
        # -Wno-narrowing: the generated lexers and parsers initialise their
        # short/char state tables from int literals like 65535, which GCC
        # makes an error in list-init and MSVC accepts. The old quiet pass
        # hid the whole class under -w; the sources are grandfathered
        # generator output (AGENTS.md R7), so the diagnostic is off rather
        # than the files patched.
        base = [cxx, '-fsyntax-only', '-std=c++23', '-fpermissive', '-fms-extensions', '-Wno-narrowing'] + \
               [f'-D{d}' for d in DEFS + ['NDEBUG' if opt.release else '_DEBUG']]
        cmds = {}
        for proj in PROJECTS:
            inc = ['-I', os.path.join(shadow, proj), '-I', os.path.join(shadow, 'stubs')]
            for sib in sibling_includes(proj):
                if opt.sim_only and sib == 'NeuronClient':
                    continue
                inc += ['-I', os.path.join(shadow, sib)]
            cmds[proj] = base + inc
        ours = tuple(os.path.join(shadow, p) for p in PROJECTS)

        bad = 0
        with ThreadPoolExecutor(max_workers=opt.jobs) as ex:
            for rel, rc, err in ex.map(partial(check, cmds, ours, shadow), files):
                if rc != 0:
                    bad += 1
                    lines = [l for l in err.splitlines() if ' error:' in l]
                    # the -fpermissive catch reports warnings, not errors
                    if not lines:
                        lines = [l for l in err.splitlines() if l.strip()]
                    print(f'--- {rel}: {len(lines)} error(s)')
                    for l in lines[:12]:
                        print('   ', l.replace(shadow + '/', ''))
        plat = 'x86' if opt.x86 else 'x64'
        if opt.sim_only:
            print(f'\n{len(files) - bad}/{len(files)} candidate server units are '
                  f'client-free ({plat}); {bad} still reach NeuronClient')
        else:
            print(f'\n{len(files) - bad}/{len(files)} units clean ({plat})')
        return 1 if bad else 0
    finally:
        shutil.rmtree(shadow, ignore_errors=True)


if __name__ == '__main__':
    sys.exit(main())
