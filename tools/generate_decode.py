"""Generate C++ matching/length/reserved-byte tables; never assign encodings."""
import sys
from pathlib import Path
from encoding import tables


def generate():
    maps = tables()
    names = list(maps)
    lines = ['// Generated from doc/ISA_ENCODING.md; do not edit.', '#pragma once',
             '#include <cstdint>', 'namespace vcpu::encoding {',
             'struct Entry { uint8_t length, dispatch, min_revision; uint8_t masks[5], expected[5]; };',
             'inline constexpr Entry tables[5][256] = {']
    for name, entries in maps.items():
        lines.append('{ // ' + name)
        for byte, entry in enumerate(entries):
            length, dispatch, revision = 0, 0, 4
            masks, expected = [0]*5, [0]*5
            if entry:
                length = entry.get('length', 0)
                dispatch = names.index(entry['dispatch']) if 'dispatch' in entry else 0
                revision = 5 if entry['id'] == 'swi' or entry['id'] == 'getx' and byte == 0x34 else 4
                for check in entry.get('encoding_checks', []):
                    masks[check['byte']] = check['mask']
                    expected[check['byte']] = check['equals']
            lines.append('{%d,%d,%d,{%s},{%s}},' % (length, dispatch, revision,
                         ','.join(map(str, masks)), ','.join(map(str, expected))))
        lines.append('},')
    return '\n'.join(lines + ['};', '}']) + '\n'


if __name__ == '__main__':
    Path(sys.argv[1]).write_text(generate())
