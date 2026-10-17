#!/usr/bin/env python3
"""Flat-image assembler/disassembler for explicitly selected ISA 0.4/0.5."""
from __future__ import annotations
import argparse
from dataclasses import dataclass
from pathlib import Path
import re
from encoding import metadata, tables, available, ROOT

META = metadata()
TABLES = tables()
BANKS = META['register_banks']
ENTRIES = {e['id']: (name, e) for name, t in META['tables'].items() for e in t['entries'] if 'dispatch' not in e}
PREFIXES = {e['dispatch']: e['match']['value'] for e in META['tables']['primary']['entries'] if 'dispatch' in e}
BRANCHES = [e['mnemonic'] for e in META['tables']['primary']['entries'] if e.get('semantic', '').startswith('branch_')]


def require(ok, message):
    if not ok:
        raise ValueError(message)


def number(text):
    text = text.strip()
    require(re.fullmatch(r'[+-]?(?:\$[0-9a-fA-F]+|0x[0-9a-fA-F]+|[0-9]+)',text) is not None,'Invalid number: '+text)
    sign = -1 if text.startswith('-') else 1
    text = text.lstrip('+-')
    return sign * int(text[1:], 16) if text.startswith('$') else sign * int(text, 0 if text.startswith('0x') else 10)


@dataclass(frozen=True)
class Instruction:
    text: str
    mnemonic: str
    args: tuple[str, ...]


def parse(source):
    items = []
    for line in source.splitlines():
        line = line.split(';')[0].strip()
        if not line:
            continue
        if ':' in line and re.match(r'^\w+:', line):
            label, line = line.split(':', 1)
            items.append(label)
            line = line.strip()
            if not line:
                continue
        parts = line.split(None, 1)
        items.append(Instruction(line, parts[0].upper(), tuple(a.strip().lower() for a in parts[1].split(',')) if len(parts)>1 else ()))
    return items


def emit(identifier, fields=None, operand=0, revision='0.5'):
    fields = fields or {}
    table, e = ENTRIES[identifier]
    m = e['match']
    data = bytearray(e['length'])
    pos = META['tables'][table]['inspect_byte']
    data[pos] = m.get('value', m.get('values', [0])[0])
    if table != 'primary':
        data[0] = PREFIXES[table]
    for f in e['fields']:
        val = fields[f['name']]
        if isinstance(val, str):
            require(val in BANKS[f['bank']], f"Invalid {f['bank']} register {val}")
            val = BANKS[f['bank']].index(val)
        mask = (1 << f['width'])-1
        require(0 <= val <= mask, 'Field out of range')
        data[f['byte']] = (data[f['byte']] & ~(mask << f['lsb'])) | val << f['lsb']
    for o in e['operands']:
        bits = 8*o['width_bytes']
        low = -(1 << (bits-1)) if o['signed'] or e['semantic']=='move_immediate_zero_extend' else 0
        high = (1 << (bits-1))-1 if o['signed'] else (1 << bits)-1
        require(low <= operand <= high, f'Operand out of range: {operand}')
        data[o['byte']:o['byte']+o['width_bytes']] = (operand & ((1 << bits)-1)).to_bytes(o['width_bytes'], 'little')
    decoded, _ = decode(bytes(data), revision)
    require(decoded['id']==identifier, 'Encoding mismatch')
    return bytes(data)


def encode(ins, pc, labels, revision='0.5'):
    op, args = ins.mnemonic, ins.args
    aliases = {a['mnemonic']: a['expands_to'] for a in META['aliases'] if 'adjustment' not in a and not a.get('same_general_register_operands')}
    op = aliases.get(op, op)
    if op in ('INCP','DECP'):
        require(len(args)==1, 'Pointer alias needs one destination')
        return encode(Instruction(ins.text,'ADDP',('#1' if op=='INCP' else '#-1', args[0])),pc,labels,revision)
    def imm(text):
        require(text.startswith('#'), 'Expected immediate')
        return number(text[1:])
    def target(text):
        return labels[text] if text in labels else number(text)
    if op in ('NOP','HALT','DI','EI','RET','IRET'):
        require(not args, 'Unexpected operand')
        return emit(op.lower(), revision=revision)
    if op in BRANCHES:
        require(len(args)==1, 'Branch needs one target')
        return emit(op.lower(), operand=target(args[0])-pc-3, revision=revision)
    if op in ('ADDP','ADJSP','SWI'):
        require(len(args)==(2 if op=='ADDP' else 1), 'Wrong operand count')
        identifier = 'addp_'+args[1] if op=='ADDP' else op.lower()
        require(identifier in ENTRIES, 'Invalid pointer destination')
        return emit(identifier, operand=imm(args[0]), revision=revision)
    if op in ('PUSH','POP','SHL','SHR','SAR'):
        require(len(args)==1, 'Expected one register')
        return emit(op.lower(), {'source' if op=='PUSH' else 'destination':args[0]}, revision=revision)
    if op in ('CALL','JS','JL'):
        require(len(args)==1, 'Expected one target')
        if op=='JS' and args[0] in BANKS['B1']:
            return emit('js_register', {'target':args[0]}, revision=revision)
        value = target(args[0])
        require(0<=value<=(0xffffff if op=='JL' else 65535), 'Invalid target')
        return emit({'CALL':'call','JS':'js_absolute','JL':'jl'}[op], operand=value, revision=revision)
    if op in ('GETX','SETX'):
        require(len(args)==2, 'Expected context and register')
        context, reg = args if op=='GETX' else args[::-1]
        if op=='SETX':
            require(context in BANKS['context'][:17], 'Context metadata is read-only')
        return emit(op.lower(), {'context':context, 'destination' if op=='GETX' else 'source':reg}, revision=revision)
    if op in ('XOR','AND','OR','ADC','SBC','ADD','SUB','CMP'):
        require(len(args)==2, 'Expected two accumulators')
        return emit(op.lower(), {'source':args[0], 'destination':args[1]}, revision=revision)
    require(op in ('MOV','MOV.W','MOV.B') and len(args)==2, 'Unsupported instruction: '+ins.text)
    src,dst=args
    bits = 8 if op=='MOV.B' else 16
    if src.startswith('#'):
        return emit(f'mov_immediate_{bits}', {'destination':dst}, imm(src), revision)
    if src.startswith('[') or dst.startswith('['):
        store = dst.startswith('[')
        memory, reg = (dst,src) if store else (src,dst)
        require(memory.endswith(']'), 'Invalid memory operand')
        inner=memory[1:-1].replace(' ','')
        pointer = re.fullmatch(r'(?:(ds|cs):)?(r[4-7])([+-].+)?',inner)
        fields={'source' if store else 'destination':reg}
        direction='store' if store else 'load'
        if pointer:
            segment,base,disp=pointer.groups(); value=number(disp) if disp else 0
            fields['base']=base
            identifier=f'mov_b1_{segment or "ds"}_{"disp16" if value else "direct"}_{direction}_{bits}'
        else:
            match=re.fullmatch(r'(dp|sp)([+-].+)?',inner)
            base=match[1] if match else 'absolute'; value=number(match[2] or '0') if match else number(inner)
            identifier=f'mov_{base}_{direction}_{bits}'
        return emit(identifier,fields,value,revision)
    require(op!='MOV.B', 'Register MOV is word only')
    if src==dst and src in BANKS['general']:
        return emit('nop',revision=revision)
    if src.startswith('~'):
        return emit('mov_not',{'source':src[1:],'destination':dst},revision=revision)
    def bank(reg):
        for b in ('B0','B1','B2','B3'):
            if reg in BANKS[b]: return b.lower()
        raise ValueError('Unknown register: '+reg)
    sb,db=bank(src),bank(dst)
    identifier='mov_acc' if sb==db=='b0' else f'mov_{sb}_{db}'
    require(identifier in ENTRIES, 'Transfer needs an explicit accumulator temporary')
    return emit(identifier, {'source':src,'destination':dst}, revision=revision)


def assemble(source, revision='0.5', origin=0x100):
    require(revision in ('0.4','0.5'), 'Unsupported ISA revision')
    items=parse(source); labels={}; pc=origin; positions={}
    for i,item in enumerate(items):
        if isinstance(item,str):
            require(item.lower() not in labels, 'Duplicate label'); labels[item.lower()]=pc
        else:
            positions[i]=pc
            # Label operands have fixed lengths in the frozen ISA.
            if item.mnemonic in BRANCHES: length=3
            elif item.mnemonic in ('CALL','JS','JL') and item.args[0] not in BANKS['B1']:
                length={'CALL':4,'JS':3,'JL':5}[item.mnemonic]
            else: length=len(encode(item,pc,{},revision))
            pc+=length
    require(0<=origin<=65535 and pc<=65536, 'Image crosses code segment')
    return [(positions[i],encode(item,positions[i],labels,revision),item.text) for i,item in enumerate(items) if not isinstance(item,str)]


def decode(data, revision='0.5'):
    require(bool(data), 'Missing opcode')
    byte=data[0]; e=TABLES['primary'][byte]
    require(available(e,byte,revision), 'Illegal opcode')
    if 'dispatch' in e:
        require(len(data)>1, 'Missing extension byte'); byte=data[1]; e=TABLES[e['dispatch']][byte]
        require(available(e,byte,revision), 'Illegal extension')
    for i in range(e['length']):
        require(i<len(data), 'Truncated instruction')
        for c in e.get('encoding_checks',[]):
            if c['byte']==i: require(data[i]&c['mask']==c['equals'], 'Reserved encoding bits')
    values={f['name']:BANKS[f['bank']][(data[f['byte']]>>f['lsb'])&((1<<f['width'])-1)] for f in e['fields']}
    for o in e['operands']:
        values[o['name']]=int.from_bytes(data[o['byte']:o['byte']+o['width_bytes']], 'little', signed=o['signed'])
    return e,values


def disassemble(data, pc=0x100, revision='0.5'):
    e,v=decode(data,revision); name=e['id']; op=e['mnemonic']; args=[]
    def literal(value): return f'${value:X}' if value>=0 else f'-${-value:X}'
    if e['semantic'].startswith('branch_'): args=[literal(pc+e['length']+v['displacement'])]
    elif name.startswith('addp_'): args=['#'+str(v['adjustment']),e['destination']]
    elif name=='adjsp': args=['#'+str(v['adjustment'])]
    elif name=='swi': args=['#'+str(v['service'])]
    elif name=='getx': args=[v['context'],v['destination']]
    elif name=='setx': args=[v['source'],v['context']]
    elif 'address' in e:
        a=e['address']; base=a['base']
        if base=='absolute': mem=literal(v['offset'])
        else:
            disp=v.get('displacement',v.get('offset',0)); b=v['base'] if base=='B1' else base
            mem=(a['segment']+':' if base=='B1' else '')+b+(('+' if disp>=0 else '-')+literal(abs(disp)) if disp else '')
        memory='['+mem+']'; args=[v['source'],memory] if 'source' in v else [memory,v['destination']]
        op+='.'+('B' if e['transfer_bits']==8 else 'W')
    elif name.startswith('mov_immediate'): op+='.'+('B' if e['transfer_bits']==8 else 'W'); args=['#'+literal(v['immediate']),v['destination']]
    elif 'source' in v and 'destination' in v: args=[('~' if name=='mov_not' else '')+v['source'],v['destination']]
    elif 'source' in v: args=[v['source']]
    elif 'destination' in v: args=[v['destination']]
    elif 'target' in v: args=[v['target'] if isinstance(v['target'],str) else literal(v['target'])]
    return op+(' '+','.join(args) if args else ''),e['length']


def main():
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('action',choices=['assemble','disassemble']); p.add_argument('input',type=Path)
    p.add_argument('--revision',required=True,choices=['0.4','0.5']); p.add_argument('--origin',type=lambda x:int(x,0),default=0x100); p.add_argument('-o','--output',type=Path)
    a=p.parse_args()
    if a.action=='assemble':
        listing=assemble(a.input.read_text(),a.revision,a.origin)
        if a.output: a.output.write_bytes(b''.join(row[1] for row in listing))
        else:
            for pc,data,text in listing: print(f'{pc:04X}  {data.hex(" ").upper():<14} {text}')
    else:
        data=a.input.read_bytes(); pos=0; lines=[]
        while pos<len(data):
            text,length=disassemble(data[pos:],a.origin+pos,a.revision); lines.append(text); pos+=length
        result='\n'.join(lines)+'\n'
        if a.output: a.output.write_text(result)
        else: print(result,end='')

if __name__=='__main__': main()
