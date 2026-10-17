"""Assemble the guest kernel integration images using the production toolchain."""
import sys
from pathlib import Path
from isa import assemble, parse

ROOT = Path(__file__).resolve().parents[1]
IMAGES = {'boot': 0x100, 'irq': 0x1000, 'swi': 0x1200, 'memory': 0x1300,
          'copy': 0x1500, 'load_context': 0x1600, 'bootstrap_context': 0x1700, 'user': 0x100,
          'console_probe': 0x1c00, 'console_memory': 0x1300, 'console': 0x8000, 'console_boot': 0x100, 'console_user': 0x100, 'console_peer': 0x100,
          'control_fault': 0x1800, 'control_probe': 0x1c00, 'controls': 0x7000}


def image_symbols(source, rows, origin):
    """Locate labels in production-assembled rows without another encoder."""
    symbols = {}; iterator = iter(rows); pc = origin
    for item in parse(source):
        if isinstance(item, str): symbols[item] = pc
        else:
            address, data, _ = next(iterator)
            assert address == pc
            pc += len(data)
    return symbols


def control_registry(rows, symbols):
    sites = []
    for pc, _, source in rows:
        if not symbols['owned_start'] <= pc < symbols['owned_end']:
            # Pinned finishers may RET toward an unavailable registered operation
            # continuation. Their own fetch/operand failures remain fatal.
            if symbols['finish_publish'] <= pc < symbols['abort_impl'] and source=='RET':
                sites.append((pc,0x1001))
            continue
        flags = 0x10  # A missing instruction byte at this exact registered PC.
        instruction=parse(source)[0]
        if instruction.mnemonic.split('.')[0] in ('CALL','CALLR','RET','JS','BRA','BEQ','BNE','BCC','BCS','BHI','BLS','BGE','BLT','BGT','BLE','BMI','BPL','BVS','BVC'):
            flags |= 0x1000  # Only a registered operation target can recover.
        if '[' in source:
            flags |= 0x20 | (0x100 if instruction.args[0].startswith('[') else 0x200)
        if instruction.mnemonic in ('LDC','STC'):
            flags |= 0x40 | 0x100  # Invalid image/header and READ preflight.
        if source.startswith('SETC '): flags |= 0x40
        if pc in (symbols['commit_entry_configure'], symbols['commit_prepare_user_return']): flags |= 0x8a0
        if pc == symbols['commit_prepare_user_return']: flags |= 0x400
        for name in ('descriptor_install', 'descriptor_remove', 'descriptor_readback', 'map_publish', 'map_invalidate',
                     'entry_configure', 'backing_query', 'uaccess_begin', 'uaccess_end',
                     'prepare_user_return', 'map_acquire', 'map_release', 'uaccess_clear'):
            if symbols['committed_'+name] <= pc < symbols['end_'+name]: flags |= 1
        sites.append((pc, flags))
    data = len(sites).to_bytes(2, 'little')
    data += b''.join(pc.to_bytes(2, 'little')+flags.to_bytes(2, 'little') for pc, flags in sites)
    assert 0xa000+len(data) < 0xc000
    return sites, data


def generate():
    lines = ['// Generated guest images; do not edit.', '#pragma once',
             '#include <cstdint>', '#include <vector>', 'namespace kernel_images {']
    for name, origin in IMAGES.items():
        source = (ROOT / f'examples/kernel_{name}.asm').read_text()
        rows = assemble(source, origin)
        symbols = image_symbols(source, rows, origin)
        image = b''.join(row[1] for row in rows)
        if name in ('console_user', 'console_peer'):
            assert origin+len(image)<=0x200
        if name == 'copy':
            memory=(ROOT/'examples/kernel_memory.asm').read_text()
            assert f'CMPI.W #${symbols["copy_fault"]:04X},r0' in memory
            assert f'MOV.W #${symbols["copy_error"]:04X},r0' in memory
            assert any(pc == symbols['copy_fault'] and source.startswith('MOVU.W') for pc, _, source in rows)
            assert any(pc == symbols['copy_error'] for pc, _, _ in rows)
        lines += [f'inline constexpr uint16_t {name}_origin = {origin};',
                  f'inline const std::vector<uint8_t> {name} = {{' + ','.join(map(str, image)) + '};']
        if name == 'console':
            assert origin+len(image)<0xa000
            assert f'CMPI.W #{symbols["idle"]},r0' in source
            assert f'MOV.W #{symbols["idle_resume"]},r0' in source
            memory=(ROOT/'examples/kernel_console_memory.asm').read_text()
            assert f'CMPI.W #{symbols["copy_fault"]},r0' in memory
            assert f'MOV.W #{symbols["copy_error"]},r0' in memory
            for label, pc in symbols.items():
                lines.append(f'inline constexpr uint16_t console_{label} = {pc};')
        if name == 'copy':
            for label in ('copy_fault', 'copy_error'):
                lines.append(f'inline constexpr uint16_t {label}_pc = {symbols[label]};')
        if name == 'controls':
            assert symbols['control_abort'] == 0x705b and origin+len(image)<0xa000
            for label, pc in symbols.items():
                lines.append(f'inline constexpr uint16_t control_{label} = {pc};')
            sites, registry = control_registry(rows, symbols)
            lines.append('inline const std::vector<uint8_t> control_registry = {'+', '.join(map(str,registry))+'};')
            lines.append('inline const std::vector<uint16_t> control_sites = {'+', '.join(str(pc) for pc, _ in sites)+'};')
    return '\n'.join(lines + ['}']) + '\n'


if __name__ == '__main__':
    Path(sys.argv[1]).write_text(generate())
