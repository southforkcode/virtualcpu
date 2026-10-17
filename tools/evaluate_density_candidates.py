#!/usr/bin/env python3
"""Census current instructions and cost unassigned density candidates; never execute them."""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
from encoding import metadata, payload_text
from measure_density import report
ROOT=Path(__file__).resolve().parents[1]


def groups(sites):
    result=defaultdict(list)
    for s in sites:result[s['image']].append(s)
    return [sorted(ss,key=lambda s:s['pc']) for ss in result.values()]

def contiguous(ss):return all(a['pc']+a['bytes']==b['pc'] for a,b in zip(ss,ss[1:]))
def norm(s):return s['text'].upper().replace(' ','')
def occurrence(ss,**extra):
    return dict(image=ss[0]['image'],source=ss[0]['source'],pc=ss[0]['pc'],label=ss[0].get('label',''),
        instructions=[s['text'] for s in ss],code_bytes=sum(s['bytes'] for s in ss),
        retired=sum(s['retired'] for s in ss),complete_executions=min(s['retired'] for s in ss),
        ram_read_bytes=sum(s['ram_read_bytes'] for s in ss),ram_write_bytes=sum(s['ram_write_bytes'] for s in ss),**extra)


def scan(sites):
    out=defaultdict(list)
    for ss in groups(sites):
        pcs={s['pc'] for s in ss}
        for n,s in enumerate(ss):
            identifier=s['identifier'];v=s['values']
            if identifier in ('addi_16','subi_16') and v['immediate']==1:out['word_unit'].append(occurrence([s],candidate_bytes=2,incremental_bytes_saved=1,retirements_saved=0))
            if identifier in ('addi_8','subi_8') and v['immediate']==1:out['byte_unit'].append(occurrence([s],incremental_bytes_saved=0,retirements_saved=0))
            if identifier in ('getc','setc') and (v.get('selector',999)<3 or 0x10<=v.get('selector',999)<=0x14):
                out['control'].append(occurrence([s],selector=v['selector'],direction=identifier))
            if identifier=='call':
                target=v['target'];d8=target-(s['pc']+2);d16=target-(s['pc']+3)
                out['near_call'].append(occurrence([s],target=target,rel8_current_layout_fit=-128<=d8<=127,
                    rel16_current_layout_fit=-32768<=d16<=32767,
                    candidate_bytes=2 if -128<=d8<=127 else 3 if -32768<=d16<=32767 else 4,
                    internal_label_target=target in pcs))
            if identifier=='js_register':out['indirect_jump'].append(occurrence([s]))
            if identifier in ('push','pop') and 'register_save_restore' in s['tags']:out['register_save_restore'].append(occurrence([s]))
            if identifier.startswith('roli_') and v.get('count')==8:out['byte_swap_existing'].append(occurrence([s]))
            # Structural counts are not compiler legality proofs. Label entries,
            # aliasing, RAM backing, dead scratch/flags and fault maps are separate gates.
            pair=ss[n:n+2]
            if len(pair)==2 and contiguous(pair):
                a,b=pair
                if a['identifier']=='mov_immediate_16' and a['values']['immediate']==5 and b['identifier']=='setc' and b['values']['selector']==127 and a['values']['destination']==b['values']['source']:
                    out['entry_commit_command'].append(occurrence(pair))
                if a['identifier'].startswith('mov_b1_ds_direct_') and a['values'].get('displacement')==0 and b['identifier'].startswith('addp_'):
                    base=a['values']['base'];width=1 if a['identifier'].endswith('_8') else 2
                    value=a['values'].get('destination',a['values'].get('source'))
                    if b['identifier']=='addp_'+base and b['values']['adjustment']==width:
                        out['post_increment'].append(occurrence(pair,base=base,value=value,width=width,b0_value=value in ('r0','r1','r2','r3'),candidate_bytes=3,
                            incremental_bytes_saved=a['bytes']+b['bytes']-3,retirements_saved=min(a['retired'],b['retired'])))
                if a['identifier']=='mov_immediate_16' and b['semantic']=='store_memory' and a['values']['destination']==b['values'].get('source'):
                    out['memory_immediate'].append(occurrence(pair,constant=a['values']['immediate'],scratch=a['values']['destination']))
                if a['identifier']=='shli_16' and b['identifier']=='sari_16' and a['values'].get('count')==b['values'].get('count')==8 and a['values']['destination']==b['values']['destination']:
                    out['sext_shift_pair'].append(occurrence(pair,candidate_bytes=2))
            triple=ss[n:n+3]
            if len(triple)==3 and contiguous(triple):
                a,b,c=triple
                if norm(a)=='MOV.W#0,R1' and norm(b)=='SUBR0,R1' and norm(c)=='MOVR1,R0':
                    out['negate'].append(occurrence(triple,literal_operand_before=n>0 and ss[n-1]['identifier']=='mov_immediate_16' and ss[n-1]['values']['destination']=='r0',candidate_bytes=2))
            four=ss[n:n+4]
            if len(four)==4 and contiguous(four):
                a,b,c,d=four
                if a['identifier']=='andi_8' and a['values']=={'destination':'r0','immediate':255} and b['identifier']=='cmpi_16' and b['values']=={'destination':'r0','immediate':128} and c['identifier'] in ('bcc','bcc_short') and c['pc']+c['bytes']+c['values']['displacement']==d['pc']+d['bytes'] and d['identifier']=='ori_16' and d['values']=={'destination':'r0','immediate':65280}:
                    out['sext_branch'].append(occurrence(four,candidate_bytes=2,optimized_existing_bytes=6,
                        body_entries=a['retired'],optimized_existing_retirements=2*a['retired'],candidate_retirements=a['retired']))
                if a['identifier'].startswith('mov_b1_ds_direct_load_') and b['identifier'].startswith('mov_b1_ds_direct_store_') and a['values'].get('displacement')==b['values'].get('displacement')==0:
                    width=1 if a['identifier'].endswith('_8') else 2
                    if b['identifier'].endswith('_8' if width==1 else '_16') and a['values']['destination']==b['values']['source'] and a['values']['base']!=b['values']['base'] and c['identifier']=='addp_'+a['values']['base'] and d['identifier']=='addp_'+b['values']['base'] and c['values']['adjustment']==d['values']['adjustment']==width:
                        out['post_increment_copy'].append(occurrence(four,width=width,candidate_bytes=6,incremental_bytes_saved=4,
                            retirements_saved=2*min(x['retired'] for x in four),gate='Known disjoint RAM and two representable advances; moves first advance across store, so no automatic fault/IRQ equivalence.'))
            five=ss[n:n+5]
            if len(five)==5 and contiguous(five) and s['image'].startswith(('compiler:','helper:')):
                texts=[norm(x) for x in five]
                if texts[:3]==['PUSHR0','MOV.W#1,R0','POPR1'] and texts[3] in ('ADDR0,R1','SUBR0,R1') and texts[4]=='MOVR1,R0':
                    out['compiler_unit_expression'].append(occurrence(five,optimized_existing_bytes=3,candidate_bytes=2,
                        baseline_optimizer_bytes_saved=sum(x['bytes'] for x in five)-3,incremental_bytes_saved=1,
                        baseline_optimizer_retirements_saved=4*min(x['retired'] for x in five),candidate_extra_retirements_saved=0))
    return dict(out)


def summarize(scanned):
    return {name:dict(sequences=len(rows),code_bytes=sum(r['code_bytes'] for r in rows),retired=sum(r['retired'] for r in rows),
        ram_read_bytes=sum(r['ram_read_bytes'] for r in rows),ram_write_bytes=sum(r['ram_write_bytes'] for r in rows)) for name,rows in scanned.items()}


def cost_models(scanned):
    unit=scanned.get('word_unit',[]);expr=scanned.get('compiler_unit_expression',[])
    sext=scanned.get('sext_branch',[])+scanned.get('sext_shift_pair',[])
    calls=scanned.get('near_call',[]);post=scanned.get('post_increment',[])
    pinned=[p for p in post if p['b0_value'] and p['label'] in ('initialize_zero','zero_pcb')]
    return dict(
        word_unit=dict(bytes_saved=len(unit),retirements_saved=0),
        compiler_unit=dict(existing_optimizer_bytes_saved=sum(x['baseline_optimizer_bytes_saved'] for x in expr),existing_optimizer_retirements_saved=sum(x['baseline_optimizer_retirements_saved'] for x in expr),additional_inc_dec_bytes_saved=len(expr),additional_inc_dec_retirements_saved=0),
        sext=dict(branch_to_shift_bytes_saved=sum(x['code_bytes']-6 for x in scanned.get('sext_branch',[])),additional_sext_bytes_saved=4*len(sext),additional_sext_retirements_saved=sum(x.get('body_entries',x['complete_executions']) for x in sext)),
        relative_call=dict(rel8_sites=sum(x['rel8_current_layout_fit'] for x in calls),rel16_sites=sum(x['rel16_current_layout_fit'] for x in calls),absolute_fallback_sites=sum(not x['rel16_current_layout_fit'] for x in calls),bytes_saved=sum(x['code_bytes']-x['candidate_bytes'] for x in calls),retirements_saved=0),
        pinned_initialization_post_increment=dict(sites=len(pinned),bytes_saved=sum(x['incremental_bytes_saved'] for x in pinned),retirements_saved=sum(x['retirements_saved'] for x in pinned),proof='Current initialization loops cover pinned K RAM 5000..5A8F (1352 words, final base 5A90) and two 40-byte PCBs, final base below 4300. Any new fusion requires image/recovery/IRQ review.'),
        note='Independent model deltas; do not sum. No modeled host-time, data-traffic, register-pressure or stack improvement is claimed.')


def post_increment_gate(base,width,value_register=0,base_register=4,kind='RAM',next_pc=0x100):
    """Design oracle only: planned rejection priority, no CPU or callbacks."""
    if width not in (1,2) or value_register not in range(4) or base_register not in range(4,8):return 'Illegal Instruction'
    if not 0<=base<=65535-width:return 'Invalid State'
    if kind!='RAM':return 'Memory Access'
    if not 0<=next_pc<=65535:return 'Memory Access'
    return 'OK'


def selection_contracts():
    return [
      dict(id='entry',name='LDC/STC ENTRY',decision='defer',why='One configured entry install per system boot. Most five-word traffic preserves staging, which a committed-entry transfer cannot replace. Optimize shared preservation first.',reconsider='Repeated per-thread kernel entry replacement beyond staging preservation.',budget='0 assigned/held. Possible eight existing-escape tags (four bases, two directions), 2 bytes; ten-byte RAM record plus configured-state policy.'),
      dict(id='proc',name='LDC/STC PROC',decision='defer',why='No full live three-control bundle in scheduler hot paths; LDX handles saved controls. User-access scopes edit two controls with leases and recovery.',reconsider='A measured atomic live process_map/uaccess_map/uaccess_segment consumer beyond CTX.',budget='0 assigned/held. Eight escape tags, 2-byte instruction, six-byte record; publication and map-zero disabling must match SETC, no frame edits or scheduler authority.'),
      dict(id='inc_dec',name='INC.W / DEC.W',decision='assigned in working ISA',why='Each existing word unit immediate saves one byte with identical flags and no retirement reduction. Optimize compiler literal-one expressions to ADDI/SUBI before crediting this one-byte incremental gain.',reconsider='Recheck measured site counts and allocations during integration.',budget='Preferred eight ext1a tags, 2 bytes, B0 only. Alternative eight primaries gives 1 byte at eight primary positions. Byte forms are aliases to current ADDI.B/SUBI.B, zero assigned tags.',
           semantics='Word B0 rd only, default .W. Compute old rd +/- 1 modulo 65536. NZCV exactly ADDI.W/SUBI.W #1 (C means no borrow on decrement); preserve I/E. U/K legal; precise fetch/decode/final-PC faults and no effects on failure. Never alias flag-preserving INCP/DECP.',
           compiler='Scalar unit arithmetic only, with exact integer promotion/truncation. Preserve checked carry/borrow guards. Simplify full compiler expression only when r1 scratch is dead; do not credit removed spills/moves to INC itself.',
           conformance=['All 65536 inputs, both operations and initial CVIE states against immediate forms','Raw B0 tags; reject B1/DP/SP, invalid suffix/operands; byte alias roundtrip','Truncation, final-PC rollback, IRQ retirement, updated branches/fault sites and full replay']),
      dict(id='post_increment',name='MOV post-increment',decision='assigned in working ISA',why='Measured pinned zero-fill and copy loops pair ordinary B1 transfers with width-sized ADDP. Three-byte reuse removes two code bytes and one retirement per legal pair without another primary.',reconsider='B1 value registers, CS/MOVU/displacement forms and MMIO need separate measured extensions.',budget='Sixteen assigned ext15 tags: direction x width x four B0 values; third byte 000000pp selects B1 (3 bytes). The normative map defines these tags. Alternative 64 fragmented non-held ext1f tags for 2-byte B0/value/base forms; all-GPR 3-byte variant needs a new primary with 32 mode tags.',
           semantics='MOV.B/W [rp+],ra and MOV.B/W ra,[rp+], ra=r0..r3, rp=r4..r7, DS only, no displacement. Sample original registers. Update rp by 1/2 with widened unsigned range check, never wrap. Zero-extend byte loads. Preserve NZCVIE. Complete legal fetch/decode, advance-range check (Invalid State), complete ordinary data translation/rights/backing with RAM-only DEVICE rejection, following-PC range validation, then one atomic RAM transfer and pointer commit. Faults preserve all destination/register/flag/memory effects; no MMIO callbacks. U/K legal.',
           compiler='Only known ordinary RAM with provably representable advance and no middle entry, volatile/device access or dependency on a separate transfer/ADDP IRQ boundary. B0/B1 operands make load/base and store/base aliases unencodable; reject B1 value/base aliases before execution. Store samples old source. Use ordinary tail transfers at FFFF/FFFE and for terminating bytes; do not rewrite fault recovery without regenerating exact sites. No automatic current SmallC rewrite without RAM/range/liveness proof.',
           conformance=['Byte/word loads/stores, four values/four bases, all flags and U/K rights','FFFF+1 / FFFE+2 fail before data access, ordinary final tail still succeeds; FFFE byte and FFFD word increments succeed','Illegal alias/reserved descriptors and missing bytes before operand checks; hole/rights/kind/alignment/final-PC priority and full rollback','MMIO rejected in both profiles without samples or commits, including console byte input; do not add byte MMIO','Proven-safe kernel initialization/copy integration, sentinel tails, fault retry/fixup and changed IRQ/replay schedules']),
      dict(id='relative_call',name='PC-relative near CALLR',decision='assigned in working ISA',why='Current near CALL is four bytes. Relative near forms reduce internal-call code size with the same retirement/stack cost; relocated drivers show the local-call use. Absolute CALL remains for out-of-range/fixed targets.',reconsider='Final relaxation and relocation fit must be checked after all layout changes.',budget='Preferred two new primary values (one rel8, one rel16), lengths 2/3. Alternative one new primary with a width descriptor gives 3/4 bytes and loses the rel16 density gain. These forms are assigned in the normative encoding map.',
           semantics='CALLR label defaults to relaxing rel8, .S forces rel8, .W forces rel16. Signed offset is added to the widened offset immediately after the whole instruction, no wrapping. Same CS/mode/map. Use existing near CALL validation/stack semantics and two-byte return frame; saved return is this selected end. Preserve all flags. Complete decode, return/target and stack validation in existing CALL order; atomic commit, no result on failure.',
           compiler='Near same-CS targets with relocations resolved after layout. Preserve absolute CALL fallback for out-of-range targets and fixed slots; CALLF/RETF and indirect JS unchanged. Relative calls do not relocate absolute data or all kernel images. Iterate branch and call relaxation together to a stable layout; forced widths retain final validation.',
           conformance=['Forward/backward rel8 and rel16 endpoints plus one beyond, no target/return wrap, exact saved return','Mixed branch/call cascading relaxation, forced widths and absolute fallback, relocation at two origins','Stack/protection/fetch/MMIO/final-state priority identical to near CALL, bad source frames and zero committed effects','Production roundtrips, updated literal instruction lengths/fault maps, nested calls and full replay']),
      dict(id='lea',name='LEA / indexed pointer formation',decision='defer',why='Current checked-pointer study retains software errors 1..6, bank moves and spills; hypothetical register add savings do not establish a safe allocator improvement.',reconsider='B1 residency/index allocation prototype beating optimized checked ADDP/B0 arithmetic with the same error ABI.',budget='0 assigned/held. Narrow two-register checked add could cost 16 escape tags/2 bytes; scale/displacement/selector variants need a separate layout and cannot silently discard overflow/space checks.'),
      dict(id='pushm_popm',name='PUSHM/POPM',decision='defer',why='Current compiler saves all four B1 registers even in leaves. Executed liveness alternative removes more work without an opcode. Whole-block atomic faults differ from committed scalar prefixes.',reconsider='Implement ABI/liveness save elimination, then measure residual multi-register groups and interrupt-latency cost.',budget='0 assigned/held. Two escape tags plus mask8 (3 bytes), or two primaries plus mask (2 bytes). Mask only r0..r7; need specified order, empty-mask behavior, complete RAM preflight, SP bounds and atomic register/SP/memory publication; no PC/CSR/frame authority.'),
      dict(id='indirect_call',name='Indirect CALL',decision='defer',why='Only a synthetic dispatch probe uses a JS trampoline; SmallC has no function pointers. A shared trampoline already supplies the near return ABI.',reconsider='Real function-pointer/dispatch workload plus measured complete-call gain beyond a shared trampoline.',budget='0 assigned/held. Register-only B1 targets could cost four escape tags/2 bytes, sampled before SP writes with near CALL validation. Memory-indirect is a separate fetch/alias/fault design.'),
      dict(id='neg',name='NEG',decision='defer',why='Most measured unary-negate bodies precede literal operands; constant folding removes these bodies without an opcode. Recount nonconstant use after folding.',reconsider='Frequent remaining nonconstant signed negation with dead scratch and defined NZCV, especially MIN input.',budget='0 assigned/held. Four escape tags/2 bytes for word B0. Potential semantics 0-old rd modulo 65536, NZCV exactly SUB; byte form needs independent zero-extension evidence.'),
      dict(id='sext',name='SEXT',decision='assigned in working ISA',why='Real signed-byte normalization remains multi-instruction even after replacing branches with the safe six-byte shift pair. A two-byte SEXT saves four bytes and one retirement relative to that optimized pair.',reconsider='No wider/other-bank sign extension without workload evidence.',budget='Preferred four ext1a tags/2 bytes, one B0 operand. Alternative four primary values/1 byte spends four primaries for marginal density. Use the assigned encoding in the normative map.',
           semantics='SEXT ra (optional .W synonym only) samples low eight bits and writes their signed 16-bit extension. NZ reflect the full word, C/V/I/E preserved. No .B form or B1 destination. U/K legal; ordinary precise fetch/decode/final-PC rollback. It is not a comparison; preserved V is not forced to zero.',
           compiler='Replace i8 normalization/casts/loads only where old C/V are dead. Existing branch/shift sequences change C/V differently. Preserve u8 zero-extension and byte arithmetic promotions. No extra register or spill.',
           conformance=['All 256 byte patterns x all 256 original upper bytes, NZ and preserved CVIE in U/K','Reject .B/B1/SP and bad tags/truncation, final-PC rollback, no temporary clobber','Signed-byte mixed arithmetic, cast/argument/return normalization and full guest replay']),
      dict(id='memory_immediate',name='Memory-immediate MOV',decision='defer',why='Adjacent literal/store counts include cold initialization and live register values. Hoisting/shared records and constant selection must be measured first; unrestricted layouts exceed five bytes.',reconsider='Repeated stores after constant hoisting with dead temporary, complete operand/constant-pool costs and audited length/MMIO semantics.',budget='0 assigned/held. Restricted B1 word form needs escape/mode/base/imm16 (5 bytes); absolute word form can require 6 bytes and displacement forms 7. Any >5-byte form requires consumer audit. Only ordinary legal MMIO widths and exactly one transaction after preflight.'),
      dict(id='zero_compare',name='Compare against zero',decision='defer new opcode; select existing instructions',why='TST.B and OR-self TST.W already test without a temporary and preserve CV. CMPI #0 establishes subtraction CV when required; these flag contracts differ.',reconsider='A shorter measured form with explicit flags beyond correct TST/CMPI selection.',budget='0; retain assigned TST/TSTI/CMPI. No speculative zero-compare alias with different C/V.'),
      dict(id='bswap',name='BSWAP',decision='defer',why='ROLI.W #8 already swaps word bytes in three bytes. No real endian-conversion consumer in the corpus.',reconsider='Frequent byte swapping with a complete flag contract and shorter measured encoding.',budget='0 assigned/held. Potential four escape tags/2 bytes would save one byte, not a retirement, over ROLI.W #8.'),
      dict(id='clz_ctz',name='CLZ/CTZ',decision='defer',why='No compiler/kernel workload requests either operation; bit-helper costs cannot justify unmeasured demand.',reconsider='Real bitmap/allocation/bit-index workload; specify zero -> 16 or another explicit result, flags, and signedness.',budget='0 assigned/held. Potential eight escape tags/2 bytes for word B0, not allocated.'),
      dict(id='other_extensions',name='General B1 ALU, wide multiply, register-count/through-carry shifts, wider pointer/stack immediates',decision='defer',why='Ordinary MUL/DIV/MOD are already assigned and must not remain in a deferred catch-all. Wide multiplication and DIVMOD remain deferred; other generalized operations have no new compiler consumer.',reconsider='Measured operations beyond the checked-pointer study, constant shifts, ADDP/ADJSP sequences and existing helpers.',budget='0 assigned/held; each needs independent semantics, bank/flags/overflow and encoding review.')
    ]


def candidate_contracts():
    contracts=selection_contracts()
    ids={e['id'] for t in metadata()['tables'].values() for e in t['entries']}
    assigned={'inc_dec':('inc_16','1A F0..F7, two-byte word B0; byte immediate aliases'),
              'post_increment':('mov_post_load_8','15 0100dzrr 000000pp, DS RAM-only, three bytes'),
              'relative_call':('callr','56 rel8 / 57 rel16, two/three bytes'),
              'sext':('sext','1A F8..FB, byte-to-word B0, two bytes')}
    for c in contracts:
        if c['id'] in assigned and assigned[c['id']][0] in ids:
            c['assigned_layout']=assigned[c['id']][1]
    return contracts


def build_report(build):
    r=report(build);rows=[]
    execution={w['name']:w for w in json.loads((build/'compiler/execution.json').read_text())['programs']}
    for w in r['workloads']:
        scanned=scan(w['sites']);rows.append(dict(name=w['name'],family=w['family'],variant=w.get('variant'),
            code_bytes=w['distinct_code_bytes'],data_bytes=w['initialized_data_bytes'],workspace_bytes=w['reserved_workspace_bytes'],
            actual=w['metrics'],host=w.get('host'),compiled_bank_moves=w.get('compiled_bank_moves'),compiled_spills=w.get('compiled_spills'),compiled_reloads=w.get('compiled_reloads'),
            maximum_user_stack_bytes=execution.get(w['name'],{}).get('maximum_user_stack_bytes'),
            maximum_helper_stack_bytes=execution.get(w['name'],{}).get('maximum_helper_stack_bytes'),
            counts=summarize(scanned),models=cost_models(scanned),sites=scanned))
    # Independent scopes: compiler rows repeat shared kernel images. Never add
    # whole-system counts across the corpus or sum overlapping candidate models.
    pure=[w for w in r['workloads'] if w['family']=='compiler' and w['variant']=='compiled']
    compiler_scans=[]
    total=defaultdict(lambda:dict(sequences=0,code_bytes=0,retired=0,ram_read_bytes=0,ram_write_bytes=0))
    for w in pure:
        scanned=scan([s for s in w['sites'] if s['image'].startswith(('compiler:','helper:'))]);compiler_scans.append(scanned)
        for name,counts in summarize(scanned).items():
            for k,v in counts.items():total[name][k]+=v
    contracts=candidate_contracts()
    tablecounts={}
    for name,table in metadata()['tables'].items():
        matched=set()
        for e in table['entries']:
            m=e['match'];matched.update(m['values'] if 'values' in m else [b for b in range(256) if b&m['mask']==m['value']])
        tablecounts[name]=dict(assigned=len(matched),unassigned=256-len(matched))
    ids={e['id'] for t in metadata()['tables'].values() for e in t['entries']}
    remaining=dict(primary_values=0 if 'callr' in ids else 2,ext15_tags=0 if 'mov_post_load_8' in ids else 16,ext1a_tags=(0 if 'inc_16' in ids else 8)+(0 if 'sext' in ids else 4))
    return dict(schema_version=1,isa='working',metadata_sha256=hashlib.sha256(payload_text().encode()).hexdigest(),
        provenance=r['provenance'],candidate_contracts=contracts,
        candidate_source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        per_workload=rows,compiler_only_sum_across_44_binaries=dict(total),
        compiler_only_models=cost_models({key:[row for scanned in compiler_scans for row in scanned.get(key,[])] for key in {k for scanned in compiler_scans for k in scanned}}),
        encoding_budget=dict(actual=tablecounts,remaining_allocations=remaining,
            note='Only the normative metadata assigns bytes; 32 control reserved patterns remain held, and deferred candidates reserve no bytes.'),
        limits=['Occurrence cost models are not measured integration deltas; assigned families are marked explicitly. Current execution is reported by the workload census.',
                'Rows preserve full code/data/workspace, moves/spills, actual RAM traffic and five host timing samples where available. Categories overlap; totals are not additive. Compiler-only sum counts distinct binaries, not unique shared-library bytes or one representative application.',
                'No modeled reduction in guest RAM operands, spills, host time or interrupt latency is inferred from fewer opcodes. Fusion changes instruction fault PCs and IRQ opportunities; any changed binary needs fresh record/replay and recovery validation.',
                'Adjacent memory/ADDP, literal/store, negate and save patterns are occurrence counts. They are not proof of RAM backing, pointer range, flags/scratch liveness, alias safety, absence of middle entry or fault equivalence.',
                'CALLR fit uses current absolute targets and candidate ends before joint branch/call relaxation; final relocation and signed ranges must be validated after layout. Stack/data bytes are unchanged in this model.',
                'Wide MUL and combined DIVMOD remain deferred. No compiler language feature or protected authority is expanded.'])


def markdown(r):
    by={w['name']:w for w in r['per_workload']}
    lines=['# Current control and density candidates','','This census marks assigned families and records reconsideration gates for deferred instructions. Models describe remaining opportunities in current software.','',
        '| Candidate | Decision | Evidence and reconsideration |','|---|---|---|']
    for c in r['candidate_contracts']:lines.append(f'| {c["name"]} | {c["decision"]} | {c["why"]} Reconsider: {c["reconsider"]} |')
    lines+=['','## Executed census','','| Workload | Code/data/workspace bytes | Word unit sites / retirements | Adjacent post-increment pairs / pair executions | Near call sites / executions |','|---|---:|---:|---:|---:|']
    for name in ('kernel_demo','console_demo','terminal','preempt_stack','signed_bytes','arithmetic_promotions','variable_stride'):
        w=by[name];counts=w['counts'];unit=counts.get('word_unit',{});post=w['sites'].get('post_increment',[]);calls=counts.get('near_call',{})
        lines.append(f'| {name} | {w["code_bytes"]}/{w["data_bytes"]}/{w["workspace_bytes"]} | {unit.get("sequences",0)} / {unit.get("retired",0)} | {len(post)} / {sum(p["complete_executions"] for p in post)} | {calls.get("sequences",0)} / {calls.get("retired",0)} |')
    lines+=['','The full JSON retains every source/PC, frequency, RAM traffic, moves/spills, stack and timing observations. Compiler rows repeat shared kernel images; do not sum those whole-system rows. Source hashes bind these counts to the current execution artifacts.','',
        'The live models below describe remaining patterns, not historical integration savings. Compare matched probes in the current workload census for executed alternatives.','',
        '## Selected contracts and conformance','']
    for c in r['candidate_contracts']:
        if not c['decision'].startswith('assigned'):continue
        lines+=[f'### {c["name"]}','',c['semantics'],'',c['compiler'],'','Encoding alternatives: '+c['budget'],'','Required conformance:']+['- '+x for x in c['conformance']]+['']
    lines+=['## Interactions and accounting','','INC/DEC credits only one byte beyond an optimized ADDI/SUBI #1. Removing literal loads, expression spills or bank moves is a separate compiler optimization. SEXT credits four bytes/one retirement beyond the six-byte shift-pair baseline, with dead C/V required. CALLR changes bytes and return offsets, not call retirements or stack use. Post-increment credits a legal transfer/ADDP pair only; its baseline transfer can commit before ADDP faults, whereas the fused proposal cannot. FFFF/FFFE tail transfers must remain ordinary instructions.','',
        'PUSHM/POPM estimates cannot include saves that liveness can eliminate. ENTRY read/write estimates cannot count saved staging as committed entry. PROC cannot count CTX saved controls or all individual uaccess changes as a bundle. Memory-immediate selection cannot assume the loaded temporary is dead. No combined savings total is published: the patterns overlap and layout/interrupt schedules change.','',
        '## Encoding budget','','The assigned density families cost two primaries, sixteen ext15 tags and twelve ext1a tags. Current counts are primary 113/143 (142 outside held 6F), ext15 256/0 and ext1a 172/84. No held control holes or 80–AF encodings change, all lengths remain 1–5 bytes, and deferred candidates reserve no bytes.','',
        '## Reproduction and limits','','```sh','cmake -S . -B /tmp/virtualcpu-density-review -DCMAKE_BUILD_TYPE=Release','cmake --build /tmp/virtualcpu-density-review -j4','ctest --test-dir /tmp/virtualcpu-density-review --output-on-failure','python3 tools/evaluate_density_candidates.py --build /tmp/virtualcpu-density-review --output /tmp/isa-density-review/candidates.json','```','']+['- '+x for x in r['limits']]+['']
    return '\n'.join(lines)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    if a.output.exists() or a.output.with_suffix('.md').exists():raise SystemExit('Refusing to overwrite review evidence')
    r=build_report(a.build.resolve());a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(r,indent=2,sort_keys=True)+'\n');a.output.with_suffix('.md').write_text(markdown(r))
    print(f'{len(r["candidate_contracts"])} dispositions; {len(r["per_workload"])} executed workloads; no assignments')
if __name__=='__main__':main()
