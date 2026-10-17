#!/usr/bin/env python3
"""Refresh or verify published byte listings from the current assembly sources."""
import argparse
from pathlib import Path
import re
from isa import ROOT, assemble, parse


def listing(name, origin):
    source=(ROOT/'examples'/name).read_text()
    rows=iter(assemble(source,origin=origin));lines=[]
    for item in parse(source):
        if isinstance(item,str):
            lines.append(' '*21+item+':')
        else:
            pc,data,text=next(rows)
            lines.append(f'{pc:04X}  {data.hex(" ").upper():<14} {text}')
    return '\n'.join(lines)


def update(check=False):
    path=ROOT/'doc/ISA_EXAMPLES.md';old=path.read_text()
    pattern=r'(<!-- EXAMPLE ([\w.]+) (\d+) -->\n```text\n)(.*?)(\n```)'
    count=0
    def replace(match):
        nonlocal count
        count+=1
        return match[1]+listing(match[2],int(match[3]))+match[5]
    new=re.sub(pattern,replace,old,flags=re.S)
    if count!=14:raise ValueError('Expected fourteen published source images')
    if check and new!=old:raise ValueError('Stale published listings; run tools/generate_example_listings.py')
    if not check:path.write_text(new)
    print(f'{count} current ISA listings verified' if check else f'{count} current ISA listings generated')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--check',action='store_true')
    update(parser.parse_args().check)
