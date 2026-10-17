#!/usr/bin/env python3
"""Join native byte-traffic/per-PC observations to current production assembly."""
import argparse
from collections import Counter
import hashlib
import gzip
import json
from pathlib import Path
import platform
import re
import statistics
from encoding import payload_text
from isa import assemble, decode
from generate_kernel_fixtures import IMAGES, image_symbols, control_registry
from measure_workloads import build_report
ROOT=Path(__file__).resolve().parents[1]
DENSITY=ROOT/'examples/workloads/density'

def digest(data):return hashlib.sha256(data).hexdigest()
def load(path):return json.loads(path.read_text())

def image(name,source,origin,path,annotations=()):
    rows=assemble(source,origin);symbols=image_symbols(source,rows,origin);notes={i['pc']:i for i in annotations};result=[]
    for pc,code,text in rows:
        entry,values=decode(code);ins=dict(notes.get(pc,{}));label=next((n for n,a in reversed(list(symbols.items())) if a<=pc),'entry')
        ins.update(image=name,source=path,pc=pc,bytes=len(code),text=text,identifier=entry['id'],label=label,semantic=entry['semantic'],values=values)
        tags=[];identifier=entry['id'];semantic=entry['semantic'];selector=values.get('selector',-1)
        if identifier in ('getc','setc'):
            tags+=['control_scalar']
            if 0x20<=selector<=0x36:tags+=['descriptor_control']
            if 0x10<=selector<=0x14:tags+=['entry_control']
            if selector<3:tags+=['process_control']
            if selector==0x7f:tags+=['control_command']
        if identifier in ('inc_16','dec_16'):tags+=['unit_arithmetic','compact_unit_arithmetic']
        if identifier=='sext':tags+=['sign_extension','compact_sign_extension']
        if identifier.startswith('mov_post_'):tags+=['post_increment_transfer']
        if identifier in ('callr','callr_short'):tags+=['relative_call']
        if identifier in ('mul','divu','divs','modu','mods'):tags+=['native_integer_arithmetic']
        if identifier in ('ldx_ctx','stx_ctx'):tags+=['context_image_load' if identifier=='ldx_ctx' else 'context_image_store']
        if identifier in ('ldc_desc','stc_desc'):
            tags+=['descriptor_control','descriptor_image_install' if identifier=='ldc_desc' else 'descriptor_image_readback']
        if identifier.startswith(('getx','setx')):tags+=['context_scalar']
        if identifier in ('addi_8','addi_16','subi_8','subi_16') and values.get('immediate')==1:tags+=['unit_arithmetic']
        if identifier.startswith('addp_'):tags+=['checked_pointer_update']
        if semantic.startswith('call_'):tags+=['call_site']
        if identifier=='js_register':tags+=['indirect_jump']
        if identifier in ('push','pop'):
            tags+=['stack_register_transfer']
            if ins.get('kind') in ('prologue','epilogue') or values.get('source',values.get('destination')) in ('r4','r5','r6','r7') and not annotations:tags+=['register_save_restore']
        if identifier in ('mov_b0_b1','mov_b1_b0'):tags+=['bank_move']
        if ins.get('kind') in ('spill','reload'):tags+=[ins['kind']]
        ins['tags']=tags;result.append(ins)
    def mark(first,last,tag):
        for i in result[first:last]:
            if tag not in i['tags']:i['tags'].append(tag)
    # Compiler multiply labels enclose an actual software loop, not all uses of *.
    for label,start in symbols.items():
        if label.startswith('sc_multiply_'):
            end=next((a for n,a in symbols.items() if n.startswith('sc_mulend_') and a>start),None)
            if end:
                for i in result:
                    if start<=i['pc']<end:i['tags'].append('multiply_loop')
    if name.endswith('multiply.asm') or name.startswith('probe:'):
        start=symbols.get('multiply');end=symbols.get('divide',origin+sum(i['bytes'] for i in result))
        if start is not None and not any(i['identifier']=='mul' for i in result if start<=i['pc']<end):
            for i in result:
                if start<=i['pc']<end:i['tags'].append('multiply_loop')
    for n,i in enumerate(result):
        # Complete adjacent saved-context word pair, including the actual RAM MOV.
        if n+1<len(result):
            nxt=result[n+1]
            if i['identifier'].startswith('getx') and nxt['semantic']=='store_memory' and i['values'].get('destination')==nxt['values'].get('source'):mark(n,n+2,'context_spill_pair')
            if i['semantic']=='load_memory_zero_extend' and nxt['identifier'].startswith('setx') and i['values'].get('destination')==nxt['values'].get('source'):mark(n,n+2,'context_reload_pair')
        text=i['text'].upper().replace(' ','')
        if text=='CMPI.W#128,R0' and n+2<len(result) and result[n+2]['text'].upper().replace(' ','') in ('ORI.W#65280,R0','ORI.W#$FF00,R0'):mark(n,n+3,'sign_extension')
        if text=='SHLI.W#8,R0' and n+1<len(result) and result[n+1]['text'].upper().replace(' ','')=='SARI.W#8,R0':mark(n,n+2,'sign_extension')
        # SmallC's literal-one, POP, ADD/SUB lowering; this is occurrence census,
        # not proof that deleting these instructions preserves the caller's ABI.
        if i['identifier'].startswith('mov_immediate') and i['values'].get('immediate')==1:
            for end in (n+1,n+2):
                if end<len(result) and result[end]['identifier'] in ('add','sub') and result[end]['values'].get('source')==i['values'].get('destination') and (end==n+1 or result[n+1]['identifier']=='pop'):
                    mark(n,end+1,'unit_arithmetic_lowering')
    for operation in ('descriptor_install','descriptor_remove','descriptor_readback'):
        if 'do_'+operation in symbols:
            for ins in result:
                if symbols['do_'+operation]<=ins['pc']<symbols['end_'+operation]:ins['tags'].append(operation+'_body')
    return dict(name=name,source=path,origin=origin,code_bytes=sum(i['bytes'] for i in result),binary_sha256=digest(b''.join(b for _,b,_ in rows)),instructions=result)

def kernel_images():
    return {n:image(n,(ROOT/f'examples/kernel_{n}.asm').read_text(),o,f'examples/kernel_{n}.asm') for n,o in IMAGES.items()}

def census(name,groups,raw,data_bytes,workspace,host=None):
    # groups = [(mode,map,CS,image)]; unique source images count once, duplicate
    # physical loads are reported separately. Source-qualified PC survives labels.
    unique={g[3]['name']:g[3] for g in groups};lookup={};sites={}
    for mode,process,cs,im in groups:
        for ins in im['instructions']:
            key=f'{mode}:{process}:{cs}:{ins["pc"]}'
            if key in lookup:raise ValueError('Overlapping source attribution '+key)
            identity=f'{im["name"]}:{ins["pc"]}'
            lookup[key]=identity
            if identity not in sites:sites[identity]=dict(ins,retired=0,attempts=0,ram_read_bytes=0,ram_write_bytes=0,fetch_bytes=0,mmio_sample_bytes=0,device_commits=0,backing_probes=0,descriptor_lookups=0,execution_contexts={})
    unassigned=[]
    for key,counts in raw['sites'].items():
        if key not in lookup:
            if counts['retired']:raise ValueError(f'{name}: unattributed retired PC {key}')
            unassigned.append(dict(site=key,**counts));continue
        row=sites[lookup[key]];row['execution_contexts'][key]=counts
        for field,count in counts.items():row[field]+=count
    if sum(s['retired'] for s in sites.values())!=raw['totals']['retired']:raise ValueError('PC retirement accounting')
    summary={}
    for tag in sorted({t for i in sites.values() for t in i['tags']}):
        selected=[s for s in sites.values() if tag in s['tags']]
        summary[tag]={'static_sites':len(selected),'code_bytes':sum(s['bytes'] for s in selected),'retired':sum(s['retired'] for s in selected),'ram_read_bytes':sum(s['ram_read_bytes'] for s in selected),'ram_write_bytes':sum(s['ram_write_bytes'] for s in selected)}
    code=sum(im['code_bytes'] for im in unique.values());loaded=sum(g[3]['code_bytes'] for g in groups)
    result=dict(name=name,distinct_code_bytes=code,loaded_code_bytes=loaded,initialized_data_bytes=data_bytes,code_plus_initialized_data_bytes=code+data_bytes,reserved_workspace_bytes=workspace,code_plus_data_and_workspace_bytes=code+data_bytes+workspace,metrics={k:v for k,v in raw.items() if k!='sites'},categories=summary,sites=list(sites.values()),unattributed_nonretired=unassigned)
    for field in ('maximum_ram_boundary_site','maximum_backing_boundary_site','maximum_instrumented_step_site'):
        key=raw.get(field)
        if key in lookup:
            ins=sites[lookup[key]];result['metrics'][field+'_instruction']={k:ins[k] for k in ('source','pc','text','label')}
    if host:
        ns=host['uninstrumented_host_ns'];median=statistics.median(ns)
        result['host']={'uninstrumented_samples_ns':ns,'median_ns':median,'min_ns':min(ns),'max_ns':max(ns),'retired_per_second_at_median':round(raw['totals']['retired']*1e9/median),'instrumented_ns':host['instrumented_host_ns']}
        result['replay_header']=host['header'];result['events']=host['events']
    return result

def report(build):
    kernels=kernel_images()
    native=load(build/'density/execution.json');probes=load(build/'density/density_images.json')
    if native['artifact_manifest_sha256']!=digest((build/'density/density_images.json').read_bytes()):raise ValueError('Density artifact/execution mismatch')
    manifest=load(build/'compiler/compiler_images.json');execution=load(build/'compiler/execution.json')
    if execution['artifact_manifest_sha256']!=digest((build/'compiler/compiler_images.json').read_bytes()):raise ValueError('Compiler artifact/execution mismatch')
    if manifest['encoding_sha256']!=digest(payload_text().encode()):raise ValueError('Encoding metadata mismatch')
    for files in (probes['sources'],manifest['files']):
        for path,expected in files.items():
            if digest((ROOT/path).read_bytes())!=expected:raise ValueError('Stale generated source: '+path)
    outputs=[];specs={s['name']:s for s in probes['programs']}
    registry=len(control_registry(assemble((ROOT/'examples/kernel_controls.asm').read_text(),0x7000),image_symbols((ROOT/'examples/kernel_controls.asm').read_text(),assemble((ROOT/'examples/kernel_controls.asm').read_text(),0x7000),0x7000))[1])
    for run in native['programs']:
        name=run['name']
        if name in specs:
            p=specs[name];im=image('probe:'+name,p['assembly'],p['origin'],'tools/generate_density_fixtures.py + '+','.join(p['helpers']))
            groups=[(1,0,0,im)];data=p['data_bytes']+4;workspace=0
            # Observable output records and software status need RAM even when
            # default-zero loader backing supplied it rather than a data file.
            occupied={a+i for a,b in p['data'] for i in range(len(b))};required={a+i for a,b in p['checks'] for i in range(len(b))}
            workspace=len(required-occupied)
            if p.get('controls'):
                control=kernels['controls'];record_bytes=registry
                if p.get('control_artifact'):
                    a=p['control_artifact'];control=image('controls_'+p['name'],a['assembly'],0x7000,'generated current-ISA comparison from kernel_controls.asm');record_bytes=len(a['registry'])
                groups.append((1,0,0,control));data+=record_bytes;workspace+=2704
            row=census(name,groups,run['census'],data,workspace,run);row.update(family=p['family'],comparison=p['comparison'],inputs=p.get('inputs'))
        else:
            names=('boot','irq','swi','memory','copy','load_context','bootstrap_context','controls','control_fault') if name=='kernel_demo' else ('console_boot','console','console_memory','load_context','bootstrap_context','controls','control_fault')
            groups=[(1,0,0,kernels[n]) for n in names]
            groups += [(0,7,1,kernels['user' if name=='kernel_demo' else 'console_user']),(0,8,1,kernels['user' if name=='kernel_demo' else 'console_peer'])]
            # Explicit loader vectors/registry and console's parameter words.
            row=census(name,groups,run['census'],36+registry+(8 if name=='console_demo' else 0),2704+(84+96 if name=='kernel_demo' else 116+40+96),run)
            row['family']='system';row['workspace_scope']='Control region $5000-$5A8F plus two PCBs, active request records; console also rollback image. Excludes stacks and user buffers; see limitations.'
        outputs.append(row)
    common=('console_memory','load_context','bootstrap_context','controls','control_fault')
    generated={n:image('compiler_kernel:'+n,a['assembly'],a['origin'],'generated compiler kernel: '+n) for n,a in manifest['kernel_artifacts'].items()}
    artifacts={a['name']:a for a in manifest['artifacts']}
    for run in execution['programs']:
        a=artifacts[run['name']];im=image('compiler:'+a['name'],a['assembly'],0x100,'examples/workloads/'+a['source'],a['instructions'])
        groups=[(0,7,1,im),(0,8,1,kernels['console_peer']),(1,0,0,generated['helper_boot' if a.get('helper_artifact') else 'boot']),(1,0,0,generated['kernel'])]+[(1,0,0,kernels[n]) for n in common]
        if a.get('helper_artifact'):
            h=a['helper_artifact'];groups.append((1,0,0,image('helper:'+a['name'],h['assembly'],0xb000,'examples/workloads/'+a['helper'],h['instructions'])))
        # Failure handlers halt at this pinned sentinel, outside assembled images.
        groups.append((1,0,0,image('fatal_sentinel','HALT',0x1400,'tests/compiler_conformance.cpp')))
        row=census(a['name'],groups,run['system_census'],len(a['data'])+36+registry+8,2704+116+40+96)
        row.update(family='compiler',variant=a['variant'],compiled_code_bytes=run['static_bytes'],compiled_data_bytes=run['data_bytes'],compiled_retired=run['retired_instructions'],expected_guard=a.get('guard',0),expected_arithmetic_fault=a.get('arithmetic_fault'),compiled_bank_moves=run['bank_moves'],compiled_spills=run['spills'],compiled_reloads=run['reloads'],events=run['events'],replay_header=run['replay_header'])
        outputs.append(row)
    comparisons=[];by_name={r['name']:r for r in outputs}
    for row in outputs:
        if row.get('comparison'):
            base=by_name[row['comparison']];comparisons.append({'baseline':base['name'],'safe_alternative':row['name'],'deltas_alternative_minus_baseline':{k:row[k]-base[k] for k in ('distinct_code_bytes','initialized_data_bytes','code_plus_data_and_workspace_bytes')},'retired_delta':row['metrics']['totals']['retired']-base['metrics']['totals']['retired'],'ram_read_delta':row['metrics']['totals']['ram_read_bytes']-base['metrics']['totals']['ram_read_bytes'],'ram_write_delta':row['metrics']['totals']['ram_write_bytes']-base['metrics']['totals']['ram_write_bytes']})
    # Existing compiler-vs-hand references remain explicitly narrower fixed-input
    # contracts; count helper implementations once in each complete image.
    existing=build_report(build,build/'compiler/execution.json')
    return {'schema_version':1,'isa':'working','metadata_sha256':digest(payload_text().encode()),'adoption_contract':load(DENSITY/'adoption.json'),'provenance':{'host':platform.platform(),'build':str(build),'host_build':existing['provenance']['host_build'],'sources':{str(p.relative_to(ROOT)):digest(p.read_bytes()) for p in [ROOT/'tools/measure_density.py',ROOT/'tools/generate_density_fixtures.py',ROOT/'tests/density_census.cpp',DENSITY/'census.hpp',ROOT/'src/cpu.hpp',ROOT/'src/cpu.cpp',ROOT/'src/protected.cpp']},'artifacts':{str(p.relative_to(build)):digest(p.read_bytes()) for p in [build/'density/density_images.json',build/'density/execution.json',build/'compiler/compiler_images.json',build/'compiler/execution.json',build/'density_census',build/'compiler_conformance',build/'libvirtualcpu.a']}},'workloads':outputs,'safe_software_comparisons':comparisons,'existing_hand_reference_comparisons':existing['hand_reference_comparisons'],'limitations':[
        'DESC/1 LDC/STC are assigned. Context-image LDX/STX are assigned; word MUL/DIV/MOD and arithmetic cause 8 are assigned; software/native helper comparison probes retain explicit software status while compiled /% propagate cause eight.',
        'Categories overlap and count instruction sites/retirements, not optimizer-proven removable operations. Sign-extension tags cover the conversion body; initialization and callers remain in total costs.',
        'RAM counts are actual emulator byte reads/writes, not estimates: fetch is a subset; subtract fetch for operand plus vector reads. Faulting sampled reads count; staged discarded stores do not. Descriptor/context host containers and translation probes are not guest RAM traffic.',
        'MMIO samples are pure pre-commit reads, not transactions. Device commits are counted independently (one per accepted word transaction); samples can occur before a fault. No MMIO callbacks are added.',
        'Maximum observed boundary RAM bytes and backing probes identify observed atomic work, not global architectural interrupt latency or host worst-case execution time. Maximum instrumented step time excludes census bookkeeping but includes counter overhead and host scheduling noise. Backing probes and descriptor lookups count backing-classification and descriptor-helper calls, separately from RAM reads; internal tree comparisons, insertion and allocator work are not counted. All existing successful instructions retire once; protected entry is a separate boundary.',
        'Code counts unique assembled image once; loaded-code count includes duplicate user images. Initialized data includes loader vectors/registry/parameters, not the entire zero-filled RAM allocation. Workspace is additional declared kernel/observable output storage, excludes stacks and user-buffer reservations; totals are not total process RAM requirements.',
        'Probes run pinned K RAM with caller-clobbered NZCV and preserved B1 for arithmetic helpers. Copy source/destination spans are valid and disjoint. Signed extension alternatives allow flag differences; these are ABI-safe only with dead flags.',
        'Relative-position baselines use absolute CALL relocated at assembly time; matched native probes use CALLR. Indirect calls use a CALL-to-JS trampoline. CALLR is assigned and internal compiler calls use it; existing absolute calls and the JS trampoline remain. A linker/relocation-object format and function-pointer language support are not claimed.',
        'Decimal probes use a checked five-digit unsigned parser and five-digit zero-padded formatter. Random arithmetic seed is fixed; probes do not exhaust 2^32 operand pairs.',
        'Uninstrumented throughput uses five fresh-machine samples of step loop only (plus console schedule checks/input application), excluding loading/header construction and output checking. Instrumented timing includes census map/string overhead. Tiny-probe timings are noisy; compiler replay harness is not a throughput benchmark.',
        'Descriptor lifecycle uses the shared control library, initialization once, atomic descriptor-image install/readback preserving selectors/flags, replacement and invalid-record rejection. The matched cycle probes include ownership/generation checks, staging backup, recovery registry and cleanup; the scalar reference has equivalent successful ABI results on pinned RAM with I=0, not identical intermediate fault/IRQ states. Native descriptor and kernel conformance establish fault/recovery behavior separately.',
        'Synthetic probes alone do not justify a new instruction. Existing hand references specialize fixed inputs and checks; their advantage is not an unavoidable ISA cost.'
    ]}

def markdown(r):
    lines=['# Current working ISA workload census','',f'Captured {len(r["workloads"])} workloads. DESC/1 LDC/STC are assigned; CTX/1 LDX/STX are assigned; word integer arithmetic and cause 8 are assigned; software probes remain comparison baselines.','',
           'The JSON report and its compressed per-PC sidecar contain every attributed PC, operation category, exact byte traffic, schedules, source/artifact hashes and comparison criteria.','',
           '| Workload | Unique code | Initialized data | Additional workspace | Retired | RAM read/write bytes | Max U/K stack |','|---|---:|---:|---:|---:|---:|---:|']
    selected={'kernel_demo','console_demo','descriptor_lifecycle','descriptor_cycle_scalar','descriptor_cycle_bulk','descriptor_cycle_absent_image','udiv_small_simple','udiv_small_binary','udiv_small_binary_native','sdiv_0_native','mul_signed_native','parse_max_native','format_65535_native','scale10_native','scale8_native','scale8_shift','checked_scale6_loop','checked_scale6_native','arithmetic_words','arithmetic_decimal','arithmetic_preempt','udiv_max','sdiv_0','mul_signed','parse_max','format_65535','copy_B_unit','copy_W_unit','sext_branch','sext_shifts','scale10_multiply','scale10_strength','call_direct_256','call_indirect_256','saves_conservative','saves_live'}
    for w in r['workloads']:
        if w['name'] not in selected:continue
        m=w['metrics'];t=m['totals'];lines.append(f'| {w["name"]} | {w["distinct_code_bytes"]} | {w["initialized_data_bytes"]} | {w["reserved_workspace_bytes"]} | {t["retired"]} | {t["ram_read_bytes"]}/{t["ram_write_bytes"]} | {m["maximum_user_stack_bytes"]}/{m["maximum_kernel_stack_bytes"]} |')
    lines+=['','Kernel images and control helpers are counted once per complete workload, including cold initialization. Dynamic counts include all K/U/peer work. Category totals overlap. Twenty-word frame copies include their RAM instructions, not only GETX/SETX.','',
            '| Operation census | Kernel static sites / retired | Console static sites / retired |','|---|---:|---:|']
    systems={w['name']:w for w in r['workloads'] if w['name'] in ('kernel_demo','console_demo')}
    for tag in ('descriptor_control','control_scalar','context_spill_pair','context_reload_pair','context_image_store','context_image_load','unit_arithmetic','checked_pointer_update','call_site','register_save_restore','bank_move'):
        values=[]
        for name in ('kernel_demo','console_demo'):
            c=systems[name]['categories'].get(tag,{'static_sites':0,'retired':0});values.append(f'{c["static_sites"]} / {c["retired"]}')
        lines.append('| '+tag+' | '+' | '.join(values)+' |')
    lines+=['','The kernel IRQ performs four STX spills and four LDX replacements. Initial bootstrap retains twenty scalar SETX/RAM pairs. Console scheduling retains software generation/eligibility checks, an independent forty-byte backup and its guarded LDX recovery. Other scalar context-pair counts include individual copy-service fields. Per-PC detail distinguishes those sites and every compiler multiplication, sign extension, unit adjustment, spill/reload and prologue/epilogue.','',
            '| Safe existing alternative | Code delta | Code+data+workspace delta | Retired delta |','|---|---:|---:|---:|']
    for p in r['safe_software_comparisons']:
        d=p['deltas_alternative_minus_baseline'];lines.append(f'| {p["baseline"]} → {p["safe_alternative"]} | {d["distinct_code_bytes"]:+} | {d["code_plus_data_and_workspace_bytes"]:+} | {p["retired_delta"]:+} |')
    lines+=['','These are software/native alternatives with current encodings. Hoisting constants can enlarge short loops; binary division costs more code and stack than repeated subtraction on small inputs. Strength reduction and live-save elimination can remove much of a proposed instruction’s apparent benefit. The full baseline includes guards, wrappers, helper bodies and control infrastructure.','',
            '| Demo | Median uninstrumented host ns | Retired/s | Observed max RAM bytes/boundary | Max frame depth |','|---|---:|---:|---:|---:|']
    for w in r['workloads']:
        if w['name'] in ('kernel_demo','console_demo'):
            h=w['host'];m=w['metrics'];lines.append(f'| {w["name"]} | {h["median_ns"]} | {h["retired_per_second_at_median"]} | {m["maximum_observed_boundary_ram_bytes"]} | {m["maximum_exception_depth"]} |')
    lines+=['','| Demo | Longest observed instrumented step, ns | Source instruction | Largest backing-probe count in one boundary |','|---|---:|---|---:|']
    for name,w in systems.items():
        m=w['metrics'];ins=m['maximum_instrumented_step_site_instruction'];lines.append(f'| {name} | {m["maximum_instrumented_step_ns"]} | {ins["source"]} `${ins["pc"]:04X}`: `{ins["text"]}` | {m["maximum_boundary_backing_probes"]} |')
    lines+=['','Host samples are observations on the recorded machine/build, not instruction timing promises or worst-case latency bounds. Detailed per-sample ranges and instrumented timings are in JSON.','', '## Reproduce','', 'From the repository root:','', '```sh','cmake -S . -B /tmp/virtualcpu-density-review -DCMAKE_BUILD_TYPE=Release','cmake --build /tmp/virtualcpu-density-review -j 4','ctest --test-dir /tmp/virtualcpu-density-review --output-on-failure','python3 tools/measure_density.py --build /tmp/virtualcpu-density-review --output /tmp/isa-density-review/report.json','```','',
            'The tool refuses to replace an existing report. Use a new output directory for every experiment. The comparison criteria are in examples/workloads/density/adoption.json. Per-PC detail is in the compressed JSON sidecar named by the report.','', '## Limits and interpretation','']
    lines += ['- '+s for s in r['limitations']]
    return '\n'.join(lines)+'\n'

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    sidecar=args.output.with_suffix('.sites.json.gz')
    if any(p.exists() for p in (args.output,args.output.with_suffix('.md'),sidecar)):raise SystemExit('Refusing to overwrite review evidence; choose a fresh output path')
    r=report(args.build.resolve());args.output.parent.mkdir(parents=True,exist_ok=True)
    sites={w['name']:w.pop('sites') for w in r['workloads']}
    compressed=gzip.compress(json.dumps(sites,sort_keys=True,separators=(',',':')).encode(),mtime=0)
    r['per_pc_detail']={'file':sidecar.name,'sha256':digest(compressed),'format':'gzip JSON; workload -> attributed instruction sites'}
    sidecar.write_bytes(compressed);args.output.write_text(json.dumps(r,indent=2,sort_keys=True)+'\n');args.output.with_suffix('.md').write_text(markdown(r));print(f'{len(r["workloads"])} workloads attributed; report {args.output}')
if __name__=='__main__':main()
