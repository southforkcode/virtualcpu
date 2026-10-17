# ISA worked examples

The first ten examples use the revision 0.4 subset of [revision 0.5 baseline](ISA.md#revision-05-baseline), with unchanged bytes and ordinary execution behavior. Each is an independent image at CS:$0100 with CS=DS=SS=0, SB=0, SP=ST=$FFFE, I=0, and segment zero installed as RAM. Set reset vector 0 to `00 01 00 00` when booting one of those images. Other vectors are unused on successful runs. Addresses and bytes are hexadecimal.

The software-trap example specifies its own setup. The C++ CPU executes the ten worked programs and syscall caller/handler with their stated results; the assembler reproduces all 184 published instruction encodings. See [README.md](../README.md#validation) for current validation. The density evidence below records the completed design studies and does not supply executable revision 0.6 programs. [ISA.md](ISA.md#conformance-requirements) contains the validation contract, and [ISA_ENCODING.md](ISA_ENCODING.md) contains the machine-readable table.

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

This revision 0.4-compatible example starts with the common initial state above, then sets DS=3. Install `hello` plus NUL at physical `$032000` and the word `$0055` at CS:$0300. Install segment 3 as RAM. It copies six bytes to `$032100`, stores the CS constant at `$032108`, and finishes with r4=$2005, r5=$2105, and r6=$0300. Both signed memory displacement and signed pointer adjustment are exercised; no DP transfers or accumulator temporaries are needed to advance the pointers.

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

The original six examples retain their encodings and results. B1 traversal avoids accumulator pointer updates; count-controlled termination handles a byte at offset $FFFF without wrapping. ADJSP supports compact stack-local allocation, and register-target JS supports table dispatch. Keep the current B1 preservation convention until caller-liveness measurements justify changing the ABI. The approved revision 0.6 extension directions and remaining deferrals are defined in [ISA.md](ISA.md#code-density-decisions).

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

## Code-density evidence

These are retained results of the completed RAM-only design studies. Temporary
assemblers, execution models, private study encodings and their runner have been
removed after the architectural direction was accepted. The tables are historical design evidence,
not reproducible benchmark commands or v0.6 conformance results. Production
conformance tests continue to execute the baseline images above.

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
v0.5 return frame and demonstrates no isolation. The interrupt ACK is a RAM write,
not a device. All eight baseline programs were compared against the real v0.5 CPU;
64 program/feature variants were checked in the retired RAM model.

| Workload | v0.5 bytes | rel8 | B1 simm6 | imm4 | ADDI+CMPI | SP uimm8 | Post-inc | All | Instructions v0.5 → all | B0/B1 used v0.5 → all |
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

The [revision 0.6 draft density decisions](ISA.md#code-density-decisions) are the current
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
