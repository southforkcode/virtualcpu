# Virtual CPU

A deterministic 16-bit CPU with four B0 accumulators, four B1 address/storage
registers, protected process maps, checked stacks, and precise instruction faults.

The **working ISA is the sole target** for the emulator, tools, examples and tests.
[ISA.md](doc/ISA.md) defines architecture and conformance;
[ISA_ENCODING.md](doc/ISA_ENCODING.md) contains the sole machine-readable encoding
source (schema 5); [ISA_EXAMPLES.md](doc/ISA_EXAMPLES.md) contains runnable examples,
software ABIs and current measurements. The authoritative
[implementation record](doc/ISA.md#implementation-status-and-remaining-work)
distinguishes implemented behavior, executed scenarios and remaining software work.

The [bootable bulk example](doc/ISA_EXAMPLES.md#bootable-bulk-transfer-and-nested-retry)
exercises LDC/STC and LDX/STX with nested fault recovery. Current compiler and
workload measurements are described in the examples reference.

## Build and validate

Requires CMake, a C++17 compiler and Python 3.10+ with its standard library.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
build/kernel_demo
build/bulk_demo
build/bulk_demo console-v1
build/console_demo --record build/terminal.jsonl
build/console_demo --replay build/terminal.jsonl
```

The kernel demo executes guest setup, two private user processes, four timer
switches and two nested user-copy fault fixups. The console demo blocks A on read,
runs B, consumes recorded input, wakes A and emits `HELLO\n`. CTest checks state at
every boundary and final RAM under replay, including cancellation and fault paths.
These are pinned integration fixtures; a production kernel needs dynamic allocation
and general process lifecycle policies.

For an AddressSanitizer/UndefinedBehaviorSanitizer build:

```sh
cmake -S . -B build-sanitized -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-sanitized -j4
ctest --test-dir build-sanitized --output-on-failure
```

## Library and tools

[src/cpu.hpp](src/cpu.hpp) exposes `VirtualCPU(ram_bytes, profile)`. RAM defaults
to 16 MiB and the profile to `MachineProfile::MinimalV1`; select `ConsoleV1` for
console input/output. Load vectors/code/data with `load`, configure installed
backing with `map`, then `reset`. Reset boots K with identity map 0; guest code
must configure protected exception entry and publish user maps before using them.
Physical frame `$FF` contains MMIO followed by holes.

`step` executes an instruction or eligible interrupt entry and returns false for
a halted/terminal poll. Host calls run on the CPU thread between steps. Mutable
registers, `load`, `map` and `peek` are trusted loader/debugger interfaces outside
recorded execution. `context()` exposes the top saved base words and
`protected_state()` exposes read-only controls, maps, frames and devices.

Submit source-specific typed events with `apply_boundary_events` or JSONL with
`apply_boundary_json`. Minimal-v1 connects edge source 1 and level source 2;
console-v1 owns source 2 for RX-ready. Guest timer source 0 is deterministic.
`run()`, `boundary()` and `next_sequence()` are nonwrapping decimal strings.
Host errors return `HostError` without partial input commits or guest exceptions.

`replay_header()` binds exact RAM/backing artifacts at reset boundary zero.
Verify it before stepping and deliver events at their recorded run/boundary.
`advance_idle_to` records virtual-time advancement; wall-clock time never advances
the timer. See the [host contract](doc/ISA.md#deterministic-host-events-and-replay).

The assembler defaults to the working ISA and origin `$0100`:

```sh
python3 tools/isa.py assemble examples/fibonacci.asm -o build/fibonacci.bin
python3 tools/isa.py disassemble build/fibonacci.bin
python3 tools/generate_example_listings.py --check
```

Tools support labels, aliases and source/destination operand order, with no linker,
directives or executable header. B1 displacements -32..31 and SP offsets 0..255
use compact encodings. Branches and near CALLR relax jointly to rel8;
`.S`/`.W` force their widths.
Disassembly retains branch width but canonicalizes memory assembly to its shortest
form; retain raw images when noncanonical encodings must be preserved. Seven decoder
tables and ordered reserved-byte checks are generated from the embedded JSON.

## Compiler and measurements

[SmallC](tools/smallc.py) compiles the [documented C-like subset and ABI](doc/ISA_EXAMPLES.md#smallc-compiler-and-executable-workloads)
through the production assembler. Its 44 compiled workloads and three assembly
references include native multiply/divide/remainder, arrays/structures, near DS/SS
pointers, checked failures, precise arithmetic faults, compact unit arithmetic,
sign extension, relative internal calls, preempted calls and a
generated blocking terminal.

```sh
python3 tools/smallc.py examples/workloads/struct_array.c --assembly build/struct_array.asm --artifact build/struct_array.json
python3 tools/measure_workloads.py --build build --output build/compiler/measurements.json
python3 tools/evaluate_pointer_arithmetic.py --build build --output build/compiler/pointer-evaluation.json
```

[Current measurements](examples/workloads/measurements.json) record code hashes,
actual retirements, stack use, moves/spills, addressing distributions and schedules.
The [pointer study](examples/workloads/pointer_study.json) keeps modeled costs
separate from executed results. Register-to-pointer arithmetic remains unassigned
pending register-allocation evidence and a defined overflow-error ABI.
