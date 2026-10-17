"""Assemble the guest kernel integration images using the production toolchain."""
import sys
from pathlib import Path
from isa import assemble

ROOT = Path(__file__).resolve().parents[1]
IMAGES = {'boot': 0x100, 'irq': 0x1000, 'swi': 0x1200, 'memory': 0x1300,
          'copy': 0x1500, 'load_context': 0x1600, 'user': 0x100}


def generate():
    lines = ['// Generated guest images; do not edit.', '#pragma once',
             '#include <cstdint>', '#include <vector>', 'namespace kernel_images {']
    for name, origin in IMAGES.items():
        rows = assemble((ROOT / f'examples/kernel_{name}.asm').read_text(), 'working', origin)
        image = b''.join(row[1] for row in rows)
        if name == 'copy':
            assert any(pc == 0x150f and source.startswith('MOVU.W') for pc, _, source in rows)
            assert any(pc == 0x1521 for pc, _, _ in rows)
        lines += [f'inline constexpr uint16_t {name}_origin = {origin};',
                  f'inline const std::vector<uint8_t> {name} = {{' + ','.join(map(str, image)) + '};']
    return '\n'.join(lines + ['}']) + '\n'


if __name__ == '__main__':
    Path(sys.argv[1]).write_text(generate())
