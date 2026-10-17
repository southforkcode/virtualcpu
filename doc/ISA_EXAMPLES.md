# ISA worked examples

Documentation: [Architecture and conformance](ISA.md) · [Encoding metadata](ISA_ENCODING.md).

All examples target the working ISA. Byte listings are generated from the linked
assembly sources using the production assembler, including short branches,
compact B1/SP operands and current immediate instructions. Each of the first ten
images starts at CS:$0100, CS=DS=SS=0, SB=0, SP=ST=$FFFE, I=0 with installed RAM.
Reset vector 0 is `00 01 00 00`. Successful standalone examples need no exception
entry; configure protected entry before expecting recoverable faults or SWI.
The [implementation record](ISA.md#implementation-status-and-remaining-work)
distinguishes executed fixtures from illustrative source.

The initial worked paths are executed by `cpu_conformance`; the later arithmetic
and retry examples use `integer_arithmetic`.

| Source | Retired instructions | Result |
|---|---:|---|
| fibonacci.asm | 57 | F(10) = 55 |
| array_sum_dp.asm | 33 | Sum = 15 |
| insertion_sort.asm | 142 | -3, -1, 0, 2, 3 |
| byte_copy.asm | 42 | hello plus NUL copied |
| factorial.asm | 46 | 5! = 120 |
| multiword.asm | 9 | $00040000 |
| segmented_copy.asm | 48 | Private-segment string and CS constant copied |
| segment_end.asm | 21 | Sum = 12, final pointer $FFFF |
| stack_locals.asm | 22 | Frame/callee-preserved values restored |
| dispatch.asm | 6 | Indirect target stores $BEEF |

[The B1 array-sum variant](../examples/array_sum.asm) uses the same input/output
contract with a B1 pointer instead of DP. Every standalone assembly source is
checked with the current assembler/disassembler, including its selected origin.

## Fibonacci

No data initialization. Compute F(10); DS:$2000 must contain `$0037` (55).
Two-byte DEC.W decrements the loop count with word NZCV flags.

[Source](../examples/fibonacci.asm).

<!-- EXAMPLE fibonacci.asm 256 -->
```text
0100  15 08 00       MOV.B #0,r0
0103  15 09 01       MOV.B #1,r1
0106  15 0A 0A       MOV.B #10,r2
0109  10 2A          TST.W r2
010B  60 08          BEQ done
                     loop:
010D  13             MOV r0,r3
010E  14             MOV r1,r0
010F  10 6D          ADD r3,r1
0111  1A F6          DEC.W r2
0113  61 F8          BNE loop
                     done:
0115  15 50 00 20    MOV.W r0,[$2000]
0119  01             HALT
```

## Array traversal and summation

Initialize DS:$2000 with words `[1, 2, 3, 4, 5]`. DS:$2100 must contain `$000F` (15). DP ends at `$200A`. The pre-loop zero test also supports an empty array.

[Source](../examples/array_sum_dp.asm).

<!-- EXAMPLE array_sum_dp.asm 256 -->
```text
0100  15 08 00       MOV.B #0,r0
0103  15 09 05       MOV.B #5,r1
0106  15 02 00 20    MOV.W #$2000,r2
010A  10 9A          MOV r2,dp
010C  10 25          TST.W r1
010E  60 0C          BEQ done
                     loop:
0110  15 22 00 00    MOV.W [dp+0],r2
0114  10 68          ADD r2,r0
0116  24 02          ADDP #2,dp
0118  1A F5          DEC.W r1
011A  61 F4          BNE loop
                     done:
011C  15 50 00 21    MOV.W r0,[$2100]
0120  01             HALT
```

## Signed insertion sort

Initialize DS:$2000 with words `[3, -1, 2, -3, 0]`. The five words must become `[-3, -1, 0, 2, 3]`. r4 holds the current address, r5 the exclusive end, r6 the base. Checked ADDP advances the outer pointer. Negative DP displacement loads the previous element; BLE compares signed values.

[Source](../examples/insertion_sort.asm).

<!-- EXAMPLE insertion_sort.asm 256 -->
```text
0100  15 04 02 20    MOV.W #$2002,r4
0104  15 05 0A 20    MOV.W #$200A,r5
0108  15 06 00 20    MOV.W #$2000,r6
                     outer:
010C  10 A0          MOV r4,r0
010E  10 A5          MOV r5,r1
0110  1A 04          CMP r1,r0
0112  62 2A          BCS done
0114  10 92          MOV r0,dp
0116  15 22 00 00    MOV.W [dp+0],r2
011A  13             MOV r0,r3
                     inner:
011B  10 A9          MOV r6,r1
011D  1A 07          CMP r1,r3
011F  60 13          BEQ place
0121  10 9E          MOV r3,dp
0123  15 20 FE FF    MOV.W [dp-2],r0
0127  1A 08          CMP r2,r0
0129  6D 09          BLE place
012B  15 60 00 00    MOV.W r0,[dp+0]
012F  8F 02 00       SUBI.W #2,r3
0132  6E E7          BRA inner
                     place:
0134  10 9E          MOV r3,dp
0136  15 62 00 00    MOV.W r2,[dp+0]
013A  20 02          ADDP #2,r4
013C  6E CE          BRA outer
                     done:
013E  01             HALT
```

## NUL-terminated byte copying

Initialize DS:$2000 with bytes `68 65 6C 6C 6F 00` (hello plus NUL). The same six bytes must appear at DS:$2100. Source and destination must not overlap. MOV.B zero-extends each byte; the terminating NUL is copied before exit.

[Source](../examples/byte_copy.asm).

<!-- EXAMPLE byte_copy.asm 256 -->
```text
0100  15 04 00 20    MOV.W #$2000,r4
0104  15 05 00 21    MOV.W #$2100,r5
                     loop:
0108  15 88 00       MOV.B [r4],r0
010B  15 C8 01       MOV.B r0,[r5]
010E  1A 40          TST.B r0
0110  60 06          BEQ done
0112  20 01          ADDP #1,r4
0114  21 01          ADDP #1,r5
0116  6E F0          BRA loop
                     done:
0118  01             HALT
```

## Recursive factorial with stack locals

No data initialization. Compute 5! using relaxing near CALLR, DEC.W and native
MUL; DS:$2000 must contain `$0078` (120). Each frame pushes n as a local,
reads it at `[sp+0]`, then removes it before RET. SP returns to `$FFFE`;
minimum SP is `$FFEA`. The low-word product suffices for this bounded input.

[Source](../examples/factorial.asm).

<!-- EXAMPLE factorial.asm 256 -->
```text
0100  15 08 05       MOV.B #5,r0
0103  56 05          CALLR fact
0105  15 50 00 20    MOV.W r0,[$2000]
0109  01             HALT
                     fact:
010A  1F 00          PUSH r0
010C  94 01 00       CMPI.W #1,r0
010F  69 0D          BLS base
0111  1A F4          DEC.W r0
0113  56 F5          CALLR fact
0115  28 32 00       MOV.W [sp+0],r2
0118  1A A8          MUL r2,r0
011A  1F 0B          POP r3
011C  1F 11          RET
                     base:
011E  15 08 01       MOV.B #1,r0
0121  1F 0B          POP r3
0123  1F 11          RET
```

## Multiword carry propagation

Compute `$0001FFFF + $00020001 = $00040000`. DS:$2000 receives the low word `$0000`; DS:$2002 receives the high word `$0004`. ADD produces carry consumed by ADC; intervening stores preserve the resulting flags.

[Source](../examples/multiword.asm).

<!-- EXAMPLE multiword.asm 256 -->
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

## B1 pointers in independent code and data segments

This example starts with the common initial state above, then sets DS=3. Install `hello` plus NUL at physical `$032000` and the word `$0055` at CS:$0300. Install segment 3 as RAM. It copies six bytes to `$032100`, stores the CS constant at `$032108`, and finishes with r4=$2005, r5=$2105, and r6=$0300. Both signed memory displacement and signed pointer adjustment are exercised; no DP transfers or accumulator temporaries are needed to advance the pointers.

[Source](../examples/segmented_copy.asm).

<!-- EXAMPLE segmented_copy.asm 256 -->
```text
0100  15 08 03       MOV.B #3,r0
0103  10 D2          MOV r0,ds
0105  15 04 00 20    MOV.W #$2000,r4
0109  15 05 00 21    MOV.W #$2100,r5
010D  15 06 02 03    MOV.W #$0302,r6
0111  15 A2 FA       MOV.W [cs:r6-2],r2
0114  22 FE          ADDP #-2,r6
0116  15 C2 21       MOV.W r2,[ds:r5+8]
                     loop:
0119  15 88 00       MOV.B [ds:r4],r0
011C  15 C8 01       MOV.B r0,[ds:r5]
011F  1A 40          TST.B r0
0121  60 06          BEQ done
0123  20 01          ADDP #1,r4
0125  21 01          ADDP #1,r5
0127  6E F0          BRA loop
                     done:
0129  01             HALT
```

## Count-controlled traversal through offset $FFFF

Install bytes `03 04 05` at DS:$FFFD through DS:$FFFF. The program sums them to `$000C` at DS:$2000 and stops with r4=$FFFF. The counter is checked before ADDP, so no one-past-end pointer is required. A separate attempted ADDP #1 at that final pointer must fault with Invalid State.

[Source](../examples/segment_end.asm).

<!-- EXAMPLE segment_end.asm 256 -->
```text
0100  15 04 FD FF    MOV.W #$FFFD,r4
0104  15 09 03       MOV.B #3,r1
0107  15 0A 00       MOV.B #0,r2
                     loop:
010A  15 88 00       MOV.B [r4],r0
010D  10 62          ADD r0,r2
010F  1A F5          DEC.W r1
0111  60 04          BEQ done
0113  20 01          ADDP #1,r4
0115  6E F3          BRA loop
                     done:
0117  15 52 00 20    MOV.W r2,[$2000]
011B  01             HALT
```

## Stack locals with ADJSP

The callee saves r4/r5, reserves twelve local bytes with ADJSP #-12, accesses slot zero, then releases the frame with ADJSP #12. Expect words `$000E`, `$1111`, `$2222` at DS:$2000/$2002/$2004 and `$0007` at DS:$3000/$3002. SP returns to $FFFE; minimum SP is $FFEC. This follows the current callee-preserved B1 convention.

[Source](../examples/stack_locals.asm).

<!-- EXAMPLE stack_locals.asm 256 -->
```text
0100  15 04 11 11    MOV.W #$1111,r4
0104  15 05 22 22    MOV.W #$2222,r5
0108  15 08 07       MOV.B #7,r0
010B  56 0D          CALLR framed
010D  15 50 00 20    MOV.W r0,[$2000]
0111  15 54 02 20    MOV.W r4,[$2002]
0115  15 55 04 20    MOV.W r5,[$2004]
0119  01             HALT
                     framed:
011A  1F 04          PUSH r4
011C  1F 05          PUSH r5
011E  25 F4          ADJSP #-12
0120  28 70 00       MOV.W r0,[sp+0]
0123  15 04 00 30    MOV.W #$3000,r4
0127  15 05 02 30    MOV.W #$3002,r5
012B  15 C0 00       MOV.W r0,[r4]
012E  28 31 00       MOV.W [sp+0],r1
0131  15 C1 01       MOV.W r1,[r5]
0134  10 64          ADD r1,r0
0136  25 0C          ADJSP #12
0138  1F 0D          POP r5
013A  1F 0C          POP r4
013C  1F 11          RET
```

## Register-target code-table dispatch

Install the target label offset as a little-endian word at CS:$0300 (the resolved value is shown below). Load that word into r5, then JS r5. It jumps within current CS without another memory dereference or a stack push. The `$DEAD` fallthrough is skipped; DS:$2000 becomes `$BEEF` and SP stays $FFFE. The table word is `$010E`.

[Source](../examples/dispatch.asm).

<!-- EXAMPLE dispatch.asm 256 -->
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

## Software trap and system-call example

Install [syscall.asm](../examples/syscall.asm) at `0:$0100` and
[syscall_handler.asm](../examples/syscall_handler.asm) at `1:$0200`; set vector 6
to `00 02 01 00`. Configure protected entry DS=SS=0, SB=0, ST=SP=$FFFE using
entry staging and COMMIT_ENTRY before executing SWI. The stack must have backed,
readable/writable next-push space even though this handler does not push. K entry
uses valid live K stack state; U entry uses the configured independent K stack.
The fixture runs from K and restores every saved field through IRET.

Example service 1 returns saved r0+r1 modulo 65536 in r0 and status zero in r1;
other service numbers return r0=0/r1=1. These service numbers are software policy.
At entry xpc=$0108, xcs=0, xcause=6 and xtrap=1; address metadata is clear.
The caller halts at $0109 with r0=12/r1=0 and all other caller state restored.

Caller:

<!-- EXAMPLE syscall.asm 256 -->
```text
0100  15 08 07       MOV.B #7,r0
0103  15 09 05       MOV.B #5,r1
0106  26 01          SWI #1
0108  01             HALT
```

Handler:

<!-- EXAMPLE syscall_handler.asm 512 -->
```text
0200  1F 34 02       GETX xtrap,r2
0203  96 01 00       CMPI.W #1,r2
0206  61 12          BNE unsupported
0208  1F 20 00       GETX xr0,r0
020B  1F 21 01       GETX xr1,r1
020E  10 64          ADD r1,r0
0210  1F 40 00       SETX r0,xr0
0213  10 05          XOR r1,r1
0215  1F 41 01       SETX r1,xr1
0218  1F 12          IRET
                     unsupported:
021A  10 00          XOR r0,r0
021C  15 09 01       MOV.B #1,r1
021F  1F 40 00       SETX r0,xr0
0222  1F 41 01       SETX r1,xr1
0225  1F 12          IRET
```

<!-- WORKED_IMAGES_END -->

## Far call and near helper

This working/minimal-v1 example uses two separately assembled images:
[far_call.asm](../examples/far_call.asm) in CS=0 at `$0100`, and
[far_callee.asm](../examples/far_callee.asm) in CS=1 at `$0200` (physical
`$010200`). Start with CS=DS=SS=0, PC=$0100, SB=0, SP=ST=$FFFE, flags=0,
and both code images/stack installed RAM. Reset vector 0 is `00 01 00 00`.
It runs on the protected working machine using reset identity kernel descriptors
for executable code and writable/readable RAM stacks.

```sh
python3 tools/isa.py assemble examples/far_call.asm --origin 0x100 -o build/far_call.bin
python3 tools/isa.py assemble examples/far_callee.asm --origin 0x200 -o build/far_callee.bin
```

Caller, fourteen bytes:

<!-- EXAMPLE far_call.asm 256 -->
```text
0100  15 00 05 00    MOV.W #5,r0
0104  15 01 01 00    MOV.W #1,r1
0108  54 00 02 01 00 CALLF $010200
010D  01             HALT
```

Callee and near helper, nine bytes:

<!-- EXAMPLE far_callee.asm 512 -->
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
transition occurs. [Appendix C](ISA.md#implementation-status-and-remaining-work)
records execution and source-round-trip evidence for these images.

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
python3 tools/isa.py assemble examples/immediate_shifts.asm -o build/immediate_shifts.bin
python3 tools/isa.py disassemble build/immediate_shifts.bin
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
rotation leaves the low byte unchanged. Expected results cover all nine stored
words; [Appendix C](ISA.md#implementation-status-and-remaining-work) records the
exact execution and source-round-trip evidence.

Compared with eight original one-bit word shifts (sixteen bytes/eight
instructions), `SHRI.W #8,rd` uses three bytes/one instruction and commits once.
The original sequence permits seven additional IRQ boundaries; matching final
values is not a promise of identical interrupt or fault observation points.
Rotations are true rotations within 8/16 bits; they never consume the input C.

## Word multiplication, division and remainder

[arithmetic.asm](../examples/arithmetic.asm) is assembled by the production
toolchain and executed by `integer_arithmetic`. Its five result words at
DS:`$4000–$4008` are 2500, 14, 2, -14 and -2:

```asm
MOV.W #100,r0
MOV.W #25,r1
MUL r1,r0             ; low product = 2500
MOV.W #100,r0
MOV.W #7,r1
DIVU r1,r0            ; quotient = 14
MOV.W #$FF9C,r0       ; -100
MODS r1,r0            ; remainder = -2
```

These word B0 operations update N/Z and preserve C/V/I/E. MUL wraps to the low
word and supplies no overflow evidence for checked pointer scaling. DIVS truncates
toward zero; MODS returns the dividend's sign. Zero divisors raise Arithmetic
Fault, as do both DIVS and MODS for -32768/-1. No byte, wide-product or combined
DIVMOD form is assigned.

Install vector 8 at K map 0, selector 0 `$0020–$0023` before using faultable
arithmetic. [arithmetic_fault.asm](../examples/arithmetic_fault.asm) demonstrates
a narrow retry policy for a registered DIVU at PC `$0100`: verify xcause/xpc,
write seven into saved xr1, then IRET to retry the original instruction. The
native suite executes the assembled handler in U and K, plus a known-site skip
fixup and arithmetic nested inside a K syscall. The CPU records cause 8 and the
unchanged operands/flags, without an additional subreason or memory diagnostic.
The handler policy is software, not an automatic CPU divisor substitution.

Protected kernel/compiler loaders initialize Reset plus causes 1–8; successful
standalone examples can provide only Reset when they need no exception entry.
The kernel demos route arithmetic
to their existing fatal policy; the control library does not treat it as a
recoverable memory fault. Replay binds the new vector bytes to fresh artifacts.
SmallC 0.3 emits MUL, DIVU/DIVS and MODU/MODS for ordinary scalar operations.
Its checked index arithmetic retains explicit overflow guards. See the
[compiler language and runtime rules](#supported-language-and-limits).

## Executable protected kernel demo

Build and run `build/kernel_demo` from the repository root. CMake uses the
production assembler and [generate_kernel_fixtures.py](../tools/generate_kernel_fixtures.py)
to build the linked runtime images and nested-handler test probes for the
working ISA. The
[loader](../examples/kernel.cpp) installs vectors, code and zeroed data, then
only calls `step`; descriptor setup and every context switch execute in guest code.

| Image | Kernel selector 0 offset / physical placement | Purpose |
|---|---|---|
| [kernel_boot.asm](../examples/kernel_boot.asm) | `$0100` | Commit entry, publish maps 7/8, initialize twenty-word process images, enable timer and bootstrap U |
| [kernel_irq.asm](../examples/kernel_irq.asm) | `$1000` | Source-0 IRQ: spill current U frame, replace it with the other process, ACK and IRET |
| [kernel_swi.asm](../examples/kernel_swi.asm) | `$1200` | Bootstrap, copy service and progress/shutdown services |
| [kernel_memory.asm](../examples/kernel_memory.asm) | `$1300` | Match registered user-access failure, edit K recovery PC and saved access controls, IRET |
| Fatal HALT | `$1400` | Stop unexpected exceptions/services |
| [kernel_copy.asm](../examples/kernel_copy.asm) | `$1500` | Leased one-word copy; registered fault `$1539`, error continuation `$1551` |
| [kernel_load_context.asm](../examples/kernel_load_context.asm) | `$1600` | LDX validates/replaces CTX/1 from r5; return r0=0/1 |
| [kernel_bootstrap_context.asm](../examples/kernel_bootstrap_context.asm) | `$1700` | Scalar initial SETX setup before PREPARE_USER_RETURN |
| [kernel_controls.asm](../examples/kernel_controls.asm) | `$7000` | Reusable control API, journal, leases and depth-indexed access scopes |
| [kernel_control_fault.asm](../examples/kernel_control_fault.asm) | `$1800` | Exact-site control recovery; route unowned Memory faults to the copy handler |
| Recovery registry data | `$A000–$A5FD` | 383 instruction-PC/cause/access records generated from production assembly |
| [kernel_user.asm](../examples/kernel_user.asm) | `$010100` and `$020100` | Same virtual U program in two private physical frames |

Vectors 0/5/6 select boot/IRQ/SWI. Other assigned exception vectors select the
control recovery handler; unowned Memory faults proceed to `$1300`, and
unregistered or unexpected failures take fatal HALT. Kernel state at `$3002/$3004` records user progress, `$3006` counts
switches, and `$3008` counts recovered copies. Process images occupy `$4000/$4100`.
Map 7/8 code selector 1 is execute-only, data selector 3 maps physical frames
3/4, and stack selector 2 maps frames 5/6 with policy `$1000–$2000`.
Both user counters are virtual `3:$2000`; they reside at physical `$032000/$042000`.

Boot initializes library ownership before configuring a pinned kernel stack at
`$C000–$FFFE`, independent of either user stack. Each PCB holds a generation-one
map lease; generation metadata is stored at PCB offset 40. SWI 0 creates
a K-origin frame; PREPARE_USER_RETURN validates and marks it before first U IRET.
Each user issues SWI 2 with an intentionally absent target selector 5. The copy
uses MOVU and faults under U permissions while the caller's valid data selector
remains 3. A second frame captures the K MOVU instruction and exact memory
metadata. The handler checks origin/phase/access/width/PC/CS/reason, clears live
access through the library, repairs saved access controls and redirects only that
K frame to the error path. The copy service then ends its leased scope.
The copy service then edits the lower U frame to return error 1. No user-map
invalidation or user-stack access is needed for recovery.

After library boot, the timer deadline is the snapshotted current time plus 4096;
it repeats every 512 retired instructions. Deadline words use an explicit carry
chain, so added initialization work cannot make a fixed absolute deadline stale.
Its pinned, I=0 handler saves all twenty writable words, loads the other known
valid image, acknowledges source 0, and returns through hardware validation.
These fixed images use PCB leases and a trusted known-valid return layout. A production
scheduler must prevalidate candidates and keep a rollback image across its
replacement window as specified in the software ABI. The protected conformance
suite separately rejects an invalid candidate at IRET and restores the original
process through a nested K rollback continuation.

Successful shutdown requires both private counters at least 3 and at least four
timer switches; it disables the timer/PIC and HALTs in the SWI handler. The demo
completes 13,948 CPU boundaries with four switches and two recovered copy faults.
The conformance run executes a second identical machine, compares registers,
frames, controls, mappings and devices at every boundary, and compares final RAM.

## Guest control-management library

[kernel_controls.asm](../examples/kernel_controls.asm) is ordinary working-mode
assembly assembled by the production toolchain, with no new opcodes or host-only
control calls. The [software ABI](ISA.md#guest-control-management-library-abi)
defines ownership, pinning, status and lifetime requirements. The library is
placed at kernel selector 0 offset $7000; these fixed software entry slots are
seven bytes each. r4 points to a 32-byte kernel request starting within
$6000–$67DF. All words below are little-endian and may be unaligned.

| Entry | Routine | Request inputs / effect |
|---|---|---|
| `$7000` | descriptor_install(map, segment, generation, descriptor) | Validate the seven fields and commit one descriptor |
| `$7007` | descriptor_remove(map, segment, generation) | Commit a canonical all-zero absent descriptor |
| `$700E` | map_publish(map, generation) | Publish and return the nonzero current generation at request+18 |
| `$7015` | map_invalidate(map, generation) | Require quiescence; invalidate and return next generation, or quarantine at $FFFF |
| `$701C` | entry_configure(tuple) | Commit a validated DS/SS=0, pinned kernel stack tuple |
| `$7023` | backing_query(address) | Copy result words before releasing scratch ownership |
| `$702A` | uaccess_begin(map, segment, generation) | Acquire a transient lease and establish a scope for the current frame depth |
| `$7031` | uaccess_end(map, generation) | Restore the scope's prior controls and release exactly its reference |
| `$7038` | prepare_user_return() | Check bootstrap preconditions and execute pure full-image CPU validation |
| `$703F` | map_acquire(map, generation) | Hold a PCB/continuation lifetime reference |
| `$7046` | map_release(map, generation) | Release a held lifetime reference |
| `$704D` | uaccess_clear() | Disable live access without releasing another frame depth's scope |
| `$7054` | initialize() | Exclusive reset-boot path; reject configured entry, live maps/controls or active frames |
| `$705E` | descriptor_readback(map, segment, generation) | STC the committed DESC/1 record into request+0..16; retain generation |

The first eighteen request bytes are a native DESC/1 record, so descriptor_install
uses LDC directly after ownership/generation/format checks. descriptor_remove
keeps the efficient scalar commit with one hoisted zero and no temporary record.
Readback uses STC, validating readable header and complete writable output before
any byte changes. Generation at +18 is outside the descriptor image.

The instruction record can also be used directly in owned, pinned K RAM:

```asm
; r4 points to nine words: 7, 3, $23, $0019, 0, 0, 1, 0, 0.
; r5 points to eighteen writable bytes, with readable header words 7, 3.
LDC [r4],DESC
STC DESC,[r5]
```

Neither instruction publishes map 7, advances its pointer, changes flags or uses
shared descriptor staging. The final readback is the committed record; scalar
GETC/SETC remain available. The native descriptor suite executes all base-register
encodings, faults and aliases. Writable saved-context images use CTX/1:

```asm
; Active exception frame, I=0; owned pinned forty-byte buffers in K DS.
STX [r4]
LDX [r5]
IRET
```

LDX validates the whole candidate while preserving CPU authority and live
process/access controls. IRET revalidates and restores separately. A saved image
containing invalid scalar edits may require SETX repair before it can be reloaded.

The private abort jump is $705B. These addresses are software placement, not ISA
allocations. The generator verifies the slots and emits label constants; kernel
sources use the same fixed ABI. r0 returns OK=0, INVALID=1, BUSY=2, FAULT=3 or
STALE=4, and r1 identifies a retained committed effect. r4–r7/SP and NZCVI are
preserved; r2/r3 are scratch. Test r0 explicitly after calling: status does not
replace the preserved flags. IRQs are disabled while the owner holds staging,
then the caller's policy is restored before RET. Depth eight returns BUSY before
acquiring ownership so an operation always has a free recovery slot.

| Request byte offset | Descriptor / map / scope operations | Entry operation | Query operation |
|---|---|---|---|
| 0 | map ID | ignored | physical address low word |
| 2 | segment (where needed) | ignored | physical address high eight bits |
| 4–16 | frame, flags, lower, upper_lo, upper_hi, sb, st (complete DESC/1 at 0–16) | DS, SS, SB, ST, SP at 4–12 | ignored |
| 18 | expected generation; publish/invalidate output | ignored | ignored |
| 20,22,24,26 | unused | unused | copied kind, run_lo, run_hi, hardware valid |
| 28 | unused | unused | completed-output marker; trust only with status OK |

Before first publication a newly initialized map has FREE state and generation
zero; publishing establishes generation one. After invalidation the returned
next generation is required for rebuilding and republishing. Reference count
$FFFF refuses acquisition. No numeric generation wrap reauthorizes a stale
handle: exhaustion quarantines the map until a new reset boot discards old
software handles. Map 0 cannot be published/invalidated, and descriptor helpers
refuse kernel selectors 0/$FF so their own mappings remain pinned. Descriptor
format checks do not allocate backing or decide which physical frames are safe
to grant U access; the trusted kernel must enforce that allocation policy.

| Kernel RAM | Ownership |
|---|---|
| `$4000/$4100` | Twenty-word PCB images plus generation at +40; each retains one map lease |
| `$5000–$50DF` | Library lock/owner/operation/checkpoint/commit journal and shared-control backups |
| `$50E0` | Test-only nested diagnostic probe result; not a CPU register or runtime queue |
| `$5200–$59FF` | 256 map records of eight bytes: generation, references, state, reserved |
| `$5A00–$5A8F` | Nine sixteen-byte depth scopes: active, map, generation, prior ua map/segment, reserved |
| `$6000/$6040/$6080` | Demo control request, user-copy request, nested clear/probe request |
| `$7000–$7824` | Library code, including pinned cleanup/compensation |
| `$A000–$A5FD` | Registry: count followed by exact PC/flags pairs |
| `$C000–$FFFD` | Kernel stack policy; exclusive top $FFFE |

A single operation backs up selectors plus all seven descriptor-shadow words,
five entry-shadow words and both query-address words. A nested caller returns
BUSY without using the global journal or query latch. The query helper deliberately
leaves the read-only result latch caller-clobbered; its address shadow is restored
and completed output is owned by the caller. Direct SETC commands in unrelated
kernel subsystems would bypass this convention. The boot, SWI, copy and memory
handlers call library entry slots; complete scheduler images use STX/LDX while
bootstrap, diagnostics and individual fault-result edits retain GETX/SETX.

Recovery at $1800 matches owner depth, K origin/CS and an exact generated registry of 379 operation PCs plus four pinned-finisher RET
sites. Transfer-target recovery requires that the target itself be a registered
operation instruction; broken cleanup/finisher fetches remain fatal. The generated flags distinguish fetch faults, read/write operands,
entry probes, registered control-transfer targets, PREPARE's U-return validation,
and durable command boundaries.
Invalid/Stack failures are admitted only at eligible instruction sites. Faults
outside registered operations, including broken pinned cleanup, are fatal.
For a matching fault, IRET redirects the one K frame to compensation; internal
CALL stack space is unwound to the checkpoint, preserving a lower syscall frame.

A completed hardware descriptor/map/entry/query/bootstrap command remains
committed even if its next instruction faults. Publish/invalidate finish metadata
from the journal idempotently. An aborted begin/acquire/release compensates its
reference/authority changes; an end that passed readiness completes teardown.
These are deliberate software outcomes, not a multi-instruction transaction.
The status/committed pair tells callers whether retry or cleanup is appropriate.

The additional [kernel_control_probe.asm](../examples/kernel_control_probe.asm)
is a test handler at $1C00: during recovery it attempts a backing query, verifies
BUSY/no commit, records that deferral, then enters the production fault handler.
Native tests use fixed per-call backing holes and injected typed IRQs to exercise
staging and commit boundaries, operand failures, lower-frame retention, malformed
inputs, stale references, quarantine and read-only-latch ownership. These trusted
failure fixtures are distinct from normal replay. The actual demo uses guest-only
setup, two PCB leases and temporary copy scopes; all transient leases are released
on both copy error paths, while the two resumable PCBs retain their references.

## Bootable bulk transfer and nested retry

Build and run `build/bulk_demo` or `build/bulk_demo console-v1`. This small
K-origin example executes LDC, STC, STX and LDX without the scheduler library.
The [loader](../examples/bulk_guest.hpp) installs vectors, code, guard bytes and
empty buffers before reset. All entry setup, descriptor construction, saved
context editing and recovery execute in guest code. The example owns pinned
ordinary RAM and keeps I=0; it does not publish a user map or replace a process.
The full kernel/console examples retain software generation leases and policy.

| Source | K selector 0 placement | Role |
|---|---:|---|
| [bulk_boot.asm](../examples/bulk_boot.asm) | `$0100` | Configure protected entry, construct/install DESC/1, read it back, SWI, store result and HALT |
| [bulk_context.asm](../examples/bulk_context.asm) | `$0300` | Save the top K frame twice, edit candidate r0, deliberately invalidate candidate CSR, call loader, IRET |
| [bulk_recover.asm](../examples/bulk_recover.asm) | `$0500` | Admit only cause 4 at K `$0700`, depth two; repair candidate CSR and retry that exact LDX |
| [bulk_load.asm](../examples/bulk_load.asm) | `$0700` | LDX through r5, then ordinary RET |

Vector 0 selects boot, vector 6 the context handler, and vector 4 recovery.
Every other exception vector selects fatal HALT at `$1400`. The production
[fixture generator](../tools/generate_bulk_fixtures.py) verifies code placement
and the registered cross-image `$0700` call/fault site; it also emits image,
metadata and source hashes for conformance evidence.

DESC/1 at `$2000` contains the nine words `7, 3, 3, $0019, 0, 0, 1, 0, 0`:
map 7, segment 3, frame 3, PRESENT/U R/W, full exclusive upper bound `$10000`,
no stack policy. LDC installs it directly without shared staging or publication.
The odd readback record at `$2021` starts with readable words `7, 3` and must
finish byte-for-byte equal to the original eighteen-byte record.

The odd forty-byte CTX/1 buffers at `$4001` and `$4101` use exactly the
[writable layout](ISA.md#atomic-writable-exception-context-transfers): r0–r7,
PC/CS/CSR, SP/SB/ST/SS, DP/DS, then the three saved process/access controls.
STX exports the original SWI frame into both. Guest code changes candidate r0
to `$BEEF` and candidate CSR at +20 to `$0040`; E is not a writable saved bit.
The first LDX raises Invalid State and preserves the entire original lower
frame. The nested handler checks cause, PC, CS, origin, depth and a one-shot
recovery guard, clears candidate CSR, and IRETs to retry `$0700`. LDX then
validates/replaces only the writable payload. RET and the outer IRET restore
separately, retaining CPU frame authority and normal return validation.

| Operation in this example | Exact bytes | Effect |
|---|---|---|
| `LDC [r4],DESC` | `1F C0` | Install eighteen-byte self-contained descriptor |
| `STC DESC,[r5]` | `1F C5` | Export committed descriptor using readable destination header |
| `STX [r4]` / `STX [r5]` | `1F CC` / `1F CD` | Export twenty writable saved words |
| `LDX [r5]` | `1F C9` | Reject invalid CSR once, then load repaired candidate atomically |

Both profiles complete 70 boundaries and 69 retirements, reaching depth two
once. `$2200` is `$BEEF`, `$2202` is 1, map 7 remains unpublished, SP is `$FFFE`,
all guard bytes remain `$A5`, and no frame remains active. There are five
successful bulk transfers and two LDX attempts; the failed attempt does not
retire. `bulk_integration` binds each profile's replay to its reset artifacts,
compares every retained field at every boundary and all final loader RAM, and
checks the unchanged lower frame during nested recovery. The complete example
is executable; the short direct-operation snippets above require caller setup.

## Executable blocking-console terminal

The `console_demo` target selects Working mode with
`MachineProfile::ConsoleV1`. The [console profile](ISA.md#console-machine-profile-and-recorded-input)
defines the device registers, interrupt source and version-2 replay records.
The loader installs images and configuration before reset. After reset, host
input reaches the machine only through recorded JSONL boundary events.

```sh
build/console_demo --record build/terminal.jsonl
build/console_demo --replay build/terminal.jsonl
```

The single-run recorder writes the exact loader RAM/backing SHA-256 header and
one canonical input record. The replayer verifies the header before stepping,
queues the recorded boundary, and supplies no automatic input. It rejects stale,
malformed, leftover and wrong-profile events. The demo accepts its recorder's
canonical scheduling syntax; the CPU API accepts the full strict JSON contract.
A reset requires a new loader image/header and run queue; this single-run driver
rejects reset streams. The native console suite separately exercises recorded
reset and rejection of old-run queued input.

A calls `read(console, buffer, 32)` with no bytes available. The kernel stores
A's return image and software request, takes a generation lease, marks A blocked,
and returns all helper calls before selecting B. B increments a private counter
and checks preserved registers. Once B has made observable progress, the runner
records `hello\r\n`. Source 2 copies those seven bytes to A using MOVU, completes
A's saved result and marks A runnable. Its handler returns B's unchanged frame.
A later eligible timer selects A, which resumes after SWI, checks preserved
NZCV/registers, uppercases ASCII, omits CR, counts LF and emits `HELLO\n` through
a kernel write service. It then shuts down the fixture.

### Console guest layout and software ABI

| Region | Purpose |
|---|---|
| `$0100` | `kernel_console_boot.asm`, private maps 7/8 and two PCB leases |
| `$1300` | `kernel_console_memory.asm`, exact MOVU byte-store fixup |
| `$1400` | Fatal HALT retaining the failing frame |
| `$1600` | Shared validated LDX loader; failure return at `$1606` |
| `$1700` | Scalar initial bootstrap image setup |
| `$1800` | Shared control-library fault dispatcher |
| `$1C00` | Test-only `kernel_console_probe.asm`; absent from normal loader |
| `$3006/$3008/$300A/$300C` | Schedule selections / copy faults / blocked reads / completed requests |
| `$3020/$3022/$3024/$3026` | Map reuse / stale-handle result / shutdown cancellations / test probe |
| `$3030/$3040` | Deferred idle timer work / owned one-byte RX staging |
| `$4000/$4100` | A/B trusted PCBs and request records |
| `$4200` | Rollback image for the current outer user frame |
| `$5000–$5A8F` | Shared control-library lock, map generations/leases and scopes |
| `$6000/$6100/$61C0` | Boot/control request, serialized copy/lifetime request, fault clear request |
| `$7000` | Shared guest control-management library |
| `$8000` | `kernel_console.asm`, IRQ/SWI, scheduler and software continuations |
| `$A000` | Shared exact-site control-library recovery registry |
| Physical `$10100/$20100` | A terminal / B progress program, virtual CS=1 PC=$0100 |
| Physical `$32000/$42000` | A receive buffer / B private progress counter |
| Physical `$32100/$32102/$32104` | A initial count / destination offset / selector |
| Physical `$32020–$32028` | A returned count/status, completion, LF and preservation witnesses |
| Physical `$42010` | B fixture mode: 0 progress, 1 blocking read, 2 cancel A |

Each PCB starts with the twenty writable return words in the existing scheduling
ABI. The following offsets are bytes, not word indices:

| Offset | Meaning |
|---:|---|
| 40 | PCB map-generation handle; saved process-map word at 34 identifies owner |
| 42 | State: RUNNABLE=1, BLOCKED_READ=2, DYING=3 |
| 44 | Requested maximum, 0–32 |
| 46 / 48 | Destination offset / selector |
| 50 / 52 | Completed-byte count / status |
| 54 | Software continuation: NONE=0, READ_RETURN=1 |
| 56 | Request generation-lease ownership, 0/1 |

Service 2 takes r0=count, r1=destination selector, r4=offset. It returns r0=bytes,
r1=status and preserves all other registers, NZCV and I. Status 0 is success,
1 is invalid request/copy failure, and 2 is cancellation. Zero count succeeds
without validating a pointer. Counts above 32 and selectors other than this
fixture's pinned writable selector 3 fail immediately with zero bytes and no
FIFO consumption. No-data positive reads block. Available data completes with
1–count bytes, without waiting to fill the request. The recorded request owns a
map-generation lease independently of its PCB; each live MOVU scope takes one
additional lease. Completion releases both transient leases exactly once.

The one-byte staging slot belongs to the serialized I=0 handler. A successful RX
instruction consumes a byte before the separate MOVU store. A store fault drops
that staged byte and completes the request with status 1 and the number already
copied; remaining device bytes stay queued. There is no implicit retry or rollback
of a previous device read. At offset $FFFF, a successfully copied byte is counted;
a request for more fails before pointer wrap and leaves the next FIFO byte intact.
A failed scope begin consumes no byte. Exact Memory/UACCESS/OPERAND/write/byte
faults at `copy_fault` redirect to `copy_error`; other faults take the fatal path.
The nested fixup clears live and saved access controls, and normal scope cleanup
releases the retained lease. Staging contents remain diagnostic after completion.

Service 4 transmits the low byte of r0. Service 5 is a progress checkpoint.
Service 1 cancels any remaining peer read, disables devices and halts with one
retained outer user frame; PCB diagnostic leases remain owned. The fixture does
not claim a production frame allocator or arbitrary process creation.

Service 3 is a privileged software policy exercised by B: mark A DYING before
releasing its request and PCB leases, invalidate map 7, bind selector 3 to successor
physical frame 7, and publish generation 2. The old PCB handle is then deliberately
checked and rejected as STALE. Late input stays queued without writing either
old or successor backing. DYING A is never resumed. A production cancellation
caller owns the policy for delivering a cancellation result or destroying a PCB.

### Scheduling and replay invariants

Scheduling is reached by a jump at the shared stack baseline, with I=0, depth 1,
and a U-origin top frame. All transient CALL chains have returned. The current
image is copied into a separate rollback slot before replacement. Candidate
validation checks the live generation/PCB lease, fixed CS/DS/SS and stack policy,
PC/SP ranges, legal CSR bits and cleared access controls. Boot code/backing and
these two process descriptors are pinned and immutable while runnable. The
validator depends on that fixed layout; a dynamic allocator must additionally
validate its selected descriptors/backing before calling this loader. A rejected
candidate restores the independently saved current image. The test-only IRQ hook
corrupts A's candidate PC and verifies B continues with its preserved image.

Saving uses STX for both the PCB and the independent `$4200` backup. The pinned
loader executes LDX at `$1600`, returns zero on success, and neither changes live
association nor publishes scheduler state. A guard at `$3032` admits recovery only
around the candidate call. The unowned-fault dispatcher recognizes that exact
instruction, K origin, I=0 and prior depth one; candidate Invalid/Stack/Memory or
source preflight failure returns status one through `$1606`. Missing instruction
bytes and unregistered faults remain fatal. The nested frame is unwound before
the scheduler restores the independent backup and returns the original process.
Tests corrupt CSR, stack, source backing and candidate target after software
validation and verify full-state replay and unchanged lower-frame authority.
Both guards/leases and the backup remain software obligations. Normal runnable
maps, entry state and bookkeeping are pinned; unexpected later IRET/publication
faults still require the general compensation ABI or fatal kernel policy.

When both processes block, the kernel retains one outer U frame and executes
EI/HALT without a suspended helper chain. A K-origin console IRQ may complete a
PCB but never replaces that outer frame. If it interrupted the pending HALT,
it advances only its own K return PC to the idle continuation, preventing a lost
wakeup. The outer continuation disables interrupts and selects a validated
runnable PCB. Nested timers only defer work. Source 2 is enabled while requests
wait; otherwise queued excess bytes are left for the next read without an IRQ
storm. A timer and RX simultaneously pending retain lowest-source arbitration.

`console_conformance` records each stimulus and runs a second machine with the
same loader image and records. It compares all registers/contexts, descriptors,
control staging, query/entry state, map publication, timer/PIC, diagnostic records,
RX/TX contents, sequence/run/boundary counters and commit counts at every boundary,
then compares all loader RAM. Scenarios include early/delayed/short/32-byte reads,
excess input, simultaneous interrupts, all-blocked idle advance/wakeup, the
EI/HALT window, invalid requests, segment-end partial copies, nested copy faults
at depths 2/3, cancellation with pending/late input, map reuse, and invalid-candidate
rollback. Device tests exercise exact register rights/widths, FIFO/capture bounds,
JSON and transactional errors, reset disposal and artifact/profile binding.
The earlier minimal-v1 four-switch/two-fixup demo remains a separate fixture.

## SmallC compiler and executable workloads

[SmallC 0.3.1](../tools/smallc.py) compiles a deliberately small C-like language to
assigned Working-mode assembly. The production assembler is the only instruction
encoder. [corpus.json](../examples/workloads/corpus.json) defines the inputs and
expected outcomes. [measurements.json](../examples/workloads/measurements.json)
records current binary/source/build hashes, exact event schedules and measured costs.

```sh
python3 tools/smallc.py examples/workloads/struct_array.c --assembly build/struct_array.asm --artifact build/struct_array.json
python3 tools/measure_workloads.py --build build --output build/compiler/measurements.json
```

### Supported language and limits

Objects use `u8`, `i8`, `u16`, `i16`, fixed-size one-dimensional arrays, and named
fixed-layout `struct` types. Functions return scalars or near pointers. The subset
supports declarations, assignment, numeric casts, unary arithmetic/complement/not,
addition/subtraction/multiplication/division/remainder, bit operations, constant-count shifts,
comparisons, short-circuit `&&`/`||`, `if`/`else`, `while`, `for`, calls and return.
Array indexing, `&`, pointer dereference, `.` and `->` are supported. Decimal/0x
literals, comments, constant static initializers and aggregate brace initializers
are accepted. `sizeof(type)` is a compile-time constant. Ordinary integer multiplication uses MUL; division and remainder use DIVU/DIVS
and MODU/MODS after integer promotion. Unsigned division/remainder by a positive
literal power of two use shifts/masks, and scalar multiplication by a literal
power of two uses a shift. Signed division retains truncation toward zero.
Checked variable index multiplication retains its carry-checked loop, using
DEC.W for the nonzero loop counter with the same NZCV behavior as SUBI.W #1. Constant
element scaling uses a shift or MUL only after proving the full product fits;
MUL preserves C/V and cannot itself detect overflow.

Values are little-endian. Bytes have alignment 1; words and pointers alignment 2.
A struct aligns each member naturally, uses the maximum member alignment, and
rounds its total size accordingly. Array stride is the full padded element size.
Examples include a byte/word record of size 4 and a three-byte/word record of size
6. Static objects are zero-initialized, including padding; explicit aggregate
initializers zero unspecified members. Automatic objects require initialization
before use; the compiler does not prove initializedness or pointer lifetimes.

Byte arithmetic promotes to 16 bits; an operation with a `u16` operand is unsigned,
otherwise signed. Addition, subtraction and multiplication wrap modulo 65536, and byte assignment/casts
truncate then zero- or sign-extend as appropriate. Comparisons use that signedness;
signed right shift is arithmetic. Shift counts must be constants 0–16. These are
explicit SmallC rules, including defined signed wrap, rather than a claim of full
ISO C semantics. Expression operands evaluate left-to-right, call arguments
right-to-left, and assignment evaluates the value before its target address.
Division truncates toward zero; signed remainder has the dividend's sign or is
zero. A zero divisor or signed MIN/-1 in either `/` or `%` propagates the CPU
Arithmetic Fault (cause 8) with precise saved registers, flags and PC. The supplied
guest kernel treats unexpected arithmetic faults as fatal; it does not convert
them into pointer-error codes or a successful return. The same policy applies
to compiled kernel helpers. Invalid constant initializers are compile-time errors.
Ordinary expressions retain left-to-right evaluation; quotient/remainder
expressions are evaluated separately, including side effects. Existing checked
pointer errors 1–6 and their ABI remain unchanged.

Fallthrough or a bare return yields zero. Function/block declarations share one
function scope; shadowing is rejected. Identifier case is significant.

Unsupported constructs include preprocessing/includes, strings, floating point,
32-bit integers, variable-length/multidimensional arrays,
pointer-to-pointer declarations, aggregate value copies/parameters/returns,
function pointers, variadic/indirect calls, dynamic shift counts, `break`/`continue`,
external linking and a general standard library. `void` functions are unsupported.
Returning an SS pointer and casting between DS and SS pointers are rejected.
Static pointer initializers require null or an explicit compatible near-pointer
cast of a numeric constant expression; static pointer arithmetic and address
relocation expressions are unsupported. The fixture
limits global data to `$2000–$2DFF`, each automatic frame to 1024 bytes and each
user image to `$0100–$3FFF`; these are compiler/loader policies, not ISA limits.

### SmallC calling and checked-pointer ABI

r0 holds expression results and function returns. r0–r3 and NZCV are caller-clobbered;
r4–r7 are callee-saved. The baseline allocates named locals to SS stack slots,
uses r0/r1 for values, r2/r3 for scratch and r4 for formed addresses; r7 holds the
frame base. This fixed allocation respects the B0/B1 arithmetic distinction.
Expression temporaries spill explicitly; no register coloring or range optimizer
is claimed. I and segment state are preserved by ordinary functions.

Callers push one 16-bit argument slot per parameter, right-to-left, CALL, then
remove the arguments. Callees push r4/r5/r6/r7, allocate an even local area, set
r7=SP, and reverse that sequence before RET. Parameter 0 is at frame-base +
`local_bytes + 10`, subsequent parameters at +2 increments. Direct locals use
SP addressing with compiler-tracked temporary depth. The exact frame sizes and
exported function entry addresses are in each generated artifact. Recursion is
not statically bounded; the hardware's checked stack governs exhaustion.

A near pointer is a 16-bit offset with a static address-space type. `u16 *p` points
into DS; `ss u16 *p` into SS. Globals decay to DS pointers and local arrays/addresses
to SS pointers. No selector is silently dropped. DS/SS casts or implicit cross-space
argument/assignment conversions are rejected. Same-space explicit pointer casts
and raw integer-to-near-pointer casts are available; source code owns object
lifetime and valid backing. A raw cast does not grant access outside its segment
or process map. SS-pointer dereferences temporarily select DS=SS, perform the
access and restore DS; the saved DS value remains live in r3 across interrupts.

Pointer `p+n`/`p-n` supports nonnegative counts. Signed-negative index/count values
are rejected, so move backward with `p-n`, not `p+(-n)`. Direct array indexing also
checks its declared count. Scaling, address-context additions/subtractions and
multiplication check overflow before any narrowed address is used; non-power-of-two
strides use MUL after a full-product bound check. Pointer arithmetic checks carry/borrow,
and a typed dereference checks the complete object's width at the segment end.
Raw pointers retain segment bounds, not an object-length capability. Arithmetic
computed earlier into a scalar follows the scalar wrap rule; checked address
expressions cannot recover a scalar's earlier lost arithmetic history.

Pointer difference requires identical pointee/address-space types, a signed
16-bit representable byte distance, and an exact power-of-two element stride.
Its result is an element count. The compiler rejects difference for other strides.
The conservative byte-distance limit also applies when division would make a
larger distance representable. SS pointer lifetimes remain the caller's obligation.

| Check result | Meaning |
|---:|---|
| 1 | Direct array index outside its declared count |
| 2 | Signed-negative pointer count/index |
| 3 | Index arithmetic or element scaling overflow/underflow |
| 4 | Near-pointer addition carry or subtraction borrow |
| 5 | Complete typed dereference crosses the segment end |
| 6 | Pointer difference range or element alignment failure |

A failed check stores its code at process DS:`$2F04` and executes SWI 255. This
fixture terminates through the kernel's fatal service path, retaining one U frame
and the current stack for diagnosis. It is a compiler-defined source failure,
not an ISA Memory exception, and performs no failed target access. Exact trap PCs
and saved flags are recorded per binary. Successful functions balance their stack
and preserve B1; intentional guard termination does not unwind a return chain.

User-only intrinsics are `read(u8 *buffer,u16 count)`, `putc(value)`, `yield()` and
`io_status()`. They use existing SWI services 2/4/5, respectively. Read returns the
count and stores its status at DS:`$2F02`; `io_status()` reads it. The existing
up-to-32-byte/blocking semantics apply. The entry wrapper stores main's result at
DS:`$2F00` and invokes shutdown service 1. Library images omit this wrapper and
reject user I/O intrinsics. The generated pure kernel helper at K `$B000` uses the
same argument/callee-save ABI, accesses a local struct through an SS pointer and
returns 1011 to its guest boot caller. A separate compiled helper exercises a
fatal signed-overflow arithmetic fault in K; it does not return a success value.

### Compiler execution fixture and replay schedule

All binaries run on Working/console-v1 with the guest kernel control library,
private A/B data and stacks, timer preemption and recorded input. The compiler
fixture uses executable CS bounds `$0100–$3FFF` for both processes and accepts
saved DS=2 or 3, allowing an interrupted SS access to resume before restoring DS.
These guest map/validator changes are assembled separately. The compiler fixture
uses a 509-retirement timer period to avoid phase-locking its repetitive SS loop
to the bulk scheduler; the standalone demos retain period 512. Each binary records
its own changed schedule. The `preempt_stack` workload
proves an actual IRQ during DS=SS, restored context and a balanced caller frame.

The terminal is compiled from `terminal.c`. It blocks in read while B increments
its private counter, receives recorded `hello\r\n`, counts a line, resets its column
counter, uppercases ASCII, omits CR and outputs `HELLO\n`. A generated `at()` call
loop and the SS-pointer loop also survive timer switches. Boundary programs cover
byte offsets 0/$FFFF, a word at $FFFE, carry/borrow, scaling/index overflow,
negative/out-of-count indexing and difference range/alignment failures. Native
checks verify results, short-circuit side effects, signed-byte behavior, data
canaries, K/peer memory isolation, B1 preservation and stack balance.

Each program is replayed from its own exact loader image. The runner records input
at the first U boundary after A is BLOCKED_READ and B has made at least three
progress iterations. Compiled terminal input arrives at B=8904; its hand-written
reference uses B=8898. Their final B counts differ. The current measurements JSON includes every
record's run/B/sequence and the per-binary artifact header. Applying one binary's
numeric schedule to another is not a replay-equivalence test. The runner compares
all retained registers, frames, descriptors, staging, latches, PIC/timer/console,
sequence and diagnostics at every boundary, plus all final loader RAM.

## Compact addressing and branches

B1 memory displacements -32..31 use three bytes; other signed 16-bit displacements
use five. SP offsets 0..255 use three bytes and 256..65535 use four. SP offsets
are unsigned and must remain in the allocated stack. Raw long in-range encodings
are legal, but assembly chooses compact forms.

Unsuffixed branches relax to rel8 when their final target fits. `.S` forces rel8
and `.W` forces rel16. Disassembly prints the width so branch bytes round-trip.
All forms retain widened address checks, precise faults and atomic memory/MMIO
transactions. MOVU keeps its independent zero/disp16 forms and reserved base bits.

## Current workload measurements

Build and run the CTest suite, then reproduce the current report:

```sh
python3 tools/measure_workloads.py --build build --output examples/workloads/measurements.json
python3 tools/evaluate_pointer_arithmetic.py --build build --output examples/workloads/pointer_study.json
```

[measurements.json](../examples/workloads/measurements.json) records current
production assembly/code/data hashes, compiler/build inputs, actual retirements,
stack use, moves/spills, addressing distributions and per-binary replay schedules.
Measurements describe the pinned corpus and compiler, not unavoidable ISA costs.
The hand-written references match selected fixed workloads with specialized loops.

<!-- COMPILER_BASELINE_BEGIN -->
| Program | Code bytes | Retired compiled | Total B | B0/B1 moves | Spills/reloads | Max U stack | Address B0 ALU | Check instructions |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| array_byte | 195 | 178 | 8659 | 6 | 21/21 | 32 | 4 | 4 |
| array_word | 200 | 222 | 8703 | 6 | 26/26 | 18 | 10 | 35 |
| array_word_hand | 41 | 34 | 8515 | 0 | 0/0 | 4 | 0 | 0 |
| struct_array | 372 | 247 | 8728 | 9 | 26/26 | 18 | 16 | 60 |
| struct_array_hand | 62 | 34 | 8515 | 0 | 0/0 | 4 | 0 | 0 |
| local_struct | 155 | 61 | 8542 | 5 | 1/1 | 28 | 0 | 4 |
| variable_stride | 249 | 304 | 8785 | 6 | 29/29 | 38 | 20 | 52 |
| pointer_math | 332 | 102 | 8583 | 3 | 9/9 | 18 | 9 | 22 |
| byte_boundary | 128 | 47 | 8528 | 4 | 4/4 | 14 | 1 | 1 |
| word_boundary | 93 | 32 | 8513 | 3 | 1/1 | 14 | 0 | 4 |
| fail_add (guard 4) | 118 | 21 | 8492 | 1 | 1/1 | 14 | 1 | 6 |
| fail_sub (guard 4) | 119 | 21 | 8492 | 1 | 1/1 | 14 | 1 | 6 |
| fail_scale (guard 3) | 136 | 19 | 8490 | 1 | 1/1 | 14 | 0 | 5 |
| fail_negative (guard 2) | 131 | 25 | 8496 | 1 | 2/1 | 16 | 0 | 5 |
| fail_bounds (guard 1) | 109 | 22 | 8493 | 1 | 2/1 | 16 | 0 | 5 |
| fail_width (guard 5) | 72 | 16 | 8487 | 1 | 0/0 | 12 | 0 | 5 |
| fail_index_overflow (guard 3) | 141 | 38 | 8509 | 1 | 3/1 | 22 | 2 | 9 |
| pressure | 290 | 337 | 8818 | 5 | 37/37 | 42 | 0 | 0 |
| signed_bytes | 327 | 84 | 8565 | 1 | 5/5 | 14 | 0 | 0 |
| shifts | 94 | 38 | 8519 | 1 | 2/2 | 16 | 0 | 0 |
| preemption | 241 | 5696 | 17413 | 193 | 577/577 | 32 | 192 | 480 |
| terminal | 437 | 640 | 13867 | 9 | 65/65 | 18 | 7 | 21 |
| terminal_hand | 129 | 149 | 12731 | 0 | 0/0 | 10 | 0 | 0 |
| kernel_caller | 172 | 66 | 8554 | 5 | 3/3 | 10 | 0 | 4 |
| packed_struct | 257 | 152 | 8633 | 5 | 15/15 | 18 | 4 | 28 |
| local_array | 233 | 169 | 8650 | 6 | 16/16 | 36 | 3 | 3 |
| fail_difference (guard 6) | 104 | 25 | 8496 | 1 | 1/1 | 16 | 1 | 5 |
| fail_alignment (guard 6) | 113 | 29 | 8500 | 1 | 1/1 | 16 | 1 | 7 |
| constants | 145 | 62 | 8543 | 3 | 4/4 | 22 | 0 | 0 |
| preempt_stack | 230 | 15142 | 43705 | 769 | 1537/1537 | 34 | 256 | 256 |
| arithmetic_words | 125 | 52 | 8533 | 1 | 5/5 | 22 | 0 | 0 |
| arithmetic_signed | 1056 | 359 | 8840 | 13 | 30/30 | 20 | 24 | 108 |
| arithmetic_promotions | 1092 | 374 | 8855 | 13 | 30/30 | 22 | 24 | 108 |
| arithmetic_calls | 326 | 172 | 8653 | 5 | 15/15 | 28 | 0 | 0 |
| arithmetic_constants | 304 | 96 | 8577 | 1 | 6/6 | 12 | 0 | 0 |
| arithmetic_decimal | 576 | 769 | 9250 | 12 | 98/98 | 40 | 15 | 25 |
| arithmetic_preempt | 243 | 8868 | 26432 | 1 | 1537/1537 | 22 | 0 | 0 |
| arithmetic_power2 | 304 | 102 | 8583 | 1 | 7/7 | 16 | 0 | 0 |
| arithmetic_struct_scale | 234 | 190 | 8671 | 4 | 19/19 | 22 | 6 | 36 |
| fail_divu_zero (arithmetic fault) | 68 | 16 | 8484 | 1 | 1/1 | 16 | 0 | 0 |
| fail_divs_zero (arithmetic fault) | 75 | 19 | 8487 | 1 | 1/1 | 16 | 0 | 0 |
| fail_modu_zero (arithmetic fault) | 68 | 16 | 8484 | 1 | 1/1 | 16 | 0 | 0 |
| fail_mods_zero (arithmetic fault) | 75 | 19 | 8487 | 1 | 1/1 | 16 | 0 | 0 |
| fail_divs_overflow (arithmetic fault) | 75 | 19 | 8487 | 1 | 1/1 | 16 | 0 | 0 |
| fail_mods_overflow (arithmetic fault) | 75 | 19 | 8487 | 1 | 1/1 | 16 | 0 | 0 |
| fail_divu_preempt (arithmetic fault) | 185 | 19486 | 55829 | 1 | 3074/3074 | 20 | 0 | 0 |
| kernel_arithmetic_fault (arithmetic fault) | 107 | 18 | 8281 | 1 | 1/1 | 0 | 0 | 0 |

| Matched fixed-input reference | Compiled / hand bytes | Compiled / hand retired | Static ratio | Retired ratio | B change |
|---|---:|---:|---:|---:|---:|
| array_word | 200/41 | 222/34 | 4.88× | 6.53× | +188 |
| struct_array | 372/62 | 247/34 | 6.00× | 7.26× | +213 |
| terminal | 437/129 | 640/149 | 3.39× | 4.30× | +1136 |
<!-- COMPILER_BASELINE_END -->

## Checked register-to-pointer arithmetic decision

A checked B0-to-B1 register add remains unassigned. `ADDP.R` is study notation and
is rejected by the assembler. The [design contract](ISA.md#checked-register-to-pointer-add-deferred-design-contract)
proposes an unsigned B0 addend, B1 destination, widened range checking, preserved
flags and precise Invalid State overflow; DP and signed/subtract variants are
excluded. Existing B0 ADD and signed-immediate ADDP retain their own semantics.

[pointer_study.json](../examples/workloads/pointer_study.json) separates actual
per-PC execution counts from unencoded two-/three-byte cost models. Across the
current twenty-seven successful programs, replacing every modeled final add with a
two-byte candidate saves 43 code bytes and 283 retirements but adds 142 explicit
bank moves. Selecting only seven byte-access sites saves 18 bytes/284 retirements
with no extra moves. Neither removes a spill or reload. A three-byte candidate
saves two bytes for the all-site substitution, eleven for byte sites only.

Those estimates assume spare registers and dead flags and replace the compiler's
SWI #255 / software reason 4 with an architectural fault. Keeping the current
software guard makes the substitution larger (125 extra bytes/851 retirements in
the two-byte all-site model). Existing safe lowering preserves that error ABI at
zero extra cost. All negative-index, bound, scale and access-width checks remain.
The models do not simulate candidate instructions, relocated schedules or equivalent
exception frames.

Defer adoption until a pointer-residency/register-allocation prototype demonstrates
benefit against an equally optimized safe existing-ISA implementation and the
language/kernel overflow ABI is chosen. A resident-B1 microsequence could save
more than the current B0-based lowering; that remains an experiment. Numeric
encoding, tooling, precise-fault and replay integration would need review before
assignment. Mathematical range checks are study validation, not CPU conformance.

## Assigned density operations and retained decisions

Word INC/DEC, B0 SEXT, DS RAM-only B0/B1 post-increment MOV and relative near
CALLR are assigned and executable. See the
[allocation ledger](ISA_ENCODING.md#assigned-density-allocation-budget) and
[current workload measurements](#current-workload-measurements).
Other [Appendix B candidates](ISA.md#extension-status-and-retained-evidence)
remain deferred with no speculative opcodes.

[The executed example](../examples/density.asm) starts in K with CS=DS=SS=0,
ordinary RAM and byte `$80` at `$4200`. It exercises word INC, a byte DEC alias,
SEXT, post-increment load/store and CALLR/RET. It leaves `$FFFF` at `$4000`,
`$FF80` at `$4300`, r1=`$FF81`, r4=`$4201`, r5=`$4302` and the initial SP.
Its production-assembled bytes execute and replay in the native density suite.

SmallC 0.3 selects word INC/DEC for unchecked scalar +1/-1, SEXT for signed-byte
normalization, and relaxing CALLR for internal functions. Existing ADDI/SUBI
selection removes literal loads and expression spills; only the incremental
one-byte reduction belongs to INC/DEC. Byte arithmetic still promotes to word.
All checked address guards remain; the checked multiplication loop uses DEC.W
only after its nonzero test and successful guarded addition. The compiler does not infer RAM/range proofs
for post-increment from raw near pointers; pinned kernel initialization uses it
explicitly. The arithmetic-fault preemption workload now uses 512 loop iterations
and yields at 256, retaining its actual peer-progress gate despite shorter code.

Post-increment accepts only B0 values/B1 bases and DS RAM. FFFF-byte and
FFFE-word tail accesses must use ordinary MOV when no representable advanced
pointer exists. Terminating-byte loops must avoid advancing a sentinel tail.
MMIO, including console RX, requires ordinary MOV; the fused RAM-only instruction
rejects devices without callbacks. Preserve fault recovery and IRQ-boundary
requirements when fusing a transfer and ADDP; they are not universally equivalent.

Matched current-ISA probes retain counters, guards and ABI while comparing
INC/DEC, post-increment copies, SEXT versus two shifts, and CALLR at two origins.
Density-only gains are separated from retirements; source/target layout and timer
opportunities are regenerated. Shared kernel/candidate savings are not counted
multiple times.

### System integration validation

The [density probes](../examples/workloads/density/README.md) and compiler corpus
provide 254 current workloads. SmallC 0.3.1 uses DEC.W in checked-multiply loops
with the same retirement count and overflow guards as SUBI.W #1. The 27
successful compiled programs total 8,410 code bytes and 34,561 user/helper
retirements. Generate current reports to inspect complete data/workspace, stack,
register pressure, traffic, host observations and per-binary replay inputs.

The standalone factorial now uses MUL and CALLR, reducing 92 retirements to 46
while preserving result and stack depth. Unit decrements and the stack-local
call use compact forms. The fixed B1 array-sum source uses post-increment over
ordinary RAM with representable advance; DP traversal, sentinel copy loops and
the final-offset example retain their required addressing and tail behavior.

Bulk LDC/STC DESC and LDX/STX are already used by the descriptor library and
scheduler examples above. The bootable bulk example and native conformance
cover exact retry and full-state replay. ENTRY/PROC forms remain deferred.
