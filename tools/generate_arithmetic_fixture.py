"""Assemble the published arithmetic example for native execution tests."""
from pathlib import Path
import sys
from isa import assemble,ROOT

out=Path(sys.argv[1])
source=ROOT/'examples/arithmetic.asm'
code=b''.join(b for _,b,_ in assemble(source.read_text(),0x100))
handler=b''.join(b for _,b,_ in assemble((ROOT/'examples/arithmetic_fault.asm').read_text(),0x300))
out.write_text('#pragma once\n#include <vector>\nnamespace arithmetic_fixture {\ninline const std::vector<unsigned char> code = {'+','.join(map(str,code))+'};\ninline const std::vector<unsigned char> retry = {'+','.join(map(str,handler))+'};\n}\n')
