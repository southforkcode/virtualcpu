#!/usr/bin/env python3
"""SmallC 0.1: checked near-pointer C-like baseline; production ISA assembler only."""
from __future__ import annotations
import argparse
from dataclasses import dataclass
import hashlib
import json
import re
from pathlib import Path
from isa import assemble, decode, parse

VERSION = '0.1.0'
def integer(text):return int(text,16 if text.startswith('0x') else 10)

SCALARS = {'u8': (1, False), 'i8': (1, True), 'u16': (2, False), 'i16': (2, True), 'void': (0, False)}

@dataclass(frozen=True)
class Type:
    name: str
    element: Type | None = None
    length: int = 0
    space: str = 'ds'
    @property
    def pointer(self): return self.name == 'pointer'
    @property
    def signed(self): return self.name in ('i8', 'i16')
    @property
    def scalar(self): return self.name in SCALARS and self.name != 'void' or self.pointer
    def size(self, structs):
        if self.pointer: return 2
        if self.name == 'array': return self.length * self.element.size(structs)
        if self.name in SCALARS: return SCALARS[self.name][0]
        return structs[self.name]['size']
    def align(self, structs):
        if self.name == 'array': return self.element.align(structs)
        if self.name in structs:return structs[self.name]['align']
        return min(2, self.size(structs)) or 1

U16, I16 = Type('u16'), Type('i16')
def ptr(t, space='ds'): return Type('pointer', t, space=space)
def require(ok, text):
    if not ok: raise ValueError(text)
def align(n, a): return (n+a-1)//a*a

@dataclass
class Node:
    op: str
    args: tuple = ()
    line: int = 0

class Parser:
    def __init__(self, source):
        self.tokens=[]; pos=0; line=1
        pattern=re.compile(r'\s+|//[^\n]*|/\*[\s\S]*?\*/|0x[0-9a-fA-F]+|[0-9]+|[A-Za-z_]\w*|==|!=|<=|>=|&&|\|\||<<|>>|->|[{}()\[\];,.+\-*/&|^~!=<>]')
        while pos<len(source):
            match=pattern.match(source,pos)
            require(match is not None, f'line {line}: unsupported token near {source[pos:pos+12]!r}')
            text=match[0]
            if not text.isspace() and not text.startswith(('//','/*')): self.tokens.append((text,line))
            line+=text.count('\n');pos=match.end()
        self.tokens.append(('<eof>',line));self.i=0;self.structs={}
    def peek(self, n=0): return self.tokens[self.i+n][0]
    def take(self): value=self.peek();self.i+=1;return value
    def eat(self, value):
        if self.peek()==value:self.i+=1;return True
        return False
    def need(self,value): require(self.eat(value),f'line {self.tokens[self.i][1]}: expected {value!r}, found {self.peek()!r}')
    def name(self):
        value=self.take();require(re.fullmatch(r'[A-Za-z_]\w*',value) is not None and value not in SCALARS, 'Expected identifier');return value
    def starts_type(self):return self.peek() in (*SCALARS, 'struct', 'ss', 'ds')
    def type(self):
        space=self.take() if self.peek() in ('ss','ds') else 'ds'
        if self.eat('struct'):
            name='struct '+self.name();require(name in self.structs,'Unknown struct '+name)
        else:name=self.take();require(name in SCALARS,'Unsupported type '+name)
        result=Type(name)
        if self.eat('*'):
            require(name!='void','void pointers are unsupported');result=ptr(result,space)
            require(self.peek()!='*','Pointer-to-pointer declarations are unsupported')
        else:require(space=='ds','Address-space qualifiers require a pointer')
        return result
    def declarator(self, t):
        name=self.name()
        if self.eat('['):
            count=integer(self.take());require(0<count<=4096,'Array size must be 1..4096');self.need(']');t=Type('array',t,count)
        require(t.name!='void','Objects cannot have void type');return name,t
    def initializer(self):
        if not self.eat('{'):return self.expr()
        values=[]
        if not self.eat('}'):
            while True:
                values.append(self.initializer())
                if self.eat('}'):break
                self.need(',')
        return values
    def declaration(self):
        t=self.type();name,t=self.declarator(t);value=self.initializer() if self.eat('=') else None
        self.need(';');return Node('decl',(name,t,value))
    def program(self):
        globals_=[];functions={}
        while self.peek()!='<eof>':
            if self.peek()=='struct' and self.peek(2)=='{':
                self.take();name='struct '+self.name();self.need('{');fields={};offset=0;alignment=1
                while not self.eat('}'):
                    t=self.type();field,t=self.declarator(t);self.need(';');require(field not in fields,'Duplicate field '+field)
                    a=t.align(self.structs);alignment=max(alignment,a);offset=align(offset,a);fields[field]=(t,offset);offset+=t.size(self.structs)
                self.need(';');require(name not in self.structs and fields,'Duplicate/empty struct');self.structs[name]={'fields':fields,'size':align(offset,alignment),'align':alignment};continue
            t=self.type();name=self.name()
            if self.eat('('):
                params=[]
                if not self.eat(')'):
                    while True:
                        pt=self.type();pn,pt=self.declarator(pt);require(pt.scalar,'Aggregate parameters unsupported');params.append((pn,pt))
                        if self.eat(')'):break
                        self.need(',')
                require(name not in ('read','putc','io_status','yield'),'Intrinsic name is reserved');require(name not in functions,'Duplicate function '+name);functions[name]=(t,params,self.statement())
            else:
                if self.eat('['):count=integer(self.take());require(0<count<=4096,'Array size must be 1..4096');self.need(']');t=Type('array',t,count)
                value=self.initializer() if self.eat('=') else None;self.need(';');require(t.name!='void','Void global');globals_.append((name,t,value))
        require(functions,'No functions');return self.structs,globals_,functions
    def statement(self):
        if self.eat('{'):
            stmts=[]
            while not self.eat('}'):require(self.peek()!='<eof>','Unclosed block');stmts.append(self.statement())
            return Node('block',tuple(stmts))
        if self.eat('return'):
            value=Node('num',(0,)) if self.peek()==';' else self.expr();self.need(';');return Node('return',(value,))
        if self.eat('if'):
            self.need('(');cond=self.expr();self.need(')');yes=self.statement();no=self.statement() if self.eat('else') else Node('block');return Node('if',(cond,yes,no))
        if self.eat('while'):
            self.need('(');cond=self.expr();self.need(')');return Node('while',(cond,self.statement()))
        if self.eat('for'):
            self.need('(')
            init=self.declaration() if self.starts_type() else Node('expr',(self.expr(),))
            if init.op!='decl':self.need(';')
            cond=self.expr();self.need(';');step=self.expr();self.need(')');body=self.statement()
            return Node('block',(init,Node('while',(cond,Node('block',(body,Node('expr',(step,))))))))
        if self.starts_type():return self.declaration()
        if self.eat(';'):return Node('block')
        e=self.expr();self.need(';');return Node('expr',(e,))
    PRECEDENCE={'=':1,'||':2,'&&':3,'|':4,'^':5,'&':6,'==':7,'!=':7,'<':8,'>':8,'<=':8,'>=':8,'<<':9,'>>':9,'+':10,'-':10,'*':11}
    def expr(self, minimum=1):
        lhs=self.unary()
        while self.peek() in self.PRECEDENCE and self.PRECEDENCE[self.peek()]>=minimum:
            op=self.take();rhs=self.expr(self.PRECEDENCE[op]+(0 if op=='=' else 1));lhs=Node(op,(lhs,rhs))
        return lhs
    def unary(self):
        if self.peek() in ('-','+','!','~','&','*'):op=self.take();return Node('unary'+op,(self.unary(),))
        if self.eat('sizeof'):
            self.need('(');t=self.type();self.need(')');return Node('sizeof',(t,))
        if self.peek()=='(' and self.peek(1) in (*SCALARS,'struct','ss','ds'):
            self.take();t=self.type();self.need(')');return Node('cast',(t,self.unary()))
        if self.eat('('):e=self.expr();self.need(')')
        elif re.fullmatch(r'0x[0-9a-fA-F]+|[0-9]+',self.peek()):e=Node('num',(integer(self.take()),));require(e.args[0]<=65535,'Literal exceeds 16 bits')
        else:e=Node('name',(self.name(),))
        while True:
            if self.eat('['):index=self.expr();self.need(']');e=Node('index',(e,index))
            elif self.peek() in ('.','->'):op=self.take();e=Node(op,(e,self.name()))
            elif self.eat('('):
                args=[]
                if not self.eat(')'):
                    while True:
                        args.append(self.expr())
                        if self.eat(')'):break
                        self.need(',')
                e=Node('call',(e,tuple(args)))
            else:break
        return e

@dataclass
class Variable:
    type: Type
    space: str
    offset: int
    local: bool=False
@dataclass
class Place:
    type: Type
    space: str
    offset: int=0
    local: bool=False
    dynamic: bool=False

class Compiler:
    def __init__(self, source, origin=0x100, library=False):
        self.source=source;self.structs,self.globals,self.functions=Parser(source).program();self.origin=origin;self.library=library
        self.lines=[];self.annotations=[];self.labels=0;self.vars={};self.data=bytearray();self.global_vars={};self.depth=0;self.context_kind=None
        self.current='';self.frames={};self.failures={}
        cursor=0x2000
        for name,t,init in self.globals:
            require(name not in ('read','putc','io_status','yield') and name not in self.global_vars and name not in self.functions,'Duplicate/reserved global '+name)
            cursor=align(cursor,t.align(self.structs));self.global_vars[name]=Variable(t,'ds',cursor)
            while len(self.data)<cursor-0x2000:self.data.append(0)
            self.data+=self.initial_data(t,init);cursor+=t.size(self.structs)
        require(cursor<=0x2e00,'Global data exceeds pinned $2000..$2DFF region')
    def function_label(self,name):return 'sc_fn_'+name.lower()+'_'+name.encode().hex()
    def fresh(self, prefix='l'):self.labels+=1;return f'sc_{prefix}_{self.labels}'
    def label(self, name):self.lines.append(name+':')
    def emit(self, text, kind='body'):
        self.lines.append(text);self.annotations.append({'kind':kind if kind!='body' else self.context_kind or kind,'function':self.current})
    def push(self, reg='r0', kind='spill'):self.emit('PUSH '+reg,kind);self.depth+=2
    def pop(self, reg='r1', kind='reload'):self.emit('POP '+reg,kind);self.depth-=2;require(self.depth>=0,'Internal temporary imbalance')
    def guard(self, branch, code):
        label=self.failures.setdefault(code,f'sc_failure_{code}');self.emit(branch+' '+label,'guard')
    def constant(self, node):
        if node is None:return 0
        def narrow(value,t):
            require(t.scalar,'Constant requires scalar type')
            bits=8*t.size(self.structs);value&=(1<<bits)-1
            return value-(1<<bits) if t.signed and value&(1<<(bits-1)) else value
        if node.op=='num':return node.args[0]
        if node.op=='sizeof':return node.args[0].size(self.structs)
        if node.op=='cast':
            target,child=node.args;source=self.type_of(child)
            require(not(source.pointer and target.pointer) or source.space==target.space,'Cross-segment pointer cast rejected')
            return narrow(self.constant(child),target)
        unary={'unary-':lambda x:-x,'unary+':lambda x:x,'unary~':lambda x:~x,'unary!':lambda x:int(not x)}
        if node.op in unary:return narrow(unary[node.op](self.constant(node.args[0])),self.type_of(node))
        require(node.op in ('+','-','*','<<','>>','|','&','^'),'Expected constant expression')
        require(not any(self.type_of(child).pointer for child in node.args),'Static pointer arithmetic is unsupported; form checked pointers at runtime')
        a,b=map(self.constant,node.args)
        if node.op in ('<<','>>'):require(0<=b<=16,'Shift count must be a constant 0..16')
        value={'+':lambda:a+b,'-':lambda:a-b,'*':lambda:a*b,'<<':lambda:a<<b,'>>':lambda:a>>b,'|':lambda:a|b,'&':lambda:a&b,'^':lambda:a^b}[node.op]()
        return narrow(value,self.type_of(node))
    def initial_data(self,t,init):
        if t.scalar:
            require(not isinstance(init,list),'Scalar cannot have aggregate initializer')
            if init is not None:
                source=self.type_of(init);require(source.scalar,'Non-scalar constant initializer')
                if source.pointer or t.pointer:require(source==t or t.pointer and init.op=='num' and init.args[0]==0,'Pointer initializer needs explicit compatible cast')
                if t.pointer:require(init.op=='cast' and init.args[0]==t or init.op=='num' and init.args[0]==0,'Static pointer initializer requires null or explicit cast')
            value=self.constant(init)&((1<<(8*t.size(self.structs)))-1);return value.to_bytes(t.size(self.structs),'little')
        require(init is None or isinstance(init,list),'Aggregate needs braces');values=init or [];out=bytearray(t.size(self.structs))
        parts=[(t.element,i*t.element.size(self.structs)) for i in range(t.length)] if t.name=='array' else list(self.structs[t.name]['fields'].values())
        require(len(values)<=len(parts),'Too many initializers')
        for i,(pt,offset) in enumerate(parts):out[offset:offset+pt.size(self.structs)]=self.initial_data(pt,values[i] if i<len(values) else None)
        return out
    def type_of(self,n,decay=True):
        op,a=n.op,n.args
        if op=='name':
            require(a[0] in self.vars,'Unknown object '+a[0]);v=self.vars[a[0]];t=v.type
            return ptr(t.element,v.space) if decay and t.name=='array' else t
        if op=='num':return I16 if a[0]<32768 else U16
        if op=='sizeof':return U16
        if op=='cast':return a[0]
        if op=='unary&':
            t=self.type_of(a[0],False);return ptr(t,self.space_of(a[0]))
        if op in ('unary*','index'):
            t=self.type_of(a[0]);require(t.pointer,'Dereference/index needs a pointer');r=t.element
            return ptr(r.element,t.space) if decay and r.name=='array' else r
        if op in ('.','->'):
            t=self.type_of(a[0],False)
            if op=='->':require(t.pointer,'Arrow needs pointer');t=t.element
            require(t.name in self.structs and a[1] in self.structs[t.name]['fields'],'Unknown struct field '+a[1]);ft=self.structs[t.name]['fields'][a[1]][0]
            return ptr(ft.element,self.space_of(n)) if decay and ft.name=='array' else ft
        if op=='call':
            require(a[0].op=='name','Indirect calls unsupported');name=a[0].args[0]
            if name in ('read','putc','io_status','yield'):return U16
            require(name in self.functions,'Unknown function '+name);return self.functions[name][0]
        if op=='=':return self.type_of(a[0])
        if op in ('unary+','unary-','unary~'):
            st=self.type_of(a[0]);require(st.scalar and not st.pointer,'Unary arithmetic needs an integer');return U16 if st==U16 else I16
        if op in ('unary!','==','!=','<','>','<=','>=','&&','||'):return I16
        l,r=map(self.type_of,a)
        if op in ('+','-') and l.pointer:return I16 if r.pointer else l
        require(l.scalar and r.scalar and not l.pointer and not r.pointer,'Unsupported pointer/aggregate arithmetic')
        return U16 if l==U16 or r==U16 else I16
    def space_of(self,n):
        if n.op=='name':return self.vars[n.args[0]].space
        if n.op in ('index','unary*','->'):return self.type_of(n.args[0]).space
        if n.op=='.':return self.space_of(n.args[0])
        raise ValueError('Not an addressable object')
    def convert(self, source, target, n=None):
        require(target.scalar and source.scalar,'Aggregate/void value copies unsupported')
        if source.pointer or target.pointer:
            require(source==target or target.pointer and n is not None and n.op=='num' and n.args[0]==0,'Pointer types/address spaces need an explicit compatible cast')
        self.normalize(target)
    def normalize(self,t):
        if t.name in ('u8','i8'):
            self.emit('ANDI.B #255,r0')
            if t.signed:
                done=self.fresh('sign');self.emit('CMPI.W #128,r0');self.emit('BCC '+done);self.emit('ORI.W #65280,r0');self.label(done)
    def pointer_offset(self,base,index,subtract=False,bound=None):
        pt=self.type_of(base);it=self.type_of(index);require(pt.pointer and it.scalar and not it.pointer,'Pointer offset needs an integer')
        old=self.context_kind;self.context_kind='address';self.expression(base);self.push();self.expression(index,checked=True);self.pop('r1')
        if it.signed:self.emit('TST.W r0','guard');self.guard('BMI',2)
        if bound is not None:self.emit(f'CMPI.W #{bound},r0','guard');self.guard('BCS',1)
        size=pt.element.size(self.structs)
        if size>1:
            self.emit(f'CMPI.W #{65535//size},r0','guard');self.guard('BHI',3)
            if size&(size-1)==0:self.emit(f'SHLI.W #{size.bit_length()-1},r0','address')
            else:
                self.emit('MOV r0,r2','address');self.emit('MOV.W #0,r0','address');self.emit(f'MOV.W #{size},r3','address');loop=self.fresh('scale');self.label(loop)
                self.emit('ADD r2,r0','address');self.emit('SUBI.W #1,r3','address');self.emit('BNE '+loop,'address')
        if subtract:self.emit('SUB r0,r1','address');self.guard('BCC',4);self.emit('MOV r1,r0','address')
        else:self.emit('ADD r1,r0','address');self.guard('BCS',4)
        self.context_kind=old
    def width_guard(self,t):
        size=t.size(self.structs)
        if size>1:self.emit(f'CMPI.W #{65536-size},r0','guard');self.guard('BHI',5)
    def place(self,n):
        op,a=n.op,n.args
        if op=='name':v=self.vars[a[0]];return Place(v.type,v.space,v.offset,v.local)
        if op in ('.','->'):
            p=self.place(a[0]) if op=='.' else self.place(Node('unary*',(a[0],)))
            require(p.type.name in self.structs,'Member requires struct');t,offset=self.structs[p.type.name]['fields'][a[1]]
            return Place(t,p.space,p.offset+offset,p.local,p.dynamic)
        if op=='unary*':
            t=self.type_of(a[0]);require(t.pointer,'Dereference needs pointer');self.expression(a[0]);self.width_guard(t.element);self.emit('MOV r0,r4','address');return Place(t.element,t.space,dynamic=True)
        if op=='index':
            bt=self.type_of(a[0],False);pt=self.type_of(a[0]);self.pointer_offset(a[0],a[1],bound=bt.length if bt.name=='array' else None)
            self.width_guard(pt.element);self.emit('MOV r0,r4','address');return Place(pt.element,pt.space,dynamic=True)
        raise ValueError('Not an lvalue: '+op)
    def address(self,p):
        if p.dynamic:self.emit('MOV r4,r0','address')
        elif p.local:self.emit('MOV r7,r0','address')
        else:self.emit(f'MOV.W #{p.offset},r0','address');return
        if p.offset:self.emit(f'ADDI.W #{p.offset},r0','address')
    def memory(self,p,store=False):
        suffix='B' if p.type.size(self.structs)==1 else 'W'
        if p.dynamic:
            if p.space=='ss':self.emit('MOV ds,r3');self.emit('MOV ss,r2');self.emit('MOV r2,ds')
            mem='[r4'+(f'+{p.offset}' if p.offset else '')+']'
        else:mem=f'[sp+{p.offset+self.depth}]' if p.local else f'[{p.offset}]'
        self.emit(f'MOV.{suffix} '+(f'r0,{mem}' if store else f'{mem},r0'))
        if p.dynamic and p.space=='ss':self.emit('MOV r3,ds')
        if not store:self.normalize(p.type)
    def expression(self,n,checked=False):
        op,a=n.op,n.args;t=self.type_of(n)
        if op in ('num','sizeof'):self.emit(f'MOV.W #{a[0] if op=="num" else a[0].size(self.structs)},r0');return t
        if op in ('name','index','.','->','unary*'):
            p=self.place(n)
            if p.type.name=='array':self.address(p)
            else:require(p.type.scalar,'Aggregate values unsupported');self.memory(p)
            return t
        if op=='unary&':self.address(self.place(a[0]));return t
        if op=='cast':
            st=self.expression(a[1],checked)
            require(not (st.pointer and t.pointer) or st.space==t.space,'Cross-segment pointer cast rejected')
            require(st.scalar and t.scalar,'Invalid cast');self.normalize(t);return t
        if op=='=':
            target=self.type_of(a[0],False);st=self.expression(a[1],checked);self.convert(st,target,a[1]);self.push();p=self.place(a[0]);self.pop('r0');self.memory(p,True);return target
        if op=='call':return self.call(a[0].args[0],a[1])
        if op in ('&&','||'):
            short=self.fresh('short');end=self.fresh('bool');self.expression(a[0],checked);self.emit('TST.W r0');self.emit(('BEQ ' if op=='&&' else 'BNE ')+short)
            self.expression(a[1],checked);self.emit('TST.W r0');self.emit(('BEQ ' if op=='&&' else 'BNE ')+short)
            self.emit(f'MOV.W #{1 if op=="&&" else 0},r0');self.emit('BRA '+end);self.label(short);self.emit(f'MOV.W #{0 if op=="&&" else 1},r0');self.label(end);return t
        if op.startswith('unary'):
            self.expression(a[0],checked)
            if op=='unary-':self.emit('MOV.W #0,r1');self.emit('SUB r0,r1');self.emit('MOV r1,r0')
            elif op=='unary~':self.emit('MOV ~r0,r0')
            elif op=='unary!':
                yes=self.fresh('not');end=self.fresh('notend');self.emit('TST.W r0');self.emit('BEQ '+yes);self.emit('MOV.W #0,r0');self.emit('BRA '+end);self.label(yes);self.emit('MOV.W #1,r0');self.label(end)
            return t
        lt,rt=map(self.type_of,a)
        if lt.pointer and op in ('+','-') and not rt.pointer:self.pointer_offset(a[0],a[1],op=='-');return t
        if lt.pointer and rt.pointer and op=='-':
            require(lt==rt,'Pointer difference needs matching address space/type');size=lt.element.size(self.structs);require(size&(size-1)==0,'Pointer difference requires power-of-two element size')
            self.expression(a[0]);self.push();self.expression(a[1]);self.pop('r1');negative=self.fresh('negative');end=self.fresh('difference')
            self.emit('CMP r0,r1','address');self.emit('BCC '+negative,'address');self.emit('SUB r0,r1','address');self.emit('MOV r1,r0','address');self.emit('CMPI.W #32767,r0','guard');self.guard('BHI',6);self.emit('BRA '+end)
            self.label(negative);self.emit('SUB r1,r0','address');self.emit('CMPI.W #32768,r0','guard');self.guard('BHI',6);self.emit('MOV.W #0,r1');self.emit('SUB r0,r1','address');self.emit('MOV r1,r0','address');self.label(end)
            if size>1:self.emit('MOV r0,r1');self.emit(f'ANDI.W #{size-1},r1','guard');self.guard('BNE',6);self.emit(f'SARI.W #{size.bit_length()-1},r0','address')
            return t
        require(not lt.pointer and not rt.pointer or op in ('==','!=','<','>','<=','>=' ) and lt==rt,'Incompatible pointer operation')
        if op in ('<<','>>'):
            count=self.constant(a[1]);require(0<=count<=16,'Shift count must be a constant 0..16');self.expression(a[0],checked)
            if checked and op=='<<' and count:self.emit(f'CMPI.W #{65535>>count},r0','guard');self.guard('BHI',3)
            self.emit(f'{"SHLI" if op=="<<" else "SARI" if lt.signed else "SHRI"}.W #{count},r0');return t
        self.expression(a[0],checked);self.push();self.expression(a[1],checked);self.pop('r1')
        if op in ('==','!=','<','>','<=','>='):
            self.emit('CMP r0,r1');yes=self.fresh('true');end=self.fresh('compare');signed=not(lt==U16 or rt==U16 or lt.pointer)
            branch={'==':'BEQ','!=':'BNE','<':'BLT' if signed else 'BCC','>':'BGT' if signed else 'BHI','<=':'BLE' if signed else 'BLS','>=':'BGE' if signed else 'BCS'}[op]
            self.emit(branch+' '+yes);self.emit('MOV.W #0,r0');self.emit('BRA '+end);self.label(yes);self.emit('MOV.W #1,r0');self.label(end);return t
        if op=='*':
            loop=self.fresh('multiply');end=self.fresh('mulend')
            if checked:
                self.emit('MOV r0,r2','address');self.emit('MOV.W #0,r0','address');self.label(loop);self.emit('TST.W r2','guard');self.emit('BEQ '+end,'guard');self.emit('ADD r1,r0','address');self.guard('BCS',3);self.emit('SUBI.W #1,r2','address');self.emit('BRA '+loop,'address')
            else:
                skip=self.fresh('mulskip');self.emit('MOV.W #0,r3');self.emit('MOV.W #16,r2');self.label(loop);self.emit('SHRI.W #1,r0');self.emit('BCC '+skip);self.emit('ADD r1,r3');self.label(skip);self.emit('SHLI.W #1,r1');self.emit('SUBI.W #1,r2');self.emit('BNE '+loop);self.emit('MOV r3,r0')
            self.label(end);return t
        require(op in ('+','-','&','|','^'),'Unsupported binary '+op);self.emit({'+' :'ADD','-':'SUB','&':'AND','|':'OR','^':'XOR'}[op]+' r0,r1')
        if checked and op in ('+','-'):self.guard('BCS' if op=='+' else 'BCC',3)
        self.emit('MOV r1,r0');return t
    def call(self,name,args):
        if name in ('read','putc','io_status','yield'):
            require(not self.library,'User I/O intrinsics are unavailable in library images')
            if name=='read':
                require(len(args)==2 and self.type_of(args[0])==ptr(Type('u8')),'read needs a DS u8 pointer and count')
                require(self.type_of(args[1]).scalar and not self.type_of(args[1]).pointer,'read count must be integer')
                self.expression(args[0]);self.push();self.expression(args[1]);self.pop('r1');self.emit('MOV r1,r4');self.emit('MOV.W #3,r1');self.emit('SWI #2','call');self.emit('MOV.W r1,[12034]')
            elif name=='putc':require(len(args)==1,'putc needs one argument');self.expression(args[0]);self.emit('ANDI.B #255,r0');self.emit('SWI #4','call')
            elif name=='yield':require(not args,'yield has no arguments');self.emit('SWI #5','call');self.emit('MOV.W #0,r0')
            else:require(not args,'io_status has no arguments');self.emit('MOV.W [12034],r0')
            return U16
        result,params,_=self.functions[name];require(len(args)==len(params),'Wrong argument count for '+name)
        for arg,(_,pt) in reversed(list(zip(args,params))):st=self.expression(arg);self.convert(st,pt,arg);self.push(kind='call')
        self.emit('CALL '+self.function_label(name),'call')
        if args:self.emit(f'ADJSP #{2*len(args)}','call');self.depth-=2*len(args)
        return result
    def declarations(self,n):
        if n.op=='decl':yield n.args
        elif n.op=='block':
            for x in n.args:yield from self.declarations(x)
        elif n.op=='if':yield from self.declarations(n.args[1]);yield from self.declarations(n.args[2])
        elif n.op=='while':yield from self.declarations(n.args[1])
    def initialize_local(self,p,init):
        if p.type.scalar:
            st=self.expression(init or Node('num',(0,)));self.convert(st,p.type,init);self.memory(p,True);return
        require(isinstance(init,list),'Aggregate initializer needs braces');parts=[(p.type.element,i*p.type.element.size(self.structs)) for i in range(p.type.length)] if p.type.name=='array' else list(self.structs[p.type.name]['fields'].values())
        require(len(init)<=len(parts),'Too many local initializers')
        for i,(t,ofs) in enumerate(parts):
            sub=init[i] if i<len(init) else Node('num',(0,)) if t.scalar else []
            self.initialize_local(Place(t,p.space,p.offset+ofs,p.local),sub)
    def statement(self,n):
        op,a=n.op,n.args
        if op=='block':
            for stmt in a:self.statement(stmt)
        elif op=='decl':
            if a[2] is not None:v=self.vars[a[0]];self.initialize_local(Place(v.type,v.space,v.offset,True),a[2])
        elif op=='expr':self.expression(a[0])
        elif op=='return':
            st=self.expression(a[0]);rt=self.functions[self.current][0]
            if rt.name!='void':self.convert(st,rt,a[0])
            self.emit('BRA '+self.exit)
        elif op=='if':
            no=self.fresh('else');end=self.fresh('endif');self.expression(a[0]);self.emit('TST.W r0');self.emit('BEQ '+no);self.statement(a[1]);self.emit('BRA '+end);self.label(no);self.statement(a[2]);self.label(end)
        elif op=='while':
            loop=self.fresh('loop');end=self.fresh('loopend');self.label(loop);self.expression(a[0]);self.emit('TST.W r0');self.emit('BEQ '+end);self.statement(a[1]);self.emit('BRA '+loop);self.label(end)
        require(self.depth==0,'Temporary leaked across statement')
    def compile(self):
        if not self.library:
            require('main' in self.functions and not self.functions['main'][1],'main() required')
            self.emit('CALL '+self.function_label('main'),'call');self.emit('MOV.W r0,[12032]');self.emit('SWI #1','call');self.emit('HALT')
        for name,(result,params,body) in self.functions.items():
            require(result.scalar,'Aggregate/void returns unsupported');require(not(result.pointer and result.space=='ss'),'Returning SS pointers is unsupported');require(body.op=='block','Function body required')
            self.current=name;self.vars=dict(self.global_vars);size=0
            for vn,t,_ in self.declarations(body):
                require(vn not in self.vars and vn not in self.functions and vn not in ('read','putc','io_status','yield') and vn not in [p[0] for p in params],'Shadowing/duplicate local '+vn)
                size=align(size,t.align(self.structs));self.vars[vn]=Variable(t,'ss',size,True);size+=t.size(self.structs)
            size=align(size,2);require(size<=1024,'Local frame exceeds 1024 bytes');self.frames[name]=size
            for i,(pn,pt) in enumerate(params):require(pn not in self.vars and pn not in self.functions and pn not in ('read','putc','io_status','yield'),'Duplicate/reserved parameter '+pn);self.vars[pn]=Variable(pt,'ss',size+10+2*i,True)
            self.exit=self.fresh('return');self.label(self.function_label(name))
            for r in ('r4','r5','r6','r7'):self.emit('PUSH '+r,'prologue')
            if size:self.emit(f'ADJSP #{-size}','prologue')
            self.emit('MOV sp,r0','prologue');self.emit('MOV r0,r7','prologue');self.statement(body)
            self.emit('MOV.W #0,r0');self.label(self.exit)
            if size:self.emit(f'ADJSP #{size}','epilogue')
            for r in ('r7','r6','r5','r4'):self.emit('POP '+r,'epilogue')
            self.emit('RET','epilogue')
        self.current='guard_runtime'
        for code,label in self.failures.items():
            self.label(label);self.emit(f'MOV.W #{code},r0','guard');self.emit('MOV.W r0,[12036]','guard');self.emit('SWI #255','guard');self.emit('HALT','guard')
        asm='\n'.join(self.lines)+'\n';rows=assemble(asm,self.origin);require(self.origin+sum(len(b) for _,b,_ in rows)<(0xc000 if self.library else 0x4000),'Code exceeds pinned executable region')
        require(len(rows)==len(self.annotations),'Instruction annotation count mismatch')
        labels={};cursor=self.origin;row_iter=iter(rows)
        for item in parse(asm):
            if isinstance(item,str):labels[item]=cursor
            else:
                pc,code,_=next(row_iter);require(pc==cursor,'Assembler layout mismatch');cursor+=len(code)
        instructions=[]
        for (pc,code,text),note in zip(rows,self.annotations):
            entry,values=decode(code);d=dict(note,pc=pc,bytes=len(code),text=text,family=entry['semantic'],identifier=entry['id'],bank_move=entry['id'] in ('mov_b0_b1','mov_b1_b0'))
            if entry['id'].startswith('mov_b1_') and entry.get('address',{}).get('base')=='B1':d.update(addressing='b1',displacement=0)
            d['address_alu']=note['kind']=='address' and entry['semantic'] in ('add','sub','and','or','xor','alu_immediate','shift_rotate_immediate')
            for operand in entry['operands']+entry.get('bit_operands',[]):
                if operand['name'] in ('displacement','offset'):
                    d['displacement']=values[operand['name']];d['addressing']='sp' if entry['id'].startswith('mov_sp') else 'b1' if 'b1_' in entry['id'] else 'other'
                if entry['semantic'].startswith('branch_'):d['branch_distance']=values['displacement']
            instructions.append(d)
        return {'compiler':VERSION,'flags':['--baseline'],'origin':self.origin,'assembly':asm,'code':list(b''.join(b for _,b,_ in rows)),'data':list(self.data),'instructions':instructions,'frames':self.frames,
                'entrypoints':{name:labels[self.function_label(name)] for name in self.functions},'globals':{n:{'offset':v.offset,'size':v.type.size(self.structs),'type':v.type.name} for n,v in self.global_vars.items()},'source_sha256':hashlib.sha256(self.source.encode()).hexdigest()}

def compile_source(source,origin=0x100,library=False):
    return Compiler(source,origin,library).compile()

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--version',action='version',version='SmallC '+VERSION);parser.add_argument('--baseline',action='store_true',help='Explicitly select the default stack-allocation baseline');parser.add_argument('source',type=Path);parser.add_argument('--assembly',type=Path,required=True);parser.add_argument('--artifact',type=Path);parser.add_argument('--origin',type=lambda x:int(x,0),default=0x100);parser.add_argument('--library',action='store_true');args=parser.parse_args()
    try:artifact=compile_source(args.source.read_text(),args.origin,args.library)
    except (ValueError,KeyError) as error:parser.exit(1,f'{args.source}: {error}\n')
    args.assembly.write_text(artifact['assembly'])
    if args.artifact:args.artifact.write_text(json.dumps(artifact,indent=2)+'\n')
