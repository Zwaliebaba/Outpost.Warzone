#!/usr/bin/env python3
"""Syntax-check every translation unit with mingw-w64 against a shadow tree.

There is no MSVC in the Linux container, so this is the fast first pass: it
catches the portable C++ errors, which is most of them. It is not a build --
it cannot link, and MSVC disagrees with GCC in both directions. The Windows
CI build remains the authority.

The shadow neutralises the things GCC cannot process: includes whose case
does not match the real filename, and the MSVC-only headers NeuronCore.h
pulls in but never uses.

Usage:  tools/crosscheck.py [-j N] [file.cpp ...]
"""
import os, re, shutil, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.abspath(os.path.dirname(__file__)))
CXX = 'i686-w64-mingw32-g++'

# --release swaps _DEBUG for NDEBUG. The two configurations do not compile the
# same code - DEBUG gates blocks all over the tree - so a clean debug run says
# nothing about the release branches and vice versa.
RELEASE = '--release' in sys.argv

# CINTERFACE is deliberately absent. It used to be here because the legacy
# DirectX files called COM through lpVtbl, but nothing in the tree defines it
# any more -- the projects never did, and the D3D9 work moved the call sites to
# C++ syntax. Defining it here made seven units fail against a build that is
# green, which is the harness lying rather than finding anything.
DEFS = ['WIN32', 'NDEBUG' if RELEASE else '_DEBUG',
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


def sibling_includes(proj):
    """The sibling projects proj compiles against, read from its .vcxproj.

    Taken from AdditionalIncludeDirectories rather than hardcoded, so a file
    moving between projects does not silently take the harness out of step
    with MSVC -- which is exactly what happened when the client layer moved
    from NeuronCore to NeuronClient.
    """
    path = os.path.join(ROOT, proj, f'{proj}.vcxproj')
    try:
        text = open(path, encoding='utf-8').read()
    except OSError:
        return []
    found = []
    for block in re.findall(r'<AdditionalIncludeDirectories>([^<]*)<', text):
        for entry in block.split(';'):
            name = entry.strip().replace('\\', '/').rstrip('/').rsplit('/', 1)[-1]
            if name in PROJECTS and name != proj and name not in found:
                found.append(name)
    return found

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


def sources():
    out = []
    for proj in PROJECTS:
        for f in sorted(os.listdir(os.path.join(ROOT, proj))):
            if f.endswith('.cpp'):
                out.append(f'{proj}/{f}')
    return out


def build_shadow(dst):
    """Copy the tree, then apply the neutralisations."""
    real = {}
    for proj in PROJECTS:
        src = os.path.join(ROOT, proj)
        if not os.path.isdir(src):
            continue
        os.makedirs(os.path.join(dst, proj), exist_ok=True)
        for f in os.listdir(src):
            p = os.path.join(src, f)
            if os.path.isfile(p):
                shutil.copy2(p, os.path.join(dst, proj, f))
                real.setdefault(proj, {})[f.lower()] = f

    stubdir = os.path.join(dst, 'stubs')
    os.makedirs(stubdir, exist_ok=True)
    for s in STUBS:
        open(os.path.join(stubdir, s), 'w').write('#pragma once\n')

    # mingw ships the system headers in lower case and the shadow lives on a
    # case-sensitive filesystem, so "Windows.h" has to be forwarded by hand.
    known = {n for m in real.values() for n in m}
    for name in sorted(system_includes(dst)):
        if name.lower() != name and name.lower() not in known:
            open(os.path.join(stubdir, name), 'w').write(
                f'#pragma once\n#include <{name.lower()}>\n')

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

    for proj in PROJECTS:
        d = os.path.join(dst, proj)
        for f in os.listdir(d):
            if not f.endswith(('.cpp', '.h')):
                continue
            p = os.path.join(d, f)
            rel = f'{proj}/{f}'
            try:
                t = open(p, encoding='utf-8', errors='surrogateescape').read()
            except OSError:
                continue
            orig = t

            # Include case: MSVC resolves case-insensitively, the shadow is on
            # a case-sensitive filesystem, so rewrite to the real name.
            def fix(m, proj=proj):
                name = m.group(2)
                for where in (proj, *sibling_includes(proj)):
                    hit = real.get(where, {}).get(name.lower())
                    if hit and hit != name:
                        return m.group(1) + hit + m.group(3)
                return m.group(0)

            t = re.sub(r'(#\s*include\s*")([^"]+)(")', fix, t)

            if t != orig:
                open(p, 'w', encoding='utf-8', errors='surrogateescape').write(t)


def system_includes(dst):
    """Every name included anywhere in the shadow, lower-cased key aside."""
    names = set()
    pat = re.compile(r'#\s*include\s*[<"]([^">]+)[">]')
    for proj in PROJECTS:
        d = os.path.join(dst, proj)
        for f in os.listdir(d):
            if f.endswith(('.cpp', '.h')):
                t = open(os.path.join(d, f), encoding='utf-8', errors='surrogateescape').read()
                names.update(m.group(1) for m in pat.finditer(t))
    return {n.rsplit('/', 1)[-1] for n in names}


# -fpermissive is needed: this codebase names anonymous types with
# `using X = enum {...}`, which MSVC accepts and GCC calls "declared using
# unnamed type", and 81 units use it. But -fpermissive also downgrades genuine
# type errors to warnings, and -w then hides them -- which is how a batch of
# NETPLAYERID-for-DPID conversions passed here and failed on MSVC.
#
# So the diagnostics that -fpermissive would otherwise swallow are matched back
# out of a warning-visible run. Keep this list to things MSVC really rejects.
# Matched against a -fpermissive run with warnings visible, where these appear
# as "warning: invalid conversion ... [-fpermissive]" rather than as errors.
# The unnamed-type diagnostic carries the same tag and is deliberately not here.
PERMISSIVE_HIDES = re.compile(r'(invalid conversion|cannot convert).*\[-fpermissive\]')


def check(shadow, rel):
    proj = rel.split('/')[0]
    inc = ['-I', os.path.join(shadow, proj), '-I', os.path.join(shadow, 'stubs')]
    for sib in sibling_includes(proj):
        inc += ['-I', os.path.join(shadow, sib)]
    defs = DEFS
    # no -w, so the downgraded diagnostics are still printed
    cmd = [CXX, '-fsyntax-only', '-std=c++23', '-fpermissive',
           '-fms-extensions', '-w', '-Wno-everything'] + \
          [f'-D{d}' for d in defs] + inc + [os.path.join(shadow, rel)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        return rel, r.returncode, r.stderr

    # second pass, warnings visible, only to catch what -fpermissive hid
    loud = [c for c in cmd if c not in ('-w', '-Wno-everything')]
    r2 = subprocess.run(loud, capture_output=True, text=True)
    # Only our own files are judged. The vendored msquic headers trip this on
    # a mingw-vs-SDK signature difference in RtlIpv4StringToAddressA, which is
    # a gap in mingw rather than anything MSVC would reject.
    ours = tuple(os.path.join(shadow, p) for p in PROJECTS)
    hidden = [l for l in r2.stderr.splitlines()
              if PERMISSIVE_HIDES.search(l) and l.startswith(ours)]
    if hidden:
        return rel, 1, '\n'.join(hidden)

    return rel, 0, ''


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('-')]
    jobs = 8
    for a in sys.argv[1:]:
        if a.startswith('-j'):
            jobs = int(a[2:] or 8)

    if shutil.which(CXX) is None:
        print(f'{CXX} not found', file=sys.stderr)
        return 2

    files = args or sources()
    files = [f for f in files if not SKIP.match(f)]

    shadow = tempfile.mkdtemp(prefix='xcheck-')
    try:
        build_shadow(shadow)
        bad = 0
        with ThreadPoolExecutor(max_workers=jobs) as ex:
            for rel, rc, err in ex.map(lambda f: check(shadow, f), files):
                if rc != 0:
                    bad += 1
                    lines = [l for l in err.splitlines() if ' error:' in l]
                    # the -fpermissive catch reports warnings, not errors
                    if not lines:
                        lines = [l for l in err.splitlines() if l.strip()]
                    print(f'--- {rel}: {len(lines)} error(s)')
                    for l in lines[:12]:
                        print('   ', l.replace(shadow + '/', ''))
        print(f'\n{len(files) - bad}/{len(files)} units clean')
        return 1 if bad else 0
    finally:
        shutil.rmtree(shadow, ignore_errors=True)


if __name__ == '__main__':
    sys.exit(main())
