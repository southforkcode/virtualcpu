#!/usr/bin/env python3
"""Current workload study: measured existing code plus explicitly unencoded checked-add models."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path

from isa import TABLES, assemble
from encoding import payload_text

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def checked_add(base, addend):
    """Mathematical unsigned candidate only; no decoder, CPU or exception model."""
    if not 0 <= base <= 65535 or not 0 <= addend <= 65535:
        raise ValueError('Expected unsigned 16-bit offsets/addends')
    result = base + addend
    if result > 65535:
        raise OverflowError('Invalid State proposed; no narrowed result')
    return result


def pointer_sites(instructions):
    """Only pointer-range guards, excluding integer/scale overflow guards."""
    sites = []
    for i, ins in enumerate(instructions[:-1]):
        following = instructions[i+1]
        if ins['kind'] != 'address':
            continue
        if ins['text'] == 'SUB r0,r1' and following['text'] == 'BCC sc_failure_4':
            sites.append(dict(pc=ins['pc'], operation='subtract', kind='not_supported',
                              baseline_pcs=[ins['pc'], following['pc']]))
            continue
        if ins['text'] != 'ADD r1,r0' or following['text'] != 'BCS sc_failure_4':
            continue
        region = instructions[i:i+2]
        kind = 'b0_result'
        tail = instructions[i+2:]
        if tail and tail[0]['text'] == 'MOV r0,r4':
            # Only discard the B0 result when it is overwritten before its next use.
            j = 1
            if [r['text'] for r in tail[j:j+3]] == ['MOV ds,r3', 'MOV ss,r2', 'MOV r2,ds']:
                j += 3
            if j < len(tail) and (tail[j]['text'] == 'POP r0' or
                                    tail[j]['text'].startswith(('MOV.B [r4', 'MOV.W [r4')) and tail[j]['text'].endswith(',r0')):
                kind = 'b1_byte_access'
                region = instructions[i:i+3]
        elif (len(tail) >= 3 and tail[0]['text'].startswith('CMPI.W #') and
              tail[1]['text'] == 'BHI sc_failure_5' and tail[2]['text'] == 'MOV r0,r4'):
            kind = 'b1_width_checked_access'
            # Keep the width compare/branch unchanged; replace only the add/guard/move.
            region = instructions[i:i+2] + [tail[2]]
        sites.append(dict(pc=ins['pc'], operation='add', kind=kind,
                          baseline_pcs=[r['pc'] for r in region],
                          baseline_bytes=sum(r['bytes'] for r in region),
                          baseline_instructions=len(region),
                          baseline_bank_moves=sum(r['bank_move'] for r in region)))
    return sites


def site_model(site, candidate_bytes):
    """Fixed-layout successful fast path, assuming dead flags and scratch r4.

    B0 currently holds base in r1 and index in r0. Retain the B0 result for width
    checks/expressions. This is NOT an ABI-compatible compiler transformation:
    removing BCS changes SWI #255/code 4 into the proposed Invalid State fault.
    """
    kind = site['kind']
    operations = 2 if kind == 'b1_byte_access' else 3
    moves = operations-1
    return dict(saved_bytes=site['baseline_bytes']-(2*moves+candidate_bytes),
                saved_retired_instructions=site['baseline_instructions']-operations,
                added_bank_moves=moves-site['baseline_bank_moves'],
                saved_spills=0, saved_reloads=0)


def guarded_site_model(site, candidate_bytes):
    """Retain the compiler's failure code/flags, with an extra scratch B0.

    MOV r0,r2; ADD r1,r0; BCS failure_4; MOV r1,r4; [ADDP.R r2,r4].
    r0 remains the result, r4 the checked pointer. No scratch is spilled in this
    optimistic model. The extra MOV changes scratch state at the software trap;
    this preserves the language error ABI, not an identical exception frame.
    """
    moves_to_b1 = site['kind'] != 'b0_result'
    return dict(added_bytes=1+2+candidate_bytes-(2 if moves_to_b1 else 0),
                added_retired_instructions=3-(1 if moves_to_b1 else 0),
                added_bank_moves=1-site['baseline_bank_moves'],
                saved_spills=0, saved_reloads=0)


def evaluate(build):
    manifest_path = build/'compiler/compiler_images.json'
    execution_path = build/'compiler/execution.json'
    manifest = json.loads(manifest_path.read_text())
    execution = json.loads(execution_path.read_text())
    artifacts = {r['name']: r for r in manifest['artifacts']}
    if set(artifacts) != {r['name'] for r in execution['programs']}:
        raise ValueError('Corpus mismatch')
    if manifest['encoding_sha256'] != hashlib.sha256(payload_text().encode()).hexdigest():
        raise ValueError('Manifest uses different ISA metadata')
    if execution['artifact_manifest_sha256'] != digest(manifest_path):
        raise ValueError('Execution uses a different artifact manifest')
    rows = []
    invariant_metrics = ('static_bytes', 'retired_instructions', 'system_retired_instructions',
                         'boundaries', 'bank_moves', 'spills', 'reloads',
                         'maximum_user_stack_bytes', 'maximum_helper_stack_bytes',
                         'guard_trap_pc', 'exit_csr', 'events', 'replay_header')
    for run in execution['programs']:
        name = run['name']
        a = artifacts[name]
        if run['static_bytes'] != len(a['code'])+len(a.get('helper_artifact',{}).get('code',[])):
            raise ValueError(f'Executed code size mismatch: {name}')
        if hashlib.sha256(bytes(a['code'])).hexdigest() != a['binary_sha256']:
            raise ValueError(f'Invalid artifact code hash: {name}')
        instructions = a['instructions']+a.get('helper_artifact', {}).get('instructions', [])
        pcs = {r['pc']: r for r in instructions}
        counts = {int(k): v for k, v in run['dynamic_instruction_pcs'].items()}
        if len(pcs) != len(instructions) or not set(counts) <= set(pcs) or sum(counts.values()) != run['retired_instructions']:
            raise ValueError(f'Invalid retirement accounting: {name}')
        sites = pointer_sites(instructions) if a['variant'] == 'compiled' else []
        for site in sites:
            site['executions'] = counts.get(site['pc'], 0)
            site['baseline_region_retired'] = sum(counts.get(pc, 0) for pc in site['baseline_pcs'])
            if site['operation'] == 'add':
                site['models'] = {str(size): site_model(site, size) for size in (2, 3)}
                if not a.get('guard') and not a.get('arithmetic_fault') and any(counts.get(pc, 0) != site['executions'] for pc in site['baseline_pcs']):
                    raise ValueError(f'Nonuniform successful region: {name}/{site["pc"]}')
        models = {}
        guarded_models = {}
        # Failed programs are measurements, never treated as equivalent fast paths.
        if a['variant'] == 'compiled' and not a.get('guard') and not a.get('arithmetic_fault'):
            for size in (2, 3):
                totals = Counter()
                guarded_totals = Counter()
                for s in sites:
                    if s['operation'] != 'add':
                        continue
                    model = s['models'][str(size)]
                    totals['saved_bytes'] += model['saved_bytes']
                    for key in ('saved_retired_instructions', 'added_bank_moves', 'saved_spills', 'saved_reloads'):
                        totals[key] += model[key]*s['executions']
                    guarded = guarded_site_model(s, size)
                    guarded_totals['added_bytes'] += guarded['added_bytes']
                    for key in ('added_retired_instructions', 'added_bank_moves', 'saved_spills', 'saved_reloads'):
                        guarded_totals[key] += guarded[key]*s['executions']
                models[str(size)] = dict(totals)
                guarded_models[str(size)] = dict(guarded_totals)
        rows.append(dict(name=name, variant=a['variant'], expected_guard=a.get('guard', 0), expected_arithmetic_fault=a.get('arithmetic_fault'),
                         measured={k: run[k] for k in invariant_metrics if k not in ('events', 'replay_header')},
                         sites=sites, models=models, guarded_models=guarded_models))
    successful = [r for r in rows if r['variant'] == 'compiled' and not r['expected_guard'] and not r['expected_arithmetic_fault']]
    all_compiled = [r for r in rows if r['variant'] == 'compiled']
    models = {}
    guarded_models = {}
    byte_only_models = {}
    for size in (2, 3):
        totals = Counter()
        guarded_totals = Counter()
        byte_totals = Counter()
        for r in successful:
            totals.update(r['models'][str(size)])
            guarded_totals.update(r['guarded_models'][str(size)])
            for site in r['sites']:
                if site['kind'] != 'b1_byte_access':
                    continue
                byte_totals['sites'] += 1
                byte_totals['executions'] += site['executions']
                model = site['models'][str(size)]
                byte_totals['saved_bytes'] += model['saved_bytes']
                for key in ('saved_retired_instructions', 'added_bank_moves', 'saved_spills', 'saved_reloads'):
                    byte_totals[key] += model[key]*site['executions']
        models[str(size)] = dict(totals)
        guarded_models[str(size)] = dict(guarded_totals)
        byte_only_models[str(size)] = dict(byte_totals)
    count = lambda group, operation: sum(sum(s['operation'] == operation for s in r['sites']) for r in group)
    visits = lambda group, operation: sum(sum(s['executions'] for s in r['sites'] if s['operation'] == operation) for r in group)
    # Assemble ONLY the current safe resident-B1 baseline. Never encode the candidate.
    resident = assemble('MOV r4,r1\nADD r0,r1\nBCS overflow\nMOV r1,r4\noverflow: NOP')[:4]
    tables = {name: {'assigned': sum(e is not None for e in table),
                     'unassigned': sum(e is None for e in table)} for name, table in TABLES.items()}
    return dict(schema_version=1, decision='defer',
                provenance=dict(manifest_sha256=digest(manifest_path), execution_sha256=digest(execution_path),
                                files={str(p.relative_to(ROOT)): digest(p) for p in
                                       (Path(__file__).resolve(), ROOT/'tests/test_pointer_evaluation.py',
                                        ROOT/'tests/compiler_conformance.cpp', ROOT/'tools/smallc.py', ROOT/'doc/ISA_ENCODING.md')},
                                mode=manifest['mode'], profile=manifest['profile'], compiler=manifest['compiler']),
                candidate=dict(mnemonic='ADDP.R ra,rp (study notation only; assembler rejects)',
                               operands='ra=r0-r3 unsigned 16-bit; rp=r4-r7; DP excluded',
                               result='widen unsigned(rp)+unsigned(ra); commit iff <=65535; no underflow possible',
                               flags='preserve every CSR bit',
                               failure='proposed Invalid State, instruction address; retain all pre-instruction state before entry',
                               memory='no segment, permissions, backing or MMIO probe',
                               decoder_cost=dict(operand_combinations=16, two_byte_model='16 tags in an existing escape; 0 new primaries/tables; no numeric slot selected',
                                                 three_byte_model='one escape tag plus descriptor; 16 valid operand combinations and reserved-bit validation'),
                               assigned_bytes=False),
                assumptions=['All candidate sizes, retirements and moves are modeled, not executed or assembled.',
                             'Fixed layout; retain all bounds, negative-index, integer/scale and access-width checks, failure stubs, temporaries, calls and stack ABI.',
                             'Optimistically assume scratch r4 and dead incoming/outgoing ALU flags; liveness/allocation may reduce savings.',
                             'Only successful programs contribute modeled savings; unchanged failures validate the existing compiler contract only.',
                             'Guard-retaining models need one extra scratch B0, retain the same result/error code and ALU flags, and may change scratch saved state; they are not instruction-atomic equivalence claims.',
                             'Removing the carry branch changes the fault ABI and intermediate IRQ/replay boundaries; no equivalent replay schedule is claimed.',
                             'For the current SWI #255/code 4 contract, the best compatible choice is the existing sequence: zero ISA saving. An ADDP.R inserted after the same carry precheck is redundant.'],
                current_execution_verified=True, execution_checks=execution['checks'], tables=tables,
                totals=dict(successful_programs=len(successful), all_compiled_programs=len(all_compiled),
                            successful_bytes=sum(r['measured']['static_bytes'] for r in successful),
                            successful_retired=sum(r['measured']['retired_instructions'] for r in successful),
                            successful_bank_moves=sum(r['measured']['bank_moves'] for r in successful),
                            successful_spills=sum(r['measured']['spills'] for r in successful),
                            successful_reloads=sum(r['measured']['reloads'] for r in successful),
                            add_sites_all=count(all_compiled, 'add'), add_executions_all=visits(all_compiled, 'add'),
                            subtract_sites_all=count(all_compiled, 'subtract'), subtract_executions_all=visits(all_compiled, 'subtract'),
                            add_sites_successful=count(successful, 'add'), add_executions_successful=visits(successful, 'add'),
                            models=models, byte_only_models=byte_only_models, guarded_models=guarded_models),
                resident_b1_baseline=dict(assembly=[r[2] for r in resident], bytes=sum(len(r[1]) for r in resident),
                                          retired_success=4, bank_moves=2, spills=0, reloads=0,
                                          modeled_candidate_bytes=[2, 3], modeled_candidate_retired=1,
                                          limitations='Requires a pointer resident in B1, dead ALU flags, and explicit acceptance of the different fault/IRQ contract; not current compiler output.'),
                programs=rows,
                reconsider_when=['A register-allocation/pointer-residency prototype measures savings against an equally optimized safe existing-ISA baseline.',
                                 'The guest language/kernel ABI explicitly chooses software pointer errors versus recoverable Invalid State, including saved-state/continuation rules.',
                                 'Unsigned addition and separately measured signed/subtraction needs are reviewed without reinterpreting existing ADD/ADDP bytes.',
                                 'If adopted later, complete numeric encoding review, tools/runtime integration and precise-fault/replay conformance.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = evaluate(args.build.resolve())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True)+'\n')
    print(json.dumps({'decision': report['decision'], 'totals': report['totals']}, indent=2))


if __name__ == '__main__':
    main()
