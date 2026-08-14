#!/usr/bin/env python3
"""Syntax-check every translation unit with mingw-w64 against a shadow tree.

There is no MSVC in the Linux container, so this is the fast first pass: it
catches the portable C++ errors, which is most of them. It is not a build --
it cannot link, and MSVC disagrees with GCC in both directions. The Windows
CI build remains the authority.

The shadow neutralises the things GCC cannot process:

  * the MSVC inline assembly in Fractions.h, RendMode.cpp, PieDraw.cpp
  * includes whose case does not match the real filename

Usage:  tools/crosscheck.py [-j N] [file.cpp ...]
"""
import os, re, shutil, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.abspath(os.path.dirname(__file__)))
CXX = 'i686-w64-mingw32-g++'

DEFS = ['WIN32', '_DEBUG', '_CRT_SECURE_NO_WARNINGS', '_CRT_NONSTDC_NO_DEPRECATE',
        'DIRECTINPUT_VERSION=0x0700', 'CINTERFACE', '__STDC__=1']

# Sources whose bodies are MSVC inline asm. GCC cannot parse Intel-syntax
# __asm blocks at all, so the shadow empties them; they are not what this
# check is looking at.
ASM_FILES = {'NeuronCore/PieDraw.cpp', 'NeuronCore/RendMode.cpp',
             'NeuronCore/Fractions.h', 'NeuronCore/AMD3D.h'}

SKIP = re.compile(r'^(DX9|GameData|Docs|tools)/')

# MSVC-only headers that ship with the Concurrency Runtime. NeuronCore.h
# includes them but nothing uses them, so an empty stub is enough for GCC.
STUBS = ['concurrent_queue.h', 'concurrent_unordered_map.h', 'mdspan',
         'restrictederrorinfo.h', 'stacktrace']


def sources():
    out = []
    for proj in ('NeuronCore', 'Outpost'):
        for f in sorted(os.listdir(os.path.join(ROOT, proj))):
            if f.endswith('.cpp'):
                out.append(f'{proj}/{f}')
    return out


def build_shadow(dst):
    """Copy the tree, then apply the three neutralisations."""
    real = {}
    for proj in ('NeuronCore', 'Outpost', 'DX9/Include', 'DX9/Include/DShowIDL'):
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

    for proj in ('NeuronCore', 'Outpost'):
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

            # AMD3D.h is nothing but 3DNow! opcode macros spelled as _asm
            # _emit inside #defines. Once the asm blocks that use them are
            # gone the macros are unreferenced, so the shadow empties it.
            if rel == 'NeuronCore/AMD3D.h':
                t = '#pragma once\n'
            elif rel in ASM_FILES:
                t = strip_asm(t)

            # Include case: MSVC resolves case-insensitively, the shadow is on
            # a case-sensitive filesystem, so rewrite to the real name.
            def fix(m):
                name = m.group(2)
                for where in (proj, 'NeuronCore', 'DX9/Include'):
                    hit = real.get(where, {}).get(name.lower())
                    if hit and hit != name:
                        return m.group(1) + hit + m.group(3)
                return m.group(0)

            t = re.sub(r'(#\s*include\s*")([^"]+)(")', fix, t)

            # MSVC's headers reach _getpid transitively; mingw's do not.
            if rel == 'NeuronCore/W95Trace.cpp':
                t = t.replace('#include "pch.h"', '#include "pch.h"\n#include <process.h>', 1)

            # std::exception(const char *) is an MSVC extension.
            if rel == 'NeuronCore/Debug.h':
                t = t.replace('std::exception("Fatal Error")',
                              'std::runtime_error("Fatal Error")')

            if t != orig:
                open(p, 'w', encoding='utf-8', errors='surrogateescape').write(t)


def system_includes(dst):
    """Every name included anywhere in the shadow, lower-cased key aside."""
    names = set()
    pat = re.compile(r'#\s*include\s*[<"]([^">]+)[">]')
    for proj in ('NeuronCore', 'Outpost'):
        d = os.path.join(dst, proj)
        for f in os.listdir(d):
            if f.endswith(('.cpp', '.h')):
                t = open(os.path.join(d, f), encoding='utf-8', errors='surrogateescape').read()
                names.update(m.group(1) for m in pat.finditer(t))
    return {n.rsplit('/', 1)[-1] for n in names}


def strip_asm(t):
    """Replace __asm/_asm blocks and _emit macro bodies with nothing."""
    out, i, n = [], 0, len(t)
    while i < n:
        m = re.compile(r'\b(__asm|_asm)\b').search(t, i)
        if not m:
            out.append(t[i:])
            break
        out.append(t[i:m.start()])
        j = m.end()
        while j < n and t[j] in ' \t\r\n':
            j += 1
        if j < n and t[j] == '{':
            depth, j = 0, j
            while j < n:
                if t[j] == '{':
                    depth += 1
                elif t[j] == '}':
                    depth -= 1
                    if depth == 0:
                        j += 1
                        break
                j += 1
            out.append('{}')
        else:                                    # single-instruction form
            while j < n and t[j] not in ';\n':
                j += 1
            out.append(';' if j < n and t[j] == ';' else '')
            if j < n and t[j] == ';':
                j += 1
        i = j
    return ''.join(out)


def check(shadow, rel):
    proj = rel.split('/')[0]
    inc = ['-I', os.path.join(shadow, proj), '-I', os.path.join(shadow, 'DX9/Include'),
           '-I', os.path.join(shadow, 'stubs')]
    if proj == 'Outpost':
        inc += ['-I', os.path.join(shadow, 'NeuronCore')]
    cmd = [CXX, '-fsyntax-only', '-std=c++20', '-fpermissive', '-w',
           '-fms-extensions', '-Wno-everything'] + \
          [f'-D{d}' for d in DEFS] + inc + [os.path.join(shadow, rel)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return rel, r.returncode, r.stderr


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
                    print(f'--- {rel}: {len(lines)} error(s)')
                    for l in lines[:12]:
                        print('   ', l.replace(shadow + '/', ''))
        print(f'\n{len(files) - bad}/{len(files)} units clean')
        return 1 if bad else 0
    finally:
        shutil.rmtree(shadow, ignore_errors=True)


if __name__ == '__main__':
    sys.exit(main())
