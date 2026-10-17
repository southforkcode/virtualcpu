# Virtual CPU

A deterministic 16-bit CPU with four B0 accumulators, four B1 address/storage
registers, protected process maps, checked stacks, and precise instruction faults.

The [architecture reference manual](doc/ISA.md) defines the working ISA.
[ISA_ENCODING.md](doc/ISA_ENCODING.md) contains its sole machine-readable encoding
source. [ISA.md Appendix C](doc/ISA.md#review-feedback-and-completion) records completed
review phases and conformance, including protected execution, devices and tools.

Select `--revision working` in the tools or `Revision::Working` in the CPU API
for the protected machine. It implements all currently assigned instructions:

- Byte/word ADDI, SUBI, CMPI, ANDI, ORI and XORI at the preserved `$80–$AF` encodings.
- GETC/SETC descriptor, map, kernel-entry and backing-query controls.
- Forty-two readable saved-context words, twenty editable words, eight nested
  frames, privilege exceptions, and validated U/K returns.
- MOVU byte/word user-buffer transfers with precise faults and nested recovery.
- TST/TSTI, far CALLF/RETF, and immediate byte/word shifts and rotates with counts 0–16.
- The `minimal-v1` PIC/timer window at `$FF0000–$FF00FF`, deterministic instruction
  time, explicit idle advancement, and transactional host events/replay validation.

Legacy modes `0.4` and `0.5` retain their existing encoding and execution contracts,
including the single saved context and source-less IRQ API. New encodings remain
unavailable in those modes. TST/TST.W retain their OR-self alias in every mode.
Metadata schema **3** requires runtime gating and early allowed-selector checks;
older consumers must reject it instead of ignoring those checks.

## Build

Requires CMake, a C++17 compiler and Python 3.10+ with its standard library.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
build/kernel_demo
```

The demo runs guest assembly from [examples/kernel_boot.asm](examples/kernel_boot.asm)
and six companion images. The production assembler generates their native test
fixtures during the build. Guest code configures protected entry, publishes maps
7/8, bootstraps user mode, switches between private processes on timer interrupts,
and recovers from bad user pointers through a nested memory handler. Both processes
use the same virtual selectors/offsets and distinct physical frames. No host register
mutation or context switching occurs after reset. Expected output:

```text
PASS: 5783 boundaries, 4 timer switches, 2 recovered user-copy faults
```

This is a pinned kernel integration fixture. It demonstrates the assigned hardware
interfaces; a production kernel still needs its own allocation, locking, map lease,
copy-loop, scheduler validation, and process lifecycle policies. See
[ISA_EXAMPLES.md](doc/ISA_EXAMPLES.md#executable-protected-kernel-demo) for placement
and recovery details.

## Library and tools

[src/cpu.hpp](src/cpu.hpp) exposes the CPU library. Load code, data and packed
vectors with `load`, configure installed backing with `map`, then call `reset`
to boot through vector 0. `step` performs one instruction or eligible interrupt
entry; it returns false for a halted/terminal poll that makes no progress.
All host calls run on the CPU thread between steps.

RAM defaults to 16 MiB; a smaller constructor and `map(..., false)` allow holes.
Working mode always reserves physical frame `$FF`: its first 256 bytes are MMIO
and the remainder holes. Reset map 0 supplies identity kernel descriptors; user
maps and active kernel-entry configuration start empty. Guest controls must
configure entry before exceptions can successfully enter a handler.

`peek`, `load`, `map` and mutable registers are trusted loader/debugger interfaces.
They bypass guest restrictions and are outside deterministic replay when used
without recording. `context()` exposes the top legacy-compatible saved words;
`protected_state()` exposes read-only maps, frames, controls and device state.
`diagnostic()` and `terminal_diagnostic()` preserve fault/entry diagnostics.

In working mode, submit typed `BoundaryEvent` batches using `apply_boundary_events`
or UTF-8 JSONL using `apply_boundary_json`. External edge source 1 and level source
2 are connected; timer source 0 comes from guest timer configuration. The legacy
`request_irq()` API throws in working mode. `run()`, `boundary()` and
`next_sequence()` return canonical decimal strings; boundary ordinals do not wrap.
Host errors return `HostError` without creating guest exceptions or partially
applying a batch.

A replay driver loads the exact 16 MiB RAM/backing artifacts, resets the CPU,
and calls `verify_replay_header` before stepping. Verification checks exact bytes,
profile, lowercase SHA-256 digests, current run and reset boundary. The driver owns
future-input queues: deliver all records for the current run/boundary before
`step`; discard old-run read-ahead after reset and verify the next run header.
`advance_idle_to(target)` submits a recorded idle event with the current sequence.
No wall clock advances the timer. The exact JSONL contract and error priorities
are in [ISA.md](doc/ISA.md#deterministic-host-events-and-replay).

The build derives six decoder tables and early byte validators from the embedded
JSON. C++ applies ordered validation and stages registers, context, controls,
RAM and MMIO commits until all selected checks pass.

Assemble/disassemble flat images with an explicit revision and optional 16-bit
origin (default `$0100`):

```sh
python3 tools/isa.py assemble examples/kernel_boot.asm --revision working -o build/kernel_boot.bin
python3 tools/isa.py disassemble build/kernel_boot.bin --revision working
```

The tools support labels, source/destination operand order, named/numeric selectors,
and documented aliases. They provide no linker, directives, macros or executable
header. Vectors and placement belong to the loader. Disassembly canonicalizes long
B1/MOVU forms with zero displacement to shorter assembly; retain raw images when
noncanonical bytes must be preserved.

## Validation

Three CTest suites pass in Release and AddressSanitizer/UndefinedBehaviorSanitizer
builds:

- `cpu_conformance`: **749,338 assertions** covering the retained ALU, decode,
  memory/stack, branch, trap/IRQ, published programs, exhaustive byte tests,
  immediate shifts/rotates and mixed near/far call behavior. Legacy modes remain
  isolated; working fixtures use configured entry and typed interrupts.
- `protected_conformance`: **3,173,471 checks** covering all 48 immediate ALU
  opcodes and every byte operand pair, word boundary values, flags/branches,
  controls/maps, selector endpoints, descriptor rejection, publication/invalidation,
  all context words and privilege classes, depths 1–8, invalid returns/rollback,
  terminal entry stages, MOVU forms/aliases and fault reasons, atomic stores,
  U far calls and stack policy, PIC arbitration/nesting/ACK, MMIO preflight,
  timer snapshots/deadlines/overflow, idle/reset, JSON event rollback and replay
  artifact hashes, deterministic boundary state, and the guest kernel demo.
- `toolchain`: **12 parameterized tests** checking the ledger against all six
  tables, metadata validation, exact new encodings and round trips, all registers
  and selectors, immediate/displacement endpoints, reserved/truncated forms,
  compatibility isolation, 184 retained published encodings, far/shift examples,
  and seven guest kernel images.

Counts include parameterized checks and are not a coverage percentage or a formal
proof of every conformance checklist item. [ISA.md Appendix C](doc/ISA.md#architectural-conformance-requirements)
separates required acceptance behavior from the evidence actually run. The ISA
retains candidate review status; unassigned compact forms and additional devices
are outside the implemented machine profile.
