#!/usr/bin/env python3
"""Repackage only the PSP XMB title; keep the exact C10 executable and media."""
from pathlib import Path
import argparse, struct

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('input',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();b=bytearray(a.input.read_bytes())
    assert b[:4]==b'\x00PBP'
    start,end=struct.unpack_from('<II',b,8);s=bytearray(b[start:end])
    magic,version,keys,data,count=struct.unpack_from('<4s4I',s)
    assert magic==b'\x00PSF'
    for i in range(count):
        e=20+i*16;k,f,n,m,d=struct.unpack_from('<HHIII',s,e)
        if s[keys+k:s.index(0,keys+k)]==b'TITLE':
            v='战争进化史2'.encode()+b'\0';assert f==0x204 and len(v)<=m
            s[data+d:data+d+m]=v.ljust(m,b'\0');struct.pack_into('<I',s,e+4,len(v));break
    else: raise ValueError('TITLE not found')
    b[start:end]=s
    with a.output.open('xb') as out:out.write(b)

if __name__=='__main__':main()
