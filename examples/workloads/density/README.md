# Current-ISA density probes

These executable probes compare current-ISA implementations under explicit
input/output, flag, fault and register-preservation contracts.
The copy probes use existing software helpers. Arithmetic compares software
helpers with assigned native instructions and explicit status wrappers. Descriptor lifecycle
now executes assigned LDC/STC DESC/1. The demos execute assigned CTX/1 STX/LDX;
word MUL/DIV/MOD and cause 8 are assigned. These software-helper probes retain
their original ABI/status behavior for comparison; SmallC 0.3 uses native scalar arithmetic and propagates cause 8.
The [comparison criteria](adoption.json) define the measurement contract.

`tools/generate_density_fixtures.py` composes drivers and these assembly helpers
with the production assembler. Its generated JSON retains source, symbols,
inputs, expected memory, bytes and hashes. The native `density_census` target
executes 205 probes, checks results and balanced stacks, and compares observed
execution with five executions with measurement disabled. It also runs both
kernel demos. The compiler conformance target supplies the 47 compiled
and hand-written cases, with full-state replay comparisons at each boundary.

Arithmetic helpers take r0/r1, return the product or quotient in r0, and return
the remainder in r1 for division. They preserve B1, I and E; r0–r3 and NZCV are
caller-clobbered. Signed division truncates toward zero, and the remainder has
the dividend's sign. A little-endian software status word at DS:$3F00 is initially
zero: 1 means zero divisor, 2 means MIN/-1, and 3 means invalid decimal input or
overflow. Error helpers return zero results. These software statuses do not
describe the native CPU arithmetic exception (cause 8). Truncated multiplication is unsuitable for
checked pointer scaling without additional overflow checks.

The cases include deterministic randomized signed/unsigned division, multiply,
checked five-digit parsing, zero-padded five-digit formatting, byte/word copies,
all 256 signed-byte conversions, constant strength reduction, register-save
liveness, relocated direct calls and an indirect CALL-to-JS trampoline. The
existing compiler corpus supplies checked arrays/structures and kernel helpers;
the kernel demo supplies atomic twenty-word context switching and nested copy recovery.
A separate descriptor lifecycle exercises the reusable control library, one-time
initialization, creation, replacement, atomic committed readback and rejected updates.

Safe alternatives have explicit limits: copy buffers are valid and disjoint;
shift-based sign extension requires dead flags; leaf save elimination preserves
all registers the leaf can actually change. The trampoline uses the existing
near-call return frame and remains segment-local. Relocated calls are absolute
CALL instructions assembled at two origins, providing the baseline against
which the assigned CALLR probes are compared.

Build and run `ctest` as shown in the [project README](../../../README.md#build-and-validate).
`tools/measure_density.py` rejects stale artifact/source hashes and refuses to
overwrite an existing report. It joins mode/map/CS/PC
observations to source instructions, then writes summary JSON, Markdown and a
compressed per-PC JSON sidecar. Use a fresh output path for each experiment.

The CPU's optional host-only traffic counters are disabled by default and do not
enter replay state. They count actual RAM accesses, including speculative operand
reads before a fault and exception-vector reads, and committed RAM writes.
Instruction-fetch reads are a subset. Pure MMIO samples, device commits, backing
probes and descriptor lookups remain separate. These count backing-classification
and descriptor-helper calls, not internal tree comparisons, insertions or allocator
work. Loader/debugger reads are excluded
from step counts. Timing reports distinguish the instrumented observer, five
uninstrumented loop samples, and deterministic retirement ticks; none establishes
an architectural worst-case latency bound. The report lists footprint scope and
all other measurement limits explicitly.

The descriptor-cycle comparisons share one request ABI and the same complete
control-library wrappers. The scalar reference expands install/readback into
GETC/SETC and uses hoisted-zero removal. Production uses LDC/STC for install/
readback and keeps scalar removal. The measured absent-image alternative replaces
only removal with [a private RAM image](absent_descriptor.asm). These comparisons
assume owned, pinned valid RAM, I=0 and no competing publisher; equal successful
outputs do not equate multi-instruction scalar fault/IRQ behavior with atomic
LDC/STC. Each library image, its recovery registry, records, guards and wrappers
is counted once in footprint totals. Per-operation body categories supplement
the complete wrapper-inclusive measurements.

`tools/evaluate_density_candidates.py` produces a source-qualified occurrence
census and independent cost models using the current probes. Assigned families
are marked explicitly; deferred forms remain rejected. Model savings exclude
compiler optimizations already available and are never added across overlapping
patterns or repeated shared kernels.

After building and running CTest, generate reports with fresh output paths:

```sh
python3 tools/measure_density.py --build build --output /tmp/isa-density/report.json
python3 tools/evaluate_density_candidates.py --build build --output /tmp/isa-density/candidates.json
```
