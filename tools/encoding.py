"""Read and validate the normative embedded encoding table (stdlib only)."""
import json
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]


def payload_text():
    text = (ROOT / 'doc/ISA_ENCODING.md').read_text()
    section = text.split('\n<!-- ISA_ENCODING_JSON_BEGIN -->\n', 1)[1].split('\n<!-- ISA_ENCODING_JSON_END -->', 1)[0]
    return section.split('```json\n', 1)[1].split('\n```', 1)[0]


def metadata():
    return validate(json.loads(payload_text()))


def validate(result):
    if result['isa_revision'] != 'working' or result['schema_version'] != 5:
        raise ValueError('Unsupported ISA metadata revision/schema')
    if result['instruction_bytes'] != {'min': 1, 'max': 5}:
        raise ValueError('Instruction length requires a consumer capacity audit')
    banks, maps, identifiers = result['register_banks'], result['tables'], set()
    if list(maps)[0] != 'primary':
        raise ValueError('Primary decode table must be first')
    for name, table in maps.items():
        inspected = table['inspect_byte']
        if inspected != (0 if name == 'primary' else 1):
            raise ValueError('Unsupported inspected byte')
        for entry in table['entries']:
            if entry['id'] in identifiers:
                raise ValueError('Duplicate encoding identifier')
            identifiers.add(entry['id'])
            match = entry['match']
            if 'values' in match:
                values = match['values']
                if set(match) != {'values'} or not values or len(values) != len(set(values)) or any(type(v) is not int or not 0 <= v < 256 for v in values):
                    raise ValueError('Invalid match values')
            elif set(match) != {'mask', 'value'} or any(type(match[k]) is not int or not 0 <= match[k] < 256 for k in match) or match['value'] & ~match['mask']:
                raise ValueError('Invalid match mask')
            if 'dispatch' in entry:
                if name != 'primary' or entry['dispatch'] not in maps or entry['dispatch'] == 'primary' or 'length' in entry:
                    raise ValueError('Invalid dispatch target')
                continue
            length = entry['length']
            if type(length) is not int or not inspected < length <= 5:
                raise ValueError('Instruction length exceeds decode capacity')
            occupied = set()
            operand_names = set()
            for field in entry['fields']:
                if field['name'] in operand_names:
                    raise ValueError('Duplicate operand name')
                operand_names.add(field['name'])
                byte, lsb, width = field['byte'], field['lsb'], field['width']
                if not (0 <= byte < length and 0 <= lsb < 8 and 1 <= width <= 8-lsb) or field['bank'] not in banks or len(banks[field['bank']]) > 1 << width:
                    raise ValueError('Invalid field bounds/bank')
                bits = {(byte, bit) for bit in range(lsb, lsb+width)}
                if occupied & bits:
                    raise ValueError('Overlapping operand fields')
                occupied |= bits
            for operand in entry.get('bit_operands', []):
                byte, lsb, width = operand['byte'], operand['lsb'], operand['width']
                if (any(type(v) is not int for v in (byte, lsb, width)) or
                        not (inspected < byte < length and 0 <= lsb < 8 and 1 <= width <= 8-lsb) or
                        type(operand['signed']) is not bool or operand['name'] in operand_names):
                    raise ValueError('Invalid packed operand')
                bits = {(byte, bit) for bit in range(lsb, lsb+width)}
                if occupied & bits:
                    raise ValueError('Overlapping operand fields')
                occupied |= bits
                operand_names.add(operand['name'])
            for operand in entry['operands']:
                if operand['name'] in operand_names:
                    raise ValueError('Duplicate operand name')
                operand_names.add(operand['name'])
                byte, width = operand['byte'], operand['width_bytes']
                if not (byte > inspected and 1 <= width <= 4 and byte+width <= length):
                    raise ValueError('Invalid operand bounds')
                bits = {(b, bit) for b in range(byte, byte+width) for bit in range(8)}
                if occupied & bits:
                    raise ValueError('Overlapping operand fields')
                occupied |= bits
            checked = set()
            for check in entry.get('encoding_checks', []):
                byte = check['byte']
                if not inspected < byte < length or byte in checked:
                    raise ValueError('Invalid encoding check byte')
                checked.add(byte)
                if 'allowed_values' in check:
                    values = check['allowed_values']
                    if not values or len(values) != len(set(values)) or any(type(v) is not int or not 0 <= v < 256 for v in values):
                        raise ValueError('Invalid selector values')
                elif not (0 <= check['mask'] <= 255 and 0 <= check['equals'] <= 255) or check['equals'] & ~check['mask']:
                    raise ValueError('Invalid reserved-bit check')
        for byte in range(256):
            if sum(matches(entry, byte) for entry in table['entries']) > 1:
                raise ValueError(f'Overlapping encoding {name}:{byte:02x}')
    targets = [e['dispatch'] for e in maps['primary']['entries'] if 'dispatch' in e]
    if sorted(targets) != sorted(set(maps)-{'primary'}):
        raise ValueError('Extension tables need exactly one dispatch')
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
