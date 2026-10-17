# ISA worked examples

Documentation: [Architecture and conformance](ISA.md) · [Encoding metadata and allocation](ISA_ENCODING.md).

The first ten examples use the implemented instruction subset of the [working ISA](ISA.md). Each is an independent image at CS:$0100 with CS=DS=SS=0, SB=0, SP=ST=$FFFE, I=0, and segment zero installed as RAM. Set reset vector 0 to `00 01 00 00` when booting one of those images. Other vectors are unused on successful runs. Addresses and bytes are hexadecimal.

The software-trap example specifies its own setup. The C++ CPU executes the ten worked programs and syscall caller/handler with their stated results; the assembler reproduces all 184 baseline published instruction encodings. See [README.md](../README.md#validation) for current validation. The density evidence below records completed design studies; the protected-kernel demo has its own executable images. [ISA.md Appendix C](ISA.md#architectural-conformance-requirements) contains the validation contract, and [ISA_ENCODING.md](ISA_ENCODING.md) contains the machine-readable table.

## Contents

- [Worked programs](#fibonacci) and [software traps](#software-trap-and-system-call-example)
- [Far calls](#far-call-and-near-helper) and [immediate shifts/rotates](#immediate-shift-and-rotate-examples)
- [Code-density evidence](#code-density-evidence) and [calling conventions](#calling-convention-evaluation)
- [Protected kernel demo](#executable-protected-kernel-demo)
- [Test-instruction review and measurements](#test-instruction-review-and-measurements)

## Fibonacci

No data initialization. Compute F(10); DS:$2000 must contain `$0037` (55). r4 holds the constant one and moves through r3 for subtraction.

```text
0100  15 08 00       MOV.B #0,r0
0103  15 09 01       MOV.B #1,r1
0106  15 0A 0A       MOV.B #10,r2
0109  15 0C 01       MOV.B #1,r4
010C  10 2A          OR r2,r2
010E  40 0B 00       BEQ done
                     loop:
0111  13             MOV r0,r3
0112  14             MOV r1,r0
0113  10 6D          ADD r3,r1
0115  10 A3          MOV r4,r3
0117  10 7E          SUB r3,r2
0119  41 F5 FF       BNE loop
                     done:
011C  15 50 00 20    MOV.W r0,[$2000]
0120  01             HALT
```

Code size: 33 bytes. Successful run: 68 instructions.

## Array traversal and summation

Initialize DS:$2000 with words `[1, 2, 3, 4, 5]`. DS:$2100 must contain `$000F` (15). DP ends at `$200A`. The pre-loop zero test also supports an empty array.

```text
0100  15 08 00       MOV.B #0,r0
0103  15 09 05       MOV.B #5,r1
0106  15 0B 02       MOV.B #2,r3
0109  15 0C 01       MOV.B #1,r4
010C  15 02 00 20    MOV.W #$2000,r2
0110  10 9A          MOV r2,dp
0112  10 25          OR r1,r1
0114  40 13 00       BEQ done
                     loop:
0117  15 22 00 00    MOV.W [dp+0],r2
011B  10 68          ADD r2,r0
011D  10 8A          MOV dp,r2
011F  10 6E          ADD r3,r2
0121  10 9A          MOV r2,dp
0123  10 A2          MOV r4,r2
0125  10 79          SUB r2,r1
0127  41 ED FF       BNE loop
                     done:
012A  15 50 00 21    MOV.W r0,[$2100]
012E  01             HALT
```

Code size: 47 bytes. Successful run: 50 instructions.

## Signed insertion sort

Initialize DS:$2000 with words `[3, -1, 2, -3, 0]`. The five words must become `[-3, -1, 0, 2, 3]`. r4 holds the current address, r5 the exclusive end, r6 the base, and r7 the word stride. Negative DP displacement loads the previous element; BLE compares signed values.

```text
0100  15 04 02 20    MOV.W #$2002,r4
0104  15 05 0A 20    MOV.W #$200A,r5
0108  15 06 00 20    MOV.W #$2000,r6
010C  15 0F 02       MOV.B #2,r7
                     outer:
010F  10 A0          MOV r4,r0
0111  10 A5          MOV r5,r1
0113  1A 04          CMP r1,r0
0115  42 35 00       BCS done
0118  10 92          MOV r0,dp
011A  15 22 00 00    MOV.W [dp+0],r2
011E  13             MOV r0,r3
                     inner:
011F  10 A9          MOV r6,r1
0121  1A 07          CMP r1,r3
0123  40 16 00       BEQ place
0126  10 9E          MOV r3,dp
0128  15 20 FE FF    MOV.W [dp-2],r0
012C  1A 08          CMP r2,r0
012E  4D 0B 00       BLE place
0131  15 60 00 00    MOV.W r0,[dp+0]
0135  10 AD          MOV r7,r1
0137  10 77          SUB r1,r3
0139  4E E3 FF       BRA inner
                     place:
013C  10 9E          MOV r3,dp
013E  15 62 00 00    MOV.W r2,[dp+0]
0142  10 A0          MOV r4,r0
0144  10 AD          MOV r7,r1
0146  10 64          ADD r1,r0
0148  10 B0          MOV r0,r4
014A  4E C2 FF       BRA outer
                     done:
014D  01             HALT
```

Code size: 78 bytes. Successful run: 162 instructions.

## NUL-terminated byte copying

Initialize DS:$2000 with bytes `68 65 6C 6C 6F 00` (hello plus NUL). The same six bytes must appear at DS:$2100. Source and destination must not overlap. MOV.B zero-extends each byte; the terminating NUL is copied before exit.

```text
0100  15 00 00 20    MOV.W #$2000,r0
0104  15 01 00 21    MOV.W #$2100,r1
0108  15 0B 01       MOV.B #1,r3
                     loop:
010B  10 92          MOV r0,dp
010D  15 2A 00 00    MOV.B [dp+0],r2
0111  10 96          MOV r1,dp
0113  15 6A 00 00    MOV.B r2,[dp+0]
0117  10 2A          OR r2,r2
0119  40 07 00       BEQ done
011C  10 6C          ADD r3,r0
011E  10 6D          ADD r3,r1
0120  4E E8 FF       BRA loop
                     done:
0123  01             HALT
```

Code size: 36 bytes. Successful run: 55 instructions.

## Recursive factorial with stack locals

No data initialization. Compute 5! using recursive calls and repeated-addition multiplication; DS:$2000 must contain `$0078` (120). Each frame pushes n as a local, reads it at `[sp+0]`, then removes it before RET. SP returns to `$FFFE`; minimum SP is `$FFEA`. No multiply instruction or register-preservation pseudo is required.

```text
0100  15 08 05       MOV.B #5,r0
0103  1F 10 0C 01    CALL fact
0107  15 50 00 20    MOV.W r0,[$2000]
010B  01             HALT
                     fact:
010C  1F 00          PUSH r0
010E  15 09 01       MOV.B #1,r1
0111  1A 04          CMP r1,r0
0113  49 1C 00       BLS base
0116  10 74          SUB r1,r0
0118  1F 10 0C 01    CALL fact
011C  11             MOV r0,r1
011D  15 32 00 00    MOV.W [sp+0],r2
0121  15 08 00       MOV.B #0,r0
0124  15 0B 01       MOV.B #1,r3
                     multiply:
0127  10 64          ADD r1,r0
0129  10 7E          SUB r3,r2
012B  41 F9 FF       BNE multiply
012E  1F 0B          POP r3
0130  1F 11          RET
                     base:
0132  15 08 01       MOV.B #1,r0
0135  1F 0B          POP r3
0137  1F 11          RET
```

Code size: 57 bytes. Successful run: 101 instructions.

## Multiword carry propagation

Compute `$0001FFFF + $00020001 = $00040000`. DS:$2000 receives the low word `$0000`; DS:$2002 receives the high word `$0004`. ADD produces carry consumed by ADC; intervening stores preserve the resulting flags.

```text
0100  15 00 FF FF    MOV.W #$FFFF,r0
0104  15 09 01       MOV.B #1,r1
0107  15 0A 01       MOV.B #1,r2
010A  15 0B 02       MOV.B #2,r3
010D  10 68          ADD r2,r0
010F  10 4D          ADC r3,r1
0111  15 50 00 20    MOV.W r0,[$2000]
0115  15 51 02 20    MOV.W r1,[$2002]
0119  01             HALT
```

Code size: 26 bytes. Successful run: 9 instructions.

## B1 pointers in independent code and data segments

This example starts with the common initial state above, then sets DS=3. Install `hello` plus NUL at physical `$032000` and the word `$0055` at CS:$0300. Install segment 3 as RAM. It copies six bytes to `$032100`, stores the CS constant at `$032108`, and finishes with r4=$2005, r5=$2105, and r6=$0300. Both signed memory displacement and signed pointer adjustment are exercised; no DP transfers or accumulator temporaries are needed to advance the pointers.

```text
0100  15 08 03       MOV.B #3,r0
0103  10 D2          MOV r0,ds
0105  15 04 00 20    MOV.W #$2000,r4
0109  15 05 00 21    MOV.W #$2100,r5
010D  15 06 02 03    MOV.W #$0302,r6
0111  15 B2 02 FE FF MOV.W [cs:r6-2],r2
0116  22 FE          ADDP #-2,r6
0118  15 D2 01 08 00 MOV.W r2,[ds:r5+8]
                     loop:
011D  15 88 00       MOV.B [ds:r4],r0
0120  15 C8 01       MOV.B r0,[ds:r5]
0123  10 20          OR r0,r0
0125  40 07 00       BEQ done
0128  20 01          ADDP #1,r4
012A  21 01          ADDP #1,r5
012C  4E EE FF       BRA loop
                     done:
012F  01             HALT
```

Code size: 48 bytes. Successful run: 48 instructions.

## Count-controlled traversal through offset $FFFF

Install bytes `03 04 05` at DS:$FFFD through DS:$FFFF. The program sums them to `$000C` at DS:$2000 and stops with r4=$FFFF. The counter is checked before ADDP, so no one-past-end pointer is required. A separate attempted ADDP #1 at that final pointer must fault with Invalid State.

```text
0100  15 04 FD FF    MOV.W #$FFFD,r4
0104  15 09 03       MOV.B #3,r1
0107  15 0B 01       MOV.B #1,r3
010A  15 0A 00       MOV.B #0,r2
                     loop:
010D  15 88 00       MOV.B [r4],r0
0110  10 62          ADD r0,r2
0112  10 7D          SUB r3,r1
0114  40 05 00       BEQ done
0117  20 01          ADDP #1,r4
0119  4E F1 FF       BRA loop
                     done:
011C  15 52 00 20    MOV.W r2,[$2000]
0120  01             HALT
```

Code size: 33 bytes. Successful run: 22 instructions.

## Stack locals with ADJSP

The callee saves r4/r5, reserves twelve local bytes with ADJSP #-12, accesses slot zero, then releases the frame with ADJSP #12. Expect words `$000E`, `$1111`, `$2222` at DS:$2000/$2002/$2004 and `$0007` at DS:$3000/$3002. SP returns to $FFFE; minimum SP is $FFEC. This follows the current callee-preserved B1 convention.

```text
0100  15 04 11 11    MOV.W #$1111,r4
0104  15 05 22 22    MOV.W #$2222,r5
0108  15 08 07       MOV.B #7,r0
010B  1F 10 1C 01    CALL framed
010F  15 50 00 20    MOV.W r0,[$2000]
0113  15 54 02 20    MOV.W r4,[$2002]
0117  15 55 04 20    MOV.W r5,[$2004]
011B  01             HALT
                     framed:
011C  1F 04          PUSH r4
011E  1F 05          PUSH r5
0120  25 F4          ADJSP #-12
0122  15 70 00 00    MOV.W r0,[sp+0]
0126  15 04 00 30    MOV.W #$3000,r4
012A  15 05 02 30    MOV.W #$3002,r5
012E  15 C0 00       MOV.W r0,[r4]
0131  15 31 00 00    MOV.W [sp+0],r1
0135  15 C1 01       MOV.W r1,[r5]
0138  10 64          ADD r1,r0
013A  25 0C          ADJSP #12
013C  1F 0D          POP r5
013E  1F 0C          POP r4
0140  1F 11          RET
```

Code size: 66 bytes. Successful run: 22 instructions.

## Register-target code-table dispatch

Install the target label offset as a little-endian word at CS:$0300 (the resolved value is shown below). Load that word into r5, then JS r5. It jumps within current CS without another memory dereference or a stack push. The `$DEAD` fallthrough is skipped; DS:$2000 becomes `$BEEF` and SP stays $FFFE. The table word is `$010E`.

```text
0100  15 04 00 03    MOV.W #$0300,r4
0104  15 A5 00       MOV.W [cs:r4],r5
0107  1F 61          JS r5
0109  15 00 AD DE    MOV.W #$DEAD,r0
010D  01             HALT
                     target:
010E  15 00 EF BE    MOV.W #$BEEF,r0
0112  15 50 00 20    MOV.W r0,[$2000]
0116  01             HALT
```

Code size: 23 bytes. Successful run: 6 instructions.

## Design observations

The original six examples retain their encodings and results. B1 traversal avoids accumulator pointer updates; count-controlled termination handles a byte at offset $FFFF without wrapping. ADJSP supports compact stack-local allocation, and register-target JS supports table dispatch. Keep the current B1 preservation convention until caller-liveness measurements justify changing the ABI. The working-copy extension directions and remaining deferrals are defined in [ISA.md](ISA.md#extension-status-and-retained-evidence).

## Software trap and system-call example

[examples/syscall.asm](../examples/syscall.asm) defines an illustrative service 1: add input r0+r1 modulo 65536, return the result in r0 and status zero in r1. Other service numbers return r0=0/r1=1 (this example's unsupported-service status). These numbers are example OS policy, not architectural assignments.

Install caller bytes at CS=0, PC=$0100, and handler bytes at CS=1, PC=$0200. Set vector 6 at physical `$18` to `00 02 01 00`. Start with valid saved-restorable stack limits and ordinary RAM installed. No initialized or mapped stack storage is required because neither program accesses it. DS/SS are arbitrary valid segments; I may be zero or one and no external request is pending for this example.

Caller, 9 bytes:

```text
0100  15 08 07       MOV.B #7,r0
0103  15 09 05       MOV.B #5,r1
0106  26 01          SWI #1
0108  01             HALT
```

Handler, 42 bytes in segment 1:

```text
0200  1F 34 02       GETX xtrap,r2
0203  15 0B 01       MOV.B #1,r3
0206  1A 0E          CMP r3,r2
0208  41 12 00       BNE unsupported
020B  1F 20 00       GETX xr0,r0
020E  1F 21 01       GETX xr1,r1
0211  10 64          ADD r1,r0
0213  1F 40 00       SETX r0,xr0
0216  10 05          XOR r1,r1
0218  1F 41 01       SETX r1,xr1
021B  1F 12          IRET
unsupported:
021D  10 00          XOR r0,r0
021F  15 09 01       MOV.B #1,r1
0222  1F 40 00       SETX r0,xr0
0225  1F 41 01       SETX r1,xr1
0228  1F 12          IRET
```

At entry, xpc=$0108, xcs=0, xcause=6, xtrap=1, and the address words are zero. The caller eventually halts with r0=12, r1=0, r2–r7/DP/segments/stack/CSR restored, and PC=$0109. The handler's temporary r2/r3 and arithmetic flags do not escape. Changing SWI's immediate to an unsupported value resumes at the same address with r0=0/r1=1.

<!-- WORKED_IMAGES_END -->

## Far call and near helper

This implemented working-mode example uses two separately assembled images:
[far_call.asm](../examples/far_call.asm) in CS=0 at `$0100`, and
[far_callee.asm](../examples/far_callee.asm) in CS=1 at `$0200` (physical
`$010200`). Start with CS=DS=SS=0, PC=$0100, SB=0, SP=ST=$FFFE, flags=0,
and both code images/stack installed RAM. Reset vector 0 is `00 01 00 00`.
It runs on the protected working machine using reset identity kernel descriptors
for executable code and writable/readable RAM stacks.

```sh
python3 tools/isa.py assemble examples/far_call.asm --revision working --origin 0x100 -o build/far_call.bin
python3 tools/isa.py assemble examples/far_callee.asm --revision working --origin 0x200 -o build/far_callee.bin
```

Caller, fourteen bytes:

```text
0100  15 00 05 00    MOV.W #5,r0
0104  15 01 01 00    MOV.W #1,r1
0108  54 00 02 01 00 CALLF $010200
010D  01             HALT
```

Callee and near helper, nine bytes:

```text
0200  1F 10 05 02    CALL helper
0204  55             RETF
                     helper:
0205  10 64          ADD r1,r0
0207  1F 11          RET
```

CALLF pushes `0D 01 00 00` at SS:$FFFA; the near CALL pushes `04 02` at
SS:$FFF8. The helper uses RET to return within CS=1; its caller uses RETF
to restore CS=0/PC=$010D. The run halts after eight instructions with r0=6,
r1=1, PC=$010E, CS=0, SP=$FFFE, and minimum SP=$FFF8. DS/SS remain zero.
ADD changes flags normally; CALL/CALLF/RET/RETF preserve them individually.
Return mnemonics must match their frame types. No syscall or privilege
transition occurs. The CPU conformance suite executes these exact image bytes;
the toolchain checks source assembly and round trips independently.

The [C++ loader](../examples/far_call.cpp) runs those two assembled images with
an explicit instruction limit and checks the final result. After the normal
CMake build and the assembly commands above, run from the repository root:

```sh
c++ -std=c++17 -Isrc examples/far_call.cpp build/libvirtualcpu.a -o build/far_call_example
build/far_call_example
```

## Immediate shift and rotate examples

[immediate_shifts.asm](../examples/immediate_shifts.asm) is an executable
working-mode image at CS:$0100, with CS=DS=SS=0, SB=0, SP=ST=$FFFE,
flags=0 and ordinary installed RAM. Set reset vector 0 to `00 01 00 00`.

```sh
python3 tools/isa.py assemble examples/immediate_shifts.asm --revision working -o build/immediate_shifts.bin
python3 tools/isa.py disassemble build/immediate_shifts.bin --revision working
```

The 89-byte image halts after 26 instructions at PC=$0159. It stores the
following word results at DS:$2000–$2010 in order:

| Operation and input | Stored result | C produced |
|---|---|---|
| SHRI.W #8 on `$12AB` (extract upper byte) | `$0012` | 1 |
| ROLI.W #8 on `$12AB` (swap bytes) | `$AB12` | 0 |
| SHLI.W #16 on `$12AB` | `$0000` | 1 |
| SARI.W #16 on `$80AB` | `$FFFF` | 1 |
| ROLI.W #16 on `$12AB` | `$12AB` | 1 |
| RORI.W #16 on `$12AB` | `$12AB` | 0 |
| ROLI.B #8 on `$1282` | `$0082` | 0 |
| RORI.B #16 on `$0082` | `$0082` | 1 |
| SHLI.B #0 on `$12AB` | `$12AB` | Preserved 1 |

The final MOV loads `$12AB` without changing flags, then the zero-count byte
operation preserves its upper byte and all flags. Final r0=$12AB and N/Z/C=1/0/1
still reflect the earlier byte rotation; they are not recomputed from that word.
V/I/E remain zero. Positive byte rotations clear the upper byte even when a full
rotation leaves the low byte unchanged. The CPU executes these exact bytes and
checks all nine result words; the toolchain reproduces and round-trips the source.

Compared with eight original one-bit word shifts (sixteen bytes/eight
instructions), `SHRI.W #8,rd` uses three bytes/one instruction and commits once.
The original sequence permits seven additional IRQ boundaries; matching final
values is not a promise of identical interrupt or fault observation points.
Rotations are true rotations within 8/16 bits; they never consume the input C.

## Code-density evidence

These are retained results of the completed RAM-only design studies. Temporary
assemblers, execution models, private study encodings and their runner have been
removed after the architectural direction was accepted. The tables are historical design evidence,
not reproducible benchmark commands or full working-copy conformance results. Production
conformance tests continue to execute the baseline images above.

The imm4 columns below refer to the retired signed-four-bit, word-ALU prototype,
not the assigned immediate `.B`/`.W` families. Assigned byte forms compute
8-bit flags/results, while assigned word forms are three bytes. The retained
study sizes therefore must not be attributed to the new formats without
remeasuring. The earlier SUBI deferral applies to that old prototype only.

### Array summation

The published 47-byte program leaves its final pointer in DP. The compact B1
source in [array_sum.asm](../examples/array_sum.asm) instead leaves the pointer in
r4 and leaves DP unchanged; it occupies 34 bytes, executes 33 instructions for
five words, and still handles an empty array. The
[DP-preserving source](../examples/array_sum_dp.asm) uses 37 bytes and 34 instructions.
Both sum modulo 65536, leave r1=0 and write DS:$2100. These standalone programs
do not promise callee preservation of r4. Input placement supports counts 0–128.

| Variant | Zero-safe bytes | Positive-count bytes | Instructions for five words | Fetched bytes |
|---|---:|---:|---:|---:|
| Published baseline | 47 | — | 50 | 123 |
| Compacted baseline, DP | 37 | 32 | 34 | 89 |
| Compacted baseline, B1 | 34 | 29 | 33 | 82 |
| rel8 only | 32 | 28 | 33 | 76 |
| B1 simm6 only | 34 | 29 | 33 | 82 |
| imm4 only | 31 | 26 | 32 | 79 |
| Combined rel8/B1/imm4 study | 29 | 25 | 32 | 73 |

Positive-count forms omit the OR/BEQ empty-array guard and require count ≥1.
Only baseline sources are executable by the current toolchain. B1 simm6 saves
nothing in this loop because its displacements are zero. Immediate arithmetic
removes a constant setup and frees r3; ADDI #-1 can supply the decrement without
a separate SUBI opcode. The study checked empty, boundary-valued, random and
128-word inputs, but its percentages are not whole-kernel estimates.

### OS-oriented workloads

Eight small workloads represented syscall dispatch, a 17-word saved-context
switch, copying, strings, lists, stack frames, an interrupt counter and structures.
All setup, handlers and constant instructions count toward code size; identical
initialized input/PCB data is excluded. The context-switch baseline replaces the
implemented return frame and demonstrates no isolation. The interrupt ACK is a RAM write,
not a device. All eight baseline programs were compared against the implemented CPU;
64 program/feature variants were checked in the retired RAM model.

| Workload | Implemented bytes | rel8 | B1 simm6 | imm4 | ADDI+CMPI | SP uimm8 | Post-inc | All | Instructions implemented → all | B0/B1 used implemented → all |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| syscall dispatch | 59 | 58 | 59 | 56 | 56 | 59 | 59 | 55 | 17 → 16 | 4/0 → 3/0 |
| context frame switch | 281 | 281 | 221 | 281 | 281 | 281 | 281 | 221 | 73 → 73 | 1/1 → 1/1 |
| memory copy (32 bytes) | 30 | 29 | 30 | 27 | 27 | 30 | 26 | 22 | 101 → 68 | 3/2 → 2/2 |
| NUL string copy | 24 | 23 | 24 | 24 | 24 | 24 | 20 | 19 | 75 → 51 | 1/2 → 1/2 |
| linked-list sum | 28 | 27 | 26 | 28 | 28 | 28 | 28 | 25 | 22 → 22 | 3/1 → 3/1 |
| stack-frame call | 62 | 62 | 62 | 62 | 62 | 54 | 62 | 54 | 21 → 21 | 2/1 → 2/1 |
| interrupt counter/ACK | 24 | 24 | 22 | 21 | 21 | 24 | 24 | 19 | 9 → 8 | 2/1 → 1/1 |
| small structure | 49 | 48 | 37 | 49 | 49 | 49 | 49 | 36 | 14 → 14 | 3/1 → 3/1 |
| **Total** | 557 | 552 | 481 | 548 | 548 | 549 | 549 | 451 | | |

Combined study code falls from **557 to 451 bytes (19.0%)**. Single-feature columns
share one baseline and their savings cannot simply be added. Compact B1 accesses
benefit context spills, lists and structures; short SP offsets benefit frames;
rel8 benefits control flow; immediates remove setup/constant registers.
ADDI+CMPI matches the three-operation immediate study, so SUBI has no additional
measured size benefit here. Post-increment benefits both copy loops, but remains
deferred pending compiler/runtime workloads.

Instruction counts are dynamic guest instructions, including SWI/IRET/HALT and
excluding hardware IRQ entry. They are not cycle counts. B0/B1 counts are distinct
explicit working registers, excluding saved xrN words: a pressure proxy, not a
compiler liveness/spill analysis. Optimized forms permit scratch-register and
relocated-PC differences; output RAM, flags, stack balance, segments and DP were
checked. The corpus is small and does not establish protection, nested recovery,
MMIO ordering or identical fault/interrupt observation points after fusion.

The [working-copy density decisions](ISA.md#extension-status-and-retained-evidence) are the current
architecture dispositions; private study bytes are not assigned encodings.

## Calling-convention evaluation

The existing recursive factorial uses no B1 registers, so either convention has no B1 save/restore cost. The new stack-frame example uses r4/r5 and its caller observes their original values after return; that is a live-across-call case. The original copy/sort examples run as standalone images and cannot by themselves establish callee-save costs.

The following is a static instruction/byte comparison for two borrowed B1 registers, not a benchmark. PUSH and POP are each two bytes.

| Caller use of r4/r5 | Current ABI: both callee-preserved | Alternative ABI: both caller-saved |
|---|---|---|
| Both dead after the call | Callee saves/restores: 4 instructions, 8 bytes | No preservation required: 0 instructions, 0 bytes |
| Both live after the call | Callee saves/restores: 4 instructions, 8 bytes | Caller saves/restores: 4 instructions, 8 bytes |
| Neither used by the callee | No preservation required | No preservation required |
| Multiple live call sites to one callee | One shared 8-byte callee sequence | Preservation sequences may be repeated at each call site |

Keep the current ABI until real call graphs and liveness show a consistent benefit. Measure total caller plus callee code bytes and executed instructions, including recursive calls and pointer-heavy leaf routines. Changing the ABI later does not require changing instruction encodings.


## Executable protected kernel demo

Build and run `build/kernel_demo` from the repository root. CMake uses the
production assembler and [generate_kernel_fixtures.py](../tools/generate_kernel_fixtures.py)
to build all seven images in explicit working mode. The
[loader](../examples/kernel.cpp) installs vectors, code and zeroed data, then
only calls `step`; descriptor setup and every context switch execute in guest code.

| Image | Kernel selector 0 offset / physical placement | Purpose |
|---|---|---|
| [kernel_boot.asm](../examples/kernel_boot.asm) | `$0100` | Commit entry, publish maps 7/8, initialize twenty-word process images, enable timer and bootstrap U |
| [kernel_irq.asm](../examples/kernel_irq.asm) | `$1000` | Source-0 IRQ: spill current U frame, replace it with the other process, ACK and IRET |
| [kernel_swi.asm](../examples/kernel_swi.asm) | `$1200` | Bootstrap, copy service and progress/shutdown services |
| [kernel_memory.asm](../examples/kernel_memory.asm) | `$1300` | Match registered user-access failure, edit K recovery PC and saved access controls, IRET |
| Fatal HALT | `$1400` | Stop unexpected exceptions/services |
| [kernel_copy.asm](../examples/kernel_copy.asm) | `$1500` | One-word copy fixture; registered fault `$150F`, error continuation `$1521` |
| [kernel_load_context.asm](../examples/kernel_load_context.asm) | `$1600` | Load twenty writable saved words from a pinned process image |
| [kernel_user.asm](../examples/kernel_user.asm) | `$010100` and `$020100` | Same virtual U program in two private physical frames |

Vectors 0/2/5/6 select boot/memory/IRQ/SWI; other assigned exceptions select
fatal HALT. Kernel state at `$3002/$3004` records user progress, `$3006` counts
switches, and `$3008` counts recovered copies. Process images occupy `$4000/$4100`.
Map 7/8 code selector 1 is execute-only, data selector 3 maps physical frames
3/4, and stack selector 2 maps frames 5/6 with policy `$1000–$2000`.
Both user counters are virtual `3:$2000`; they reside at physical `$032000/$042000`.

Boot commits a kernel-entry stack independent of either user stack. SWI 0 creates
a K-origin frame; PREPARE_USER_RETURN validates and marks it before first U IRET.
Each user issues SWI 2 with an intentionally absent target selector 5. The copy
uses MOVU and faults under U permissions while the caller's valid data selector
remains 3. A second frame captures the K MOVU instruction and exact memory
metadata. The handler checks origin/phase/access/width/PC/CS/reason, clears live
and saved access controls, and redirects only that K frame to the error path.
The copy service then edits the lower U frame to return error 1. No user-map
invalidation or user-stack access is needed for recovery.

The timer first fires at time 4096 and repeats every 512 retired instructions.
Its pinned, I=0 handler saves all twenty writable words, loads the other known
valid image, acknowledges source 0, and returns through hardware validation.
These fixed images need no allocator or stale-handle lease. A production
scheduler must prevalidate candidates and keep a rollback image across its
replacement window as specified in the software ABI. The protected conformance
suite separately rejects an invalid candidate at IRET and restores the original
process through a nested K rollback continuation.

Successful shutdown requires both private counters at least 3 and at least four
timer switches; it disables the timer/PIC and HALTs in the SWI handler. The demo
completes 5,783 CPU boundaries with four switches and two recovered copy faults.
The conformance run executes a second identical machine, compares registers,
frames, controls, mappings and devices at every boundary, and compares final RAM.

## Test-instruction review and measurements

The accepted design supports B0-only TST.B, TSTI.B and TSTI.W in working mode,
with TST/TST.W as the existing OR-self alias in all modes. The
[normative instruction entry](ISA.md#tst-tsti--test-a-register-without-writing-it)
defines widths, flags, literals and fault behavior; the
[encoding reference](ISA_ENCODING.md#assigned-test-layouts-and-runtime-modes)
defines their exact bytes and runtime gates. These measurements explain the
accepted design without assigning any UART register layout.

### Design rationale

A dedicated TST.W encoding would have the same register preservation, flags,
length and retirement behavior as OR rd,rd, so it would spend four tags without
adding capability or density. Canonical disassembly stays OR. TST.B saves one
byte over TSTI.B #$FF and directly expresses byte zero/sign testing. Extending
the three encoded forms to r4–r7 would cost twelve more tags; unary B1 word
testing would need four tags or an accumulator temporary. No measured workload
justifies that expansion. Internal and memory operands remain unsupported.

Tests sample a register once and do not access operand memory, combine an MMIO
read with a test, or provide memory atomicity. BEQ/BNE and BMI/BPL use the new
Z/N; carry/overflow predicates use retained C/V. BLT/BGE after a test are not a
fresh signed comparison. Use CMPI for character equality and range comparisons,
including carriage return and line feed.

### Size and instruction comparison

Counts below are static bytes/instructions, with equivalent branch bodies
excluded. A temporary is an additional live B0 register. MOV register copies
cost one byte for distinct B0 registers. ANDI uses its assigned and implemented
two/three-byte forms. These are local sequence measurements, not compiler or
end-to-end UART performance results.

| Task / sequence | Bytes | Instructions | Temporaries | MMIO reads |
|---|---:|---:|---:|---:|
| Word zero/sign: `OR r0,r0` or `TST.W r0` | 2 | 1 | 0 | 0 |
| Arbitrary byte zero/sign: copy, `ANDI.B #$FF` | 3 | 2 | 1 | 0 |
| Arbitrary byte zero/sign: `TST.B r0` | 2 | 1 | 0 | 0 |
| Preserve byte, test mask: copy, `ANDI.B` | 3 | 2 | 1 | 0 |
| Preserve byte, test mask: `TSTI.B` | 3 | 1 | 0 | 0 |
| Preserve word, test mask: copy, `ANDI.W` | 4 | 2 | 1 | 0 |
| Preserve word, test mask: `TSTI.W` | 4 | 1 | 0 | 0 |
| RX/TX flags: one load, two copy/ANDI.B/BNE sequences | 15 | 7 | 1 | 1 |
| RX/TX flags: two load/ANDI.B/BNE sequences | 16 | 6 | 0 | 2 |
| RX/TX flags: one load, two TSTI.B/BNE sequences | 15 | 5 | 0 | 1 |

If the upper byte is known zero, `OR rd,rd` can test byte zero using its word
Z. Its N comes from bit 15, so it cannot replace byte sign testing. Using only
the legacy register ALU, zero-only mask testing can load a mask, copy the value,
then AND registers (six bytes, three instructions, two temporaries); hoisting
the mask changes that comparison and must be accounted for separately.

### UART and kernel sequences

Assume r4 points to a hypothetical byte-wide MMIO sample register, whose bit 0
is RX-ready and bit 1 TX-ready. This fixture is not a UART register allocation.
Branches here dispatch separate handlers; no sample reread is needed:

```asm
MOV.B [r4],r0
TSTI.B #$01,r0
BNE rx_ready
TSTI.B #$02,r0
BNE tx_ready
```

For a path that processes both flags, have the RX handler return to the second
test with r0 preserved. Every repeated polling iteration deliberately samples
again; tests within an iteration use that iteration's one register snapshot.

The side-effect fixture returns `$03` on its first read and consumes that
sample; its next read returns `$00`. Testing masks 1 and 2 against the first
snapshot finds both flags. Re-reading for the second test loses TX-ready and
consumes another sample. The fixture demonstrates why reloads are not generally
equivalent without assuming a property of every real UART status register.

For an ASCII byte already in r0, `TST.B r0` followed by BEQ detects NUL, while
BMI detects bit 7 (non-ASCII). `TSTI.B #$80,r0` followed by BNE explicitly tests
the high bit. For kernel flags, `TSTI.W #$8001,r0` followed by BNE detects either
selected flag while retaining the entire original word for later tests.

### Exact test vectors

Flags list only N/Z; C/V/I/E retain their input values in every row.

| Instruction | Original rd | Mask | Final rd | N/Z |
|---|---|---|---|---|
| TST.B | `$FF00` | implicit `$FF` | `$FF00` | 0/1 |
| TST.W | `$FF00` | implicit `$FFFF` | `$FF00` | 1/0 |
| TST.B | `$1280` | implicit `$FF` | `$1280` | 1/0 |
| TSTI.B | `$12FF` | `$00` | `$12FF` | 0/1 |
| TSTI.B | `$12FF` | `$FF` | `$12FF` | 1/0 |
| TSTI.B | `$1280` | `$80` | `$1280` | 1/0 |
| TSTI.W | `$1280` | `$0080` | `$1280` | 0/0 |
| TSTI.W | `$8000` | `$8000` | `$8000` | 1/0 |
| TSTI.W | `$FFFF` | `$FFFF` | `$FFFF` | 1/0 |

### Review disposition and current validation

The accepted forms consume twelve secondary tags at $1A $40–$4B and no primary
opcodes. The word alias saves four tags against the initial sixteen-tag budget.
With immediate shifts/rotates included, ext1a is 80 assigned / 176 unassigned;
the complete primary map remains 95 / 161 with $80–$AF preserved. Metadata
schema 3 gates new forms to working mode and validates selector bytes early;
older-schema consumers must reject unsupported versions.

The production toolchain checks exact bytes, aliases, all destinations, literal
boundaries, labels, segment ends, truncation, reserved neighbors, legacy rejection,
sequence sizes and the read-to-consume fixture. CPU tests exhaust all unary input
words and byte value/mask pairs, cover flags/branches and precise failure, and
execute the forms in protected U/K. See the
[conformance requirements](ISA.md#conformance-tst-and-tsti) and
[current executed evidence](ISA.md#executed-implementation-evidence) for coverage.
The hypothetical byte-wide UART fixture is independent of the assigned minimal-v1
word-only PIC/timer window and does not claim a UART device implementation.
