"""Generate C++ matching/length/reserved-byte tables; never assign encodings."""
import sys
from pathlib import Path
from encoding import tables, RUNTIME_LEVELS


def generate():
    maps = tables()
    names = list(maps)
    validators = [tuple(range(256))]
    for table in maps.values():
        for entry in table:
            for check in (entry or {}).get('encoding_checks', []):
                if 'allowed_values' in check and tuple(check['allowed_values']) not in validators:
                    validators.append(tuple(check['allowed_values']))
    lines = ['// Generated from doc/ISA_ENCODING.md; do not edit.', '#pragma once',
             '#include <cstdint>', 'namespace vcpu::encoding {',
             'struct Entry { uint8_t length, dispatch, min_revision; uint8_t masks[5], expected[5], validation[5]; };',
             'inline constexpr uint8_t valid_bytes[][32] = {']
    for values in validators:
        lines.append('{'+','.join(str(sum(1<<(v%8) for v in values if v//8==n)) for n in range(32))+'},')
    lines += ['};', 'inline constexpr Entry tables[%d][256] = {' % len(maps)]
    for name, entries in maps.items():
        lines.append('{ // ' + name)
        for byte, entry in enumerate(entries):
            length, dispatch, revision = 0, 0, 4
            masks, expected = [0]*5, [0]*5
            validation = [0]*5
            if entry:
                length = entry.get('length', 0)
                dispatch = names.index(entry['dispatch']) if 'dispatch' in entry else 0
                revision = 5 if entry['id'] == 'swi' or entry['id'] == 'getx' and byte == 0x34 else 4
                revision = max(revision, RUNTIME_LEVELS[entry.get('minimum_runtime', '0.4')])
                for check in entry.get('encoding_checks', []):
                    if 'allowed_values' in check:
                        validation[check['byte']] = validators.index(tuple(check['allowed_values']))
                    else:
                        masks[check['byte']] = check['mask']
                        expected[check['byte']] = check['equals']
            lines.append('{%d,%d,%d,{%s},{%s},{%s}},' % (length, dispatch, revision,
                         ','.join(map(str, masks)), ','.join(map(str, expected)), ','.join(map(str, validation))))
        lines.append('},')
    return '\n'.join(lines + ['};', '}']) + '\n'


if __name__ == '__main__':
    Path(sys.argv[1]).write_text(generate())
