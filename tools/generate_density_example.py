"""Assemble the current-ISA density example for native execution."""
import sys
from pathlib import Path
from isa import assemble,ROOT
rows=assemble((ROOT/'examples/density.asm').read_text(),0x100)
code=b''.join(b for _,b,_ in rows)
Path(sys.argv[1]).write_text('#pragma once\n#include <vector>\nnamespace density_example { inline const std::vector<unsigned char> code = {'+','.join(map(str,code))+'}; }\n')
