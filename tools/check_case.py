#!/usr/bin/env python3
"""Assert every include and project entry names its file with exact case.

MSVC resolves includes case-insensitively, so a wrong name still builds on
Windows. This is what actually catches it.
"""
import os, re, sys

# Headers that genuinely do not ship in this repo. Each is reached only from a
# block guarded by a feature macro that is never defined for these builds, so
# the include is inert - but the preprocessor never sees it, and neither can we.
ABSENT_OK = {
    'qmdx.h',          # non-QMixer audio backend, dead under #if QMIXER
    'main.h',          # TEST_BED harness
    'typedefs.h', 'gateinterface.h', 'debugprint.h',   # EDITORWORLD tool
    'groundmist.h',    # JEREMY
}

SYS_OK = re.compile(r'^(windows|windowsx|stdio|stdlib|string|math|assert|time|ctype|memory|'
                    r'malloc|limits|stdarg|float|io|fcntl|direct|process|conio|search|errno|'
                    r'signal|setjmp|stddef|share|objbase|ole2|initguid|winsock2?|shellapi|'
                    r'commdlg|winres|crtdbg|new|eh|excpt|tchar|wtypes|basetsd|mmsystem|mmreg|'
                    # ships with Visual Studio under $(VCInstallDir)UnitTest\include
                    r'cppunittest)\.h$', re.I)

# Every project in the solution. DX9/Include held the vendored DirectX SDK
# headers and NetTest was the console harness; both are gone, the former as of
# the DX9 cleanup which moved the build onto the Windows SDK's own copies.
PROJECTS = ('NeuronCore', 'Outpost', 'NeuronClient', 'NeuronServer', 'NeuronCoreTest')

# Deliberately not anchored with ^...re.M. The tree is 200k lines and only 3k
# of them are includes, so the win is in not looking at the other 197k: left
# unanchored, the engine literal-searches for '#' and skips them wholesale,
# where a line-start anchor makes it try every line in turn and costs 7x more.
# The anchor is then reapplied by hand below, per match rather than per line.
INCLUDE = re.compile(r'#[ \t]*include[ \t]*"([^"]+)"')

# Real directory contents, lowercased key to the name as it is actually
# spelled on disk. This is the only way to see case on Windows - os.path.exists
# there answers case-insensitively, so asking it whether "Foo.CPP" exists when
# the file is foo.cpp gets you True and the check that this whole script exists
# to perform silently passes.
_listings = {}

def listing(d):
    m = _listings.get(d)
    if m is None:
        try: m = {f.lower(): f for f in os.listdir(d or '.')}
        except OSError: m = {}
        _listings[d] = m
    return m

def real_name(path):
    """The name path is stored under on disk, or None if it is not there."""
    d, b = os.path.split(path)
    return listing(d).get(b.lower())

def main():
    disk={}
    sources=[]
    for d in PROJECTS:
        for f in listing(d).values():
            disk.setdefault(f.lower(), f)
            if f.endswith(('.c', '.h', '.cpp')): sources.append(f'{d}/{f}')
    bad=[]

    for p in sorted(sources):
        with open(p,encoding='latin-1') as fh: text=fh.read()
        for m in INCLUDE.finditer(text):
            # the directive has to open the line, or it is a mention of one
            # inside a comment or a string rather than an include
            if text[text.rfind('\n',0,m.start())+1:m.start()].strip(): continue
            base=os.path.basename(m.group(1).replace('\\','/'))
            actual=disk.get(base.lower())
            if actual==base: continue
            # only paid per problem, not per include and not per line
            i=text.count('\n',0,m.start())+1
            if actual is None:
                if not SYS_OK.match(base) and base.lower() not in ABSENT_OK: bad.append(f"{p}:{i}: unresolved include \"{base}\"")
            else:
                bad.append(f"{p}:{i}: include \"{base}\" but file is {actual}")

    for d in PROJECTS:
        proj=f'{d}/{d}.vcxproj'
        if real_name(proj) is None: continue
        with open(proj,encoding='utf-8') as fh: t=fh.read()
        for tag,ref in re.findall(r'<(ClCompile|ClInclude|None)\s+Include="([^"]+)"', t):
            # A project may reach into a sibling for a file it builds, and
            # MSBuild spells that with backslashes.
            rel=os.path.normpath(os.path.join(d,ref.replace('\\','/')))
            actual=real_name(rel)
            if actual is None:
                bad.append(f"{proj}: {tag} {ref} does not exist")
            elif actual!=os.path.basename(rel):
                bad.append(f"{proj}: {tag} {ref} is on disk as {actual}")

    if bad:
        print(f"FAIL: {len(bad)} case/resolution problems")
        for b in bad[:40]: print("  "+b)
        if len(bad)>40: print(f"  ... and {len(bad)-40} more")
        return 1
    print("OK: all includes and project entries resolve with exact case")
    return 0

if __name__ == '__main__':
    sys.exit(main())
