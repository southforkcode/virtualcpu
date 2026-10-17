"""Build the bootable bulk-transfer example with the production assembler."""
import hashlib
import json
from pathlib import Path
import sys
from isa import ROOT, assemble
from encoding import payload_text
from generate_kernel_fixtures import image_symbols

IMAGES = {'boot': 0x100, 'context': 0x300, 'recover': 0x500, 'load': 0x700}


def generate(destination):
    out = Path(destination)
    out.parent.mkdir(parents=True, exist_ok=True)
    images = {}
    lines = ['// Production-assembled guest code; do not edit.', '#pragma once',
             '#include <cstdint>', '#include <vector>', 'namespace bulk_images {']
    for name, origin in IMAGES.items():
        path = ROOT / f'examples/bulk_{name}.asm'
        source = path.read_text()
        rows = assemble(source, origin)
        code = b''.join(data for _, data, _ in rows)
        symbols = image_symbols(source, rows, origin)
        limit = next((o for o in sorted(IMAGES.values()) if o > origin), 0x1400)
        if origin + len(code) > limit:
            raise ValueError('Overlapping bulk example code: ' + name)
        images[name] = dict(origin=origin, code=list(code), symbols=symbols,
                            source=str(path.relative_to(ROOT)), assembly=source,
                            binary_sha256=hashlib.sha256(code).hexdigest())
        lines += [f'inline constexpr unsigned {name}_origin = {origin};',
                  f'inline const std::vector<uint8_t> {name} = {{' + ','.join(map(str, code)) + '};']
        lines += [f'inline constexpr unsigned {name}_{label} = {pc};' for label, pc in symbols.items()]
    # Cross-image placement is a software ABI. Fail generation if it drifts.
    assert images['load']['symbols']['load_context'] == 0x700
    assert 'CMPI.W #$0700,r0' in images['recover']['assembly']
    assert 'CALLR $0700' in images['context']['assembly']
    paths = [ROOT / images[n]['source'] for n in IMAGES]
    paths += [ROOT / p for p in ('tools/generate_bulk_fixtures.py', 'tools/isa.py',
              'tools/encoding.py', 'tools/generate_kernel_fixtures.py',
              'doc/ISA_ENCODING.md', 'examples/bulk_guest.hpp', 'examples/bulk.cpp',
              'tests/bulk_integration.cpp', 'src/cpu.cpp', 'src/cpu.hpp',
              'src/protected.cpp', 'src/replay.cpp', 'src/memory_transaction.hpp', 'CMakeLists.txt')]
    manifest = dict(isa='working', metadata_sha256=hashlib.sha256(payload_text().encode()).hexdigest(),
                    images=images, sources={str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths})
    text = json.dumps(manifest, indent=2, sort_keys=True) + '\n'
    out.with_suffix('.json').write_text(text)
    lines += ['inline constexpr const char* manifest_sha256 = ' + json.dumps(hashlib.sha256(text.encode()).hexdigest()) + ';', '}']
    out.write_text('\n'.join(lines) + '\n')


if __name__ == '__main__':
    generate(sys.argv[1])
