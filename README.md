# Virtual CPU

A deterministic 16-bit CPU with four B0 accumulators, four B1 address/storage
registers, checked stack/pointer operations, and precise instruction-atomic
exceptions.

The [ISA documentation](doc/ISA.md) includes the frozen v0.5 baseline and the
**approved v0.6 direction, with an execution draft still under review**: U/K privilege, protected segment maps, bounded
nested exceptions, safe kernel stacks, deterministic interrupts/timers and MMIO.
The [decision report](doc/ISA.md#architectural-decision-report) revises process
replacement, user-access fault records, terminal entry failure and map-ID lifetime.
The current CPU and tools implement selectable **0.4 and 0.5** modes. Revision
0.6 binary/control assignments and implementation are pending; it is not yet an
executable mode. The [encoding document](doc/ISA_ENCODING.md) identifies those
remaining allocations without changing the frozen v0.5 payload.

## Build

Requires CMake, a C++17 compiler and Python 3.10+ with its standard library.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Library and tools

[src/cpu.hpp](src/cpu.hpp) exposes the CPU library. Load code, data and packed
vectors with `load`, configure installed backing with `map`, then call `reset`
to boot through vector 0. `step` performs one instruction or eligible interrupt
entry and returns false when a halt/terminal state makes no progress. Submit IRQs
between steps with `request_irq`. All host API calls run on the CPU thread.

RAM defaults to 16 MiB; a smaller constructor size and `map(..., false)` permit
backing holes. `peek`, `load` and the mutable register view are trusted host
loader/debugger interfaces. They do not apply guest register-write restrictions.
Host register setup uses canonical segment high bytes and NZCVI flags. E is
derived from the optional saved context. The execution-state enum distinguishes
Running, Halted, TerminalResetFault and TerminalDoubleFault; guest SETX edits the
saved context and IRET validates it atomically.

The build generates matching, length, revision and reserved-byte decoder tables
from the JSON embedded in [ISA_ENCODING.md](doc/ISA_ENCODING.md). C++ implements
execution semantics with ordered validation and staged register/context/RAM
commit. Revision 0.5 retains its single-context terminal nesting policy; the
approved v0.6 changes are documented separately within ISA.md.

Assemble or disassemble flat images with an explicit revision and optional
16-bit origin (default 0x100):

```sh
python3 tools/isa.py assemble examples/array_sum.asm --revision 0.5 -o build/array.bin
python3 tools/isa.py disassemble build/array.bin --revision 0.5
```

The assembler/disassembler reads operand layouts directly from the same metadata.
It supports labels, source/destination operand order and the documented aliases.
It supplies no object format, linker, directives, macros or OS executable header.
Placement, vectors and data are loader responsibilities. The syscall example
contains two images; assemble caller and handler separately at their documented
origins/segments. Disassembly is canonical: a long B1 form with zero displacement
reassembles to its shorter valid form. Keep original images to preserve such
noncanonical bytes exactly.

## Validation

The two retained CTest suites cover CPU conformance and the production toolchain.
The CPU suite passes **125,278 assertions**, including:

- 50,000 boundary/seeded ALU/carry cases, shifts and all branch predicates.
- All primary/extension bytes in both revisions, reserved fields and revision isolation.
- Checked pointers, CS/DS selection, aliases, stack limits, holes and atomic stores.
- SWI services/flags, saved-state restoration, entry failures and terminal diagnostics.
- IRQ boundary timing, HALT, reset and recovery across committed instructions.
- All ten published programs in both modes and both paths of the cross-segment syscall example.

The toolchain checks disjoint encoding tables, reproduces all **184** published
instruction encodings and tests aliases, operand rejection and revision selection.
The suites also pass AddressSanitizer and UndefinedBehaviorSanitizer builds.
Counts include parameterized assertions and are not a coverage percentage.
The 0.5 metadata retains its candidate execution status pending a complete audit
of individual normative conformance requirements. No current test result claims
v0.6 protection, nesting, timer or device conformance.

[ISA_Examples.md](doc/ISA_Examples.md) retains the completed code-density study
results and their limits. Temporary model code, private study encodings, standalone
proposal/reports and the experiment runner were retired after architecture
acceptance. Valid baseline example sources and real-CPU tests remain.
