#!/usr/bin/env python3
"""Canonical form of a C file: comments stripped, #if 0 regions dropped,
whitespace collapsed. Two files with the same canonical form compile the same.
"""
import re, sys

def strip_comments(t):
    out=[];i=0;n=len(t);st='code'
    while i<n:
        c=t[i];x=t[i+1] if i+1<n else ''
        if st=='code':
            if c=='/' and x=='*': st='b';i+=2;continue
            if c=='/' and x=='/':
                while i<n and t[i]!='\n': i+=1
                continue
            if c=='"': st='s';out.append(c);i+=1;continue
            if c=="'": st='q';out.append(c);i+=1;continue
            out.append(c)
        elif st=='b':
            if c=='*' and x=='/': st='code';i+=2;continue
            if c=='\n': out.append(c)
        elif st=='s':
            out.append(c)
            if c=='\\': out.append(t[i+1]);i+=2;continue
            if c=='"': st='code'
        elif st=='q':
            out.append(c)
            if c=='\\': out.append(t[i+1]);i+=2;continue
            if c=="'": st='code'
        i+=1
    return ''.join(out)

def drop_if_zero(lines):
    out=[];i=0
    while i<len(lines):
        if re.match(r'\s*#\s*if\s+0\s*$', lines[i]):
            depth=1;j=i+1;has_else=False;body=[]
            while j<len(lines) and depth:
                s=lines[j].strip()
                if re.match(r'#\s*if',s): depth+=1
                elif re.match(r'#\s*endif',s):
                    depth-=1
                    if depth==0: break
                elif depth==1 and re.match(r'#\s*(else|elif)\b',s): has_else=True
                body.append(lines[j]); j+=1
            if not has_else: i=j+1; continue
        out.append(lines[i]); i+=1
    return out

def canon(path):
    t=strip_comments(open(path,encoding='latin-1').read())
    lines=[l.strip() for l in t.splitlines()]
    lines=drop_if_zero(lines)
    return '\n'.join(l for l in lines if l)

if __name__=='__main__':
    print(canon(sys.argv[1]))
