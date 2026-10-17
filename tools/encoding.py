"""Read and validate the normative embedded encoding table (stdlib only)."""
import json
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]


def metadata():
    text = (ROOT / 'doc/ISA_ENCODING.md').read_text()
    section = text.split('\n<!-- ISA_ENCODING_JSON_BEGIN -->\n', 1)[1].split('\n<!-- ISA_ENCODING_JSON_END -->', 1)[0]
    result = json.loads(section.split('```json\n', 1)[1].split('\n```', 1)[0])
    if result['isa_revision'] != '0.5' or result['schema_version'] != 1:
        raise ValueError('Unsupported ISA metadata revision/schema')
    return result


def matches(entry, byte):
    m = entry['match']
    return byte in m['values'] if 'values' in m else byte & m['mask'] == m['value']


def tables():
    result = {}
    for name, table in metadata()['tables'].items():
        rows = []
        for byte in range(256):
            entries = [e for e in table['entries'] if matches(e, byte)]
            if len(entries) > 1:
                raise ValueError(f'Overlapping encoding {name}:{byte:02x}')
            rows.append(entries[0] if entries else None)
        result[name] = rows
    return result


def available(entry, byte, revision):
    if revision not in ('0.4', '0.5'):
        raise ValueError(f'Unsupported ISA revision: {revision}')
    return entry and not (revision == '0.4' and (entry['id'] == 'swi' or
                                               entry['id'] == 'getx' and byte == 0x34))
