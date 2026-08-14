#!/usr/bin/env python3
"""Remove commented-out code and #if 0 regions.

Deliberately conservative: a construct is only removed when it is
unambiguously disabled code. Explanatory comments are left alone.
"""
import os, re, sys, glob

# A line of C that got commented out, rather than a sentence about the code.
CODE_TAIL = re.compile(r'[;{}]\s*$')
CALL      = re.compile(r'\w+\s*\([^)]*\)\s*;?\s*$')
SIGNATURE = re.compile(r'^[A-Za-z_][\w \t\*]*\b[A-Za-z_]\w*\s*\([^;]*\)\s*$')
DIRECTIVE = re.compile(r'^#\s*(include|define|ifdef|ifndef|if|else|elif|endif|pragma)\b')
DECORATION= re.compile(r'^[\*\-=/_~<>+!\s]*$')
PROSE_HINT= re.compile(r'\b(TODO|FIXME|XXX|HACK|NOTE|NB|was |used to|should |must |note that|i\.e\.|e\.g\.)\b', re.I)

def is_code(body: str) -> bool:
    b = body.strip()
    if not b or DECORATION.match(b): return False
    if PROSE_HINT.search(b): return False
    if DIRECTIVE.match(b): return True
    if CODE_TAIL.search(b): return True
    if SIGNATURE.match(b) and ' ' in b: return True
    if CALL.match(b) and ('(' in b): return True
    return False

def spans_if_zero(lines):
    """Yield (start,end) 0-based inclusive spans of `#if 0 ... #endif`.

    Skipped when the region has an #else/#elif, since then a live branch
    is being selected rather than code being disabled.
    """
    out=[]; i=0
    while i < len(lines):
        if re.match(r'\s*#\s*if\s+0\s*(//.*|/\*.*)?$', lines[i]):
            depth=1; j=i+1; has_else=False
            while j < len(lines) and depth:
                s=lines[j].strip()
                if re.match(r'#\s*if', s): depth+=1
                elif re.match(r'#\s*endif', s): depth-=1
                elif depth==1 and re.match(r'#\s*(else|elif)\b', s): has_else=True
                j+=1
            if depth==0 and not has_else: out.append((i, j-1)); i=j; continue
        i+=1
    return out

def block_comment_spans(text):
    """0-based (start,end) line spans of /* */ comments that hold only code."""
    spans=[]
    for m in re.finditer(r'/\*.*?\*/', text, re.S):
        blk=m.group(0)
        inner=blk[2:-2]
        lines=[l for l in inner.splitlines()]
        body=[l for l in lines if l.strip()]
        if len(body) < 2: continue
        if not all(is_code(l) for l in body): continue
        # only when the comment occupies whole lines by itself
        s=text.rfind('\n',0,m.start())+1
        e=text.find('\n',m.end())
        if e==-1: e=len(text)
        if text[s:m.start()].strip() or text[m.end():e].strip(): continue
        spans.append((text[:s].count('\n'), text[:e].count('\n')))
    return spans

def process(path):
    text=open(path,encoding='latin-1').read()
    lines=text.splitlines(keepends=True)
    drop=set()
    for a,b in spans_if_zero([l.rstrip('\n') for l in lines]):
        drop.update(range(a,b+1))
    for a,b in block_comment_spans(text):
        drop.update(range(a,b+1))
    # Runs of whole-line // comments. A run containing decoration (//*****,
    # //-----) is a banner documenting the code below, so the signatures it
    # names are documentation rather than disabled code - leave those alone.
    i=0
    while i < len(lines):
        if not lines[i].strip().startswith('//'):
            i+=1; continue
        j=i
        while j < len(lines) and lines[j].strip().startswith('//'): j+=1
        run=range(i,j)
        banner=any(DECORATION.match(lines[k].strip()[2:].strip() or '') and
                   lines[k].strip()[2:].strip() for k in run)
        if not banner:
            for k in run:
                if k not in drop and is_code(lines[k].strip()[2:]): drop.add(k)
        i=j
    if not drop: return 0
    open(path,'w',encoding='latin-1').write(''.join(l for i,l in enumerate(lines) if i not in drop))
    return len(drop)

def main():
    srcs=[p for p in glob.glob('NeuronCore/*.[ch]')+glob.glob('Outpost/*.[ch]')+glob.glob('NeuronCore/*.cpp')
          if not re.search(r'_(l|y)\.[ch]$', p)]
    total=0; files=0
    for p in sorted(srcs):
        n=process(p)
        if n: total+=n; files+=1
    print(f"removed {total} lines across {files} files")

if __name__=='__main__': main()
