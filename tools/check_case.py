#!/usr/bin/env python3
"""Assert every include and project entry names its file with exact case.

MSVC resolves includes case-insensitively, so a wrong name still builds on
Windows. This is what actually catches it.
"""
import os, re, sys, glob

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
                    r'commdlg|winres|crtdbg|new|eh|excpt|tchar|wtypes|basetsd|mmsystem|mmreg)\.h$', re.I)

def main():
    disk={}
    # DX9/Include held the vendored DirectX SDK headers. It is gone as of the
    # DX9 cleanup, which moved the build onto the Windows SDK's own copies, so
    # the directory is scanned when present rather than required.
    for d in ('NeuronCore','Outpost','NetTest','DX9/Include'):
        if not os.path.isdir(d): continue
        for f in os.listdir(d): disk.setdefault(f.lower(), f)
    bad=[]

    inc=re.compile(r'#\s*include\s*"([^"]+)"')
    for p in (glob.glob('NeuronCore/*.[ch]')+glob.glob('Outpost/*.[ch]')+glob.glob('NeuronCore/*.cpp')
              +glob.glob('NetTest/*.cpp')):
        for i,line in enumerate(open(p,encoding='latin-1'),1):
            m=inc.match(line.strip())
            if not m: continue
            base=os.path.basename(m.group(1).replace('\\','/'))
            actual=disk.get(base.lower())
            if actual is None:
                if not SYS_OK.match(base) and base.lower() not in ABSENT_OK: bad.append(f"{p}:{i}: unresolved include \"{base}\"")
            elif actual!=base:
                bad.append(f"{p}:{i}: include \"{base}\" but file is {actual}")

    for proj,d in (('NeuronCore/NeuronCore.vcxproj','NeuronCore'),('Outpost/Outpost.vcxproj','Outpost'),
                   ('NetTest/NetTest.vcxproj','NetTest')):
        t=open(proj,encoding='utf-8').read()
        for tag,ref in re.findall(r'<(ClCompile|ClInclude|None)\s+Include="([^"]+)"', t):
            # NetTest reaches back into NeuronCore for the two files it builds,
            # and MSBuild spells that with backslashes.
            if not os.path.exists(os.path.join(d,ref.replace('\\','/'))):
                bad.append(f"{proj}: {tag} {ref} not found with that exact case")

    if bad:
        print(f"FAIL: {len(bad)} case/resolution problems"); [print("  "+b) for b in bad[:40]]
        if len(bad)>40: print(f"  ... and {len(bad)-40} more")
        return 1
    print("OK: all includes and project entries resolve with exact case")
    return 0

sys.exit(main())
