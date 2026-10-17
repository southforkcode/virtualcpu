#!/usr/bin/env python3
"""Reproduce current SmallC measurements; report execution separately from size estimates."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import sys
from encoding import payload_text

ROOT=Path(__file__).resolve().parents[1]
def kernel_report():
    from isa import assemble
    from generate_kernel_fixtures import IMAGES
    rows=[]
    for name,origin in IMAGES.items():
        path=ROOT/f'examples/kernel_{name}.asm';source=path.read_text()
        assembled=assemble(source,origin=origin);code=b''.join(b for _,b,_ in assembled)
        rows.append({'name':name,'origin':origin,'static_bytes':len(code),'instructions':len(assembled),
                     'binary_sha256':hashlib.sha256(code).hexdigest(),'source_sha256':hashlib.sha256(source.encode()).hexdigest()})
    groups={'minimal_demo':('boot','irq','swi','memory','copy','load_context','bootstrap_context','controls','control_fault','user'),
            'console_demo':('console_boot','console','console_memory','load_context','bootstrap_context','controls','control_fault','console_user','console_peer')}
    return {'images':rows,
            'distinct_image_totals':{name:{key:sum(r[key] for r in rows if r['name'] in group) for key in ('static_bytes','instructions')} for name,group in groups.items()},
            'scope':'Each assembled image counted once per group; excludes vectors, registry/data, duplicated user loads and compiler-specific boot/kernel variants.'}

def combine(rows,key):
    result=Counter()
    for row in rows:result.update({int(k):v for k,v in row.get(key,{}).items()})
    return dict(sorted(result.items()))
def distribution(counts,predicate):
    total=sum(counts.values());eligible=sum(n for value,n in counts.items() if predicate(int(value)))
    values=sorted((int(k),n) for k,n in counts.items())
    def percentile(f):
        target=max(1,int(total*f+0.999999));seen=0
        for value,n in values:
            seen+=n
            if seen>=target:return value
        return None
    return {'count':total,'eligible':eligible,'eligible_percent':round(100*eligible/total,2) if total else None,'min':values[0][0] if values else None,'median':percentile(.5),'p95':percentile(.95),'max':values[-1][0] if values else None,'histogram':counts}

def build_report(build,execution):
    manifest=json.loads((build/'compiler/compiler_images.json').read_text());run=json.loads(execution.read_text())
    if run['artifact_manifest_sha256']!=hashlib.sha256((build/'compiler/compiler_images.json').read_bytes()).hexdigest():raise ValueError('Execution uses a different artifact manifest')
    if manifest['encoding_sha256']!=hashlib.sha256(payload_text().encode()).hexdigest():raise ValueError('Manifest uses different ISA metadata')
    artifacts={a['name']:a for a in manifest['artifacts']};require_names=set(artifacts)
    if {r['name'] for r in run['programs']}!=require_names:raise ValueError('Execution/artifact corpus mismatch')
    rows=[]
    for raw in run['programs']:
        # Whole-system per-PC detail belongs to the density census sidecar, not
        # the ordinary compiler report (which already has user-code metrics).
        a=artifacts[raw['name']];row={k:v for k,v in raw.items() if k!='system_census'}
        row.update(source=a['source'],source_sha256=a['source_sha256'],binary_sha256=a['binary_sha256'],assembly_sha256=a['assembly_sha256'],data_sha256=a['data_sha256'],expected_result=a['result'],expected_guard=a.get('guard',0),expected_arithmetic_fault=a.get('arithmetic_fault'),frames=a['frames'],entrypoints=a['entrypoints'])
        instructions=a['instructions']+a.get('helper_artifact',{}).get('instructions',[])
        if a.get('helper_artifact'):
            helper=a['helper_artifact'];row.update(helper_binary_sha256=hashlib.sha256(bytes(helper['code'])).hexdigest(),helper_assembly_sha256=hashlib.sha256(helper['assembly'].encode()).hexdigest(),helper_frames=helper['frames'],helper_entrypoints=helper['entrypoints'],helper_source_sha256=helper['source_sha256'])
        row['static_instruction_families']=dict(sorted(Counter(i['family'] for i in instructions).items()))
        for category in ('sp','b1'):
            row['static_'+category+'_offsets']=dict(sorted(Counter(i['displacement'] for i in instructions if i.get('addressing')==category).items()))
        row['static_branch_distances']=dict(sorted(Counter(i['branch_distance'] for i in instructions if 'branch_distance' in i).items()))
        row['static_compact_sp_instructions']=sum(1 for i in instructions if i.get('addressing')=='sp' and i['bytes']==3)
        row['static_short_branch_instructions']=sum(1 for i in instructions if 'branch_distance' in i and i['bytes']==2)
        row['symbolic_compact_savings_bytes']={
            'b1_simm6':sum(2 for i in instructions if i.get('addressing')=='b1' and i['bytes']==5 and -32<=i['displacement']<=31),
            'branch_rel8':sum(1 for i in instructions if 'branch_distance' in i and i['bytes']==3 and -128<=i['branch_distance']<=127),
            'sp_uimm8':sum(1 for i in instructions if i.get('addressing')=='sp' and i['bytes']==4 and 0<=i['displacement']<=255),
        }
        rows.append(row)
    compiled=[r for r in rows if r['variant']=='compiled'];successful=[r for r in compiled if not r['expected_guard'] and not r['expected_arithmetic_fault']]
    metrics=('static_bytes','retired_instructions','boundaries','bank_moves','spills','reloads','maximum_user_stack_bytes','address_alu','checked_address_instructions')
    aggregates={name:{'count':len(group),'sum':{k:sum(r[k] for r in group) for k in metrics if not k.startswith('maximum_')},'max_user_stack_bytes':max(r['maximum_user_stack_bytes'] for r in group),'max_helper_stack_bytes':max(r['maximum_helper_stack_bytes'] for r in group),'median':{k:statistics.median(r[k] for r in group) for k in metrics}} for name,group in [('all_compiled',compiled),('successful_compiled',successful)]}
    aggregate_distributions={}
    for scope in ('static','dynamic'):
        aggregate_distributions[scope]={
            'b1_simm6':distribution(combine(compiled,scope+'_b1_offsets'),lambda x:-32<=x<=31),
            'sp_uimm8':distribution(combine(compiled,scope+'_sp_offsets'),lambda x:0<=x<=255),
            'branch_rel8':distribution(combine(compiled,scope+'_branch_distances'),lambda x:-128<=x<=127),
        }
    pairs=[];lookup={r['name']:r for r in rows}
    for a in manifest['artifacts']:
        if a['variant']!='hand':continue
        hand=lookup[a['name']];c=lookup[a['comparison']]
        pairs.append({'name':c['name'],'compiled_bytes':c['static_bytes'],'hand_bytes':hand['static_bytes'],'static_delta_bytes':c['static_bytes']-hand['static_bytes'],'static_ratio':round(c['static_bytes']/hand['static_bytes'],4),
                      'compiled_retired':c['retired_instructions'],'hand_retired':hand['retired_instructions'],'retired_delta':c['retired_instructions']-hand['retired_instructions'],'retired_ratio':round(c['retired_instructions']/hand['retired_instructions'],4),'boundary_delta':c['boundaries']-hand['boundaries']})
    references={'pairs':pairs,'median_static_ratio':statistics.median(p['static_ratio'] for p in pairs),'median_retired_ratio':statistics.median(p['retired_ratio'] for p in pairs),
                'aggregate_static_ratio':round(sum(p['compiled_bytes'] for p in pairs)/sum(p['hand_bytes'] for p in pairs),4),'aggregate_retired_ratio':round(sum(p['compiled_retired'] for p in pairs)/sum(p['hand_retired'] for p in pairs),4)}
    cache=(build/'CMakeCache.txt').read_text()
    config={}
    for name in ('CMAKE_BUILD_TYPE','CMAKE_CXX_COMPILER','CMAKE_CXX_FLAGS','CMAKE_CXX_FLAGS_RELEASE','CMAKE_EXE_LINKER_FLAGS'):
        lines=[line for line in cache.splitlines() if line.startswith(name+':')];config[name]=lines[0].split('=',1)[1] if lines else None
    provenance={k:v for k,v in manifest.items() if k not in ('artifacts','kernel_artifacts')};provenance['host_build']=config
    return {'schema_version':1,'kernel':kernel_report(),'provenance':provenance,'execution_checks':run['checks'],'programs':rows,'aggregates':aggregates,'encoding_distributions':aggregate_distributions,'symbolic_compact_savings_bytes':{k:sum(r['symbolic_compact_savings_bytes'][k] for r in compiled) for k in ('b1_simm6','branch_rel8','sp_uimm8')},'hand_reference_comparisons':references,
            'interpretation':{'instruction_counts':'Retired user code for map 7 plus compiled K helper only, including compiler wrappers/guards; system retired and B include kernel/peer/IRQ work. IRQ entry is a B transition, not a retired instruction.',
                              'addressing_histograms':'Only memory instructions count as B1 accesses; register transfers are counted separately.',
                              'stack':'Maximum observed bytes below initial U SP=$2000 or helper K SP=$FFFE, including active call frames, arguments, spills and allocated locals. Guard aborts intentionally retain their faulting stack.',
                              'schedule':'Each binary starts from its own artifact header; input is recorded when A is BLOCKED_READ and B has >=3 progress iterations at a U boundary. Events list exact per-binary B and sequence. Different binaries do not share numeric event B or fault PCs.',
                              'estimates':'Compact forms are assigned. Estimates count only residual long candidates under the current layout; forced widths and relocation can affect eligibility.',
                              'references':'Three hand-written programs match this fixed corpus I/O/result/data contract, with specialized loops and fewer general checks. Ratios measure this compiler baseline, not an unavoidable ISA cost.'}}

def markdown(report):
    lines=['| Program | Code bytes | Retired compiled | Total B | B0/B1 moves | Spills/reloads | Max U stack | Address B0 ALU | Check instructions |','|---|---:|---:|---:|---:|---:|---:|---:|---:|']
    for r in report['programs']:
        name=r['name']+(' (guard '+str(r['expected_guard'])+')' if r['expected_guard'] else ' (arithmetic fault)' if r['expected_arithmetic_fault'] else '')
        lines.append(f'| {name} | {r["static_bytes"]} | {r["retired_instructions"]} | {r["boundaries"]} | {r["bank_moves"]} | {r["spills"]}/{r["reloads"]} | {r["maximum_user_stack_bytes"]} | {r["address_alu"]} | {r["checked_address_instructions"]} |')
    lines+=['','| Matched fixed-input reference | Compiled / hand bytes | Compiled / hand retired | Static ratio | Retired ratio | B change |','|---|---:|---:|---:|---:|---:|']
    for p in report['hand_reference_comparisons']['pairs']:lines.append(f'| {p["name"]} | {p["compiled_bytes"]}/{p["hand_bytes"]} | {p["compiled_retired"]}/{p["hand_retired"]} | {p["static_ratio"]:.2f}× | {p["retired_ratio"]:.2f}× | +{p["boundary_delta"]} |')
    return '\n'.join(lines)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--build',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--execution',type=Path);parser.add_argument('--update-doc',action='store_true');args=parser.parse_args()
    build=args.build.resolve();execution=args.execution or build/'compiler/execution.json'
    if not args.execution:subprocess.run([str(build/'compiler_conformance'),str(execution)],check=True)
    report=build_report(build,execution)
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
    if args.update_doc:
        path=ROOT/'doc/ISA_EXAMPLES.md';text=path.read_text();begin='<!-- COMPILER_BASELINE_BEGIN -->';end='<!-- COMPILER_BASELINE_END -->'
        if begin not in text or end not in text:raise ValueError('Missing compiler baseline markers')
        path.write_text(text.split(begin,1)[0]+begin+'\n'+markdown(report)+'\n'+end+text.split(end,1)[1])
    print(f'{len(report["programs"])} executed binaries; measurements {args.output}')

if __name__=='__main__':main()
