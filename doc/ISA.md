# Virtual 16-bit CPU Architecture Reference Manual

**Document status:** Working copy

**Document scope:** Programmer-visible state, instruction semantics, binary encodings, exception behavior, conformance and implementation review status

## 1. Scope and architectural conventions

### Status and applicability

This manual specifies a byte-addressed virtual processor with 16-bit registers,
16-bit segment offsets and a 24-bit physical address space. Its general-register
architecture distinguishes arithmetic accumulators (B0) from address/storage
registers (B1). Instructions execute atomically and faults are precise.

This working copy describes one instruction set and one protected execution
architecture. Assigned encodings are specified in Chapter 9. Device layouts
that still require definition are listed in
Appendix C. Unassigned fields and encodings must not be inferred from prose or
from the order of a table.

This is a design specification, not a statement that every facility is already
implemented. Implementation coverage is tracked separately in the repository.

### Requirements and document boundaries

“Must”, “must not”, “shall” and “shall not” express architectural requirements.
“May” denotes a permitted choice; “should” denotes guidance. Descriptions of
instruction effects and ordered checks are requirements even without a modal verb.
Reserved fields must have the specified value; an implementation must not
silently ignore an illegal encoding or truncate an invalid architectural value.

Chapters 2–9 define processor behavior and assigned encodings. Appendix A contains
software ABI contracts, which bind software using those conventions but are not
additional CPU interlocks. Appendix B records unassigned extension proposals;
it grants no executable opcode. Appendix C identifies conformance requirements
implementation status, and remaining specification work.

[ISA_ENCODING.md](ISA_ENCODING.md) is the companion encoding reference; its
marked JSON payload is the machine-readable source used by the current tools.
That payload covers every currently assigned encoding in this working copy. This manual defines execution semantics. A
discrepancy in a represented instruction is a specification defect, not permission
to choose either behavior.
[Appendix C](#architectural-conformance-requirements) defines required validation and records implementation evidence.
[ISA_EXAMPLES.md](ISA_EXAMPLES.md) provides worked programs and informative density
studies. Examples and measurements do not override architectural requirements.

### Contents

- [1. Scope and architectural conventions](#1-scope-and-architectural-conventions)
- [2. Programmer’s model](#2-programmers-model)
- [3. Memory organization and addressing](#3-memory-organization-and-addressing)
- [4. Instruction execution](#4-instruction-execution)
- [5. Exception frames and fault records](#5-exception-frames-and-fault-records)
- [6. Protected exception architecture](#6-protected-exception-architecture)
- [7. Interrupt controller, time and devices](#7-interrupt-controller-time-and-devices)
- [8. Instruction reference](#8-instruction-reference)
- [9. Binary encoding reference](#9-binary-encoding-reference)
- [Appendix A. Software ABI conventions](#appendix-a-software-abi-conventions)
- [Appendix B. Unassigned extension proposals](#appendix-b-unassigned-extension-proposals)
- [Appendix C. Conformance and draft completion](#appendix-c-conformance-and-draft-completion)
  - [Review completion](#review-feedback-and-completion), [executed evidence](#executed-implementation-evidence), [requirements](#architectural-conformance-requirements), [exact vectors](#instruction-and-execution-vectors)

### Conventions

- Instructions use source, destination operand order. `$` denotes hexadecimal; `#` denotes an immediate.
- Assembly MOV byte immediates accept -128 through 255 and word immediates -32768 through 65535, encoding negatives in two's complement; other values are rejected rather than truncated. ADDP and ADJSP use strictly signed immediates from -128 through 127. SWI accepts only unsigned service numbers from 0 through 255. Absolute offsets are unsigned. A byte immediate still zero-extends on load, so `MOV.B #-1,r0` produces `$00FF`.
- Registers and arithmetic results are 16 bits unless stated otherwise. Arithmetic results are reduced modulo 65536; address calculations are checked before narrowing.
- Instruction operands and memory words are little-endian. A word `$1234` occupies bytes `34 12`. Unaligned word accesses are allowed.
- Assigned instructions are currently 1–5 bytes long. Future extensions may define additional lengths. There are no delay slots or implicit operand-size prefixes.
- Flags not explicitly updated remain unchanged. Ordinary moves, ADDP, ADJSP, stack operations, jumps, calls, and ordinary returns preserve flags. Explicit CSR writes, SWI/exception entry, and IRET have the effects specified below.
- All unassigned opcodes and reserved encoding fields are illegal. Software must not treat reserved encodings as NOPs.
- Execution has User and Kernel modes. Stack-allocation checks remain distinct from general memory permissions.

| Notation | Meaning |
|---|---|
| B / W | 8-bit byte / 16-bit word |
| rs / rd | Source / destination B0 register unless an entry says otherwise |
| rn / rp | Any r0–r7 / a B1 base r4–r7 |
| ea | A supported effective-address operand, not an additional encoding |
| s8(x) / s16(x) | Interpret a byte / word as signed two’s complement |
| unsigned(x) | Interpret the value without sign extension |
| next | Original PC plus decoded length, calculated without truncation |
| old_x | Value of x sampled from the instruction input state |
| R / W / X | Read / write / execute permission |
| Preserved flag | Same architectural value after successful completion |

The default operand size is word where a byte/word mnemonic is defined. Size
suffixes do not create encodings for operations that have only a word form.
No unspecified flag value is introduced: a flag not explicitly written is preserved.

### Byte and word operations

Byte ALU operations use only the low eight bits of each register operand. An
operation that writes a byte ALU result must replace the entire 16-bit destination
with that result zero-extended to sixteen bits; it must not retain the original
upper byte. Byte loads, including immediate byte loads, likewise zero-extend into
the entire destination. Word ALU operations use all sixteen operand bits and
produce sixteen-bit results. These width rules do not make a comparison write a
destination: the individual instruction defines whether a result is stored.

`CMPI.B`, `TST.B`, and `TSTI.B` use the low byte but must preserve all sixteen
destination bits, including the original upper byte. Tests discard their
temporary result. Immediate-count shifts/rotates with count zero perform no
register or flag write, including `.B`; positive byte counts zero-extend.
`MOV.B` loads, including immediate loads, preserve all
flags: zero-extension does not update N or Z, even when the loaded byte is zero
or has bit 7 set.

For example, starting with r0=`$12FF`, `ADDI.B #1,r0` writes `$0000`, whereas
`ADDI.W #1,r0` writes `$1300`. Loading byte `$80` into r0 writes `$0080`.
Subsequent operations use their own specified width; a byte load does not select
the width of a later instruction.

With r0=`$12FF`, `CMPI.B #$FF,r0` sets Z=1 and leaves r0=`$12FF`.
Loading byte `$80` into r0 instead produces `$0080` while retaining the input
N/Z/C/V/I/E values.

The [CSR rules](#csr-and-arithmetic), including the
[width-specific ALU flag rules](#width-specific-alu-flags), [instruction reference](#reading-an-instruction-entry),
and [immediate ALU encoding contract](#immediate-alu-instructions) apply these
conventions and specify each instruction's flag and write effects.

#### Register ALU width and assembly selection

Assigned register-register ADD, ADC, SUB, SBC, CMP, AND, OR, XOR, and complemented
MOV operations are word-only. SHL, SHR, and SAR also operate on words. Software
must not infer byte width from the register contents or a preceding byte load.
For example, `ADD r1,r0` with r0=`$12FF` and r1=`$0001` writes `$1300`, while
`ADDI.B #1,r0` with the same original r0 writes `$0000`.

An assembler must reject a byte suffix on a word-only operation, such as
`ADD.B r1,r0` or `AND.B r1,r0`; it must not silently discard the suffix or
substitute another instruction. A size suffix does not create an encoding that
the instruction reference has not assigned.

For the immediate ALU family, an explicit `.B` or `.W` selects that operation
width and an unsuffixed mnemonic selects `.W`. An assembler must preserve this
selection regardless of the immediate's value: `ADDI #1,r0` and `ADDI.W #1,r0`
both select the three-byte word encoding, not the two-byte `ADDI.B #1,r0`
encoding. Width selection affects the full register result and flags even when
the immediate fits in eight bits.

#### Conditional branch flag interpretation

Conditional branches are operand-width independent: they evaluate the current
CSR flags using their specified predicates. They do not examine register values,
recompute a comparison, or retain a separate operand-width state. Branch
displacement width determines the branch's reach, not the width of a comparison.

After `CMPI.B`, signed predicates such as BLT (`N != V`) and BGE (`N == V`)
reflect the signed eight-bit comparison; unsigned predicates such as BCC (`C=0`)
and BHI (`C=1 and Z=0`) reflect the unsigned eight-bit comparison. After `CMPI.W`
or register CMP, those same predicates reflect the sixteen-bit comparison.

For example, starting with r0=`$007F`, the comparisons below preserve r0 and
produce different signed branch outcomes:

| Comparison | N/Z/C/V | Following BLT | Following BCC |
|---|---|---|---|
| `CMPI.B #$80,r0` | `1/0/0/1` | Not taken: 127 is not less than -128 | Taken: 127 is less than 128 |
| `CMPI.W #$80,r0` | `1/0/0/0` | Taken: 127 is less than 128 | Taken: 127 is less than 128 |

These interpretations require the comparison flags to remain current. An
intervening flag-preserving instruction leaves the predicates unchanged; an
instruction that writes flags changes the affected inputs. In particular, logic
updates N/Z while preserving C/V, so its flags must not be treated as a fresh
subtraction/comparison result.

## 2. Programmer’s model

This chapter defines live processor state. Privileged control state and protected
exception provenance are distinct from general registers and software PCB images.

### Register organization

| Class | Width | Names | Purpose |
|---|---:|---|---|
| Accumulator (B0) | 16 | `r0–r3` | Arithmetic, logic, transfers, and memory access |
| Address/storage (B1) | 16 | `r4–r7` | General storage, CS/DS pointer bases, and checked pointer adjustment; no general ALU operations |
| Internal (B2) | 16 | `sp`, `sb`, `dp`, `csr` | Stack pointer, lower stack limit, data pointer, status |
| Internal (B3) | 16 | `ss`, `cs`, `ds`, `st` | Stack/code/data segments and exclusive stack top |
| Control | 16 | `pc` | Code offset; accessed through control-flow instructions |

| Bank / two-bit selector | `00` | `01` | `10` | `11` |
|---|---|---|---|---|
| B0 | `r0` | `r1` | `r2` | `r3` |
| B1 | `r4` | `r5` | `r6` | `r7` |
| B2 | `sp` | `sb` | `dp` | `csr` |
| B3 | `ss` | `cs` | `ds` | `st` |

Segment registers contain unsigned eight-bit segment numbers in their low byte. Reads return a zero high byte. Writes with a nonzero high byte raise Invalid State rather than silently truncating.

### B1 address registers

r4–r7 are ordinary 16-bit values with additional address uses, not tagged pointers. Any one can supply an offset within either CS or DS; the instruction chooses the segment. Pointer loads, stores, and stack saves still support all eight general registers as value operands. B1 registers remain available for ordinary storage and transfers through the existing B0/B1 move encodings.

`[ds:r4]` and `[cs:r4]` select data or code memory explicitly; `[r4]` defaults to DS. Signed 16-bit displacements are also supported, for example `[ds:r5+2]` or `[cs:r6-2]`. There is no implicit pointer update or segment carry. Reading or writing through CS accesses memory without changing PC or CS. With ordinary writable RAM, code-segment stores are allowed. If a load's destination is also its B1 base, the address uses the old register value and the load replaces it only on success.

ADDP adjusts a B1 register or DP by a signed byte constant and preserves flags. General ADD/ADC/SUB/SBC, logic, CMP, and shifts remain B0-only. ADDP checks only the offset range; a later memory transfer checks the selected segment and installed RAM. See the two-byte `$20–$24` encodings in Chapter 9.

### Internal-register writes

The table gives value and effect rules for internal-register writes in Kernel
mode. The privilege table below defines User access, including descriptor-based
segment selection and stack bounds.

| Destination | Rule |
|---|---|
| `dp` | Any 16-bit value; effective addresses are checked when used |
| `sp` | Require `SB <= new_SP <= ST`; otherwise Stack Boundary fault; no memory access or allocation initialization |
| `sb`, `st` | Any 16-bit value; may temporarily invalidate the stack while configuring it |
| `ss`, `ds` | Require high byte zero; changing the segment does not probe memory |
| `cs` | Require high byte zero; continue at the following instruction offset in the new CS, validating that target's first byte |
| `csr` | Reserved bits must be zero; write N/Z/C/V/I; ignore the supplied E bit |

Reading SP or other stack registers is allowed even when stack configuration is invalid. Stack use requires `SB <= SP <= ST`. For initialization or switching, mask interrupts, write SS/SB/ST, then write SP, then restore interrupt policy. Temporary invalid configuration does not itself fault; an intervening stack operation does. DI does not mask synchronous faults.

### CSR and arithmetic

Operand and result widths follow the [byte and word conventions](#byte-and-word-operations).
The width-specific flag effects below do not change the zero-extension rule for
instructions that write a byte result.

| Bit | Name | Meaning |
|---|---|---|
| 0 | Z | Result is zero |
| 1 | N | Result sign bit: bit 15 for word operations, bit 7 for byte ALU operations |
| 2 | C | Addition carry-out; subtraction has no unsigned borrow |
| 3 | V | Signed arithmetic overflow |
| 5 | I | Maskable interrupts enabled when one |
| 6 | E | Exception context active; maintained by the CPU |

All other bits read zero. Defined-bit mask is `$006F`; software-writable mask is `$002F`. These masks exclude reserved bit 4: `$006F = $0040 | $0020 | $000F`, and `$002F = $0020 | $000F`. E is derived from exception-active state and cannot be cleared by a CSR move. DI clears I; EI sets I. CLI and SEI are aliases for DI and EI respectively, retaining the original meanings.

Let `a` be the original destination, `b` the original source, and `c` the original C flag. Let `s16(x)` interpret a word as signed two's complement. Sources are sampled before any result is written, including when operands name the same register.

| Instruction | Wide unsigned calculation | Wide signed calculation |
|---|---|---|
| ADD | `u = a + b` | `v = s16(a) + s16(b)` |
| ADC | `u = a + b + c` | `v = s16(a) + s16(b) + c` |
| SUB, CMP | `u = a - b` | `v = s16(a) - s16(b)` |
| SBC | `u = a - b - (1-c)` | `v = s16(a) - s16(b) - (1-c)` |

For these instructions, `result = u mod 65536`, N is result bit 15, Z is `result == 0`, and V is true exactly when `v < -32768` or `v > 32767`. ADD/ADC set C when `u > 65535`. SUB/CMP set C when `a >= b`; SBC sets C when `a >= b + (1-c)`, computing that comparison without 16-bit truncation. CMP discards the result; other arithmetic writes it to the destination. I and E are preserved.

XOR, AND, OR, and complemented MOV update N/Z from the word result and preserve C/V/I/E. `TST.W r2` aliases `OR r2,r2`. TST.B and TSTI update width-specific N/Z without changing any register bits; all tests preserve C/V/I/E.

#### Width-specific ALU flags

An ALU instruction computes its flags at its selected operation width, before
any byte-result zero-extension. The original register's upper byte and the
zero-extended result's bit 15 do not participate in byte flags. Comparisons use
the same flag calculation as subtraction without writing the destination.

| Selected width | Operand mask | Result modulus | N bit | Signed range for V |
|---|---|---|---|---|
| Byte (`.B`, 8 bits) | `$00FF` | 256 | 7 | -128 through 127 |
| Word (`.W`, 16 bits) | `$FFFF` | 65536 | 15 | -32768 through 32767 |

Let `w` be the selected width, `M = 2^w`, and `a` and `b` the operand bit
patterns masked to that width. For an immediate operation, `a` is the original
destination and `b` is the encoded immediate bit pattern. Define `signed_w(x)`
as `x` when `x < M/2`, and `x-M` otherwise. Calculate sums and differences in a
wider type before reducing the result modulo M.

- N is bit `w-1` of the reduced result. Z is one exactly when that entire
  selected-width result is zero.
- ADDI sets C exactly when `a+b >= M`. SUBI and CMPI set C exactly when `a >= b`:
  C=1 means no unsigned borrow. These operations do not consume the input C.
- ADDI sets V exactly when `signed_w(a)+signed_w(b)` lies outside the selected
  signed range. SUBI and CMPI use `signed_w(a)-signed_w(b)` instead. Signed
  overflow is independent of unsigned carry or borrow.
- ANDI, ORI, and XORI set N/Z from the selected-width logical result and preserve
  the input C/V. All six immediate operations preserve I/E. Word register
  operations and shifts retain the carry-input and flag effects specified in
  their individual instruction entries.

For example, `ADDI.B #1` with destination `$12FF` produces `$0000` and
N/Z/C/V=`0/1/1/0`; with destination `$127F` it produces `$0080` and
N/Z/C/V=`1/0/0/1`. `SUBI.B #1` with destination `$1200` produces `$00FF` and
N/Z/C/V=`1/0/0/0`. The corresponding word operations compute their flags from
all sixteen bits, not these low-byte results.

### Privilege and process-local state

Reset enters K mode. Mode is protected state, never inferred from CS, E, or a flag
supplied by user software. U and K both have r0–r7, PC, CS/DS/SS, DP, SP, SB/ST,
and NZCV. I is kernel-controlled policy: **live U mode always reads I=1** and
has no architectural I=0 state. DI/EI and direct CSR writes from U fault before
changing any policy or flag. Entry saves canonical U xcsr with I=1, then clears
live K I; return validates I=1 in a U image instead of silently correcting it.
Saved K contexts may contain I=0 or I=1. E is derived from ordinary-frame depth.
Arithmetic updates only its defined flags.

| State / operation | U-mode behavior | K-mode behavior |
|---|---|---|
| B0/B1, DP, checked ADDP, SP/ADJSP, CALL/RET and CALLF/RETF | Existing checks and effects; calls/returns never change privilege or maps | Same |
| Read CSR, CS/DS/SS, SP/SB/ST | Allowed; exposes only current live state | Allowed |
| Direct CSR write, DI/EI, HALT | Privilege exception, no effects | Existing effects, E remains derived |
| Write DS | Select any present user data-capable descriptor; no memory probe | Select kernel descriptor |
| Write CS, JL, CALLF, RETF | Validate user executable descriptor and target; remain U | Select kernel executable descriptor; remain K |
| Write SS | Select a present user stack descriptor; atomically install its SB/ST policy and empty SP=ST | Existing checked configuration semantics |
| Write SP / ADJSP | Allowed within descriptor-installed SB/ST | Existing checks |
| Write SB/ST | Privilege exception | Allowed; temporary invalid stack still possible |
| GETX/SETX, IRET | Privilege exception, even with no active frame | Access top frame only / checked return |
| SWI #service | Safe K entry; imm8 preserved | Nested synchronous trap, within nesting limit |
| Maps, address-space selector, vectors, entry tuple, timer/PIC | Privileged | Protected control/MMIO operations |

User segment selection grants no rights: each subsequent access checks the current
process map and U rights. CS-relative data loads require R, stores require W;
executable code is not implicitly readable or writable. Arbitrary data aliases of
SS remain subject to memory rights, even though stack allocation checks apply
only to stack operations. User stack descriptors have explicit stack capability
and immutable kernel-configured lower/upper bounds. Signal/alternate-stack setup
can use a kernel-installed second descriptor.

Decode/reserved-byte failures precede privilege checking. After complete fetch,
a privileged opcode in U faults before operand-state, stack, data, or target
checks. For allowed segment writes, an illegal high byte is Invalid State;
missing descriptor/permissions are memory-protection faults. Privilege faults
save the instruction start and operation class; the instruction changes nothing.
U cannot write a frame containing its own return mode, alter live maps, change
interrupt masks, or configure vector/entry state.

## 3. Memory organization and addressing

Memory accesses use protected segment translation with checked 16-bit offsets,
little-endian words and whole-instruction atomicity.

### Memory and address formation

A guest address is an eight-bit segment selector and an unsigned 16-bit offset.
The selected map resolves the selector to a physical frame and permissions.
Physical addresses contain 24 bits, giving a maximum 16 MiB address space.

The machine supplies an installed backing map. Each instruction observes a
stable descriptor/backing view. Accesses outside installed backing cause Memory
Access faults. RAM and MMIO have distinct rules; instruction fetch and stacks
require RAM. Memory contents survive CPU reset. The following table defines
virtual address formation before translation:

| Access | Segment | Offset calculation |
|---|---|---|
| Instruction fetch | CS | PC and subsequent instruction-byte offsets |
| `[addr16]` | DS | Unsigned `addr16` |
| `[dp+disp16]` | DS | `DP + s16(disp16)` |
| `[ds:rp+disp16]` | DS | `rp + s16(disp16)`; rp is r4–r7 |
| `[cs:rp+disp16]` | CS | `rp + s16(disp16)`; rp is r4–r7 |
| `[sp+off16]` | SS | `SP + unsigned(off16)` |
| PUSH/POP/CALL/RET/CALLF/RETF stack access | SS | As defined in Stack |

Calculate offsets in a wider signed or unsigned type as appropriate. Negative or greater-than-65535 offsets fault; they never wrap or carry into another segment. Every byte must fit within the same segment and be installed. Words may start at odd offsets but not at `$FFFF`. Validate the whole access before writing any byte.

DP and B1 memory displacements range from -32768 to 32767. SP offsets range from 0 to 65535 and additionally obey stack limits. Assemblers reject negative SP offsets and out-of-range displacements instead of truncating them. An encoded SP offset `$FFFF` means +65535, never -1. B1 forms select only CS or DS; they do not provide an SS-relative stack-slot mode.

A byte at offset `$FFFF` is accessible, but an offset `$10000` cannot be stored as a pointer or end sentinel. A traversal that includes the last byte must test its count/end condition before advancing past that byte. ADDP from `$FFFF` by +1 still faults; it never enters the next segment. See the count-controlled segment-end traversal in the worked examples.

### Stack

The stack grows downward within SS. SB is the inclusive lower limit. ST is an exclusive upper limit and the empty-stack pointer. The valid configuration is:

```text
0 <= SB <= SP <= ST <= $FFFF
allocated stack bytes = [SP, ST)
```

ST is a 16-bit exclusive bound: `$10000` cannot be represented, so offset `$FFFF` is never a stack byte. Reset uses `ST=SP=$FFFE`, `SB=0`. Odd bounds and pointers are allowed; words do not require alignment.

Direct stack-relative MOV computes `start = SP + unsigned(off16)` and requires a valid stack configuration, `SP <= start`, and `start + size <= ST`, with arithmetic checked before truncation. It uses SS and does not change SP. Stack slots below SP are inaccessible through this mode. Even `[sp+0]` faults on an empty stack. A register-to-SP move changes the allocation boundary; it neither clears nor writes allocated memory.

Absolute/DP-relative and B1 pointer accesses use ordinary memory rules even if their selected segment equals SS. Stack limits are bounds for stack operations, not general memory protection.

| Instruction | Checks and successful operation |
|---|---|
| `PUSH rn` | Require valid configuration and `SP-2 >= SB`; store the original register word at `SS:(SP-2)`, then set SP=SP-2 |
| `POP rn` | Require valid configuration and `SP+2 <= ST`; load the word at SS:SP into rn, then set SP=SP+2 |
| `ADJSP #simm8` | Require valid configuration and `SB <= SP+s8(simm8) <= ST`, calculated without narrowing; set SP to the checked result |
| `CALL addr16` | Require representable next, valid target in current CS, and valid stack access; push next as one word and set PC=addr16 |
| `RET` | Peek the word at SS:SP; validate it as a target in current CS; then set SP=SP+2 and PC=that word |
| `CALLF abs32` | Preflight a four-byte push, save next and old CS, then commit SP-4 and target CS:PC together |
| `RETF` | Peek both words at SS:SP, require canonical saved CS and a valid target, then commit SP+4 and saved CS:PC together |

PUSH/POP accept r0–r7 only, not internal registers. POP is canonical; PULL is an alias for POP. PUSH.W, POP.W, and PULL.W are explicit word-transfer aliases for PUSH, POP, and POP respectively. There are no byte stack operations; ADJSP has no transfer-size suffix. CALL/RET preserve CS; return addresses are offsets, not far pointers. All checks, including availability of both stack bytes and any target, precede effects. A failed push/call writes nothing; a failed pop/return changes neither the register nor SP.

For example, with `SB=$1000, SP=$1002, ST=$1004`, PUSH succeeds at `$1000`; a second PUSH faults and leaves SP and memory unchanged. With SP=ST, POP faults.

### Far call and return contract

CALLF and RETF are ordinary control transfers in the current effective map:
map 0 with K execute/stack rights in K, or process_map with U execute/stack
rights in U. They preserve execution mode, process_map, user-access controls,
exception-frame depth, all flags, DS/SS/SB/ST/DP and r0–r7. They cannot enter K
from U or select a different process map. Fetch, stack and target must be RAM;
no part of a far frame may touch MMIO. The working-mode implementation enforces RAM backing and protected rights/kind checks.

CALLF is five bytes, `$54 pc_lo pc_hi cs 00`, using JL's little-endian
packed-address convention. Source `CALLF abs32` accepts one unsigned packed
24-bit address (0..$FFFFFF), without `#` or a size suffix. The high encoded byte
must be zero; a nonzero value is Invalid State after complete instruction fetch,
not an early reserved-encoding rejection. RETF is one byte `$55`, with no operand
or suffix. No indirect far-call form is assigned. Legacy modes 0.4/0.5 reject
both primary bytes immediately, without operand fetch; working mode enables them.

Let P be CALLF's old SP, and `next = old_PC+5` calculated without narrowing.
The four-byte frame occupies `[P-4,P)` in the original SS:

| Offset from new SP | Value |
|---|---|
| 0 | next bits 7–0 |
| 1 | next bits 15–8 |
| 2 | old CS bits 7–0 |
| 3 | 0, the canonical CS word's reserved high byte |

Thus `[sp+0]` is a return-offset word and `[sp+2]` is a canonical CS word. Odd
frames are legal. SP and ST remain sixteen-bit: with ST=$FFFF the last frame
byte may be $FFFE, never $FFFF. CALLF matches RETF; near CALL matches RET.
Hardware does not tag frames or detect a mismatched return mnemonic; using RET
on a far frame or RETF on a near frame violates the software ABI.

CALLF checks in this exact order:

1. Fetch/decode all five bytes from the original CS, without segment wrapping;
   validate the packed target's zero high byte (Invalid State at instruction start).
2. Validate the old stack configuration and `[SP-4,SP)` bounds (Stack Boundary
   at the attempted start when representable).
3. Preflight the return-PC word WRITE at SS:(SP-4), then the saved-CS word WRITE
   at SS:(SP-2), each with the ordinary translation/rights/RAM/backing checks.
4. Require next <=$FFFF (Memory Access / RESULT_PC / FETCH, width 1, no
   representable resulting offset/address on failure).
5. Probe the target's first instruction byte with X rights, CONTROL_TARGET / FETCH,
   width 1. Do not decode the target or require the saved next byte to be backed
   at call time. Commit all four frame bytes, SP-4 and target CS:PC together.

RETF fetches its one byte, validates the old stack configuration and `[SP,SP+4)`
bounds, then preflights the return-PC word READ and saved-CS word READ in that
order. Both word probes must succeed before interpreting the saved CS. Nonzero
saved-CS bits 15–8 raise Invalid State at the RETF start, before any target probe.
Otherwise probe the saved target with X rights, CONTROL_TARGET / FETCH, width 1,
and commit SP+4 and saved CS:PC together. No saved DS/SS or return-mode selection
occurs; RETURN_STATE/RETURN_TARGET are reserved for exception-return validation.
Unused sequential next need not fit: RETF at PC=$FFFF can succeed.

Frame memory faults use OPERAND / WRITE for CALLF or READ for RETF, width 2,
with attempted start equal to the failing word's start. Probe bytes in increasing
offset order; retain the first missing byte as the relevant address. Origin/map
follow the executing mode. The four-byte atomic frame does not introduce xwidth=4
or a device transaction. A failure writes no frame byte, pops no byte, and changes
no SP/CS/PC/flags before ordinary exception entry. IRQs observe only the state
before or after the whole instruction. An inaccessible target wins no priority
over an earlier stack/operand/next check; an invalid saved CS wins no priority
over an unavailable frame word.

ADJSP allocates locals with a negative constant and releases them with a positive constant. It preserves all flags, touches no memory, and neither initializes new slots nor probes their RAM mapping. Stack-relative MOV checks the resulting allocation and installed bytes when used. The existing stack must be valid before adjustment; use explicit configuration-register writes to repair an invalid stack. A failing adjustment leaves SP and memory unchanged before exception entry. If the initial configuration is invalid, report SS:old_SP; otherwise report SS:new_SP when representable, or an invalid fault address if the widened new_SP is outside 0–65535.

### Protected segmented memory

A **guest virtual address** is the pair `selector:offset`.
Physical memory still has exactly 24 address bits. Each descriptor contains:
physical frame number (8 bits), present, inclusive lower/exclusive upper limits
(17-bit upper bound can represent 65536), independent U/K R/W/X rights, memory
kind (RAM/MMIO), and optional stack capability/bounds. Stack SB/ST remain 16-bit, with ST at most
65535 even when the descriptor data limit is 65536. Translation is:

```
physical = (descriptor.frame << 16) | offset
```

Descriptors are protected CPU configuration indexed by address-space ID and
selector; there are 256 map IDs and 256 descriptors per map. Host representation
may allocate maps sparsely. Map 0 is the kernel map, never user-selectable.
There is no descriptor permission to change mode. Kernel code uses K rights in
map 0. User code uses U rights in its process map. `process_map` is the protected
process association saved in frames: K may set it to 0 or a published process ID
without changing its effective kernel map 0; U cannot write it. IRET restores it
from the validated image. This lets the kernel align live association and
scheduler ownership before returning a different U process. Shared memory is created by
kernel descriptors naming the same physical frame; rights can differ. Removing
all mappings before reusing a frame is kernel responsibility; DMA is outside this specification.

Translation uses a small protected segment-mapping table with no page walker
or TLB requirement. A descriptor lookup on every access is
sufficient. Whole-segment allocation and limited per-segment bounds are a real
cost: arbitrary 4 KiB sharing, sparse mappings within one segment, transparent
page-sized copy-on-write, and traditional Linux memory layouts are not supplied.
A kernel using segment-sized allocation can provide process isolation and
recoverable memory faults. Page-based translation and an unmodified Linux port are outside this specification.

Check widened offset/width, descriptor presence and bounds, rights for **every
byte**, memory kind consistency, and installed backing before commit. Instruction
bytes require X, data reads R, writes W, RMW would require R+W. A control transfer
validates X for the first target byte in the destination map/mode; sequential
fetch checks the next instruction when fetched. An unavailable later word byte
leaves the first byte untouched. No segment carry or physical wrapping occurs.
RAM stores to aliases of code still need W. Map changes are instruction-atomic
and apply on the following boundary. Single CPU initially: no cross-core TLB or
shootdown semantics. Devices cannot mutate mappings during an instruction.

Ordinary transfers and explicit kernel user-access transfers share the
[fault record and ordering](#user-access-fault-contract) below. Existing xaddr
words retain physical-address semantics; they never silently become virtual
addresses. Each access resolves one stable descriptor/backing view for the whole
instruction. A retry re-resolves the current map; it never reuses a saved physical
address. Host backing topology and RAM/MMIO classification are fixed between
resets. Host debugger mutations are outside that execution contract.

A kernel user-access transfer uses K execution privilege, keeps kernel code and
stack in map 0, but resolves its operand under **U permissions** in the selected
process map. Its B1 base and optional signed displacement form the offset; it
uses neither live DS/CS/SS nor stack-slot allocation semantics. It cannot access
MMIO, regardless of a descriptor's rights. Byte loads zero-extend; word transfers
may be unaligned; failed stores change no byte. There is no implicit pointer update.

K-only `uaccess_map` is an 8-bit ID in a 16-bit control word. Zero is a legal
**disabled** value, never permission to use kernel map 0. Writing zero also clears
`uaccess_segment`. `uaccess_segment` accepts an 8-bit selector; nonzero high bytes
in either control write fault Invalid State without truncation. Software may
configure a segment before enabling a map; disabled selectors grant no authority.
A transfer with map zero gets a Memory Access fault with reason ACCESS_DISABLED.
A nonzero unpublished map gets MAP_INVALID. Segment existence/rights are tested
on transfer, so setting an invalid user pointer does not itself dereference it.

Both controls are saved/restored in nested K contexts. K return accepts a disabled
map or a published map plus an 8-bit segment; it does not demand that the chosen
user segment be present or readable. Otherwise a handler could not return from
an expected invalid-pointer fault. U return requires both controls zero; these
are kernel-continuation state, not user-owned registers. Invalidated nonzero map
references are rejected during return validation. Saved control indices are
assigned under [extended saved context](#extended-saved-context); MOVU transfer
encodings are assigned under [user-access encoding](#user-access-encoding).

#### Explicit user-access transfers

MOVU provides K-only byte/word loads and stores under U permissions, with source
before destination. rn may be any r0–r7; rp must be a B1 base r4–r7:

```asm
MOVU.B [rp],rn
MOVU.W [rp+disp16],rn
MOVU.B rn,[rp]
MOVU.W rn,[rp+disp16]
```

Either width supports either addressing form and direction. Unsuffixed MOVU
defaults to `.W`. The selected segment is always uaccess_segment in uaccess_map;
`[rp]` does not default to DS for this instruction. Explicit CS/DS/SS prefixes,
absolute/DP/SP addressing, immediate values, register-register transfers,
memory-memory transfers, and internal value registers have no MOVU form.
Displacements are signed -32768..32767; assembly rejects out-of-range values
rather than narrowing. An absent or zero displacement selects the three-byte
form. A raw five-byte encoding with zero displacement is valid but noncanonical.

Effective start is `unsigned(old_rp) + s16(disp16)` in a wider signed type, with
zero displacement for the short form. Both bytes of a word must fit one segment;
there is no offset wrap, segment carry, stack-slot check, or pointer update.
Byte loads replace the entire destination with a zero-extended value; word loads
replace all sixteen bits. Byte stores sample source bits 7–0; word stores sample
all sixteen bits and use little-endian order. All forms preserve N/Z/C/V/I/E,
process_map, live segment/stack registers, and the user-access controls.

All address, source, and control operands are sampled from the input state. A
load whose destination is its B1 base uses the original address before replacing
that register. A store whose source equals its base stores the original register
value at the original computed address. No form needs an implicit temporary GPR.

Legally executing K instructions fetch code and use the kernel stack under map 0;
only MOVU's explicit data operand uses the configured user map and U R/W rights,
not its K rights or the live process_map association. In U, fetch/decode still
uses the current U code map normally; a fully legal MOVU then raises Privilege
with USER_ACCESS class `$000A` before any operand-state/mapping check.

For legal K execution, the existing ordered fault contract below is normative:
checked offset/width, enabled/published map, descriptor presence/bounds/U rights,
RAM kind and backing, then following-PC range. Disabled map zero never means
kernel access. MMIO is prohibited and causes no callback, including when a RAM
descriptor names actual device backing. If U rights fail earlier, RIGHTS wins
over DEVICE. Operand failure wins over a simultaneous following-PC failure;
decode and U privilege retain their earlier priority. No load/register/store byte
or flag effect commits until all checks succeed. Whole-instruction atomicity
does not make a multi-instruction copy atomic or prevent later map changes.

See the [copy/retry/fixup ABI example](#user-buffer-copy-and-recovery-abi) for
returning through a nested K transfer fault and the lower syscall frame.

#### User-access fault contract

All mapping, protection, backing and disabled-user-access failures below use the
existing Memory Access cause/vector 2; no separate user-access vector is needed.
Every such fault records the following **logical words**, read-only through
context access. [Extended context indices and numeric enums](#extended-saved-context)
are assigned in Chapter 5; the word widths, meanings, validity rules and
reset-on-entry behavior below are the execution contract. On nonmemory entries,
scalar/validity words clear and semantic tags are NONE (zero); legacy
xaddr reporting for Illegal/Invalid State remains unchanged.

| Word(s) | Logical representation |
|---|---|
| Existing xpc/xcs | Instruction-start offset/CS of the faulting instruction, including the K instruction attempting user access |
| xmem_origin | 16-bit symbolic tag: NONE, USER (ordinary U-map access), KERNEL (map 0), or UACCESS (explicit override operand) |
| xmem_phase | 16-bit tag: instruction fetch, operand, resulting-PC check, control-target probe, return-state selection, or return-target probe; NONE for nonmemory events |
| xaccess | 16-bit tag: FETCH, READ, WRITE, or NONE; permission checked for that failed access |
| xmapid | Target map ID, zero-extended from eight bits; map zero with UACCESS means disabled, not a K access |
| xsegment | Target virtual segment, zero-extended from eight bits |
| xoffsetlo/xoffsethi | Attempted start offset as a signed two's-complement 32-bit value, split low word then high word; preserves negative/too-large calculations exactly; zero for selector-only validation with no offset |
| xvaddrlo/xvaddrhi | Relevant virtual offset in low word; high word bits 0–7 segment and bit 8 valid, remaining bits zero; both words zero when invalid |
| Existing xaddrlo/xaddrhi | Relevant physical address; existing low-word / high-eight-bits plus valid-bit layout is unchanged |
| xwidth | Requested operand bytes, 1 or 2; instruction-byte fetches and target probes use 1; selector-only validation uses 0 |
| xreason | One symbolic reason: OFFSET_RANGE, ACCESS_DISABLED, MAP_INVALID, NOT_PRESENT, BOUNDS, RIGHTS, DEVICE or BACKING; NONE otherwise |

Return-state selection checks of DS/SS descriptors do not dereference a pointer:
use USER / return-state selection, xaccess=NONE, xwidth=0, the selected map/segment,
zero xoffset words and both address-valid bits clear. The reason still identifies
NOT_PRESENT, RIGHTS or DEVICE as appropriate. Numeric tags are defined in the
[fault enumeration table](#fault-enumerations).

Origin follows the **failed access**, not the opcode. A user-access data fault has
UACCESS / operand / READ-or-WRITE and identifies its selected process map.
Failure to fetch that K instruction uses KERNEL/FETCH origin/phase with
xaccess=FETCH; its following-PC range failure uses KERNEL/RESULT_PC with
xaccess=FETCH.
An IRET probe of a U target is USER / return-target even though IRET executes in
K. Ordinary Invalid State from malformed control values is not a user-pointer
fault. Handlers must not infer user-access origin from map ID or K mode alone.

After complete legal instruction fetch/decode, privilege and instruction-state
checks precede operand checks. Operand failure order is:

1. Widened offset/width must fit one segment: OFFSET_RANGE. A representable start
   is the relevant virtual address even when the width crosses the end; otherwise
   virtual and physical validity clear and xoffset still records the attempted value.
   No descriptor is consulted at this stage, so physical validity is clear for
   any OFFSET_RANGE failure, including a representable start whose width crosses.
2. Explicit access must be enabled, then target map published: ACCESS_DISABLED or
   MAP_INVALID. No descriptor resolution or physical address is valid yet.
3. Descriptor present: NOT_PRESENT. Missing descriptors have no physical address.
4. Entire span inside descriptor bounds, then all required U/K rights, then memory
   kind: BOUNDS, RIGHTS, DEVICE respectively. A present descriptor resolves the
   physical frame; a representable start has a physical address even on these
   protection failures. User-access and U operands reject MMIO before any callback.
5. Backing availability, in increasing byte order: BACKING. Record the first
   missing byte as both relevant virtual and physical address; retain the original
   start/width separately. For pre-backing failures with a representable start,
   the relevant virtual address is that start.
6. Selected resulting PC/target validation follows operand preflight;
   use that failed fetch's origin, phase, address and reason if it fails instead.

No operand byte, destination register, pointer, control or flags commit on failure.
Entry then snapshots the K instruction start and all live user-access controls
in the new top frame; it never overwrites the lower syscall frame. Example:
map 7, segment 3, word at $FFFE may transfer both bytes; if the second backing byte
is absent, the record reports attempted $FFFE/width 2 and relevant 03:$FFFF plus
its translated physical address. READ rights allow that load but do not authorize
an equivalent store; a denied write reports RIGHTS and writes neither byte.

Recovery is a kernel exception-table policy. A handler may repair the mapping and
IRET to retry, or match an explicitly registered (xcs,xpc) fixup with UACCESS and
an allowed reason, edit that K frame's resume PC/result, then IRET to a known
error continuation. That continuation can edit the lower syscall's result and
return to U. No implicit errno, instruction skip or decoded-length guess exists.
A missing fixup or unexpected KERNEL fault follows kernel bug policy. UACCESS
origin is evidence, not an instruction to turn every kernel error into EFAULT.

A descriptor edit between transfers affects the next transfer; one word cannot
observe half old/half new mapping. Copy routines needing stable identity/rights
must hold a kernel mapping lease/lock across their range. Instruction atomicity
does not make a whole copy atomic. Fault handlers may repair a missing mapping
under the lease's repair policy, then retry against the new complete descriptor.

#### Map ownership, publication and ID reuse

Map 0 is permanently the kernel map. Each of IDs 1–255 has a protected published
bit, initially clear. Unpublished maps cannot be selected for U return or user
access. K can construct descriptors while unpublished, then publish the completed
map with one validated control commit. Descriptor edits likewise stage fields
and publish a whole descriptor at one instruction boundary; live control readback
returns committed fields only. No hardware generation bits or wider map IDs are added.

Invalidation atomically clears publication and all descriptor-present bits;
future accesses and return validation reject it. Republish cannot revive old
grants implicitly, even with sparse/lazy host storage. Construction for a
new owner must explicitly install fresh descriptors before republishing. Physical
RAM is not cleared by invalidation: the kernel must initialize private frames
before granting them to a different owner. The assigned
[protected control interface](#protected-control-interface) defines the numeric
selection, staging, readback, and publication operations.
The [map-ID lifetime ABI](#map-id-lifetime-abi) specifies software ownership and
reuse discipline; publication bits do not carry a hardware generation number.

## 4. Instruction execution

The following rules govern every assigned instruction. Privilege, translation
and exception-entry rules are applied at their stated validation phase; they do
not relax complete fetch or atomic commit.

### Instruction fetch and PC updates

Fetch all instruction bytes from the original CS, with increasing offsets and no wrapping. A fetch beyond `$FFFF` or from an uninstalled byte faults at that instruction. Fetch/decode does not architecturally advance PC before commit.

Let `next = original_PC + instruction_length`, computed without truncation:

- Ordinary instructions and HALT require `next <= $FFFF` and commit PC=next.
- Untaken conditional branches likewise require a representable next.
- Taken branches use `target = next + s16(rel16)` and require `0 <= target <= $FFFF`. Their intermediate next may equal `$10000` if the resulting target is valid.
- JS, JL, RET, RETF, and IRET use their explicit target; unused next need not be representable. CALL and CALLF require a representable next because they save that return offset.
- SWI requires a representable next because it saves that resume offset in xpc. It transfers through vector 6 without checking whether the following instruction is installed; IRET checks the selected resume target later.
- A move to CS uses next in the new segment, so next must be representable.

Explicit transfers (taken branches, JS, JL, CALL, RET, CALLF, RETF, IRET, and writes to CS) validate that the target's first byte is installed before committing. They do not decode the target instruction; a later decode fault belongs to that target instruction. Ordinary sequential execution checks the following instruction's memory on its next fetch.

Consequently, NOP at `$FFFF` faults without advancing PC, while a three-byte BRA at `$FFFD` may branch back within its segment. No instruction's bytes may themselves cross a segment boundary.

### Fault priority and commit

Atomicity applies to **one instruction**, not an instruction sequence. Each successful instruction remains committed if a later instruction faults or an IRQ is accepted between them. Fault entry snapshots the state immediately before the faulting instruction; IRQ entry snapshots the interrupted boundary. Successful SWI snapshots its input state with the following PC as xpc. None rolls back earlier instructions. This applies equally to sequences of ADJSP, ADDP, or ordinary memory/register operations.

For example, with `SB=$1000` and `SP=ST=$10C8` (200 bytes available), the first `ADJSP #-128` succeeds with SP=$1048. A second `ADJSP #-128` proposes $0FC8 and faults. The saved xsp is $1048, and xpc identifies the second adjustment. The first allocation remains in effect. Similarly, ADDP #127 from $FF80 succeeds at $FFFF; a following ADDP #1 faults with $FFFF saved, not the pointer value from before the sequence. Software must account for completed steps and explicitly undo them when recovery requires it. Prevalidating a whole allocation can avoid an expected limit failure, but it does not make several instructions atomic or prevent an IRQ from observing an intermediate state.

Checks occur in this order; the first failure wins:

1. Fetch/decode bytes in order. Reject reserved primary/secondary opcodes as soon
   as they are known; do not fetch operands of an illegal form. For legal forms,
   fetch all bytes and check reserved operand fields as they arrive.
2. Check privilege before instruction-specific operand state. A privileged
   operation in U raises Privilege without changing state.
3. Validate instruction-specific state, including segment/CSR/JL values, ADDP's
   widened result, active-frame requirements and IRET's saved-image fields.
   IRET uses the complete return-image validation order in Chapter 6.
   RETF's saved CS is checked only after both frame words are readable, as
   specified in the far-call contract; it is not an instruction-byte scalar.
4. Validate stack configuration and bounds, or widened effective offset/width.
   Stack and ADJSP failures use Stack Boundary; data/fetch failures use Memory
   Access. Translation, permissions, memory kind and backing use the ordered
   [memory fault contract](#user-access-fault-contract).
5. Preflight operand transfers without committing effects. Validate the selected
   resulting PC and explicit target after operand checks. No irreversible MMIO
   read or write occurs until every CPU and device preflight check has passed.
6. Compute and commit all effects together. SWI commits its protected entry
   bundle with the following PC saved. An eligible external interrupt is
   considered at the next boundary according to I and source-controller state.

An invalid IRET raises a nested fault when frame capacity and kernel entry are
valid; it does not pop the rejected frame. Failed entry or frame exhaustion is
terminal Double Fault. Reset/vector reads are entry operations outside ordinary
decoding. Requests arriving during an instruction are observed after completion
or synchronous entry; a synchronous fault or successful SWI entry takes precedence.

### Execution states

The execution state is independent of ordinary-frame depth. E indicates whether
any ordinary frame is active; it is not an interrupt mask.

| State | Saved exception frames / E | Execution and exit |
|---|---|---|
| Running | Depth 0–8; E=1 iff depth is nonzero | Execute instructions and accept eligible interrupts at boundaries |
| Halted | Current frames retained; E derived from depth | No fetch; an eligible IRQ or host reset exits this state |
| TerminalResetFault | No frame / E=0 | No fetch or IRQ delivery; host reset is required |
| TerminalDoubleFault | Captured frames retained; E derived from depth | No fetch or IRQ delivery; host reset is required |

HALT commits its following PC. An eligible interrupt wakes the CPU with that PC
saved; IRET resumes there without restoring Halted. Masked requests do not wake
it. A K handler with I=1 can be interrupted even when E=1. A halted CPU without an
eligible source waits for a request, deterministic idle-time advancement or reset.
Reset exits Halted and terminal states and clears pending/in-service sources.

## 5. Exception frames and fault records

This chapter defines event classification and the established saved-word
interface. Chapter 6 defines protected capture, entry and return. Chapter 7
specifies source arbitration and delivery. Exception state is CPU-managed and
never depends on the interrupted process's stack.

### Vectors and causes

The vector table uses kernel virtual byte address `4 * vector` in map 0. Each
entry is a little-endian packed address in the JL format, including a zero
reserved high byte. Vector entries and their aliases must be protected from U
writes. Reset reads vector 0 under the boot identity map. The host provides a
valid reset vector; kernel software installs the required protected handlers.

| Vector / cause | Name | Trigger |
|---:|---|---|
| 0 | Reset | Host reset; not an exception context |
| 1 | Illegal Instruction | Unassigned opcode or reserved encoding field |
| 2 | Memory Access | Fetch/data/control-target unavailable or outside its segment |
| 3 | Stack Boundary | Invalid stack configuration, direct SP-write or ADJSP violation, or stack access outside allocation/limits |
| 4 | Invalid State | Bad segment/CSR/JL/CALLF value, noncanonical RETF saved CS, ADDP result outside the offset range, inactive-context operation, or invalid IRET state |
| 5 | External Interrupt | Pending maskable interrupt accepted at an instruction boundary |
| 6 | Software Trap | SWI with a representable following PC; independent of I |
| 7 | Privilege | U attempts a legally decoded privileged operation; saved operation class identifies the denied operation |

### Precise entry and saved context

A synchronous fault leaves the instruction's ordinary registers and memory unchanged, then enters its vector. Saved PC identifies its first byte, even if a later instruction byte failed to fetch. An external interrupt saves the boundary PC where execution would resume. A successful SWI instead saves the following PC and service number; a SWI fetch/state/PC fault retains ordinary fault semantics.

Entry captures the complete input state in a protected frame, then validates and
commits a kernel execution environment atomically. Capture does not access the
ordinary stack. A U event selects protected kernel stack and DS state; a K event
retains its allocated kernel stack. The precise entry sequence is in Chapter 6.

The following assigned context indices provide the established register and
metadata interface. Additional state and fault metadata have separately assigned
[extended indices](#extended-saved-context) without changing these legacy forms.

| Index (hex) | Context name | Saved value | Writable with SETX |
|---|---|---|---|
| 00–07 | `xr0–xr7` | r0–r7 | Yes |
| 08 | `xpc` | Faulting/interrupted PC, or the following PC for SWI | Yes |
| 09 | `xcs` | CS | Yes |
| 0A | `xcsr` | CSR before entry, with E=0 | Yes |
| 0B | `xsp` | SP | Yes |
| 0C | `xsb` | SB | Yes |
| 0D | `xst` | ST | Yes |
| 0E | `xss` | SS | Yes |
| 0F | `xdp` | DP | Yes |
| 10 | `xds` | DS | Yes |
| 11 | `xcause` | Cause number; upper bits zero | No |
| 12 | `xaddrlo` | Relevant physical address bits 15–0 | No |
| 13 | `xaddrhi` | Address bits 23–16 in bits 7–0; bit 8 means valid; others zero | No |
| 14 | `xtrap` | SWI service number in bits 7–0; zero on non-SWI entry; upper bits zero | No |

This legacy context interface contains 21 words; its first 17 are writable.
Protected state and additional metadata extend the interface without renumbering
these fields. Every non-SWI entry sets xtrap=0, including a fault while fetching or validating SWI. Cause 6 distinguishes SWI #0 from those other entries. IRET does not restore read-only metadata as live registers. The saved service number avoids consuming an argument register or rereading mutable instruction memory.

The saved CSR contains NZCVI, with E=0 and every reserved bit zero. This
representation also applies to interrupted K handlers: E is derived from frame
depth after return rather than restored from xcsr. SETX may temporarily store an invalid xcsr, including E=1, and GETX reads that stored word back unchanged. IRET rejects it; it does not mask or silently clear invalid bits. A failed IRET preserves the edited frame and raises a nested fault when ordinary entry is possible.

The relevant address is the first unavailable instruction/data byte, a representable start address of an invalid access, or an explicit control target. For Illegal Instruction and Invalid State it is the instruction start. If an access starts inside the segment but its width crosses the end, report the start; if the computed start is outside the offset range, both address words are zero and the valid bit is clear. An instruction fetch crossing the segment end also has no representable missing-byte address. External interrupts and successful software traps have no fault address: both address words are zero. A failed direct SP write reports SS:new_SP; a failed IRET stack invariant reports xss:xsp. For ADJSP, an invalid initial stack reports SS:old_SP; otherwise a rejected new_SP reports SS:new_SP if representable, with both address words zero and valid clear if not. Other stack failures report the attempted access start when representable. The [fault-priority rules](#fault-priority-and-commit) determine the reported address when multiple checks fail.

### Extended saved context

The assigned extended GETX/SETX forms use a full eight-bit third-byte selector
and a GPR selector in the second byte; see
[extended context encoding](#extended-context-encoding). They address only the
top ordinary frame, just like legacy GETX/SETX. No instruction selects a lower
frame, physical frame address, or terminal diagnostic record.

| Index (hex) | Context name | Saved value | Writable with extended SETX |
|---|---|---|---|
| 20 | `xprocess_map` | Interrupted process association, zero-extended ID | Yes |
| 21 | `xuaccess_map` | Interrupted user-access map, zero-extended ID | Yes |
| 22 | `xuaccess_segment` | Interrupted user-access segment, zero-extended selector | Yes |
| 23 | `xorigin_mode` | CPU-captured mode: U=0, K=1 | No |
| 24 | `xreturn_mode` | Derived U=0/K=1 from origin and bootstrap marker | No |
| 25 | `xprior_depth` | Captured parent depth, 0–7; 0 means no parent | No |
| 26 | `xframe_depth` | Protected frame ordinal, 1–8; xprior_depth+1 | No |
| 27 | `xbootstrap` | CPU-owned one-way U-bootstrap marker, 0 or 1 | No |
| 28 | `xirq_source` | Accepted IRQ source 0–15; zero on every non-IRQ entry | No |
| 29 | `xpriv_class` | Denied operation class below; zero on every non-Privilege entry | No |
| 30 | `xmem_origin` | Memory origin enum | No |
| 31 | `xmem_phase` | Memory phase enum | No |
| 32 | `xaccess` | Failed access enum | No |
| 33 | `xmapid` | Failed access map, zero-extended eight-bit ID | No |
| 34 | `xsegment` | Failed access selector, zero-extended eight-bit value | No |
| 35 | `xoffsetlo` | Attempted signed 32-bit start offset, bits 15–0 | No |
| 36 | `xoffsethi` | Attempted signed 32-bit start offset, bits 31–16 | No |
| 37 | `xvaddrlo` | Relevant virtual offset, or zero when invalid | No |
| 38 | `xvaddrhi` | Relevant selector bits 7–0, valid bit 8, remaining bits zero | No |
| 39 | `xwidth` | Requested bytes 1/2, or 0 for selector-only validation/nonmemory | No |
| 3A | `xreason` | Memory failure reason enum | No |

These are 21 additional words: three writable image fields and eighteen
read-only words. Combined with the legacy interface, the frame exposes 42 words,
of which twenty are writable. Extended selectors `$00–$1F`, `$2A–$2F`, and
`$3B–$FF` are unassigned and illegal in the extended encoding, including legacy
indices `$00–$14`; legacy instructions remain the sole encoding of those words.
An extended access does not alias or widen the legacy five-bit field.

On capture, the new writable fields contain the exact interrupted control
values, including a map ID invalidated since it was selected. Extended SETX
stores an entire source word unchanged; it does not truncate a selector or
immediately validate publication, mode compatibility, or canonical high bytes.
GETX reads temporary invalid edits back unchanged. Full IRET validation enforces
all scalar/map/user-access rules in the established order. In particular,
setting saved xuaccess_map=0 does not clear saved xuaccess_segment: unlike a
live SETC disable, saved-image edits are independent so related fields can be
replaced in any order. U return still requires both zero.

Origin, parent linkage/depth, and bootstrap authority are CPU-owned, never copied
from a PCB through SETX. xreturn_mode is U for a U-origin frame or a marked
K-origin frame, K otherwise. PREPARE_USER_RETURN is the only operation that can
change a K-origin frame's bootstrap marker; it leaves xorigin_mode, xprior_depth,
and xframe_depth unchanged. IRET derives mode from that authority, not from an
editable CSR/image word. Reading depth exposes no authority to modify linkage.

Every new frame initializes its metadata from its own event; it never inherits
stale tags/addresses from a lower frame. Source zero is a real IRQ source only
when xcause=5; otherwise xirq_source is zero without identifying an IRQ. Likewise
xcause=6 distinguishes SWI service zero, and xcause=7 distinguishes Privilege.
All selector/mode/depth/source/enum values are zero-extended canonical words;
only xoffsethi deliberately retains sign bits of a signed widened offset.
The virtual and legacy physical address high words keep their explicit bit-8
valid flags. Invalid addresses have both associated words zero. Legacy xaddr
continues to mean a physical address, never a virtual selector:offset.
Reset discards all ordinary frames and their metadata; an inactive GETX/SETX
raises Invalid State in K rather than exposing a zero or stale saved image.

#### Fault enumerations

All values below are exact 16-bit discriminants, not bitmaps. No other value is
generated. Numeric order does not change the already specified fault priority.

| Context field | Assigned values |
|---|---|
| xmem_origin | NONE=`$0000`, USER=`$0001`, KERNEL=`$0002`, UACCESS=`$0003` |
| xmem_phase | NONE=`$0000`, FETCH=`$0001`, OPERAND=`$0002`, RESULT_PC=`$0003`, CONTROL_TARGET=`$0004`, RETURN_STATE=`$0005`, RETURN_TARGET=`$0006` |
| xaccess | NONE=`$0000`, FETCH=`$0001`, READ=`$0002`, WRITE=`$0003` |
| xreason | NONE=`$0000`, OFFSET_RANGE=`$0001`, ACCESS_DISABLED=`$0002`, MAP_INVALID=`$0003`, NOT_PRESENT=`$0004`, BOUNDS=`$0005`, RIGHTS=`$0006`, DEVICE=`$0007`, BACKING=`$0008` |

Nonmemory events clear all eleven words `$30–$3A` to zero. This includes
Privilege, Illegal Instruction, Invalid State, Stack Boundary, successful SWI,
and IRQ entry; legacy xaddr may still report an instruction/stack address as
already specified. Memory Access entries populate these words from the failed
access, including KERNEL/OPERAND selector-only ENTRY_COMMIT probes and
USER/RETURN_STATE selection probes during IRET or PREPARE_USER_RETURN. A
failure fetching the IRET/prepare instruction is instead a KERNEL/FETCH event.
Legacy xaddr reporting for nonmemory faults remains unchanged.

For example, a negative attempted offset -1 is xoffsetlo=`$FFFF`,
xoffsethi=`$FFFF`, with both virtual/physical address pairs invalid and zero;
+65536 is `$0000`/`$0001`, also invalid. A representable word start `$FFFF`
crossing the segment gives OFFSET_RANGE, width=2, a valid relevant virtual start,
and invalid physical address, since no descriptor was consulted. For map 7,
selector 3, frame `$23`, a word start `$FFFE` whose second backing byte is absent
has origin UACCESS=3, phase OPERAND=2, READ=2 or WRITE=3, reason BACKING=8,
xmapid=7, xsegment=3, xoffsetlo=`$FFFE`, xoffsethi=0, xwidth=2,
xvaddrlo=`$FFFF`, xvaddrhi=`$0103`, xaddrlo=`$FFFF`, and xaddrhi=`$0123`.

#### Privilege exception record

Privilege has **cause/vector 7**. Its four-byte packed kernel handler address
occupies map 0, selector 0, offsets `$001C–$001F`; causes/vectors 0–6 retain their
assignments. No new emergency/Double Fault vector is introduced. A legal
privileged operation attempted in U saves its instruction-start PC/CS, canonical
U xcsr with I=1, the physical instruction-start address in legacy xaddr, and
the following xpriv_class. Ordinary effects do not commit; normal protected
entry handles the event. xtrap, xirq_source, and memory words `$30–$3A` are zero.

| xpriv_class | Name | Legally decoded U operation denied |
|---|---|---|
| `$0000` | NONE | Every non-Privilege entry; never a Privilege class |
| `$0001` | CSR_WRITE | Direct MOV to live CSR |
| `$0002` | INTERRUPT_CONTROL | DI/EI, including CLI/SEI aliases |
| `$0003` | HALT | HALT |
| `$0004` | STACK_LIMIT_WRITE | Direct MOV to SB or ST |
| `$0005` | CONTEXT_READ | Legacy or extended GETX |
| `$0006` | CONTEXT_WRITE | Legacy or extended SETX |
| `$0007` | EXCEPTION_RETURN | IRET |
| `$0008` | CONTROL_READ | GETC with a readable control selector |
| `$0009` | CONTROL_WRITE | SETC with a writable control selector, including command writes |
| `$000A` | USER_ACCESS | MOVU byte/word load/store |

Class follows the decoded operation, not a source register's data. Thus an
invalid command value in an otherwise legal U SETC is Privilege/CONTROL_WRITE,
before command validation; no command-specific class is inferred. An unknown
selector, read-only SETX destination, or reserved encoding remains Illegal
Instruction before privilege and sets xpriv_class=0. A missing required byte of
an otherwise legal form is a fetch fault before privilege, also with class zero.
Once legal fetch/decode completes, U GETX/SETX/IRET is Privilege even with no
active frame, before any inactive-context or return-image check.

### IRET and nesting

IRET requires K mode and an active top frame. It validates the complete saved
image, restores its writable state and protected return mode, then pops exactly
one frame atomically. Live handler registers are discarded. E is recomputed from
remaining depth. It does not pop the ordinary stack. A failed validation leaves
the frame unchanged before entering a nested fault handler. See the
[complete return-image validation](#complete-return-image-validation) contract.

There is no saved decoded-instruction-length field. A handler redirecting xpc
must obtain a known valid continuation from program layout or safe software
decoding. A fault may precede complete fetch/decode, so neither nominal length
nor bytes already fetched supplies an implicit resume offset.

## 6. Protected exception architecture

Protected execution retains the B0/B1 register model, 16-bit offsets,
little-endian representation and precise per-instruction commit. Its fixed
architectural parameters are:

| Parameter | Value |
|---|---|
| Execution modes | User (U), Kernel (K); mode is protected state outside CSR |
| Physical memory | 24 bits |
| Address-space IDs | 0–255; map 0 reserved for kernel |
| Descriptors per map | 256, indexed by 8-bit segment selector |
| Descriptor physical frame | 8 bits, 64 KiB frame |
| Exception frames | 8 ordinary protected frames; no executable emergency context |
| IRQ sources | 16; source 0 reserved for timer |
| Virtual time | Saturating 64-bit instruction clock, with explicit recorded idle advancement |
| User interrupt enable | Always I=1; user code cannot modify it |
| Protected access encodings | GETC/SETC and extended GETX/SETX assigned under `$1F`; implemented in working mode |
| Privilege event | Cause/vector 7; operation-class enum assigned in Chapter 5 |
| User-access encodings | MOVU under `$27`, three/five-byte forms; implemented in working mode |
| Machine interfaces | PIC/timer `minimal-v1` MMIO assigned in Chapter 7; other peripheral layouts pending |

Assigned context indices `$00–$14`, CSR bit assignments, SWI `$26 imm8` and
IRET `$1F $12` are fixed. Additional context/control fields require a separately
assigned encoding family; the five-bit context selector is not widened implicitly.

### Nested contexts, entry, and terminal diagnostics

There are exactly eight protected ordinary frames. Capture is independent of
user or kernel stack memory. GETX/SETX address only the top frame. E is derived
from ordinary depth; it does not block IRQs. A K handler can set I after
its entry environment is ready, allowing other eligible sources to nest.

A frame contains the original r0–r7, PC/CS, NZCVI, SP/SB/ST/SS, DP/DS, process-map
ID and user-access map/segment, plus event metadata. Saved origin/return mode and
prior depth/linkage are read-only to SETX. Ordinary state replacement never copies
those authority fields from a PCB. A U-origin frame always returns to U. The
K-only `prepare_user_return` action may mark exactly one depth-1 K-origin frame
for U bootstrap, with live I=0; it validates the complete U image and kernel-entry
configuration before changing that one-way marker. Invalid input leaves the
marker and frame unchanged before fault entry. There is no U→K promotion action.

Entry is one uninterruptible state transition:

1. Complete the event's preceding validation. Faults save instruction start,
   SWI saves representable next plus service, IRQ saves the boundary PC/source.
2. If an ordinary slot exists, reserve it and capture all input state, including
   an invalid interrupted stack. No guest stack write or memory probe is needed
   for capture. A faulting instruction has committed no ordinary effects.
3. Stage K mode/map 0, I=0, HALT clear, protected kernel DS and vector CS:PC.
   For U→K stage the protected entry SS/SB/ST/SP; for K→K retain the current
   allocated K stack. Do not probe the user's stack.
4. Validate vector representation/readability, handler RAM/X target, kernel DS,
   and selected stack descriptor/configuration/RW RAM backing. The allocated K
   interval and a next-push slot below SP must be valid. Commit the complete live
   entry bundle only after all checks pass. Successful IRQ entry alone consumes
   pending and marks its source in-service.

Vectors remain protected four-byte little-endian packed kernel virtual addresses
under map 0. The kernel must protect all aliases of vector/control memory.
Entry does not dispatch services or impose software result conventions.

**Failed entry or capacity overflow is terminal Double Fault.** Retain every
previous frame. If capture reserved a slot, retain that event frame too; if depth
was already eight, do not push or overwrite anything. Ordinary registers/memory
stay at the pre-entry state and no handler starts. Failed IRQ entry leaves its
pending bit unconsumed. No executable emergency frame, vector, map, stack,
configuration, special IRET or retry path is defined. Host reset is the
recovery path.

The first terminal transition latches a host-only diagnostic record until reset:
terminal reason (capacity/entry failure), failing stage (reserve/vector read/
vector format/handler/DS/stack), structured underlying failure with relevant
addresses, event cause/service/source and chosen saved PC, depth/slot-captured
status, pre-entry live registers/controls, and PIC pending/in-service state.
This diagnostic is not a ninth resumable frame and is inaccessible to guest
GETX/SETX. Terminal steps and requests do not execute, accept IRQs or change the
latched record. Reset clears it and all frames. Bad reset remains a separate
TerminalResetFault with no saved frame.

#### Complete return-image validation

Use the following shared contract for IRET and software prevalidation, checking
table rows top to bottom (DS before SS within segment selection). Validate
instruction state before target backing checks; all live state and the top frame
remain unmodified if validation fails. The resulting validation fault is then
nested normally if a slot and valid K entry environment exist. Only entry failure
or exhausted depth is terminal; ordinary invalid IRET is not automatically fatal.

| Check | Required value / failure class |
|---|---|
| Frame and authority | Active top frame with CPU-owned linkage; K caller; return mode derived from origin/bootstrap marker, never supplied by the image; inactive/invalid state gives Invalid State |
| Scalar fields | GPR/offset/DP/SP/SB/ST are 16-bit; all segment/map selectors have zero high byte; xcsr contains only NZCVI, including no saved E/reserved bits; violation gives Invalid State |
| Process map | U requires published ID 1–255; K accepts 0 or a published process ID. Unpublished/noncanonical image IDs give Invalid State |
| User-access controls | K accepts disabled map or published nonzero map and an 8-bit segment, without testing its descriptor. U requires both zero. Invalid state gives Invalid State |
| U interrupt policy | Saved I must equal 1; rejection is Invalid State, never silent enable-on-return |
| Numeric stack invariant | SB≤SP≤ST; failure gives Stack Boundary before descriptor checks |
| U segment selection | DS is present U-data-capable RAM (R or W); SS is present U-RW stack-capable RAM. Check presence, rights/capability, then kind. Failures use selector-only NOT_PRESENT, RIGHTS or DEVICE respectively. CS and its saved PC are checked by the resume-target probe below |
| U stack policy | U SB/ST exactly match SS policy and fit its descriptor limits; failure gives Stack Boundary. User stack backing is checked on use, not probed wholesale at return |
| Resume target | Only the first byte at saved CS:PC is checked for X/RAM/backing under the derived mode/map; complete instruction decoding occurs after return. Failure gives Memory Access with return-target phase |

K return uses map 0 for code/stack and keeps its configured stack semantics; it
cannot restore an invalid stack invariant. Ordinary data pointers and disabled
user-access controls may still name missing memory: their next use can fault.
Return restores all writable state and pops one frame atomically only on success.
Live handler registers are otherwise discarded. Fault metadata cannot be edited
into live state. Inspectable descriptor readback and the immutable boot backing
map expose all facts needed for the kernel to apply the same U-image checks.

Recovering from a user-access fault uses a normal second frame, then returns
through the original syscall frame. A fault in K code does not imply terminal
state merely because E=1. Conformance must cover depths 1–8, nested fault/IRQ
restoration, bad IRET recovery, each failed-entry stage and depth-8 overflow with
retained frames/diagnostics. No conformance test may expect guest emergency retry.

### Reset and boot contract

Host reset clears all ordinary frames and terminal diagnostics, pending/in-service
sources and PIC enables; disables the timer with counter zero; clears r0–r7,
PC, CS, DS, SS, DP, CSR and process/user-access selectors; and enters K with
SB=0 and SP=ST=$FFFE. User maps become invalid. Kernel map 0 resets to identity frame
translation with K-only rights over the machine's physical memory kinds; no U
rights exist. MMIO remains non-executable and unsuitable for stacks regardless
of descriptor rights. Reset vector 0 uses the existing physical packed-address
format under that boot identity map and probes its RAM target before commit.
Failure is TerminalResetFault with no frames. RAM contents survive reset.

Boot code installs protected vectors, kernel mappings and kernel-entry stack/DS
before starting U. `prepare_user_return` requires valid kernel-entry configuration
as well as the complete U image. There is no emergency configuration to initialize.
Reset invalidates prior user maps and disables/clears user-access controls, rather
than inheriting an interrupted process's authority.

### Protected control interface

GETC and SETC are assigned three-byte K-only word transfers between CPU controls
and any r0–r7. They preserve all flags and do not access guest memory or MMIO.
Control commands may perform pure validation probes but never read device data.
Their [binary encoding](#protected-control-encoding) is distinct from legacy
and extended GETX/SETX. Saved-context selectors are assigned separately in
[Chapter 5](#extended-saved-context).

```asm
GETC control,rn
SETC rn,control
```

`control` is a name from the table below or an unsigned eight-bit selector literal
without `#`. Register operands are GPRs only. No size suffix, immediate-source,
internal-register, or memory form exists. An assembler rejects unknown selectors,
wrong operand counts, and reads/writes unsupported by the selected control.

#### Control selectors and reset values

All controls are 16-bit words. R/W describes GETC/SETC permission; command is
write-only. Unknown selectors and wrong access directions are Illegal Instruction,
recognized when the third byte arrives, before privilege checking. Legal U-mode
transfers raise Privilege, even if their value, command, or current state would
also be invalid. Invalid data in a legal K write raises Invalid State unless a
command's explicitly ordered validation specifies Stack Boundary or Memory Access.

| Selector | Control name | Access | Value / host-reset state |
|---|---|---|---|
| `$00` | `process_map` | R/W | 0 or published ID 1–255; reset 0; high byte must be zero |
| `$01` | `uaccess_map` | R/W | ID 0–255, reset 0; high byte zero; zero disables and also clears uaccess_segment |
| `$02` | `uaccess_segment` | R/W | Selector 0–255, reset 0; high byte zero |
| `$03` | `mode` | R | U=0, K=1; GETC itself requires K; reset 1 |
| `$04` | `frame_depth` | R | Ordinary-frame count 0–8; reset 0 |
| `$05` | `entry_configured` | R | 0 until successful ENTRY_COMMIT, then 1 until reset; not a guarantee that later map edits preserve entry validity |
| `$10–$14` | `entry_ds`, `entry_ss`, `entry_sb`, `entry_st`, `entry_sp` | R/W | Shadow tuple, in this order; reset all zero; DS/SS have zero high byte |
| `$18–$1C` | `active_entry_ds`, `active_entry_ss`, `active_entry_sb`, `active_entry_st`, `active_entry_sp` | R | Committed tuple, in this order; reset all zero |
| `$20` | `desc_map` | R/W | Descriptor/map operation target ID 0–255; reset 0; high byte zero |
| `$21` | `desc_segment` | R/W | Descriptor operation target selector 0–255; reset 0; high byte zero |
| `$22–$28` | `desc_frame`, `desc_flags`, `desc_lower`, `desc_upper_lo`, `desc_upper_hi`, `desc_sb`, `desc_st` | R/W | Descriptor shadow fields, in this order; reset all zero |
| `$29` | `map_published` | R | Publication bit for desc_map, zero-extended; map 0 always reads 1; other maps reset 0 |
| `$30–$36` | `active_desc_frame`, `active_desc_flags`, `active_desc_lower`, `active_desc_upper_lo`, `active_desc_upper_hi`, `active_desc_sb`, `active_desc_st` | R | Committed descriptor at desc_map:desc_segment; field order matches `$22–$28` |
| `$40–$41` | `backing_addr_lo`, `backing_addr_hi` | R/W | Shadow physical query address, low word/high eight bits; reset zero; high-word bits 15–8 must be zero |
| `$42` | `backing_kind` | R | Latched query result: HOLE=0, RAM=1, MMIO=2; reset 0 |
| `$43–$44` | `backing_run_lo`, `backing_run_hi` | R | Latched unsigned 32-bit run length, low word then high word; reset 0 |
| `$45` | `backing_valid` | R | Query result exists: reset 0; successful BACKING_QUERY sets 1 |
| `$7F` | `command` | W | Execute one command below; there is no stored/readable command value |
| All others | Reserved | — | Illegal Instruction |

Individual shadow writes enforce only their field's scalar representation.
desc_frame must have a zero high byte, desc_flags may use only bits `$01FF`,
and desc_upper_hi may be 0 or 1. Other descriptor fields are unrestricted words
until commit validates their relationships. Changing desc_map or desc_segment
does not load, clear, or publish shadow fields. GETC of a shadow never substitutes
committed data; GETC of an active descriptor never exposes uncommitted edits.

Writing a nonzero uaccess_map does not require it to be published or probe its
segment; transfer-time checks report MAP_INVALID as already specified. Setting
process_map does require publication, except for zero, and never changes the
effective K map 0. Invalidation does not rewrite live/saved process-map or
user-access references; subsequent transfers/returns validate them normally.

#### Descriptor representation and validation

The descriptor fields encode exactly this structure; there is no guest pointer
to a host descriptor and no hardware generation field:

| Field | Representation |
|---|---|
| frame | Low eight bits, high byte zero; physical frame of 64 KiB |
| flags bit 0 | PRESENT |
| flags bit 1 | MMIO when 1, RAM when 0 |
| flags bit 2 | STACK capability |
| flags bits 3, 4, 5 | U read, write, execute, respectively |
| flags bits 6, 7, 8 | K read, write, execute, respectively |
| flags bits 15–9 | Reserved zero |
| lower | Inclusive unsigned 16-bit offset |
| upper_lo / upper_hi | Exclusive 17-bit bound: `upper_lo + 65536*upper_hi`; upper_hi is 0 or 1 |
| sb / st | Unsigned 16-bit stack-policy bounds |

DESC_COMMIT checks the following rows in order; all failures here are Invalid
State. It performs no operand-memory or backing probe and does not require the
map to be published. Present mappings may deliberately include backing holes;
ordinary accesses retain their existing precise fault checks.

1. Every field has its canonical scalar representation above. An absent
   descriptor must have all seven fields zero; this is the only canonical absent
   image. A nonzero flag or field with PRESENT=0 is invalid.
2. The reconstructed upper bound is at most 65536 and lower <= upper. Thus
   upper_hi=1 requires upper_lo=0. A present empty interval is permitted and
   grants access to no bytes. Data bounds can include offset `$FFFF`.
3. Map 0 descriptors have no U rights. A descriptor marked MMIO has no U rights,
   no U/K execute rights, and no STACK capability. RAM/MMIO classification is
   also checked against the actual immutable machine backing on each access;
   assigning a RAM descriptor cannot turn a device into executable RAM.
4. Without STACK, sb=st=0. With STACK, require
   `lower <= sb <= st <= upper` and st <= 65535. Stack capability/bounds are
   independent of rights; U stack selection additionally requires U R/W RAM.

Each commit installs the whole selected descriptor at one boundary. Editing a
published map is permitted and takes effect on following instructions. It does
not revalidate every live pointer, stack, return image, or kernel-entry tuple.
In particular, a map-0 edit can remove the caller's next fetch or its handlers;
the kernel must keep its execution, vectors, and recovery mappings pinned. The
commit's own bytes use the old stable mapping and ordinary next-PC range checks.
An absent/uninstalled byte at the following PC is checked on the following fetch.

#### Assigned control commands

`SETC rn,command` samples the original rn and executes the corresponding exact
16-bit value. Other values raise Invalid State, not Illegal Instruction, after
legal fetch and privilege checks. Failed commands preserve every shadow, active
descriptor, control, map-publication bit, query latch, and bootstrap marker before
ordinary exception entry. All commands also require a representable following
PC; that check follows their state/operand validation and precedes any commit.

| Value | Command | Successful effect |
|---|---|---|
| `$0001` | DESC_COMMIT | Validate and install the descriptor shadow at desc_map:desc_segment; leave shadow and selection unchanged |
| `$0002` | DESC_LOAD | Copy all seven selected committed descriptor words into the shadow; leave selection unchanged |
| `$0003` | MAP_PUBLISH | For nonzero desc_map, publish its committed descriptor set; leave shadow unchanged; publishing an already published map is a no-op |
| `$0004` | MAP_INVALIDATE | For nonzero desc_map, atomically clear publication, reset all 256 descriptors to canonical absent, and clear all seven descriptor shadow words; leave selection unchanged |
| `$0005` | ENTRY_COMMIT | Validate the entry shadow under map 0, atomically install the complete tuple, and set entry_configured=1 |
| `$0006` | ENTRY_LOAD | Copy the complete active entry tuple into its shadow, including zeros before configuration |
| `$0007` | PREPARE_USER_RETURN | Validate and mark exactly one eligible depth-1 K-origin frame for U return, as specified below |
| `$0008` | BACKING_QUERY | Latch installed backing kind and contiguous run length at the shadow physical address; set backing_valid=1 |

Map 0 cannot be published or invalidated by command; either operation raises
Invalid State. Its descriptors can be edited with DESC_COMMIT, subject to the
no-U-rights rule. Map 0 resets to the boot identity translation described above,
with frame=selector, lower=0, upper_lo=0, upper_hi=1, and sb=st=0. For a physical
frame containing any installed MMIO byte, flags=`$00C3` (present, MMIO, K R/W);
otherwise flags=`$01C1` (present, RAM, K R/W/X), including all-hole frames. A
mixed RAM/MMIO frame initially permits only its MMIO bytes through that boot
descriptor; the kernel can install a RAM descriptor/alias for its RAM region.
Actual backing kind always remains authoritative. The host must provide reset
vector bytes and target readable/executable under this deterministic boot map.
Committed descriptor readback reflects this representation exactly. All
other maps and their descriptors reset unpublished/absent. Hardware may use
sparse storage, but readback and invalidation must have these exact effects.

MAP_PUBLISH exposes only already committed descriptors. It has no hidden shadow
flush, entry-point check, requirement for a nonempty map, or user-state mutation.
Descriptor validity was checked at each DESC_COMMIT; an empty map can be published
without granting rights. Invalidation works identically on an unpublished map,
including clearing its staging. Republish alone therefore cannot revive old
grants. The kernel explicitly constructs fresh descriptors and initializes
private backing before publishing a new owner's grants.

ENTRY_COMMIT first checks `0 <= SB <= SP <= ST <= $FFFF` and that a next-push slot
fits (`SP >= SB+2`), reporting Stack Boundary on failure. It then validates DS
and SS in that order under map 0. DS must be present, K-data-capable (R or W),
and RAM; failure is selector-only Memory Access with KERNEL/operand origin/phase,
access NONE, width 0, no relevant address, and NOT_PRESENT, RIGHTS, or DEVICE.
For SS, probe each byte of `[SP-2,ST)` in increasing order for K READ then K WRITE,
using ordinary translation/bounds/kind/backing checks with width 1. No RAM byte
is actually read or written. These probes cover the complete allocated interval
and next push without requiring unallocated bytes below SP-2 to be installed.
Each failing probe reports its own address/access/reason under KERNEL/operand.
SS must be RAM, but the K stack does not require U STACK capability. Successful
commit does not change the live stack or DS. Future protected entry revalidates
the committed tuple; entry_configured does not cache mapping/backing validation.

PREPARE_USER_RETURN requires live K mode, I=0, depth exactly one, a K-origin top
frame with its bootstrap marker clear, and entry_configured=1. After privilege,
those preconditions are checked in the listed order; failure is Invalid State.
It validates the complete saved image as a U return using the existing ordered
IRET contract, then revalidates the active entry tuple using ENTRY_COMMIT's rules.
It does not probe any particular future event vector. Only after all checks and
the following-PC check succeed does it set the one-way bootstrap marker. It
does not pop the frame, start U execution, change saved fields, or rewrite live
registers. Repeating it on a marked frame is Invalid State. IRET still validates
the saved U image when executed; marking does not authorize later invalid edits.

BACKING_QUERY uses a canonical 24-bit physical address and does not translate a
guest selector. The result describes installed backing at that address (HOLE,
RAM, or MMIO), plus the positive count of consecutive bytes of the same kind up
to the next classification boundary or physical limit `$1000000`. The count is
32 bits and may be `$01000000` for a uniform complete physical space. No callback
or device read occurs, including for MMIO. Address staging does not invalidate
or change a prior result; the next successful query replaces the whole latch.
Classification/backing is immutable between resets, so separately read result
words cannot tear unless another command replaces the latch. These queries and
committed descriptor readback expose all backing facts needed for software
return-image prevalidation, without a host-only debugger escape.

#### Shared staging and software ownership

Descriptor selection/shadow, entry shadow, and backing query address/latch are
CPU-wide configuration scratch state. They are not automatically saved/restored
in exception frames. Exception entry itself never changes them. A nested handler
that uses writable selection/shadow words must save and restore them explicitly
under the kernel convention; IRQ masking alone does not exclude synchronous
faults. Query result latches are caller-clobbered: perform query plus result reads
with I=0 in pinned K code, and restart the entire query if a synchronous fault
interrupts that sequence. A handler must not resume a partially read result after
replacing the latch. If an outer operation
had partially staged a descriptor, resuming it must restore both its selectors
and every shadow word, not reload the active descriptor and discard its edits.
Active tuples/descriptors change only through the commands above or reset.

GETC is individually atomic, not a snapshot of several controls. The kernel must
hold its mapping/lifetime locks across related readbacks and range copies. Numeric
map-ID reuse does not validate old handles: generation counters, leases/refcounts,
private-frame initialization, scheduler prevalidation, and replacement rollback
remain software ABI responsibilities. No command provides a transaction spanning
several instructions, implicit TLB, hardware generation, or automatic scheduler.

#### Protected control setup examples

These examples assemble in working mode. Use valid pinned K code, vectors,
and recovery state. To
construct a full-range U R/W data descriptor for map 7, selector 3, frame `$23`:

```asm
MOV.W #7,r0
SETC r0,desc_map
MOV.W #3,r0
SETC r0,desc_segment
MOV.W #$23,r0
SETC r0,desc_frame
MOV.W #$0019,r0     ; PRESENT + U R/W, RAM, no stack capability
SETC r0,desc_flags
MOV.W #0,r0
SETC r0,desc_lower
SETC r0,desc_upper_lo
SETC r0,desc_sb
SETC r0,desc_st
MOV.W #1,r0
SETC r0,desc_upper_hi
SETC r0,command     ; DESC_COMMIT = 1
GETC active_desc_flags,r1  ; $0019
MOV.W #3,r0
SETC r0,command     ; MAP_PUBLISH = 3; publishes only committed descriptors
```

This does not enable U execution. Empty maps and a map with only data are legal
to publish; preparing/returning a U image still requires code, data, and stack
descriptors. To configure entry with map-0 DS=SS=0, SB=`$1000`, SP=ST=`$2000`,
first ensure the required kernel RAM and vector mappings are installed:

```asm
MOV.W #0,r0
SETC r0,entry_ds
SETC r0,entry_ss
MOV.W #$1000,r0
SETC r0,entry_sb
MOV.W #$2000,r0
SETC r0,entry_st
SETC r0,entry_sp
MOV.W #5,r0
SETC r0,command     ; ENTRY_COMMIT
GETC entry_configured,r1  ; 1; live DS/SS/SP/SB/ST unchanged
```

For a first U return, install/publish map 7's code selector 1 (U X RAM), data
selector 3 (U R or W RAM), and stack selector 4 (U R/W STACK RAM with policy
SB=`$1000`, ST=`$2000`). Install code at 1:`$0100`, keep live user-access controls
zero, and set live process_map=7. Execute SWI from K to create a depth-1 K-origin
frame; its saved process_map is then 7 and saved user-access controls are zero.
In the handler, with I=0 and valid committed entry state, existing assigned SETX
selectors suffice to finish this initial image:

```asm
MOV.W #$0100,r0
SETX r0,xpc
MOV.W #1,r0
SETX r0,xcs
MOV.W #3,r0
SETX r0,xds
MOV.W #4,r0
SETX r0,xss
MOV.W #$1000,r0
SETX r0,xsb
MOV.W #$2000,r0
SETX r0,xst
SETX r0,xsp
MOV.W #$0020,r0     ; saved U I=1; N/Z/C/V/E/reserved bits zero
SETX r0,xcsr
MOV.W #7,r0
SETC r0,command    ; PREPARE_USER_RETURN, not a mode write
IRET
```

Successful prepare preserves the frame and marks its U destination; IRET restores
the full validated image and enters map 7, CS=1, PC=`$0100`. These steps use no
new saved-context selector or inferred numeric mode field. Subsequent scheduler
replacement of saved process_map/user-access controls uses the assigned extended
context interface. An unpublished map, mismatched U stack policy, or missing code
target rejects preparation before the marker changes.

## 7. Interrupt controller, time and devices

This chapter assigns the deterministic PIC/timer interface and the minimum
machine profile `minimal-v1`. These are machine/device contracts accessed through
ordinary MOV, not new CPU-control selectors or primary opcodes. Console, storage,
and other peripheral register layouts remain separate work.

### Interrupts and deterministic time

Use 16 sources with sticky PENDING, ENABLE and IN_SERVICE bitmaps. K reads status,
writes enable, and acknowledges each source with a write-one bit. A host/device
assertion sets pending; repeated edges coalesce. At a boundary, eligibility is
`pending & enable & ~in_service`, I=1, and not terminal. Choose the
lowest source index. Entry records source in the frame; only successful entry
clears its pending bit and sets its in-service bit. Failed entry leaves it pending.
Acknowledgment clears in-service only, not a later pending assertion. A level
source reasserts at the next boundary if still active. Document devices as edge
or level sources. Timer source 0 is fixed; the minimum profile below assigns
sources 1/2 and reserves the remaining slots for future peripheral contracts.

No programmable priority or priority ceiling initially. A handler must acknowledge
or mask its source before enabling reentry of the same source. Other sources can
nest when I=1. Restoring I with IRET is separate from device acknowledgment.
PIC writes and newly asserted requests take effect at defined boundaries; events
at the same boundary ordinal are batched before source arbitration. Acknowledgment
and a new edge at the same boundary preserve the new pending edge. Source numbers
are not vectors: a shared IRQ vector plus saved source is sufficient.

A 64-bit virtual instruction clock advances by one for every committed guest
instruction until saturation, including SWI, IRET and HALT. Explicit recorded
idle advancement below is its only other time-advance mechanism, so its value
is not an absolute retirement count. Faulting instructions, hardware entry,
halted polls, terminal states and host reset processing do not advance it. Fault
handlers' committed instructions do count. SWI retires once only when its live
handler-entry bundle commits; failed fetch/state/entry and capacity overflow do
not retire it. IRET retires only on successful restore. Diagnostic frame capture
is not instruction retirement. Counter reset is host-reset-defined
zero; it is per CPU, not process-local. Read atomically via a K snapshot/latch
rather than four independently sampled words. Configuration uses 16-bit shadow-register writes followed by a validated commit
command; only the complete deadline/period bundle becomes active. Failed commit
leaves the old active configuration intact. Counter reads use a snapshot command
and latched 16-bit halves, preventing torn values.

A K one-shot/periodic timer holds next deadline and optional positive period.
After instruction commit, increment time, enqueue all newly due sources, then
arbitrate before the following fetch. Periodic overruns coalesce one pending bit
and advance deadline to the first future period without drift. Deadline arithmetic
is checked; at counter exhaustion disable the timer and expose overflow status
instead of silently wrapping. Configure deadline > current time. Host events are
injected at explicit boundary numbers and recorded for replay. Masked expiry stays
pending. SWI/fault synchronous entry takes priority over newly eligible requests;
entry clears I and leaves the pending bit for later delivery.

Retired-instruction time is virtual execution time, not elapsed real time. A
future wall-clock/real-time service must be specified separately and its inputs
recorded for replay; it cannot change deterministic scheduling semantics.

An instruction-clock timer does not progress while HALTed. A deterministic host
`advance_idle_to(deadline)` event may advance virtual time to wake an idle CPU;
record it in replay input. Alternatively the kernel runs an idle loop. Do not
silently tie HALT wakeup or scheduling to wall time. Timer-driven process switching
works for running processes without idle advancement. Guest code cannot suppress
preemption with DI/HALT or a fault-free infinite loop in U mode.

### Minimum machine MMIO profile

`minimal-v1` reserves physical frame `$FF` for devices. Physical bytes
`$FF0000–$FF00FF` are installed MMIO; the rest of that frame is uninstalled
backing in this profile, never RAM. Other frames can contain RAM or holes under
the boot backing map. Reset's identity kernel descriptor therefore maps selector
`$FF` to frame `$FF` with flags `$00C3` (present, MMIO, K R/W, no U/X/STACK).
Kernel software may install a different K-only MMIO alias/bounds; register offsets
below are physical offsets within the device frame and do not move with an alias.
No guest register receives a magic port address outside normal translation.

All PIC/timer registers accept **one aligned 16-bit word transaction** only.
Byte accesses, odd starts, reserved offsets, wrong read/write directions, and
invalid write values fail pure device preflight with Memory Access/DEVICE when
prior translation/backing checks succeed, with
the requested address/width/access recorded. Every byte of the 256-byte window
is classified MMIO, including reserved offsets; these are DEVICE failures, not
missing backing. Word transfers outside the installed window still follow normal
translation/backing priority. No transaction wraps, crosses register/device
boundaries, or becomes two byte callbacks.

CPU rights, kind, and backing checks precede pure device preflight; operand
preflight precedes selected next-PC validation, as for other data transfers.
All these checks precede an infallible device commit. A rejected transaction changes no device register, latch, shadow,
or configuration; it does not retire. Independent later boundary events are not
effects of that rejected instruction. Ordinary U MOV is denied by U memory
rights, not a new privileged-opcode class; U MOVU is Privilege/USER_ACCESS before
operand checks, and K MOVU cannot reach MMIO. PIC/timer have no GETC/SETC mirror.

#### PIC registers and source assignments

| Frame offset | Register | Access | Host-reset value / meaning |
|---|---|---|---|
| `$0000` | PIC_PENDING | R | 0; sticky pending bitmap |
| `$0002` | PIC_ENABLE | R/W | 0; all sixteen source-enable bits |
| `$0004` | PIC_IN_SERVICE | R | 0; accepted-but-unacknowledged bitmap |
| `$0006` | PIC_ACK | W | No stored/readable value; write-one clears only corresponding IN_SERVICE bits |
| `$0008` | PIC_LEVEL | R | 0; currently asserted external level bitmap, bit 2 only in this profile |
| `$000A` | PIC_SOURCE_MASK | R | `$0007`; connected source bits 0–2 |

Each bitmap bit is a source index, not a vector or priority field. All sixteen
ENABLE/ACK bits are defined. Enabling an unconnected slot is legal storage but
cannot generate a request; ACK of a source not in-service is a no-op. Writes do
not clear PENDING. All PIC reads are side-effect-free, not read-to-clear; ACK
does not change ENABLE or deassert the device/host level. IRET acknowledges no
source. Only successful IRQ entry clears its selected pending bit and sets its
in-service bit; a later assertion remains pending even while in-service.

| Source | Profile assignment | Assertion behavior |
|---|---|---|
| 0 | TIMER | Internal edge per due timer event; host cannot inject source 0 |
| 1 | HOST_EDGE | External test/input edge; repeated edges coalesce |
| 2 | HOST_LEVEL | External level; assertion sets pending, deassertion does not clear a latched request |
| 3–15 | Unconnected/reserved peripheral slots | No assertions accepted/generated in this profile |

At a boundary, an asserted level reasserts its pending bit even if it is already
in-service. This can leave a final latched request after deassertion; software
must tolerate it. `eligible = PENDING & ENABLE & ~IN_SERVICE`, gated by live I=1
and nonterminal state. Lowest eligible source wins, always through shared IRQ
vector 5, with its index in xirq_source. Device acknowledgment and source masking
remain independent. No programmable priority, source remapping, or guest pending
injection register is supplied. Future machine profiles can connect other slots
only with an explicit source/edge-or-level contract and SOURCE_MASK update.

#### Timer register layout

Four-word values use little-endian **word** order: word 0 is bits 15–0, word 3
bits 63–48. Each listed offset is one complete two-byte register. Ranges below
enumerate offsets in steps of two; no byte or partial-word access is supported.

| Frame offset(s) | Register | Access | Host-reset value / meaning |
|---|---|---|---|
| `$0020` | TIMER_COMMAND | W | Exact command value below; not readable/stored |
| `$0022` | TIMER_STATUS | R | 0; bits 0 snapshot-valid, 1 time-exhausted, 2 deadline-overflow; other bits zero |
| `$0024` | TIMER_MODE | R/W | Shadow mode, reset 0; 0=disabled, 1=one-shot, 3=periodic |
| `$0026` | TIMER_ACTIVE_MODE | R | 0; active mode 0/1/3, independent of shadow |
| `$0030,$0032,$0034,$0036` | TIMER_DEADLINE_0..3 | R/W | Deadline shadow words, reset 0 |
| `$0038,$003A,$003C,$003E` | TIMER_PERIOD_0..3 | R/W | Period shadow words, reset 0 |
| `$0040,$0042,$0044,$0046` | TIMER_TIME_0..3 | R | Snapshot latch words, reset 0; never live torn counter halves |
| `$0048,$004A,$004C,$004E` | TIMER_ACTIVE_DEADLINE_0..3 | R | Active next/last deadline words, reset 0 |
| `$0050,$0052,$0054,$0056` | TIMER_ACTIVE_PERIOD_0..3 | R | Active/last period words, reset 0 |

Every other offset in `$0000–$00FF` is reserved. Shadow words accept arbitrary
16-bit data; TIMER_MODE accepts only exact 0/1/3 (all other values, including 2,
are DEVICE errors). Shadow writes have no immediate scheduling effect. Active
readback never substitutes shadow fields. Time/status/PIC reads return state at
their transaction commit, before that instruction's retirement clock tick.

| Command value | Name | Atomic successful effect |
|---|---|---|
| `$0001` | SNAPSHOT_TIME | Copy current 64-bit time into all four TIMER_TIME words; set snapshot-valid |
| `$0002` | COMMIT_TIMER | Validate and atomically install the entire shadow mode/deadline/period bundle |
| `$0003` | DISABLE_TIMER | Set active mode 0; retain active deadline/period readback and all shadows |
| `$0004` | CLEAR_DEADLINE_OVERFLOW | Clear only status bit 2; do not enable the timer or change time/PIC state |

Other commands fail DEVICE without any side effect. COMMIT_TIMER checks shadow
mode, then the following bundle rules in order: disabled mode requires deadline
and period both zero; enabled modes require deadline > current time; one-shot
requires period=0; periodic requires period>0. Values and comparisons are unsigned
64-bit. A failed commit leaves the old active bundle, shadow bundle, snapshot,
and status unchanged. Do not report failure by modifying a status/error latch
after consuming the transaction. The CPU reports the preflight DEVICE fault.

Successful COMMIT_TIMER leaves the shadow unchanged and does not clear pending,
in-service, or overflow status. It validates against time **before its own
retirement**; deadline=current+1 is legal and can fire on that instruction's
clock tick. DISABLE_TIMER does not withdraw a pending/in-service IRQ. Disabled
active mode may retain a past deadline/period for readback; only an explicit
disabled COMMIT_TIMER installs the canonical all-zero active bundle.

SNAPSHOT_TIME captures time before its own retirement tick. Subsequent reads
retire normally but read that same latch until another snapshot replaces it;
reading any word does not advance/unlock the snapshot. Timer shadow, snapshot,
and active state are per-CPU, not process-local or automatically frame-restored.
Use I=0, pinned code, and the kernel ownership convention across snapshot reads
and staged commits; restart a snapshot sequence if a fault-handler snapshot
clobbers it. Preserve partially staged words when nested code shares the device.

#### Timer deadlines, overflow, and boundary order

After a successful guest instruction, increment time by one without wrapping,
then evaluate due timer work. One-shot expiry sets pending source 0 and disables
the active timer, retaining its last deadline. Periodic expiry sets pending once
and calculates `k = floor((time-deadline)/period)+1` in a wider type; the next
deadline is `deadline + k*period`, the first strictly future period. It does not
drift to `time+period` and does not enqueue a counter of missed IRQs.

If the next periodic deadline cannot fit in 64 bits, set deadline-overflow,
disable the timer, retain the last active deadline/period, and keep the due
pending bit. At time `$FFFFFFFFFFFFFFFF`, evaluate any event due at that value,
then disable the timer and expose time-exhausted. Time saturates at this maximum;
later committed instructions still execute/retire but do not wrap time. Overflow
alone generates no extra IRQ; due work still generates its one request. Status
bit 1 is exactly `time==maximum` and cannot be cleared by a guest write; bit 2
is sticky until command 4 or reset. This also defines overflow after idle jumps.

At the following CPU boundary, apply recorded external events in sequence,
resample asserted levels, then arbitrate before any instruction fetch. All events
at that boundary are batched before arbitration. A just-completed ACK cleared
only in-service, so any due timer/new edge remains pending. Successful SWI or a
synchronous fault completes its entry before the next boundary and clears live I;
new requests do not replace that synchronous event. Hardware IRQ entry, faults,
and terminal transitions do not increment time; successful IRET, SWI, HALT, and
ordinary MMIO instructions each retire once. A failed SWI entry does not retire.

### Deterministic host events and replay

The source/event interface below is a required machine contract, not an extension
of the legacy source-less host IRQ API. Working mode implements it explicitly.
All calls run on the CPU thread between steps; asynchronous producers queue input
to that thread. No source may mutate PIC/timer/backing during an instruction.

A per-run **boundary ordinal B** starts at zero after reset. Each completed CPU
transition increments B once: a successful guest instruction (including SWI or
IRET), synchronous-fault transition including its entry, or hardware IRQ entry.
An entry failure/overflow leading to terminal state is one such transition, not
an extra instruction. HALT/terminal polls that execute no transition change
neither B nor time. B is an unsigned arbitrary-precision ordinal and never wraps;
it is distinct from the 64-bit virtual time, which also admits explicit idle jumps.

`apply_boundary_events(records)` validates an entire ordered batch against
staged host state, then applies it infallibly, with no IRQ arbitration inside the
batch. Each record must name the current run and B; the replay driver holds
future-boundary inputs until that B is reached. Multiple batches at an unchanged
halted boundary are allowed with consecutive sequence numbers. `step` resamples
levels/arbitrates after all input delivered for that boundary. Applying events
does not itself execute a CPU transition or increment B. An empty batch is a
no-op. Polls with no CPU transition need not be recorded.

#### Replay format and validation

The canonical interchange is UTF-8 JSON Lines. A run header has exactly:
`format="vcpu-replay"`, `version=1`, `profile="minimal-v1"`, `run`,
`ram_image_sha256`, and `backing_map_sha256`. Each digest is 64 lowercase hex
characters and binds an external raw artifact of exactly 16,777,216 bytes in
physical-address order. The backing artifact uses byte tags HOLE=0, RAM=1,
MMIO=2 with this profile's fixed device window; non-RAM positions in the RAM
image are zero, and installed RAM positions contain the initial loader bytes.
These artifacts determine reset/boot state, not a host-dependent serialization
of CPU objects. Verify artifacts/profile before execution. Non-recorded debugger
mutation is outside replay conformance.

Every event record has exactly common fields `run`, `boundary`, `sequence`,
`kind`, plus the kind-specific fields below. Run, boundary, and sequence use
canonical nonnegative decimal strings (`0` or a nonzero digit followed by digits).
Sequence starts at `"0"` and advances by one per event within a run, independent
of B; run IDs start at `"0"` and increment on reset. Source is a JSON integer,
asserted is a JSON boolean, and target_time is a canonical nonnegative decimal
string whose accepted range is 0..18446744073709551615. Missing/extra/duplicate keys and incorrect types are
errors, not defaults or truncation.

| kind | Additional fields | Meaning |
|---|---|---|
| `irq_edge` | `source=1` | Set source 1 pending; repeated edges coalesce |
| `irq_level` | `source=2`, `asserted=true/false` | Update level 2; true sets pending, false does not clear pending |
| `idle_advance` | `target_time` | Explicit virtual-time jump under the rules below |
| `reset` | None | Host reset; must be the only record in its batch |

For example, `{"run":"0","boundary":"23","sequence":"0","kind":"irq_edge","source":1}`
asserts source 1 at B=23. Source 0 cannot be injected externally; sources 3–15
are unconnected in this profile. If a level asserts and deasserts at the same B,
records execute in sequence and the assertion's sticky pending bit survives.
After all records, resample the final levels and arbitrate lowest source.

Host-input errors are exact, separate from guest exception vectors:
BAD_FORMAT=1, WRONG_RUN_OR_BOUNDARY=2, BAD_SEQUENCE=3, BAD_SOURCE=4,
BAD_STATE=5, BAD_TIME=6, BAD_PROFILE=7. Check header/profile/artifacts before
events; check each event's format, run/B, sequence, source, state, and time in
that order. Rejected batches consume no sequence and mutate no CPU/device/time
state. A terminal CPU accepts only reset. Hardware/guest events remain subject
to their ordinary fault/terminal semantics after valid input has been applied.

The interface returns 0 on success or the first error code, without raising a
guest exception. BAD_FORMAT covers JSON/schema/type/canonical-number errors;
BAD_PROFILE covers unsupported header version/profile, artifact length/tag
violations, and digest mismatches after valid header parsing. A reset mixed with
another record is BAD_STATE. Syntactically canonical target_time outside the
64-bit range is BAD_TIME; malformed decimal text is BAD_FORMAT. Validate each
record against earlier staged records, in record order; rollback the entire
batch on the first error. Reset is logged under the ending run's B/sequence;
the next run header precedes any new-run events. A replay driver must finish
all records at a boundary before stepping, including further batches at that B.

#### Idle advancement and reset

`advance_idle_to(target)` submits one idle_advance event at current B/sequence.
Require Halted, nonterminal state, no already eligible IRQ, and current time <
target <= maximum. It may not pass an active timer deadline that would wake the
CPU (I=1, source 0 enabled and not in-service); request that deadline first.
Reject invalid state with BAD_STATE or an invalid/over-wake target with BAD_TIME.
Masked/in-service timer periods may be crossed: evaluate due work at the new
time, coalesce pending, and advance the periodic deadline to the first future
period with checked arithmetic. This host event is the only non-retirement time
advance, does not retire instructions/increment B, and is always recorded.
The following step accepts an eligible IRQ normally; I=0 or masking can leave
the CPU halted with pending work. No wall-clock sleep/poll changes virtual time.

Reset ends the current run, discards queued future inputs, and begins the next
run at B=0/time=0/sequence=0 with PIC pending/enable/in-service/levels zero and
timer active/shadow/latches/status zero. RAM survives; emit/verify the next run's
header against that preserved RAM and its reset-time backing map. Reset clears
frames/terminal diagnostics and uses the normal reset vector/boot-map checks;
bad reset yields TerminalResetFault in the new run. Unconnected lines and the
timer never inherit assertions from the previous run.

### Timer setup and acknowledgment example

This is assigned-specification source, not current executable device support.
Assume valid pinned K code, I=0, usable protected vectors/entry, time <=127,
no pending/in-service sources, deasserted host lines, and no external input
during setup. The timer is initially disabled.
The following sets a one-shot deadline 256 and enables timer source 0. It saves
the caller's live DS in r3; protected entry DS remains independently configured.

```asm
MOV ds,r3
MOV.W #$00FF,r0
MOV r0,ds
MOV.W #0,r0
MOV.W r0,[$0032]
MOV.W r0,[$0034]
MOV.W r0,[$0036]
MOV.W r0,[$0038]
MOV.W r0,[$003A]
MOV.W r0,[$003C]
MOV.W r0,[$003E]
MOV.W #$0100,r0
MOV.W r0,[$0030]
MOV.W #1,r0
MOV.W r0,[$0024]
MOV.W #2,r0
MOV.W r0,[$0020]
MOV.W #1,r0
MOV.W r0,[$0002]
MOV r3,ds
EI
HALT
after_idle:
NOP
```

The commit occurs before time 256. HALT stops ordinary ticks; the host records
idle_advance to 256 at the halted B. This sets pending bit 0 without retirement;
the next step enters vector 5 with xirq_source=0. After the kernel dispatches
that source, its timer-handler acknowledgment path is:

```asm
MOV.W #$00FF,r0
MOV r0,ds
MOV.W #1,r0
MOV.W r0,[$0006]
IRET
```

The one-shot timer is already disabled; ACK clears only in-service bit 0. IRET
restores interrupted DS/flags and the following HALT PC at after_idle. A periodic
timer needs positive period words and mode 3; acknowledgment and rearming are
still separate actions. Snapshot/configuration and replay boundary cases are
required by [PIC/timer conformance](ISA.md#conformance-pic-timer-and-replay).

### Device architecture and ordering

Devices use protected MMIO. Reserve kernel map
descriptors for device windows; never grant U rights or user-access mappings to
them. Ordinary MOV can access permitted MMIO. PIC/timer layouts are assigned
above; console, storage, and other input layouts remain separate contracts. Devices expose
source IDs and status; kernel software translates OS calls to those registers.

All instruction fetches, vector targets, stacks, and execution must be RAM, never
MMIO. MMIO data instructions perform exactly one byte or word transaction; a
word is not two separate byte callbacks. Alignment/width/register permissions and
all CPU checks, including next/target checks, are completed before any irreversible
read or write. A transfer spanning devices or RAM/device boundaries faults before
side effects. No instruction has two device transactions in this specification.

A device has pure preflight followed by an infallible read/write commit. A failed
preflight has no effect; an implementation unable to guarantee commit success must
use RAM-backed queues or reject the operation. CPU register update and device
commit are one uninterruptible instruction. A load into its pointer base still
samples the old address. Future post-increment prechecks its updated pointer too.
Device events/results occur between instructions, after submitted commands in
program order, with no DMA initially. Storage uses explicit RAM buffers copied
by kernel code; asynchronous completion is an interrupt/status event. Record host
inputs and storage/console responses for replay. No host errno becomes an
unstructured CPU fault after a device has already consumed input.

## 8. Instruction reference

### Instruction index

| Function | Mnemonics |
|---|---|
| Arithmetic | [ADD, ADC](#add-adc--add-words); [SUB, SBC](#sbc-sub--subtract-words); [CMP](#cmp--compare-words) |
| Immediate ALU | [ADDI, SUBI, CMPI, ANDI, ORI, XORI](#addi-subi-cmpi-andi-ori-xori--immediate-arithmetic-and-logic) |
| Logic and shifts | [AND, OR, XOR](#and-or-xor--logical-word-operation); [complement](#mov-complement--complement-a-word); [SAR, SHL, SHR](#sar-shl-shr--shift-a-word-by-one-bit) |
| Immediate shifts and rotates | [SHLI, SHRI, SARI, ROLI, RORI](#immediate-count-shifts-and-rotates) |
| Tests | [TST, TSTI](#tst-tsti--test-a-register-without-writing-it) |
| Data and stack | [MOV](#mov--transfer-a-word-or-byte); [MOVU](#movu--transfer-under-user-permissions); [ADDP](#addp--adjust-a-checked-pointer); [ADJSP](#adjsp--adjust-the-stack-allocation); [POP, PUSH](#pop-push--transfer-a-word-through-the-stack) |
| Control flow | [Bcc, BRA](#bcc-bra--conditional-or-unconditional-relative-branch); [JS](#js--jump-within-the-code-segment); [JL](#jl--jump-to-a-segment-and-offset); [CALL](#call--call-within-the-code-segment); [RET](#ret--return-from-a-near-call); [CALLF](#callf--call-a-segment-and-offset); [RETF](#retf--return-from-a-far-call) |
| Exception and system | [SWI](#swi--software-exception-entry); [GETC, SETC](#getc-setc--access-protected-cpu-controls); [GETX](#getx--read-the-active-exception-frame); [SETX](#setx--edit-the-active-saved-image); [IRET](#iret--return-from-exception); [DI, EI](#di-ei--control-maskable-interrupt-acceptance); [HALT](#halt--suspend-instruction-fetch); [NOP](#nop--no-operation) |

### Reading an instruction entry

Entries are ordered by mnemonic or mnemonic family. Syntax uses source before
destination. **Operation** describes successful execution; **Condition codes**
lists every flag effect; **Exceptions** identifies instruction-specific checks;
**Encoding** identifies the assigned bytes. Chapter 9 gives bit-field layouts.

All entries use the [byte and word conventions](#byte-and-word-operations).
An entry's result-write and flag rules determine whether the selected-width
result is stored or used only to set condition codes.
The [register ALU width and assembly-selection rules](#register-alu-width-and-assembly-selection)
require rejection of unsupported byte forms and preservation of explicit or
default immediate operation width.

Every instruction can fault during fetch. Reserved primary/secondary bytes and
reserved operand fields raise Illegal Instruction as soon as the invalid form is
known; missing bytes of otherwise legal instructions raise Memory Access. Common
fault precedence, next-PC checks and atomicity apply even when an entry does not
repeat them. The original CS supplies the entire instruction. A rejected operation
commits no ordinary result; exception entry then follows the protected-frame rules.

Arithmetic,
ordinary transfers, branches, calls and pointer/stack adjustment are usable in
U and K, subject to descriptor rights. DI, EI, HALT, GETC, SETC, GETX, SETX, and
IRET and MOVU are K-only. MOV restrictions depend on the internal destination. SWI is available in both
modes and performs controlled entry. The [privilege table](#privilege-and-process-local-state)
is authoritative for state-dependent access.

### ADD, ADC — Add words

**Operands:** rs and rd are B0 registers.

```asm
ADD rs,rd
ADC rs,rd
```

**Operation:** ADD stores `(old_rd + old_rs) mod 65536`. ADC additionally adds the input C. Both operands are sampled before any write. Wide arithmetic and exact flag equations are defined under [CSR and arithmetic](#csr-and-arithmetic).

**Condition codes:** Set N/Z/C/V from the 16-bit calculation; preserve I/E.

**Exceptions:** No operand-state exception; sequential-PC validation can raise Memory Access. All entries also obey the common fetch, decode and commit rules.

**Encoding:** Two bytes, `$10` plus the operation/source/destination byte; see [ALU and bank transfers](#10-alu-and-bank-transfers).

### ADDI, SUBI, CMPI, ANDI, ORI, XORI — Immediate arithmetic and logic

**Operands:** rd is B0; immediate width is explicit or defaults to word.

```asm
ADDI.B #imm8,rd    ; likewise SUBI, CMPI, ANDI, ORI, XORI
ADDI.W #imm16,rd   ; unsuffixed mnemonic defaults to .W
```

**Operation:** Apply the named operation at the selected width. Byte writes zero-extend into the entire destination; CMPI never writes the destination. The complete width, literal and flag contract is under [Immediate ALU instructions](#immediate-alu-instructions). A word operation must not be shortened to a byte operation merely because its constant fits.

**Condition codes:** ADDI/SUBI/CMPI set N/Z/C/V; ANDI/ORI/XORI set N/Z and preserve C/V. All preserve I/E.

**Exceptions:** No operand-state exception; sequential-PC validation can raise Memory Access. All entries also obey the common fetch, decode and commit rules.

**Encoding:** Primary `$80–$AF`, with destination in the low two bits; `.B` is two bytes and `.W` three. See the assigned immediate table.

### ADDP — Adjust a checked pointer

**Operands:** rp is r4–r7; simm8 is -128..127.

```asm
ADDP #simm8,rp
ADDP #simm8,dp
```

**Operation:** Compute `unsigned(old_destination) + s8(simm8)` without narrowing. Require the sum in 0..65535, then write it. No memory, segment selection or backing probe occurs. INCP/DECP are aliases for constants +1/-1.

**Condition codes:** Preserve all flags.

**Exceptions:** An out-of-range result raises Invalid State before any write. All entries also obey the common fetch, decode and commit rules.

**Encoding:** Two bytes: `$20–$23` for r4–r7 or `$24` for DP, then simm8; see [pointer adjustment](#2024-two-byte-pointer-adjustment).

### ADJSP — Adjust the stack allocation

**Operands:** simm8 is -128..127.

```asm
ADJSP #simm8
```

**Operation:** Require the initial stack invariant, compute `new_SP = old_SP + s8(simm8)` without narrowing, and require `SB <= new_SP <= ST`. Commit SP without touching memory.

**Condition codes:** Preserve all flags.

**Exceptions:** Invalid initial configuration or rejected adjustment raises Stack Boundary. All entries also obey the common fetch, decode and commit rules.

**Encoding:** Two bytes: `$25 simm8`; see [checked stack adjustment](#25-checked-stack-adjustment).

### AND, OR, XOR — Logical word operation

**Operands:** rs and rd are B0 registers.

```asm
AND rs,rd
OR rs,rd
XOR rs,rd
```

**Operation:** Store the bitwise AND, OR or exclusive OR of the original source and destination.

**Condition codes:** Set N/Z from the 16-bit result; preserve C/V/I/E.

**Exceptions:** No operand-state exception; sequential-PC validation can raise Memory Access. All entries also obey the common fetch, decode and commit rules.

**Encoding:** Two bytes, `$10` plus the operation/source/destination byte; see [ALU and bank transfers](#10-alu-and-bank-transfers).

### TST, TSTI — Test a register without writing it

**Operands:** rd is r0–r3 only. Unsuffixed forms default to `.W`. Byte literals
accept -128..255; word literals accept -32768..65535, encoded in two's complement.
Reject other widths, registers, memory operands and out-of-range literals.

```asm
TST.B rd
TST.W rd
TSTI.B #imm8,rd
TSTI.W #imm16,rd
```

**Operation:** Sample old_rd. TST uses `temp = old_rd & width_mask`;
TSTI uses `temp = (old_rd & width_mask) & immediate`. Discard temp and preserve
all sixteen bits of rd, including its upper byte for `.B`. These operations
perform no operand memory access or MMIO transaction and imply no memory
read-modify-write atomicity. TST.W (also spelled TST) aliases `OR rd,rd`.

**Condition codes:** Set N from temp bit 7/15 and Z from temp==0; preserve C/V/I/E.
BEQ/BNE and BMI/BPL use the resulting Z/N normally. Preserved C/V are not fresh
comparison results: signed/unsigned comparison predicates after a test must not
be interpreted as if the instruction performed subtraction. Use CMP/CMPI for
comparisons.

**Exceptions:** Common fetch/decode and sequential-PC checks apply before flags
commit. No register/memory or flags change on an instruction fault, aside from
ordinary exception entry effects. Assigned forms are ordinary U/K operations;
they introduce no privilege class.

**Encoding:** TST.B is `$1A ($40+rd)`; TSTI.B is `$1A ($44+rd) imm8`;
TSTI.W is `$1A ($48+rd) imm_lo imm_hi`. TST.W emits the existing
`$10 ($20+5*rd)` OR-self encoding, disassembled as OR. See
[the review and measured sequences](ISA_EXAMPLES.md#test-instruction-review-and-measurements). The twelve new tags execute
in protected U/K when explicit `working` mode is selected.

### Bcc, BRA — Conditional or unconditional relative branch

**Operands:** Signed rel16 from the end of the selected instruction; no assigned rel8 form.

```asm
BEQ label    ; condition mnemonic from the branch table
BRA label
```

**Operation:** Evaluate the selected predicate without changing flags. If taken, set PC to `next + s16(rel16)` in the current CS; otherwise continue at next. Intermediate next may equal $10000 only for a taken branch with a valid target. All fifteen predicates are listed under [Branches and absolute jumps](#branches-and-absolute-jumps).

Predicates follow the [conditional branch flag interpretation rules](#conditional-branch-flag-interpretation)
regardless of whether the current flags came from a byte or word operation.

**Condition codes:** Preserve all flags.

**Exceptions:** Selected next/target outside its segment or an unavailable target raises Memory Access; target probes require X rights. All entries also obey the common fetch, decode and commit rules.

**Encoding:** Three bytes: `$40–$4E`, rel16 low byte, rel16 high byte. `$4F` is reserved.

### CALL — Call within the code segment

**Operands:** Unsigned 16-bit target offset.

```asm
CALL addr16
```

**Operation:** Require a representable next, a valid current-CS target and a valid two-byte push. Store next at `SS:(SP-2)`, decrement SP by two and set PC to addr16, committing together. CS is unchanged.

**Condition codes:** Preserve all flags.

**Exceptions:** Stack Boundary or Memory Access according to the common priority; a failed call writes no return word. All entries also obey the common fetch, decode and commit rules.

**Encoding:** Four bytes: `$1F $10 lo hi`.

### CALLF — Call a segment and offset

**Operands:** One packed 24-bit target in a four-byte immediate field, without
`#` or a suffix; source range 0..$FFFFFF.

```asm
CALLF abs32
```

**Operation:** Preflight the complete four-byte return frame in SS, require a
representable following PC, and validate target execute access in the current
effective map. Save next followed by the original canonical CS word at SP-4;
commit those bytes, SP-4 and target CS:PC together. Preserve mode, maps, other
registers and exception state. See the [exact ordered contract](#far-call-and-return-contract).

**Condition codes:** Preserve all flags.

**Exceptions:** Complete fetch before packed-address Invalid State; stack
bounds, both RAM words, following-PC range and target checks follow in order.
Any failure leaves the frame and ordinary state unchanged before fault entry.

**Encoding:** `$54 pc_lo pc_hi cs 00`, five bytes, working mode only in the
implementation. Existing near CALL remains `$1F $10 lo hi`.

### CMP — Compare words

**Operands:** rs and rd are B0 registers.

```asm
CMP rs,rd
```

**Operation:** Compute the flags for `old_rd - old_rs` without modifying either register.

**Condition codes:** Set N/Z/C/V, including no-borrow C; preserve I/E.

**Exceptions:** No operand-state exception; sequential-PC validation can raise Memory Access. All entries also obey the common fetch, decode and commit rules.

**Encoding:** Two bytes: `$1A 0000ssdd`.

### DI, EI — Control maskable interrupt acceptance

**Operands:** See syntax and operation.

```asm
DI
EI
```

**Operation:** DI clears I; EI sets I. The new policy takes effect when the instruction commits. CLI aliases DI; SEI aliases EI. Interrupt eligibility follows the source-controller rules.

**Condition codes:** Change I only; preserve N/Z/C/V/E.

**Exceptions:** In U mode, raise Privilege before effects. All entries also obey the common fetch, decode and commit rules.

**Encoding:** One byte: DI `$02`, EI `$03`.

### GETC, SETC — Access protected CPU controls

**Operands:** rn is any r0–r7; control is an assigned control selector with the
required read/write permission. These are word transfers with no size suffix.

```asm
GETC control,rn
SETC rn,control
```

**Operation:** GETC copies one control word to rn; SETC samples the original rn
and writes one control or executes one command. Selection, staging, validation,
publication, reset, and command semantics are normative under the
[protected control interface](#protected-control-interface).

**Condition codes:** Preserve N/Z/C/V/I/E, including commands. PREPARE_USER_RETURN
changes protected frame provenance only through its specified one-way action.

**Exceptions:** Unknown selectors, wrong access directions, and reserved tags
raise Illegal Instruction at decode. Legal U execution raises Privilege before
value/state checks. Invalid scalar values/commands raise Invalid State; entry and
bootstrap probes also use the specified Stack Boundary and Memory Access faults.
Common next-PC and atomic-commit rules apply to every transfer and command.

**Encoding:** `$1F ($A0+rn) selector8` for GETC and `$1F ($B0+rn) selector8` for
SETC; three bytes each. No assigned legacy context tag or selector is changed.

### GETX — Read the active exception frame

**Operands:** rn is r0–r7.

```asm
GETX context,rn
```

**Operation:** Copy the selected saved context word into rn. Only the top ordinary frame is addressable. Metadata words are readable.

**Condition codes:** Preserve all flags.

**Exceptions:** Inactive context raises Invalid State; U execution raises Privilege first. Unknown selectors or reserved register-byte bits raise Illegal Instruction. All entries also obey the common fetch, decode and commit rules.

**Encoding:** Legacy: `$1F ($20+index) 00000rrr`, index $00–$14. Extended:
`$1F ($80+rn) index8` for assigned extended readable indices. Both are three
bytes; see [extended context encoding](#extended-context-encoding).

### HALT — Suspend instruction fetch

**Operands:** See syntax and operation.

```asm
HALT
```

**Operation:** Commit the following PC and enter Halted. An eligible interrupt resumes execution by exception entry; host reset also exits Halted. A K handler with I=1 can be woken by an eligible interrupt; E alone does not prevent delivery.

**Condition codes:** Preserve all flags.

**Exceptions:** An unrepresentable next raises Memory Access; U execution raises Privilege first. All entries also obey the common fetch, decode and commit rules.

**Encoding:** One byte: `$01`.

### IRET — Return from exception

**Operands:** See syntax and operation.

```asm
IRET
```

**Operation:** Validate and atomically restore the active saved image. Do not pop the ordinary stack. Pop one frame and restore its protected return mode. Live handler values are discarded. See [exception return](#iret-and-nesting) and [complete return-image validation](#complete-return-image-validation).

**Condition codes:** Restore saved NZCVI; E is derived from remaining context depth.

**Exceptions:** Inactive or malformed saved state, invalid stack or invalid resume target follows the complete return-image validation order. An ordinary validation fault may nest. U execution raises Privilege. All entries also obey the common fetch, decode and commit rules.

**Encoding:** Two bytes: `$1F $12`.

### JL — Jump to a segment and offset

**Operands:** A packed 24-bit address in a 32-bit immediate field.

```asm
JL abs32
```

**Operation:** Require packed address bits 31–24 to be zero. Set CS from bits 23–16 and PC from bits 15–0 after target validation. The jump retains the current mode and uses its map and execute rights.

**Condition codes:** Preserve all flags.

**Exceptions:** Nonzero reserved high byte raises Invalid State; invalid target raises Memory Access. All entries also obey the common fetch, decode and commit rules.

**Encoding:** Five bytes: `$53 pc_lo pc_hi cs 00`.

### JS — Jump within the code segment

**Operands:** addr16 is unsigned; rp is r4–r7.

```asm
JS addr16
JS rp
```

**Operation:** Select the immediate offset or original B1 register value as target, validate its first executable byte and set PC. CS, SP and all other registers remain unchanged; no pointer word is read and no return address is pushed.

**Condition codes:** Preserve all flags.

**Exceptions:** Invalid target raises Memory Access. Target instruction decoding occurs on the subsequent fetch. All entries also obey the common fetch, decode and commit rules.

**Encoding:** Immediate: `$52 lo hi` (three bytes). Register: `$1F $60–$63` (two bytes).

### MOV — Transfer a word or byte

**Operands:** Register transfer pairs follow the bank table; memory/immediate value rn is r0–r7; ea uses a listed addressing mode.

```asm
MOV rs,rd
MOV.B #imm8,rn
MOV.W #imm16,rn
MOV.B ea,rn     ; or MOV.B rn,ea
MOV.W ea,rn     ; or MOV.W rn,ea
```

**Operation:** Copy a register, immediate or memory operand. Byte loads zero-extend; byte stores use source bits 7–0. Register transfers are word-sized and follow the legal bank combinations. Internal destinations apply their write rules. Unsuffixed immediate/memory forms default to word. Memory-to-memory transfers have no encoding.

**Condition codes:** Preserve all flags except an explicit CSR destination, which applies the CSR-write rule.

**Exceptions:** Illegal bank/form fields raise Illegal Instruction. Internal-state, stack, memory and selected-PC checks follow the common priority; privilege and rights checks apply. No partial memory write commits. All entries also obey the common fetch, decode and commit rules.

**Encoding:** Compact B0 moves use `0001ssdd` with distinct selectors; bank transfers use `$10`; immediate/memory forms use `$15`. See their encoding tables.

### MOV complement — Complement a word

**Operands:** rs and rd are B0 registers.

```asm
MOV ~rs,rd
```

**Operation:** Store `(~old_rs) & $FFFF` into rd. This is the complemented-source ALU form, distinct from an ordinary register transfer.

**Condition codes:** Set N/Z; preserve C/V/I/E.

**Exceptions:** No operand-state exception; sequential-PC validation can raise Memory Access. All entries also obey the common fetch, decode and commit rules.

**Encoding:** Two bytes: `$10 0011ssdd`.

### MOVU — Transfer under user permissions

**Operands:** One memory operand with B1 base r4–r7 and optional signed disp16;
one value register r0–r7. `.B`/`.W` selects width; no suffix defaults to word.

```asm
MOVU.B [r4],r0
MOVU.W r1,[r5-2]
```

**Operation:** Perform one U-permission RAM transfer under uaccess_map and
uaccess_segment while K code/stack remain in map 0. Byte loads zero-extend,
byte stores use the low byte, and words are little-endian and may be unaligned.
Base/source alias sampling, checked address formation, and no-pointer-update
rules are specified under [explicit user-access transfers](#explicit-user-access-transfers).

**Condition codes:** Preserve all flags.

**Exceptions:** Reserved descriptor/base bits raise Illegal Instruction before
later-byte fetch. Fully decoded U execution raises Privilege/USER_ACCESS before
operand checks. K data/next-PC failures use Memory Access with the specified
origin, phase, access, attempted offset, relevant address, and reason. No MMIO
callback or partial store is permitted. The common ordered fault rules apply.

**Encoding:** `$27 dazrrr00 000000pp [disp_lo disp_hi]`; three bytes without
displacement, five with displacement. See [user-access encoding](#user-access-encoding).

### NOP — No operation

**Operands:** See syntax and operation.

```asm
NOP
```

**Operation:** Commit the following PC without changing any other architectural state. The assembler pseudo `MOV rn,rn` for a general register emits NOP.

**Condition codes:** Preserve all flags.

**Exceptions:** An unrepresentable next raises Memory Access. All entries also obey the common fetch, decode and commit rules.

**Encoding:** One byte: `$00`.

### POP, PUSH — Transfer a word through the stack

**Operands:** rn is r0–r7; internal registers are excluded.

```asm
POP rn
PUSH rn
```

**Operation:** POP reads SS:SP, then adds two to SP. PUSH samples rn, subtracts two from SP and stores that word at the new SP. Apply the stack checks before effects; POP/PUSH are word-only. PULL aliases POP; .W aliases are accepted.

**Condition codes:** Preserve all flags.

**Exceptions:** Stack Boundary or Memory Access, with no partial transfer or pointer update. All entries also obey the common fetch, decode and commit rules.

**Encoding:** Two bytes: POP `$1F ($08+rn)`; PUSH `$1F rn`.

### RET — Return from a near call

**Operands:** See syntax and operation.

```asm
RET
```

**Operation:** Peek the word at SS:SP, validate it as a target in current CS, then add two to SP and set PC to that word atomically. CS is unchanged.

**Condition codes:** Preserve all flags.

**Exceptions:** Stack Boundary or Memory Access; a bad target prevents the pop. All entries also obey the common fetch, decode and commit rules.

**Encoding:** Two bytes: `$1F $11`.

### RETF — Return from a far call

**Operands:** None; no suffix.

```asm
RETF
```

**Operation:** Validate `[SP,SP+4)` and read its return-PC word then saved-CS
word without consuming them. Require saved CS<=255, then validate the saved
target in the current mode/map. Commit SP+4 and saved CS:PC together. Preserve
all other state and mode/map authority. Unused next need not be representable.
Use only for the topmost matching CALLF frame after releasing callee locals.

**Condition codes:** Preserve all flags.

**Exceptions:** Stack bounds and both RAM word probes precede saved-CS Invalid
State, which precedes target Memory Access. Failure pops nothing; see the
[exact ordered contract](#far-call-and-return-contract).

**Encoding:** `$55`, one byte, working mode only in the implementation.
Near RET remains `$1F $11`; IRET remains the separate exception-return operation.

### SAR, SHL, SHR — Shift a word by one bit

**Operands:** rd is B0.

```asm
SAR rd
SHL rd
SHR rd
```

**Operation:** SAR shifts right and retains the original sign bit. SHL shifts left and inserts zero; SHR shifts right and inserts zero. The displaced bit becomes C. These original forms always shift one bit; immediate counts use the separate forms below. Register-supplied counts remain unassigned.

**Condition codes:** Set N/Z/C; preserve V/I/E.

**Exceptions:** No operand-state exception; sequential-PC validation can raise Memory Access. All entries also obey the common fetch, decode and commit rules.

**Encoding:** Two bytes: `$1A`, then SAR `001100dd`, SHL `000100dd`, or SHR `001000dd`.

### Immediate-count shifts and rotates

**Operands:** Destination rd is r0–r3. SHLI, SHRI, SARI, ROLI and RORI accept
`.B`/`.W`; an unsuffixed mnemonic means `.W`. Count is an unsigned immediate
**number of bit positions**, encoded in one byte, accepting exactly 0–16 for
both widths. Reject negative or larger source counts, non-immediate/register
counts, other suffixes, B1/internal/memory destinations and extra/missing operands.

```asm
SHLI.B #count,rd
SHRI.W #count,rd
SARI.W #count,rd
ROLI.B #count,rd
RORI.W #count,rd
```

**Operation:** Count zero preserves the entire old 16-bit rd and all flags,
including the upper byte for `.B`. The instruction still fetches three bytes,
checks its following PC and retires once. For count>0, set temp to the old rd
masked to the selected width. Repeat the following single-bit step exactly
count times, keeping every intermediate temp within that width:

| Operation | Displaced bit (new C) | Single-bit step |
|---|---|---|
| SHLI | Old temp bit w-1 | Shift left, insert zero at bit 0 |
| SHRI | Old temp bit 0 | Shift right, insert zero at bit w-1 |
| SARI | Old temp bit 0 | Shift right, replicate old temp bit w-1 |
| ROLI | Old temp bit w-1 | Shift left, reinsert displaced bit at bit 0 |
| RORI | Old temp bit 0 | Shift right, reinsert displaced bit at bit w-1 |

Rotates use only the selected-width value; input C is never a rotation bit.
Neither count nor the number of steps is masked to four bits. A full-width
positive rotation retains the selected-width value but still produces fresh
N/Z/C, unlike count zero. Counts beyond the width remain defined: byte SHLI/SHRI
at 9–16 yield zero/C=0, byte SARI retains its sign-fill and displaced sign bit,
and byte rotations repeat through their eighth/sixteenth positions.

For positive counts only, write final temp into rd (zero-extend for `.B`), with
N from final bit 7/15, Z from final temp==0 and C from the last displaced bit.
Preserve V/I/E, all other registers and exception state. No operand memory access
or intermediate architectural update occurs. All internal steps form one atomic
instruction with one retirement and no internal IRQ boundary. Existing SHL/SHR/SAR
word encodings and semantics are unchanged.

**Exceptions:** Fetch/decode precedes count validation. A raw count byte 17–255
raises Invalid State at the instruction start after all three bytes have been
fetched, before selected-next-PC validation. A missing count is a fetch Memory
Access fault; an unassigned tag is Illegal Instruction before count fetch.
No destination/flag changes commit on a fault. Even count zero requires a
representable next: PC=$FFFC can finish at $FFFF; PC=$FFFD fetches fully but
fails next=$10000, unless an invalid count takes earlier priority.
These are ordinary U/K operations, with no new privilege class; protected
execution enforcement is implemented in working mode.

**Encoding:** Three bytes: `$1A tag count8`, assigned below. All forty tags
require working mode in the implementation; legacy 0.4/0.5 reject at the
tag without fetching count. See [conformance vectors](ISA.md#conformance-immediate-count-shifts-and-rotates)
and [working examples](ISA_EXAMPLES.md#immediate-shift-and-rotate-examples).

### SBC, SUB — Subtract words

**Operands:** rs and rd are B0 registers.

```asm
SBC rs,rd
SUB rs,rd
```

**Operation:** SUB stores `(old_rd - old_rs) mod 65536`. SBC additionally subtracts `1-input_C`. See the wide arithmetic equations under CSR and arithmetic.

**Condition codes:** Set N/Z/C/V; C indicates no unsigned borrow. Preserve I/E.

**Exceptions:** No operand-state exception; sequential-PC validation can raise Memory Access. All entries also obey the common fetch, decode and commit rules.

**Encoding:** Two bytes, `$10` plus the operation/source/destination byte; see [ALU and bank transfers](#10-alu-and-bank-transfers).

### SETX — Edit the active saved image

**Operands:** rn is r0–r7.

```asm
SETX rn,context
```

**Operation:** Copy the original rn word into a writable context field. The write does not validate the complete image; IRET does so. Saved CSR writes are whole-word edits, not live CSR writes. Only the top frame is addressable; protected provenance is not writable.

**Condition codes:** Preserve live flags.

**Exceptions:** Inactive context raises Invalid State; U execution raises Privilege first. Read-only/unknown selectors or reserved register-byte bits raise Illegal Instruction. All entries also obey the common fetch, decode and commit rules.

**Encoding:** Legacy: `$1F ($40+index) 00000rrr`, index $00–$10. Extended:
`$1F ($90+rn) index8` for indices $20–$22 only. Both are three bytes; see
[extended context encoding](#extended-context-encoding).

### SWI — Software exception entry

**Operands:** Unsigned service byte 0..255; no suffix.

```asm
SWI #service
```

**Operation:** Save the representable following PC and service byte, then enter vector 6 without using the caller stack. No service number is implemented by hardware. U entry switches to the protected K environment; K execution may nest. The ordered entry algorithm is specified under [software trap encoding](#26-software-trap). TRAP is an exact alias.

**Condition codes:** Save caller policy; entry clears live I and sets E. NZCV are preserved.

**Exceptions:** Missing operands and next-PC failures are ordinary faults. Capacity overflow or failed entry is terminal Double Fault. All entries also obey the common fetch, decode and commit rules.

**Encoding:** Two bytes: `$26 service`.

## 9. Binary encoding reference

This chapter records assigned binary forms. Unassigned proposals in Appendix B
must not be decoded as assigned instructions. The working copy has one primary
opcode map and one set of operand semantics.

### Instruction lengths

This summary includes real instruction forms; assembler aliases use the length of their expansion. The JSON metadata provides a fixed length for every selected encoding. An escape byte alone is not a complete instruction.

The current 1–5-byte range describes assigned forms, not a permanent
architectural maximum. Future assignments must specify their complete lengths
and retain the instruction-fetch, segment-boundary, and precise-fault rules.
Tool and emulator support for additional lengths must be implemented and verified
before those forms are advertised as executable.

| Form | Total bytes |
|---|---:|
| NOP, HALT, DI, EI, RETF; distinct B0-to-B0 MOV | 1 |
| `$10` ALU or bank transfer; `$1A` CMP/shift/TST.B; TST.W alias | 2 |
| TSTI.B / TSTI.W | 3 / 4 |
| SHLI/SHRI/SARI/ROLI/RORI, byte or word | 3 |
| ADDP to r4–r7/DP; ADJSP; SWI | 2 |
| PUSH, POP, RET, IRET; JS r4–r7 | 2 |
| Conditional branch, BRA; JS addr16 | 3 |
| GETX, SETX, GETC, SETC; MOV.B immediate load | 3 |
| B1 indirect load/store without displacement, byte or word | 3 |
| MOVU load/store without displacement, byte or word | 3 |
| MOV.W immediate load; absolute/DP/SP memory load/store, byte or word | 4 |
| CALL addr16 | 4 |
| JL abs32; CALLF abs32 | 5 |
| B1 indirect load/store with disp16, byte or word | 5 |
| MOVU load/store with disp16, byte or word | 5 |
| Immediate ALU: ADDI.B, SUBI.B, CMPI.B, ANDI.B, ORI.B, XORI.B | 2 |
| Immediate ALU: ADDI.W, SUBI.W, CMPI.W, ANDI.W, ORI.W, XORI.W | 3 |

### Complete primary opcode map

This map shows every assigned primary byte in the working instruction set.
Assigned control/context and MOVU families are included. Unassigned extension
proposals do not appear until their layouts are defined.

Rows are the high nibble and columns the low nibble. `R` means reserved and raises Illegal Instruction immediately. `MV` is a one-byte accumulator move; `E10/E15/E1A/E1F/E27` select extension tables below. `AP4–AP7` and `APD` are two-byte ADDP operations targeting r4–r7 and DP; `ASP` is two-byte ADJSP. E27 is MOVU.

For the [immediate ALU forms](#immediate-alu-instructions), each cell
names the operation and width; the low two opcode bits select destination r0–r3
in order within each four-byte group. `.B` has one following immediate byte
(two bytes total); `.W` has a little-endian immediate word (three bytes total).

| High / low | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | A | B | C | D | E | F |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | NOP | HALT | DI | EI | R | R | R | R | R | R | R | R | R | R | R | R |
| 1 | E10 | MV | MV | MV | MV | E15 | MV | MV | MV | MV | E1A | MV | MV | MV | MV | E1F |
| 2 | AP4 | AP5 | AP6 | AP7 | APD | ASP | SWI | E27 | R | R | R | R | R | R | R | R |
| 3 | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R |
| 4 | BEQ | BNE | BCS | BCC | BMI | BPL | BVS | BVC | BHI | BLS | BGE | BLT | BGT | BLE | BRA | R |
| 5 | R | R | JS | JL | CALLF | RETF | R | R | R | R | R | R | R | R | R | R |
| 6 | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R |
| 7 | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R |
| 8 | ADDI.B | ADDI.B | ADDI.B | ADDI.B | ADDI.W | ADDI.W | ADDI.W | ADDI.W | SUBI.B | SUBI.B | SUBI.B | SUBI.B | SUBI.W | SUBI.W | SUBI.W | SUBI.W |
| 9 | CMPI.B | CMPI.B | CMPI.B | CMPI.B | CMPI.W | CMPI.W | CMPI.W | CMPI.W | ANDI.B | ANDI.B | ANDI.B | ANDI.B | ANDI.W | ANDI.W | ANDI.W | ANDI.W |
| A | ORI.B | ORI.B | ORI.B | ORI.B | ORI.W | ORI.W | ORI.W | ORI.W | XORI.B | XORI.B | XORI.B | XORI.B | XORI.W | XORI.W | XORI.W | XORI.W |
| B | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R |
| C | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R |
| D | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R |
| E | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R |
| F | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R |

### Opcode allocation ledger

This ledger counts primary and secondary byte values separately. Assigned means
the instruction family is selected by that byte, not that every subsequent
operand bit pattern is legal. Available means currently unassigned and illegal;
it grants no executable encoding. The companion
[allocation budget](ISA_ENCODING.md#opcode-allocation-budget) costs symbolic
future families separately from these assignments.

The primary map has 95 assigned values, including five escapes, and 161
unassigned values. A selected form may still fail a reserved operand field or a
later state/memory check. Counts describe matching byte values, not mnemonics or
successful executions.

| Assigned primary family | Values | Count |
|---|---|---:|
| NOP, HALT, DI, EI | `$00–$03` | 4 |
| Distinct B0 register MOV | `$11–$14`, `$16–$19`, `$1B–$1E` | 12 |
| Existing escapes | `$10`, `$15`, `$1A`, `$1F` | 4 |
| ADDP r4–r7 and DP | `$20–$24` | 5 |
| ADJSP | `$25` | 1 |
| SWI | `$26` | 1 |
| MOVU descriptor escape | `$27` | 1 |
| rel16 branches, including BRA | `$40–$4E` | 15 |
| JS immediate | `$52` | 1 |
| JL immediate | `$53` | 1 |
| CALLF / RETF | `$54–$55` | 2 |
| Immediate ALU, six operations × two widths × four B0 registers | `$80–$AF` | 48 |
| **Total assigned** | Disjoint ranges above | **95** |

The immediate ALU family uses **48/256 = 18.75%** of primary space. Retain these
assigned bytes and the two-byte `.B` / three-byte `.W` instruction lengths.
Moving them under a descriptor is not required by the current allocation budget.

| Available primary range | Count |
|---|---:|
| `$04–$0F` | 12 |
| `$28–$3F` | 24 |
| `$4F–$51` | 3 |
| `$56–$7F` | 42 |
| `$B0–$FF` | 80 |
| **Total available** | **161** |

The machine-readable payload covers all assigned working encodings:

| Encoding table | Inspected byte | Assigned values | Unassigned values |
|---|---|---:|---:|
| Working primary map | First byte | 95, including five escapes | 161 |
| Tool metadata primary union | First byte | 95, including five escapes | 161 |
| Legacy 0.5 primary availability | First byte | 44, including four escapes | 212 |
| `ext10` | Second byte after $10 | 224 | 32 |
| `ext15` | Second byte after $15 | 240 | 16 |
| `ext1a` (working / metadata union) | Second byte after $1A | 80 | 176 |
| `ext1a` (legacy 0.4/0.5 modes) | Second byte after $1A | 28 | 228 |
| Working `ext1f` | Second byte after $1F | 93 | 163 |
| Tool metadata `ext1f` | Second byte after $1F | 93 | 163 |
| Working `ext27` | Second byte after $27 | 64 | 192 |

The tool primary union includes `$00–$03`, `$10–$27`, `$40–$4E`, `$52–$55`
and `$80–$AF`. Working mode enables the complete assigned map. Legacy 0.5
retains 44 primary values and rejects new primary/secondary forms at the first
unavailable inspected byte. The sixth table ext27 is enabled only in working mode.

| Secondary table | Assigned second-byte ranges | Available second-byte ranges |
|---|---|---|
| `ext10` | `$00–$DF` | `$E0–$FF` |
| `ext15` | `$00–$3F`, `$50–$FF` | `$40–$4F` |
| `ext1a` | `$00–$13`, `$20–$23`, `$30–$33`, `$40–$4B`, `$60–$87` | `$14–$1F`, `$24–$2F`, `$34–$3F`, `$4C–$5F`, `$88–$FF` |
| `ext1f` | `$00–$12`, `$20–$34`, `$40–$50`, `$60–$63`, `$80–$87`, `$90–$97`, `$A0–$A7`, `$B0–$B7` | `$13–$1F`, `$35–$3F`, `$51–$5F`, `$64–$7F`, `$88–$8F`, `$98–$9F`, `$A8–$AF`, `$B8–$FF` |
| `ext27` | `(byte & $03) == 0`: `$00,$04,…,$FC`, 64 values | `(byte & $03) != 0`, 192 values |

Reserved operand fields do not increase these primary or secondary capacities:

| Existing selected form | Legal later-byte values | Reserved-field accounting |
|---|---|---|
| B1 base selector after `$15` B1 form | Four values, `000000pp` | 252 of 256 third-byte values are illegal per selected second byte; compact B1 would reuse some of these fields, not allocate a new primary |
| GETX/SETX register byte | Eight values, `00000rrr` | 248 of 256 third-byte values are illegal per selected context tag; this does not widen the legacy context selector |
| Extended GETX/SETX index byte | GETX: 21 new indices; SETX: three writable indices | 235 read indices and 253 write indices are illegal per selected tag; legacy words have no extended aliases |
| GETC/SETC selector byte | Assigned control IDs with matching direction | Unknown IDs, GETC of command, and SETC of read-only controls are illegal at the third byte; reserved neighboring secondary tags are already illegal at the second byte |
| MOVU B1 base byte | Four values, `000000pp` | 252 third-byte values are illegal for each of 64 selected second-byte values; reject before any displacement fetch |
| JL/CALLF packed target's high byte | Zero only | Nonzero is Invalid State after complete fetch, not spare opcode capacity |

Reserved shift and transfer bits in the inspected secondary byte are already
included in that table's available count. Reserved third-byte patterns are a
different dimension and must not be added to it. Unknown/read-only selector
rules can further reject a selected instruction. All candidates remain illegal
until assigned, including proposed reuse of currently reserved descriptor bits.

When publishing an extension, update this ledger, the complete map, the companion
budget, and machine-readable coverage status together. Verify all 256 values in
each affected table for overlapping matches and distinguish specification
assignments from the subset enabled in each tool mode.

NOP/HALT/DI/EI are one byte. NOP only advances PC. HALT commits the following PC and enters the halted state described under Interrupts.

### Compact register moves

```text
0001 ss dd
```

Source and destination select B0 accumulators. When `ss != dd`, copy the source without changing flags. The equal-selector encodings are escapes, not self-moves: `$10`, `$15`, `$1A`, `$1F`.

An assembler emits one-byte NOP for `MOV rn, rn`, for r0–r7 only; this explicit pseudo preserves flags and instruction position. Distinct B1-to-B1, internal-to-internal, and internal self-moves have no direct encoding and are rejected. Software must use an explicit accumulator temporary rather than an assembler silently clobbering one. Examples: `MOV r4,r3` is `10 A3`; `MOV r3,r4` is `10 BC`.

### `$10`: ALU and bank transfers

```text
$10 0 ooo ss dd    ; two bytes; B0 source and destination
$10 1 mmm ss dd    ; two bytes; bank transfer
```

The group bit is bit 7; ooo and mmm are three-bit selectors.

| ooo | Syntax | Operation | Updated flags |
|---|---|---|---|
| 000 | `XOR rs,rd` | `rd = rs ^ rd` | N,Z |
| 001 | `AND rs,rd` | `rd = rs & rd` | N,Z |
| 010 | `OR rs,rd` | `rd = rs \| rd` | N,Z |
| 011 | `MOV ~rs,rd` | `rd = (~rs) & $FFFF` | N,Z |
| 100 | `ADC rs,rd` | `rd = rd + rs + C` | N,Z,C,V |
| 101 | `SBC rs,rd` | `rd = rd - rs - (1-C)` | N,Z,C,V |
| 110 | `ADD rs,rd` | `rd = rd + rs` | N,Z,C,V |
| 111 | `SUB rs,rd` | `rd = rd - rs` | N,Z,C,V |

| mmm | Source bank (ss) | Destination bank (dd) |
|---|---|---|
| 000 | Internal B2 | Accumulator B0 |
| 001 | Accumulator B0 | Internal B2 |
| 010 | Address/storage B1 | Accumulator B0 |
| 011 | Accumulator B0 | Address/storage B1 |
| 100 | Internal B3 | Accumulator B0 |
| 101 | Accumulator B0 | Internal B3 |
| 110–111 | Reserved | Illegal Instruction |

Transfers preserve flags except for a CSR destination, which applies the explicit CSR-write rule. Selectors refer to the bank table; the numeric B1 selector is `register_number-4`.

### `$15`: immediate and memory moves

Bit 7 of the second byte selects the addressing family: zero selects immediate/absolute/DP/SP formats; one selects B1 indirect addressing. Direction, size, and value-register fields retain their positions in both families.

#### Immediate, absolute, DP and SP forms (bit 7 = 0)

```text
$15 x d mm z rrr operand
```

For these forms x=0 at bit 7, d is bit 6, mm is bits 5–4, z is bit 3, and rrr is bits 2–0. rrr selects r0–r7. d=0 loads a register; d=1 stores a register. z=0 transfers a word; z=1 transfers a byte. Byte loads zero-extend and byte stores use register bits 7–0. All forms preserve flags.

| mm | Operand | Operand bytes | Segment and interpretation |
|---|---|---:|---|
| 00 | `#imm16` when z=0; `#imm8` when z=1 | 2 or 1 | Immediate; d=1 is illegal |
| 01 | `[addr16]` | 2 | DS, unsigned absolute offset |
| 10 | `[dp+disp16]` | 2 | DS, signed displacement |
| 11 | `[sp+off16]` | 2 | SS, unsigned offset and stack-slot checks |

Immediate byte loads are three bytes; immediate word loads and all absolute/DP/SP memory forms are four bytes. Memory width never changes the address-operand width. d=1/mm=00 is Illegal Instruction, detected from the second byte before fetching an operand.

Assembly uses `MOV.B` or `MOV.W` for immediate/memory width; unsuffixed immediate/memory MOV defaults to word. Register transfers are always word-sized.

#### B1 indirect forms (bit 7 = 1)

```text
$15 1 d s a z rrr 000000 pp [disp16]
```

The second byte is `1dsazrrr`. d/z/rrr have the same meanings as above. s at bit 5 selects DS=0 or CS=1. a at bit 4 selects no displacement=0 or a following signed 16-bit displacement=1. The third byte contains a two-bit B1 base selector pp (`00=r4` through `11=r7`); its upper six bits must be zero. When present, disp16 follows the base-selector byte in little-endian order.

The instruction is three bytes without a displacement and five with one, for either byte or word transfers. A missing displacement means zero. Assemblers use the shorter form for a zero displacement. Invalid selector-byte bits raise Illegal Instruction before any displacement is fetched. Complete effective-address and memory checks precede all effects. CS and DS used here are sampled from the instruction's input state.

| Syntax | Bytes | Meaning |
|---|---|---|
| `MOV.W [ds:r4],r0` | `15 80 00` | Load a word through DS:r4 |
| `MOV.W r1,[ds:r5+2]` | `15 D1 01 02 00` | Store a word through DS:(r5+2) |
| `MOV.B [cs:r6],r2` | `15 AA 02` | Load a zero-extended byte through CS:r6 |
| `MOV.W [cs:r6-2],r2` | `15 B2 02 FE FF` | Load a word through CS:(r6-2) |

These are memory transfers and do not change control flow. Register-target JS uses the separate `$1F $60–$63` forms below.

### `$1A`: comparison, shifts, rotates and tests

CMP and the original one-bit shifts are two bytes and accept only B0 accumulators.

| Second byte | Syntax | Operation | Updated flags |
|---|---|---|---|
| `0000 ss dd` | `CMP rs,rd` | Flags for `rd-rs`; discard result | N,Z,C,V |
| `0001 00 dd` | `SHL rd` | `rd = (old_rd << 1) & $FFFF`; C=old bit 15 | N,Z,C |
| `0010 00 dd` | `SHR rd` | `rd = old_rd >> 1`; C=old bit 0 | N,Z,C |
| `0011 00 dd` | `SAR rd` | `rd = (old_rd >> 1) \| (old_rd & $8000)`; C=old bit 0 | N,Z,C |

Shifts set N/Z from the result and preserve V/I/E. Nonzero reserved `00` fields
in shift tags are illegal. The following test forms are also assigned:

| Second byte | Syntax | Total bytes | Updated flags |
|---|---|---:|---|
| `$40–$43` | `TST.B rd` | 2 | N,Z |
| `$44–$47` | `TSTI.B #imm8,rd` | 3 | N,Z |
| `$48–$4B` | `TSTI.W #imm16,rd` | 4 | N,Z |

The low two tag bits select r0–r3. Immediates follow the tag, low byte first.
All test forms preserve all register bits and C/V/I/E. TST.W is the existing
OR-self alias under `$10`, with no additional `$1A` tag. In modes 0.4/0.5,
new tags are illegal immediately after the second byte, without immediate
fetch. In working mode, a missing immediate is a fetch fault; other unassigned
tags remain illegal before operand fetch. `$3F` and `$4C` are reserved neighbors.
Example bytes: `1A 40`, `1A 46 80`, `1A 49 00 80`.

Immediate-count shifts/rotates use three bytes `$1A tag count8`:

| Operation | Byte tag (r0–r3) | Word tag (r0–r3) |
|---|---|---|
| SHLI | `$60–$63` | `$64–$67` |
| SHRI | `$68–$6B` | `$6C–$6F` |
| SARI | `$70–$73` | `$74–$77` |
| ROLI | `$78–$7B` | `$7C–$7F` |
| RORI | `$80–$83` | `$84–$87` |

The tag is `$60 + 8*operation + 4*word + rd`, where operation is 0–4 in table
order, word=0 for byte/1 for word, and rd=0–3. The third byte is the complete
unsigned count, not a reserved-bit field. Exactly 680 streams (forty tags ×
seventeen counts) are semantically valid; 9,560 count streams select a known
instruction but fault Invalid State. This allocation consumes no primary bytes.
`$5F` and `$88` remain reserved and illegal before count fetch. Legacy modes
reject all new tags at byte 1. Example bytes: `SHRI.W #8,r0 = 1A 6C 08`;
`ROLI.W #16,r3 = 1A 7F 10`; `RORI.B #16,r0 = 1A 80 10`.
See [the normative operation and fault rules](#immediate-count-shifts-and-rotates).

### `$20–$24`: two-byte pointer adjustment

```text
opcode simm8
```

| Opcode | Destination | Syntax | Total bytes |
|---|---|---|---:|
| $20 | r4 | `ADDP #simm8,r4` | 2 |
| $21 | r5 | `ADDP #simm8,r5` | 2 |
| $22 | r6 | `ADDP #simm8,r6` | 2 |
| $23 | r7 | `ADDP #simm8,r7` | 2 |
| $24 | DP | `ADDP #simm8,dp` | 2 |

The primary opcode identifies the destination, leaving the entire second byte for a signed two's-complement constant, -128 through 127. B1 uses `0010 00pp` with pp selecting r4–r7; `$24` is the DP form. `$25` is ADJSP, `$26` is SWI, and `$28–$2F` remain reserved. ADDP accepts only r4–r7 and DP, not B0 registers, SP, other internal registers, or memory.

Compute `sum = unsigned(old_destination) + s8(simm8)` in a wider type; require `0 <= sum <= $FFFF`. Otherwise raise Invalid State with the instruction address reported, leaving the destination and flags unchanged before fault entry. No segment or memory is accessed and no offset wrapping occurs. A valid sum may point to uninstalled RAM; that is checked on dereference. The DP form uses the same checks and flag behavior as the B1 forms and changes neither DS nor any other register. Alignment and access width do not affect adjustment: +1 advances one byte and +2 advances one word.

Examples: `ADDP #1,r4` is `20 01`; `ADDP #-1,r4` is `20 FF`; `ADDP #2,r7` is `23 02`; `ADDP #-2,dp` is `24 FE`. `INCP` and `DECP` are assembler aliases for ADDP #1 and ADDP #-1 respectively, accepting the same five destinations and using the same two-byte encodings. A signed constant makes a separate subtract-pointer opcode unnecessary. There is no 16-bit immediate form yet; larger updates use multiple ADDP instructions or explicit accumulator arithmetic followed by a transfer back. Only ADDP guarantees checked pointer adjustment; ordinary B0 arithmetic retains its modulo behavior.

The five primary opcodes encode `5 * 256` destination/constant combinations.
`$1A $40–$43` are reserved and raise Illegal Instruction
as soon as the secondary byte is fetched; no constant byte is fetched.

ADDP preserves **all** CSR bits, including N/Z/C/V/I/E. It is not an arithmetic comparison or a loop-count test. For example, an intervening ADDP does not disturb CMP flags or a carry intended for ADC.

### `$25`: checked stack adjustment

`ADJSP #simm8` is `$25 simm8`, length two. The constant is signed two's complement, -128 through 127; assembly rejects values outside that range. Compute the new SP in a wider type, require the initial `SB <= SP <= ST` invariant and `SB <= new_SP <= ST`, then commit SP with every CSR bit preserved. Failure is Stack Boundary, not ADDP's Invalid State. For example, `ADJSP #-8` is `25 F8`, and `ADJSP #8` is `25 08`. ADJSP #0 is valid on a valid stack, including an empty one. Normal instruction fetch and following-PC checks still apply, and all effects are atomic. It does not change SS/SB/ST and does not save or restore registers.

### `$26`: software trap

`SWI #imm8` is `$26 imm8`, length two. The unsigned service number is 0–255;
negative/out-of-range values, omitted or register operands, and size suffixes are
rejected. `TRAP #imm8` is an exact alias. All service values enter vector 6;
none invokes a built-in host service.

Entry follows these rules in order:

1. Accept an already eligible IRQ at the boundary before fetching SWI. Otherwise
   fetch both bytes from the original CS without wrapping. Missing operand bytes
   cause a fetch Memory Access fault at the instruction start, not a software trap.
2. Require `next = old_PC + 2` to be representable without truncation. Failure is
   Memory Access at the instruction start with no representable relevant address.
   Do not probe next; IRET validates the selected resume target later.
3. Capture the caller state with xpc=next, xcs=old_CS, canonical saved NZCVI,
   xcause=6 and xtrap=zero-extended imm8. Address metadata is zero. The interrupted
   stack may be invalid and is neither used nor probed.
4. Enter the [protected kernel environment](#nested-contexts-entry-and-terminal-diagnostics)
   atomically: U selects the configured kernel stack/DS; K entry nests using its
   existing kernel stack. E does not prohibit SWI. Entry clears I and HALT.
5. Commit only after the complete entry bundle validates. Failed entry or exhausted
   frame capacity enters terminal Double Fault with the specified captured frames
   and host diagnostics retained. No partial live environment is installed.

An IRQ arriving during SWI stays pending; it cannot replace synchronous entry or
a fetch/PC fault. Successful SWI does not consume an IRQ request. A K handler may
subsequently enable interrupts. SWI retires only if its handler-entry bundle commits.

IRET restores the saved caller through the complete return-image checks. Results
must be written into the saved image with SETX; live handler register changes are
discarded. An invalid saved stack or target can cause a recoverable nested fault.
RET is not a trap return. No automatic instruction skip or errno is supplied.

`SWI #1` and `TRAP #1` both encode as `26 01`. SWI at $FFFD may save $FFFF; SWI
at $FFFE faults because next would be $10000. A missing immediate is a fetch
fault. See Appendix A and the [encoded syscall example](ISA_EXAMPLES.md#software-trap-and-system-call-example).

### User-access encoding

MOVU is assigned primary `$27`. It dispatches to the following mode descriptor;
it is not a sixth ADDP destination or a modifier of ordinary MOV:

```text
$27 dazrrr00 000000pp [disp_lo disp_hi]
```

| Field | Bits | Meaning |
|---|---|---|
| d | Second byte bit 7 | 0=load user memory into rn, 1=store rn into user memory |
| a | Second byte bit 6 | 0=no displacement, 1=signed 16-bit displacement follows base byte |
| z | Second byte bit 5 | 0=word, 1=byte |
| rrr | Second byte bits 4–2 | Value register r0–r7 |
| Reserved | Second byte bits 1–0 | Must be zero; otherwise immediate Illegal Instruction |
| pp | Third byte bits 1–0 | B1 base r4–r7 |
| Reserved | Third byte bits 7–2 | Must be zero; otherwise Illegal Instruction before displacement fetch |

There are 64 legal second-byte values (all multiples of four), 192 illegal
values, and four legal base bytes per selected descriptor. There are 128
short register/base/direction/width combinations and 128 displaced combinations;
the latter each accept every signed disp16 bit pattern. The short instruction
is three bytes; the long instruction is five, with low displacement byte first.
The long zero-displacement encoding is accepted; canonical assembly/disassembly
uses `[rp]` and reassembly chooses the short form. Preserve the original image
to retain noncanonical bytes exactly, as with ordinary MOV's long B1 form.

| Source | Exact bytes |
|---|---|
| `MOVU.B [r4],r0` | `27 20 00` |
| `MOVU.W r1,[r5-2]` | `27 C4 01 FE FF` |
| `MOVU.W [r6+2],r7` | `27 5C 02 02 00` |
| `MOVU.B [r4],r4` | `27 30 00` |
| `MOVU.W r4,[r4]` | `27 90 00` |
| `MOVU.B [r7-32768],r7` | `27 7C 03 00 80` |
| `MOVU.W [r4+0],r0` | `27 00 00` |

`27 01` is illegal immediately without a base fetch. `27 40 04` is illegal
before displacement fetch. `27`, `27 40`, or `27 40 00 FE` with the next required
byte unavailable instead gets a fetch Memory Access fault before privilege or
operand checks. Complete U decode raises Privilege/USER_ACCESS=10. Complete K
decode follows the [user-access fault contract](#user-access-fault-contract).
All instruction bytes come from the original execution map/CS; no instruction
or displacement byte is fetched from uaccess_map.

With valid operands, a short form at `$FFFC` or long form at `$FFFA` can commit
PC=`$FFFF`. At `$FFFD`/`$FFFB` respectively, all bytes fit but following PC is
`$10000`: Memory Access with KERNEL/RESULT_PC/FETCH, no data/register effects,
and no representable relevant address. An operand failure takes priority over
that resulting-PC failure. A word operand at user offset `$FFFF` is instead
UACCESS/OPERAND/OFFSET_RANGE, with valid relevant virtual start and invalid
physical address. No speculative read or store is committed in either case.

### Branches and absolute jumps

Branches are three bytes: `$40+condition`, then little-endian rel16. The assembler encodes `label - next` as a signed 16-bit displacement and rejects out-of-range labels. At `$1000`, `BNE +4` targets `$1007` when taken.

The predicates below evaluate the current CSR flags as specified under
[conditional branch flag interpretation](#conditional-branch-flag-interpretation).
The rel16 displacement does not imply a sixteen-bit ALU comparison.

| Opcode | Mnemonic | Taken when |
|---|---|---|
| $40 | BEQ | Z=1 |
| $41 | BNE | Z=0 |
| $42 | BCS | C=1; unsigned higher or same |
| $43 | BCC | C=0; unsigned lower |
| $44 | BMI | N=1 |
| $45 | BPL | N=0 |
| $46 | BVS | V=1 |
| $47 | BVC | V=0 |
| $48 | BHI | C=1 and Z=0 |
| $49 | BLS | C=0 or Z=1 |
| $4A | BGE | N=V |
| $4B | BLT | N!=V |
| $4C | BGT | Z=0 and N=V |
| $4D | BLE | Z=1 or N!=V |
| $4E | BRA | Always |
| $4F | Reserved | Illegal Instruction |

`JS addr16` is `$52 lo hi`, length three, and changes PC within current CS. It is a jump, not a call. `JL abs32` is `$53 pc_lo pc_hi cs 00`, length five. Bits 31–24 must be zero or Invalid State is raised; bits 23–16 become CS and bits 15–0 become PC. Both preserve flags.

`CALLF abs32` is `$54 pc_lo pc_hi cs 00`, length five; `RETF` is `$55`,
length one. They use the [four-byte far frame](#far-call-and-return-contract),
unchanged mode/map authority and atomic stack/target checks. CALLF preserves the
original CS/next for RETF; JL creates no return frame. Primary `$56` remains
reserved. Both new forms require working mode in the implementation.

### `$1F`: stack, calls, register jumps, and exception context

| Second byte | Syntax | Total bytes | Meaning |
|---|---|---:|---|
| $00–$07 | `PUSH rn` | 2 | Register number = second byte |
| $08–$0F | `POP rn` | 2 | Register number = second byte minus 8 |
| $10 | `CALL addr16` | 4 | Two-byte little-endian near target follows |
| $11 | `RET` | 2 | Near return |
| $12 | `IRET` | 2 | Restore saved exception context |
| $20–$34 | `GETX context,rn` | 3 | Context index = second byte minus $20; register byte follows |
| $40–$50 | `SETX rn,context` | 3 | Context index = second byte minus $40; register byte follows |
| $60–$63 | `JS rp` | 2 | pp=second byte minus $60 selects r4–r7; jump to that register's offset in current CS |
| $80–$87 | `GETX extended_context,rn` | 3 | rn=second byte minus $80; full extended index follows |
| $90–$97 | `SETX rn,extended_context` | 3 | rn=second byte minus $90; writable extended index follows |
| $A0–$A7 | `GETC control,rn` | 3 | rn=second byte minus $A0; assigned readable selector follows |
| $B0–$B7 | `SETC rn,control` | 3 | rn=second byte minus $B0; assigned writable selector follows |
| All others | Reserved | — | Illegal Instruction |

Legacy GETX/SETX use a third byte `0000 0rrr`; upper five bits must be zero.
Extended forms instead use the full index byte described below. Both families
copy one word between a GPR and the active top frame, preserve flags, and require
E=1 in K. With E=0 they raise Invalid State; legal U execution raises Privilege
first. Complete saved-image validation occurs on IRET, allowing SETX edits of
related fields in any order. Encoded writes to read-only context words are illegal.

SETX writes the entire saved word; it does not apply the live CSR-write mask to xcsr. Writing xcsr does not change live CSR or exception activity. The saved-CSR validity rules under IRET still apply when the context is restored.

`GETX xtrap,rn` uses `$1F $34 00000rrr`. xtrap is read-only, as are cause/address metadata: `$1F $54` remains illegal and is rejected before fetching a register byte.

`JS r4` through `JS r7` sample the selected B1 register as the target offset, validate the first byte at current CS:target, then commit PC. They preserve CS, SP, all general/internal registers other than PC, and all flags. They do not dereference a target word in memory and do not push a return address. For example, `JS r4` is `1F 60`; `JS r7` is `1F 63`. A table dispatch first loads the target word into B1, then uses JS. `JS [r4]`, `JS r0`, `JS dp`, and indirect CALL have no encoding. The instruction may end at `$FFFF` because unused next need not be representable, as for absolute JS. An unavailable target faults before effects; an illegal instruction at an installed target faults only when that next instruction is decoded.

### Extended context encoding

```text
GETX extended: $1F 10000rrr index8   ; $80–$87, rrr selects r0–r7
SETX extended: $1F 10010rrr index8   ; $90–$97, rrr selects r0–r7
```

Both are three-byte K-only instructions preserving all flags. The final byte
selects exactly one word in the [extended table](#extended-saved-context): reads
accept `$20–$29` and `$30–$3A`; writes accept only `$20–$22`. Known read-only
words used with SETX and all unknown extended indices are Illegal Instruction
as soon as index8 arrives, before privilege or active-frame checking. Every
reserved neighboring secondary tag is illegal before index fetch. Missing index8
on an assigned tag is a fetch Memory Access fault before privilege. Complete
legal fetch/decode precedes K privilege, active-top-frame validation, and ordinary
next-PC checks; no access/write commits on failure.

GETX/SETX assembly accepts an assigned context name or unsigned eight-bit selector
literal without `#`, then chooses legacy bytes for `$00–$14` or extended bytes
for the new words. SETX accepts only writable selectors in either family.
Unknown selectors, illegal access direction, nongeneral registers, and size
suffixes are rejected. There is no forced extended alias for a legacy word.

Examples: `GETX xprocess_map,r2` is `1F 82 20`;
`SETX r7,xuaccess_segment` is `1F 97 22`;
`GETX xpriv_class,r0` is `1F 80 29`; `GETX xreason,r3` is `1F 83 3A`.
`1F 90 23` is an illegal provenance write, including in U or with E=0.
`1F 80 14` is illegal, not another encoding of legacy xtrap. Legacy
`GETX xtrap,r0` remains `1F 34 00`, and legacy `1F 54` remains illegal before a
third-byte fetch. At PC=`$FFFD`, a valid three-byte extended form fetches fully
but faults its resulting PC without modifying register/frame state; state and
privilege faults still take their earlier specified priority.

### Protected control encoding

The assigned GETC/SETC layouts are:

```text
GETC: $1F 10100rrr selector8   ; $A0–$A7, rrr selects r0–r7
SETC: $1F 10110rrr selector8   ; $B0–$B7, rrr selects r0–r7
```

Both are three bytes. The third byte is the full control selector, not a GPR
descriptor or a legacy five-bit context index. Exact selector/command numbers
are in the [control tables](#control-selectors-and-reset-values). A read of a
write-only selector or write of a read-only selector is Illegal Instruction when
selector8 is fetched. SETC's command value is register data: an unknown command
is Invalid State after privilege, not a reserved encoding.

Every unassigned `$1F` secondary tag remains illegal without fetching a third
byte. Within `$80–$BF`, `$80–$87` and `$90–$97` are assigned extended context
tags, `$A0–$A7` and `$B0–$B7` are assigned controls, and the four intervening
eight-value groups remain reserved; the reserved bit is never an alternate bank.
Legacy GETX/SETX retain all existing bytes.

Examples: `GETC process_map,r2` is `1F A2 00`; `SETC r7,uaccess_map` is
`1F B7 01`; `SETC r0,command` is `1F B0 7F`. `1F A8` is immediately illegal;
`1F A0 7F` is an illegal command read; `1F B0 03` is an illegal mode write.
`1F A0` with its selector missing is a fetch Memory Access fault. At PC=`$FFFD`,
`1F A0 00` fetches fully but its unrepresentable following PC faults without a
register write; at `$FFFC` it may complete with PC=`$FFFF`. There is no descriptor
byte or hidden scratch-register transfer beyond the three assigned bytes.

### Immediate ALU instructions

The immediate ALU instructions operate on B0 registers `r0`–`r3` only. Syntax is `MNEMONIC.B #imm8,rd` or `MNEMONIC.W #imm16,rd`; source precedes destination. An unsuffixed immediate ALU mnemonic defaults to `.W`.

The [byte and word conventions](#byte-and-word-operations) apply to every form
below; the encoding selects the operation width explicitly.

| Operation | `.B` result | `.W` result | Updated flags |
|---|---|---|---|
| `ADDI` | `(rd & $FF) + imm8`, modulo 256, zero-extend | `rd + imm16`, modulo 65536 | N,Z,C,V |
| `SUBI` | `(rd & $FF) - imm8`, modulo 256, zero-extend | `rd - imm16`, modulo 65536 | N,Z,C,V |
| `CMPI` | Flags from `(rd & $FF) - imm8`; no write | Flags from `rd - imm16`; no write | N,Z,C,V |
| `ANDI` | `(rd & $FF) & imm8`, zero-extend | `rd & imm16` | N,Z |
| `ORI` | `(rd & $FF) \| imm8`, zero-extend | `rd \| imm16` | N,Z |
| `XORI` | `(rd & $FF) ^ imm8`, zero-extend | `rd ^ imm16` | N,Z |

For `.B`, arithmetic and logical operations replace the entire 16-bit destination
with a zero-extended 8-bit result; the original upper byte is not preserved.
`CMPI.B` preserves all 16 bits of its destination. Flags follow the normative
[width-specific ALU rules](#width-specific-alu-flags): N uses bit 7/15, Z tests
the selected-width result, arithmetic sets C/V, and logic preserves C/V. The
immediate is an unsigned bit pattern in the calculation; signed overflow uses
its selected-width two's-complement interpretation. All forms preserve I/E and
do not modify memory or segment state.

Assembler immediates accept `-128..255` for `.B` and `-32768..65535` for `.W`, encoding negative values in two's complement. Reject out-of-range values; do not truncate. Thus `#-1` and `#$FF` encode identically for `.B`, and `#-1` and `#$FFFF` encode identically for `.W`. Byte and word operands are not interchangeable: `.B` does not sign-extend its immediate to 16 bits or compute 16-bit flags.

#### Binary encoding

Each instruction uses a dedicated primary opcode containing the operation and destination selector, followed directly by the immediate in little-endian order. No register descriptor byte is present.

```text
.B: opcode imm8                ; 2 bytes
.W: opcode imm16_lo imm16_hi  ; 3 bytes
opcode = base + (rd - r0)     ; rd in r0..r3
```

| Operation | `.B` opcode range | `.W` opcode range |
|---|---|---|
| `ADDI` | `$80–$83` | `$84–$87` |
| `SUBI` | `$88–$8B` | `$8C–$8F` |
| `CMPI` | `$90–$93` | `$94–$97` |
| `ANDI` | `$98–$9B` | `$9C–$9F` |
| `ORI` | `$A0–$A3` | `$A4–$A7` |
| `XORI` | `$A8–$AB` | `$AC–$AF` |

Example bytes: `CMPI.B #$0D,r0` → `90 0D`; `ANDI.B #$7F,r0` → `98 7F`; `ADDI.W #$1234,r2` → `86 34 12`; `XORI.W #$FF00,r3` → `AF 00 FF`.

Instruction fetch, PC advancement, privilege, exception entry, fault priority and atomic commit follow the general rules. A missing immediate byte causes a fetch Memory Access fault at the instruction start; no destination or flag changes commit. A representable following PC is required for ordinary sequential completion. All six operations are valid in U and K modes. MMIO byte transfers remain separate `MOV.B` instructions; these immediate operations act on registers after a byte has been read.

## Appendix A. Software ABI conventions

**Informative with respect to hardware conformance.** The calling and scheduling
contracts here are requirements for software choosing these ABIs. The CPU does
not implement errno, service dispatch, PCB ownership, mapping leases, compiler
register allocation or a multi-instruction context-replacement transaction.
Instruction-level validation remains mandatory independently of ABI correctness.

The call and syscall conventions are stated first, followed by process-switch
and map-lifetime conventions for protected execution.
They must not be mistaken for extra executable control operations.

### Calling convention

This is a software convention, not hardware enforcement:

- Use CALL/RET for near calls in the same CS, CALLF/RETF for same- or cross-segment calls. Both retain the caller's execution mode and process-map authority.
- r0–r2 carry up to three word arguments. r0 returns a word; a double word returns low half in r0 and high half in r1.
- r0–r3, DP, and N/Z/C/V are caller-saved. r4–r7, DS, SS, SB, and ST are callee-preserved. A callee returns with the caller's SP restored and does not change interrupt policy.
- On entry, `[sp+0]` is the return offset. Near arguments begin at `[sp+2]`; far frames additionally place the canonical return CS at `[sp+2]`, so far arguments begin at `[sp+4]`. The caller removes arguments after return. Account for pushes/locals when accessing slots; frame types are not tagged or interchangeable.
- Allocate locals with ADJSP using a negative constant, then use nonnegative slot offsets. Release them with a positive adjustment before the matching RET or RETF. PUSH/POP preserve individual registers. Larger adjustments can use several ADJSP instructions, each with its own checks and commit; a later failure leaves earlier adjustments in effect. This is not an atomic whole-frame allocation.
- Exception handlers use saved context and IRET instead of this call convention; live-register changes are discarded unless copied into context with SETX.

### System-call ABI

The following is the software convention, separate from instruction semantics and the ordinary CALL ABI:

| Value | Convention |
|---|---|
| Service number | SWI immediate; handler reads xtrap |
| Word arguments | r0–r3; read their saved xr0–xr3 when needed |
| Pointer arguments | Service-specific segment/offset pairs; a default buffer convention uses DS:DP and r0 as byte count |
| Primary result | r0, written through `SETX ...,xr0` |
| Status | r1, written through `SETX ...,xr1`: zero succeeds; nonzero is an OS-defined error |
| Preserved on ordinary return | r2–r7, DP, DS, SS, SP, SB, ST, and all caller CSR flags/policy |
| Return address | Original CS and the instruction following SWI |

r0/r1 are caller-clobbered even for a service with no meaningful result; define them on every return path. Larger results use caller-supplied buffers. Status codes and service-number assignments belong to an OS ABI, not the hardware. All 256 immediate values are hardware-valid. An unsupported service returns an OS error through the normal IRET path; it is not an Illegal Instruction or Invalid State fault.

Handlers may use any live registers because IRET restores the saved caller state. SETX edits only the fields that the service intends to return. Entry selects protected kernel DS and stack state without depending on the
caller's stack. A handler may use PUSH/CALL/locals once its configured kernel
stack is valid. Synchronous faults and explicitly enabled interrupts may nest;
returning from the nested frame resumes the handler. A handler may call another
kernel routine with CALL without creating another protected exception frame.

### User-buffer copy and recovery ABI

This software ABI example uses assigned instructions, not current production
tool/emulator support. The kernel owns the fixup registry, map-generation lease,
buffer bounds, and repair/error policy; none is a CPU lookup mechanism.

Fixture: a U process in published map 7 calls a buffer-read service with byte
count in r0, pointer segment in r1 (5 here), and pointer offset in DP. Its live
DS=3 remains a separate valid data descriptor; CS and SS also remain valid.
This is a service-specific pointer convention allowed by the syscall ABI. The
kernel accepts counts 0–255 and
copies to its private DS=0 buffer `$3000–$30FF`, installed K R/W RAM. Kernel
code uses map 0, CS=2; the syscall handler starts at `$1000` below. Vector 2
selects a pinned K memory-fault handler, and vector 6 selects this syscall
handler. Kernel entry/stack backing is valid, no descriptor staging operation
is in progress, and a lease keeps map 7's identity stable while allowing the
explicit descriptor repair below. Handlers leave I=0 in this fixture.

```asm
GETX xr0,r2
CMPI.W #255,r2
BHI bad_user_buffer
GETX xdp,r4
MOV.W #$3000,r5
GETX xr1,r0
SETC r0,uaccess_segment
GETC process_map,r0
SETC r0,uaccess_map
CMPI.W #0,r2
BEQ copied
copy_byte:
MOVU.B [r4],r0
MOV.B r0,[r5]
SUBI.W #1,r2
BEQ copied
MOV r4,r0
CMPI.W #$FFFF,r0
BEQ bad_user_buffer
ADDP #1,r4
ADDP #1,r5
BRA copy_byte
copied:
GETX xr0,r0
SETX r0,xr0
MOV.W #0,r0
SETX r0,xr1
SETC r0,uaccess_map
IRET
bad_user_buffer:
MOV.W #0,r0
SETX r0,xr0
MOV.W #1,r0
SETX r0,xr1
MOV.W #0,r0
SETC r0,uaccess_map
IRET
```

The accepted maximum fits the pinned kernel buffer. Empty copies touch no user
memory. The loop tests remaining count before advancing a pointer; if a user
range needs a byte beyond `$FFFF`, it takes its known error continuation rather
than wrapping or turning a checked ADDP failure into an implicit memory fixup.
Earlier completed copies remain committed on later failure; the kernel discards
the partial private buffer on error. There is no whole-copy atomicity promise.
Success returns requested count in saved r0 and zero in saved r1; this example's
OS error convention returns r0=0/r1=1. All other user state/flags are preserved.

The chosen instruction lengths produce these exact addresses in CS=2:

| Site / continuation | Offset | Purpose |
|---|---|---|
| Handler entry | `$1000` | Ordinary SWI service dispatch destination for this fixture |
| copy_byte | `$1022` | Three-byte `27 20 00` user-access read |
| copied | `$103D` | Complete-copy continuation |
| bad_user_buffer | `$104F` | Explicit error continuation |

If these bytes or their layout change, regenerate the registry entry; a handler
must never guess a continuation from a nominal instruction length or reread a
possibly incomplete instruction. The entry is `(fault_cs=2, fault_pc=$1022,
fixup_cs=2, fixup_pc=$104F)`, with cause=2, origin=UACCESS=3, phase=OPERAND=2,
access=READ=2, width=1, and an explicitly allowed reason set. This fixture allows
OFFSET_RANGE, ACCESS_DISABLED, MAP_INVALID, NOT_PRESENT, BOUNDS, RIGHTS, DEVICE,
and BACKING (numeric reasons 1–8); the kernel also verifies its owned lease and
expected map/segment state. Other copy sites must register their own policy.
Unexpected KERNEL faults, wrong PCs/modes/access types, or unmatched events
follow kernel-bug policy, not this error path.

On a MOVU fault, the stack of protected frames is:

1. Depth 1: original U syscall frame, including the user's zero user-access
   controls; this frame is not modified by nested capture.
2. Depth 2: interrupted K copy, saved xcs=2/xpc=`$1022`, its registers/flags and
   xuaccess_map/segment, plus the exact failed-access record. Its return mode
   remains K, independent of the lower U frame.

The memory handler reads only its top frame using assigned GETX fields, verifies
the registry/event/lease conditions above, then chooses retry or fixup.

**Retry example.** For NOT_PRESENT=4 in published map 7, selector 5, assume
the kernel policy authorizes an initialized private installed RAM frame `$23`.
The following handler path publishes that complete U R/W descriptor and retries:

```asm
MOV.W #0,r0
SETC r0,uaccess_map
MOV.W #7,r0
SETC r0,desc_map
MOV.W #5,r0
SETC r0,desc_segment
MOV.W #$23,r0
SETC r0,desc_frame
MOV.W #$0019,r0
SETC r0,desc_flags
MOV.W #0,r0
SETC r0,desc_lower
SETC r0,desc_upper_lo
SETC r0,desc_sb
SETC r0,desc_st
MOV.W #1,r0
SETC r0,desc_upper_hi
SETC r0,command
IRET
```

The fixture has no interrupted descriptor staging to preserve. A general handler
must preserve/restore shared selection/shadow scratch under the Chapter 6 ABI.
It must not grant unowned memory or broaden rights merely because a fault
occurred. Backing topology cannot be changed between resets: a BACKING repair
can remap to initialized installed RAM under policy, not conjure missing backing.
The retry IRET restores the interrupted K registers and ua controls (7:5) and
resumes `$1022`; MOVU re-resolves the current descriptor. No failed read result or
partial store exists to replay. The lower U frame remains untouched until the
copy completes or takes its error path.

**Registered-fixup example.** After the same registry checks, to abort instead:

```asm
MOV.W #0,r0
SETC r0,uaccess_map
SETX r0,xuaccess_map
SETX r0,xuaccess_segment
MOV.W #$104F,r0
SETX r0,xpc
IRET
```

This edits the depth-2 K image only. Clearing both saved user-access words is
deliberate: MAP_INVALID at a registered site may capture an unpublished target map, which
IRET would reject even though the error continuation intends to disable access
later. Saved edits do not have live SETC's implicit segment-clear effect.
Other failed-access metadata is read-only and unchanged. IRET restores a valid
disabled K user-access state and jumps to `$104F`, popping only depth 2. At that
point the lower U frame is topmost, so bad_user_buffer writes its two result
words and returns to U with its original registers/flags and disabled controls.
The arbitrary target selector 5 is separate from saved DS=3; an absent target
can therefore yield a normal pointer error without invalidating the U return's
own data descriptor. Invalidating saved CS/DS/SS still requires repair or process
termination under kernel policy; fixup does not bypass return-image validation.
A general nested K caller must additionally restore its own prior control/lease
conventions; this syscall fixture starts and finishes disabled. Clearing these
words does not repair the lower frame's process map. The printed fixture's lease
keeps map 7 published; violating that lease is a kernel bug. To exercise a
recoverable MAP_INVALID case, a separate registered copy site can target an
unpublished map 8 while its lower U process map 7 remains valid. That K fixup
can return an error to U map 7; it cannot resume an invalidated U process simply
by clearing user-access controls. Every outer IRET retains full validation.

### Process context switching

A process image records the writable return fields: eight GPRs, PC/CS/CSR,
SP/SB/ST/SS, DP/DS, process-map ID and user-access controls. A resumable U image
has canonical I=1 and disabled user-access controls. Saved mode/owner-generation
are trusted PCB metadata checked in software; frame origin, return authority,
linkage/depth, cause and fault metadata are never loaded from the PCB. Timer,
PIC, kernel-entry configuration and terminal diagnostics are per CPU.

#### Replacement eligibility

The kernel context-switch ABI permits process replacement only when all hold:

1. Live execution is K with I=0.
2. Exactly one ordinary frame is active and its immutable origin is U.
3. No terminal/entry transition is active; no emergency context exists.
4. The incoming owner/map-generation handle is live and held under a lifetime lock.
5. The **entire** incoming U image has passed the complete return validation above
   and any associated future kernel-entry stack/DS configuration is valid.

These are software scheduling requirements over ISA-visible state. SETX remains
an individual privileged context-word edit; it has no hidden scheduler detection
or multi-instruction transaction. `prepare_user_return` is a distinct K-origin
bootstrap path, not an exemption for switching under a nested K frame.

#### Prevalidate, replace, publish

The kernel must use this sequence, without a dedicated switch or validation opcode:

1. DI, then copy the incoming PCB into trusted pinned K scratch RAM and preserve
   a complete outgoing image plus scheduling/entry-state backup. Acquire the map
   lifetime/mutation locks; use committed descriptor readback and boot backing
   information. Do not start with user-supplied memory as the replacement source.
2. Validate every incoming field and resume target using the shared contract.
   An invalid candidate is rejected **before any SETX or current-owner update**.
   A's frame and scheduler ownership remain intact; select another candidate or
   resume A through normal IRET. Validation sets no hardware authorization token.
3. With mappings stable, run a verified replacement sequence from pinned K RAM,
   reading pinned scratch only. Its complete fetch path, slot writes, needed
   stack and continuation addresses must be valid. Load B's writable fields with
   SETX; preserve the U-origin authority and linkage. Apply validated B-specific
   entry state, or retain a shared per-CPU entry stack. Descriptors and installed
   backing used by the replacement cannot be revoked during this window.
4. Once B's saved image and entry state are consistent, set live `process_map`
   to B, disable/clear live user-access controls, and publish associated kernel
   scheduling/current-owner state. Back up these live controls too so an abort
   restores A consistently. The effective K code/data map remains 0 throughout.
   Keep I=0 through IRET, which validates again and establishes canonical U I=1
   on success; this minimal switch ABI does not execute EI inside replacement.
   Retain the outgoing backup/lease until success is known. Software may defer
   releasing it until the next U-origin entry proves that B was resumed; no CPU
   transaction token or extra commit instruction is required.

Masking IRQs does **not** mask synchronous faults. Normal prevalidated SETX and
pinned-RAM reads should not fault under these conditions, but there is no whole-
sequence hardware rollback. If an unexpected synchronous fault occurs after the
first edit, completed words remain committed. A registered K fixup may unwind
its nested fault frame to an abort continuation, which restores A's complete
backup and associated scheduling/entry state before IRET or EI. It must not resume
an incomplete B image. An IRQ remains pending; it cannot see partial replacement
while I=0. A fault handler recognizes the registered replacement/fixup region,
not an assumption that a partially published current-owner pointer is authoritative.
If rollback cannot run or the entry environment is broken, take the
kernel's terminal path rather than claim recovery. This is compensating software
recovery, not architectural atomicity across multiple SETX instructions.

Single CPU, disabled IRQs, pinned code/data and exclusive map locks make software
prevalidation reliable without a new transaction instruction. Synchronous handlers
must honor those locks and replacement fixups. Host debugger mutations, recursive
map invalidation in a handler or changes to pinned code violate that kernel/machine
contract; they are not races that a validation result silently protects against.
IRET's final checks still prevent invalid state from becoming live.

#### Scheduling critical sections and nested IRQs

The kernel ABI requires I=0 from the start of replacement through consistency of
both the saved image and associated kernel state. PIC masking a timer source alone
is insufficient. No hardware instruction is added to enforce this ABI sequence.
A timer arriving then remains pending. A timer nested inside a device handler,
or a device IRQ nested in an enabled timer handler, may set reschedule-pending
but must not replace a process while depth>1 or the top origin is K. Return
through nested frames; switch only at the eligible outer U frame. Device ACK and
saved I restoration remain independent operations.

Blocking a syscall may save its U image plus a software continuation/request,
then unwind K call frames in software before selecting another U process. Arbitrary
suspended K-stack chains require a separate kernel continuation ABI. Top-frame
replacement does not suspend or transfer an entire protected frame stack.

### Map-ID lifetime ABI

The kernel lifetime ABI must use `(map_id, software_generation)` handles and
track references/leases. On exit it marks the owner dying immediately, prevents
new scheduling/access handles, and drains references from live process-map and
user-access controls, every protected frame, resumable PCB/continuation, fixup,
and deferred kernel request. Clear unused saved/live selectors or cancel their
continuations; do not discard protected frame provenance. An exiting process's
outer frame is replaced by a validated successor and nested handlers unwind
before its remaining CPU references can be retired. A later K reaper can finish
invalidation; no immediate numeric ID reuse is required at syscall exit.

Only after that quiescent point may the kernel invalidate, increment its software
generation, rebuild and republish the ID. Generation wrap requires continued
quarantine until every older handle has been discarded. Before use/resumption,
software verifies ownership and generation under the same lifetime lock. A dying
owner cannot be resumed merely because its map is still temporarily published.

This lease/refcount protocol is a kernel obligation, not a new CPU interlock.
Hardware rejects unpublished maps but cannot distinguish an old reference after
the same eight-bit ID is republished to someone else. Sparse descriptor storage
must not change validity or reuse semantics. In this single-CPU machine, DI plus
pinned state can protect the replacement window; synchronous handlers must honor
the same locks or abort to an established fixup instead of invalidating its maps.

## Appendix B. Unassigned extension proposals

**Informative; not part of the assigned instruction set.** The following records
approved design directions and deferred ideas. No symbolic opcode in the companion
proposal table is executable until its full binary layout is assigned.
The assigned immediate ALU, tests, far control flow and immediate shifts/rotates
in Chapters 8–9 have advanced beyond this stage.

### Extension status and retained evidence

The retained [density evidence](ISA_EXAMPLES.md#code-density-evidence) supports
the extension directions below. Immediate ALU byte/word forms are assigned in Chapter 9; the remaining [encoding proposals](ISA_ENCODING.md#unassigned-encoding-proposals)
remain symbolic. The imm4 study sizes do not measure the assigned width-specific immediate forms.
Reserved descriptor bits remain illegal until a compact format is assigned.
Candidate primary families have no numeric allocation to decode.

| Form | Disposition and semantics | Constraints and conformance |
|---|---|---|
| rel8 branches | Approved extension direction: two bytes, all fifteen conditions, signed -128..127 from the selected instruction end | Retain rel16. Relax forward/backward branches iteratively; support forced short/long forms; test endpoints, cascading shrink, segment ends and execute faults. New opcode family remains unassigned. |
| B1 simm6 | Approved extension direction: a=0 descriptor `ddddddpp` carries signed -32..31; a=1 retains disp16 and zero upper descriptor bits | Existing zero-displacement bytes are identical. Test -32/31, long fallback -33/32, CS/DS selection, base/value aliases and atomic protection faults. Machine-readable metadata remains to be added. |
| SP uimm8 | Approved extension direction: three-byte byte/word load/store with unsigned 0..255 slot offset | Retain off16 and identical allocation/SS/protection checks. Test 0/255/256, negative rejection and failed-word-store atomicity. New family remains unassigned. |
| Immediate ALU `.B`/`.W` | Assigned above: six operations, 8-bit or 16-bit immediate and operation width; two or three bytes | `$80–$AF`; exact width-specific flags and byte-result zero-extension. |
| TST/TSTI `.B`/`.W` | Assigned: unary byte test, immediate byte/word tests; unary word alias | Twelve `$1A $40–$4B` tags; implemented in working mode. [Review evidence](ISA_EXAMPLES.md#test-instruction-review-and-measurements). |
| B1 post-increment | Deferred pending compiler/runtime evidence despite copy-loop savings | If adopted, update by width; reject a load whose destination is its base; stores sample the old source/base. Validate pointer adjustment before transfer, commit both together, preserve flags, and never permit segment wrapping or an unrepresentable final pointer. |

CALLF/RETF are assigned at `$54/$55` with a four-byte PC/CS return frame and
implemented in working mode. Their [contract](#far-call-and-return-contract),
[runnable example](ISA_EXAMPLES.md#far-call-and-near-helper), and [review completion](#review-feedback-and-completion) supersede the earlier unassigned proposal.

Immediate-count byte/word SHLI/SHRI/SARI/ROLI/RORI are assigned under
`$1A $60–$87`, with counts 0–16 and three-byte encodings. Their
[normative semantics](#immediate-count-shifts-and-rotates),
[executable example](ISA_EXAMPLES.md#immediate-shift-and-rotate-examples) and
[review completion](#review-feedback-and-completion) supersede the original proposal. Existing one-bit word shifts
retain their encodings and behavior.

General B1 ALU, multiply/divide, register-count shifts/rotates, through-carry
rotations, indirect CALL,
and wider pointer/stack immediates remain deferred. The existing B1 callee-save
ABI remains a software convention. Code relocation, compact forms and instruction
fusion can change fault PCs and interrupt observation points; matching successful
outputs does not establish identical fault/interrupt observation points.

## Appendix C. Conformance and draft completion

Assigned instructions must obey the single working-copy encoding map and ordered
execution rules. This appendix brings together review completion, current
implementation evidence and the acceptance requirements. Evidence counts describe
tests actually run; requirements continue to guide independent conformance audits.

### Review feedback and completion

The architecture review and all six implementation phases are complete. This
record consolidates their accepted decisions and completion criteria; the
instruction chapters and encoding payload remain authoritative. Historical
revision overlays and intermediate task lists no longer define a separate ISA.

| Completed phase | Review response and resulting behavior | Reference |
|---|---|---|
| 1. Architectural clarity and budget | Explicit byte zero-extension and width-specific flags; word-only register ALU; width-independent branches; retained 48 immediate opcodes and an allocation ledger | [Conventions](#byte-and-word-operations), [ledger](#opcode-allocation-ledger), [encoding budget](ISA_ENCODING.md#opcode-allocation-budget) |
| 2. Protected-kernel interfaces | Assigned process-map/descriptor controls, entry/bootstrap, extended context, privilege cause/vector 7, MOVU and the minimum PIC/timer contract | Chapters 3, 5–7 and 9 |
| 3. Metadata, emulator and conformance | Integrated all assignments in schema 3 and six decode tables; implemented protected execution, precise faults, devices, deterministic host input and artifact verification; added guest kernel integration | [Executed evidence](#executed-implementation-evidence), [metadata validation](ISA_ENCODING.md#validation-and-implementation-use), [kernel demo](ISA_EXAMPLES.md#executable-protected-kernel-demo) |
| 4. Non-destructive tests | Accepted B0 TST.B/TSTI.B/TSTI.W and OR-self TST.W alias after UART/kernel size and register-pressure comparison | [Instruction contract](#tst-tsti--test-a-register-without-writing-it), [review measurements](ISA_EXAMPLES.md#test-instruction-review-and-measurements) |
| 5. Intersegment calls | Assigned and implemented CALLF/RETF with atomic four-byte PC/CS frames, preserved near calls and protected target/stack checks | [Far-call contract](#far-call-and-return-contract), [runnable example](ISA_EXAMPLES.md#far-call-and-near-helper) |
| 6. Immediate-count shifts/rotates | Assigned and implemented SHLI/SHRI/SARI/ROLI/RORI at both widths, counts 0–16, count-zero preservation and precise full-width carry | [Instruction contract](#immediate-count-shifts-and-rotates), [worked results](ISA_EXAMPLES.md#immediate-shift-and-rotate-examples) |

Across these phases, existing assigned encodings, CSR bits, legacy context
indices and near-call layouts are preserved. Legal fetch/decode precedes
privilege and operand validation; failed instructions commit no ordinary effects.
MMIO retains pure preflight and one infallible byte/word transaction at commit;
the minimal-v1 devices specifically require aligned words. Legacy 0.4/0.5
remain explicit compatibility modes, with new forms gated to working mode.

Completion required reviewed binary interfaces, matching metadata/tools/runtime,
executed conformance and accurate coverage reporting, including runnable far-call,
shift and protected-kernel examples. The retained evidence below and in
[README.md](../README.md#validation) records the achieved coverage. Future
instructions longer than five bytes require the
[consumer-capacity audit](ISA_ENCODING.md#instruction-length-audit) before allocation.
General register-register byte ALU and further general-purpose expansion remain
deferred until measured need; UART/storage/input device contracts remain separate
work. See Appendix B for other unassigned candidates and their constraints.

### Implementation status and remaining work

The protected-kernel interfaces now have numeric specification assignments.
Implementation and remaining extension/peripheral work are tracked separately:

| Item | Defined here | Implementation / remaining work |
|---|---|---|
| Protected CPU controls | GETC/SETC bytes, selectors, descriptor/map commands, entry tuple, bootstrap, backing queries, and reset/staging rules assigned in Chapters 6/9 | Implemented and tested in working mode; retain independent architecture review |
| Extended saved context | Three-byte full-index forms, 21 additional words, read-only provenance, numeric fault enums, and ordered validation assigned | Implemented and tested in working mode |
| Privilege exceptions | Cause/vector 7, operation-class field/enum including MOVU USER_ACCESS, precise entry and clearing rules assigned | Implemented and tested in working mode |
| User-access transfers | MOVU `$27`, operand layouts, U-permission translation, atomic faults, metadata, and copy/retry/fixup ABI assigned | Implemented and tested in working mode |
| Interrupts and timer | `minimal-v1` MMIO `$FF0000–$FF00FF`, register/command/reset rules, sources 0–2, deterministic event/replay and idle interfaces assigned in Chapter 7 | Implemented and tested in working mode |
| Other devices | MMIO preflight/commit, transaction widths and ordering | Console/storage/input device contracts and their replay payloads |
| Compact candidates | Proposed behavior and compatibility constraints | Final adoption, numeric allocations and machine-readable metadata |

Tools and implementations must use assigned values explicitly, and cannot claim
complete conformance from the implemented immediate or legacy subset. Remaining
unassigned extensions/peripherals must not be allocated implicitly from table
order. The [encoding proposals](ISA_ENCODING.md#unassigned-encoding-proposals)
and conformance requirements do not grant executable candidate encodings.

### Executed implementation evidence

The reference working emulator and production toolchain now implement the assigned
protected interfaces. Release and ASan/UBSan runs pass three CTest suites:
749,338 retained CPU assertions, 3,173,471 protected checks, and twelve
parameterized Python toolchain tests. The requirements below define acceptance behavior,
not a claim that every combination in this document has been exhaustively proven.

| Executed area | Evidence |
|---|---|
| Immediate ALU | All 48 opcodes; all byte operand pairs; seeded boundary word values; arithmetic NZCV, logical CV preservation, I/E, CMPI preservation and signed/unsigned branches |
| Metadata/tools | Six disjoint maps compared with the normative ledger; schema 3 and five-byte capacity validation; new-family exact bytes, round trips, endpoints, selectors, reserved/truncated bytes and legacy isolation |
| Controls/maps | Every readable control in every GPR; committed/shadow readback, publication, invalid commit, interrupted staging, invalidation/reuse, map-0 constraints, backing query and entry commit |
| Context/privilege | All 42 reads and twenty editable words; ten privilege classes; illegal/truncated decode priority; depths 1–8 and overflow; bad IRET, nested repair and candidate rollback |
| Protection/MOVU | UR/UW and execute-only checks, map/selector 255, 65536 upper bound, U stack policy/far calls/MMIO denial; every structural transfer form/base/register alias; all eight memory reasons; exact second-byte addresses and atomic store/retry |
| Entry/diagnostics | Bad user stack with independent K entry, vector-read/format/handler/DS/stack failures, retained frames/registers/terminal records and reset RAM preservation |
| Devices | Thirty assigned starts, every reserved/byte/unaligned read and every word write direction, callback counts and next-PC atomicity; sixteen enable bits, shared-vector source priority, IRQ nesting and independent ACK |
| Time/replay | Snapshot timing, failed timer commit, D=T+1 expiry, periodic phase/overflow, UINT64 saturation, HALT wake/idle limits, transactional typed/strict JSONL errors and resets, independently checked SHA-256 artifact hashes |
| Kernel integration | Guest boot, first U return, four timer switches across maps 7/8, two nested copy fixups; deterministic full-state comparison at every boundary and identical final RAM |

The pinned demo does not supply production process lifecycle, software-generation
leases, allocator locks or a complete copy loop. Those remain kernel ABI policies;
the requirements below retain broader failure-injection and lifecycle cases for
future independent conformance audits. Additional devices and unassigned compact
forms are outside this profile.

### Architectural conformance requirements

- Verify assigned encodings, early rejection of reserved forms and the working-copy primary map.
- Test privilege checks and exact fault priority in U/K modes.
- Verify two isolated processes at identical virtual selectors/offsets, U/K permissions, bounds, backing holes, and atomic failed stores.
- Verify U→SWI→K user-access fault→nested handler→fixup→U return, including invalid user stacks.
- Test return-image validation, frame provenance, depth 1–8, failed-entry stages, overflow, and retained terminal diagnostics.
- Test published-map invalidation, stale-handle software policy, scheduler prevalidation, interrupted replacement and rollback.
- Verify PIC arbitration, independent ACK, nested IRQs, deterministic timer deadlines, idle advancement, and reset behavior.
- Verify MMIO preflight/commit ordering, single word transactions, denied U access, and replay behavior.

#### Conformance: Protected CPU controls and map publication

These requirements cover the assigned [control interface](ISA.md#protected-control-interface)
and [GETC/SETC encodings](ISA.md#protected-control-encoding). They describe required
acceptance coverage; executed evidence is recorded below and in README.md.
For fault cases, install valid entry/recovery state so instruction failure can
be observed independently of a failed exception entry.

- Cover all sixteen secondary tags `$A0–$A7`/`$B0–$B7` after `$1F`, all eight
  GPRs, every selector, direction permission, command value, and exact reset word.
  Existing context tags, CSR bits, primary bytes, and supported legacy modes
  retain their contracts; reject these new tags in modes that do not enable them.
- Decode `1F A2 00`, `1F B7 01`, and `1F B0 7F` exactly. Reject reserved
  neighboring secondary tags before selector fetch, unknown selectors and wrong
  access directions at selector fetch, and invalid command values after legal
  fetch/privilege. Test every missing byte and final-PC boundary.
- Legal U GETC/SETC raises Privilege before operand-state checks; an illegal
  selector/direction remains Illegal Instruction before privilege. K writes with
  noncanonical map/frame/segment/high-address bytes, flags, or upper_hi raise
  Invalid State without truncation. All successful transfers/commands preserve
  N/Z/C/V/I/E; failed instructions commit no ordinary effects.
- Cover committed versus shadow reads, every field combination boundary,
  selection changes, DESC_LOAD, and atomic whole-descriptor replacement in both
  unpublished and published maps. A later instruction must see the entire old
  or entire new descriptor, never a mixed permission/frame/bound image.
- Cover 17-bit bounds 0, 65535, and 65536, including legal empty intervals;
  reject upper_hi=1 with nonzero low word, lower>upper, noncanonical absent
  images, stack bounds outside descriptor limits, and nonzero stack policy
  without STACK. Stack ST stays at most 65535. Commit may map a backing hole;
  the later transfer, not descriptor commit, reports BACKING.
- Verify map-0 publication is always 1, map-0 publish/invalidate are Invalid
  State, and map-0 descriptors reject U rights. MMIO descriptors reject U rights,
  execute rights, and STACK. A RAM mapping of actual MMIO never permits a fetch,
  stack transfer, or callback through a false RAM classification.
- Verify empty-map publication, repeated publication, unpublished-map readback,
  repeated invalidation, and invalidation of all 256 descriptors plus selected
  shadow words. Republish alone grants nothing. Live/saved numeric map references
  remain unchanged and are rejected on later use/return where required.
- Check reset identity fields/flags for RAM, MMIO, mixed-kind, and all-hole frames;
  query results must agree with actual byte classification, not the descriptor's
  claimed kind. Preserve RAM contents, clear user maps, shadows, active entry,
  query results, and entry_configured. Verify invalid reset vector/target handling.
- ENTRY_COMMIT preserves live stack/DS, installs all five words together, and
  checks numeric stack/next-push room before DS then SS probes. Check DS presence,
  K data rights, and RAM; SS READ then WRITE probes in increasing byte order;
  missing next-push/allocated backing; old active tuple on failure; and mapping
  changes after a successful commit. ENTRY_LOAD restores only staging.
- PREPARE_USER_RETURN rejects U execution, I=1, depth 0 or 2–8, U-origin or already
  marked frames, and unconfigured entry. Verify complete U-image validation
  precedes active entry revalidation, marker atomicity, repeated-prepare rejection,
  unchanged live state, and fresh validation by a later IRET after saved edits.
- BACKING_QUERY returns HOLE=0/RAM=1/MMIO=2 and the exact maximal positive run,
  including final physical byte and a `$01000000` full-space run. Queries invoke
  no MMIO callback and latch all result words atomically. Failed next-PC checks
  preserve the old result. Test restart after a fault-handler query replaces a
  partially read result; no automatic frame restoration of scratch is assumed.
- Interrupt a partial descriptor/entry staging sequence; let a nested handler
  use the interface, preserve/restore writable scratch, then verify the outer
  commit uses its intended complete shadow. Separately exercise the software map
  lifetime/lease, frame initialization, and scheduler rollback policies; no
  control command supplies hardware generations or sequence-wide atomicity.
- Run the [first-user-return example](ISA.md#protected-control-setup-examples)
  using only assigned guest controls and legacy SETX fields, then verify isolation
  between two maps with identical virtual selectors and different backing frames.

| Case | Input / setup | Required outcome |
|---|---|---|
| Reserved control tag | `1F A8` at the segment end | Illegal Instruction without selector fetch |
| Missing selector | `1F A0` with next byte absent | Fetch Memory Access at instruction start |
| Write-only selector read | `1F A0 7F`, in either mode | Illegal Instruction before privilege |
| Read-only selector write | `1F B0 03`, in either mode | Illegal Instruction; mode unchanged |
| Bad command data | `1F B0 7F`, r0=0 in K | Invalid State; every control/staging/latch unchanged before entry |
| Full legal fetch, invalid next | `1F A0 00` at `$FFFD` | Memory Access; destination unchanged |
| Full segment bound | lower=0, upper_lo=0, upper_hi=1 | Commit allowed; descriptor bound is 65536 |
| Bad 17-bit bound | upper_lo=1, upper_hi=1 | DESC_COMMIT Invalid State; old descriptor and shadow retained |
| Full descriptor sample | map=7, segment=3, frame=`$23`, flags=`$19`, bounds `[0,65536)`, sb=st=0 | Committed readback matches all fields; U read/write translates to physical frame `$23` |
| Invalidation then republish | Commit a descriptor, publish, invalidate, publish | Published map contains only canonical absent descriptors; no old grant revives |
| Entry next-push failure | entry SB=SP=ST=`$1000` | Stack Boundary before DS/SS probes; active tuple unchanged |
| Query final address | physical `$FFFFFF` | run=1; no physical wrap or device read |

#### Conformance: Extended saved context and privilege exceptions

The [extended context words/enums](ISA.md#extended-saved-context),
[binary forms](ISA.md#extended-context-encoding), and
[Privilege record](ISA.md#privilege-exception-record) are assigned requirements.
These interfaces are implemented in working mode. USER_ACCESS class 10 applies
to MOVU; toolchain and protected CPU suites exercise its decode/privilege ordering.

- Cover all eight read tags `$80–$87` and eight write tags `$90–$97` after `$1F`.
  Test 21 readable extended indices and three writable indices with every GPR:
  168 legal read combinations and 24 legal write combinations. Reject the 235
  other read indices and 253 other write indices for every tag. Legacy indices
  under extended tags must not become aliases; existing 21 legacy words/bytes
  and read-only legacy SETX rejection remain unchanged.
- Verify all 42 readable words, the twenty editable words, canonical source
  capture, and full-word readback after temporary invalid saved-map/user-access
  edits. SETX never updates live controls and saved xuaccess_map=0 does not clear
  saved xuaccess_segment. IRET enforces its existing scalar/map/mode/stack/target
  order, leaves an invalid image unchanged, and raises a distinct nested fault.
- Attempt writes to all eighteen read-only extended fields, including origin,
  return mode, prior/frame depth, bootstrap, IRQ source, privilege class, and all
  memory words. Illegal Instruction must precede privilege/E=0/state checks.
  Valid U accesses are Privilege before inactive-context or saved-value checks.
  Reserved secondary fields fail before selector fetch; legal missing selectors
  are fetch faults. Test every truncated position and segment/next-PC boundary.
- Check all numeric origin, phase, access, and reason discriminants, including
  zero NONE, upper reserved bits, width 0/1/2, signed widened offsets, and both
  address-valid layouts. Preserve legacy physical xaddr on memory/nonmemory
  faults. Distinguish USER return-target/selection faults from KERNEL fetch faults
  of the instruction performing IRET/prepare, and UACCESS operand faults from
  KERNEL next-PC faults of that transfer.
- Seed dirty lower/previous-frame metadata; generate each event and prove the new
  frame initializes independently. Nonmemory causes clear all eleven `$30–$3A`
  words, while applicable legacy xaddr is retained. xpriv_class is nonzero only
  for cause 7; xirq_source is meaningful only for cause 5; xtrap only for cause 6.
  Source 0 and service 0 must be distinguishable from non-IRQ/non-SWI events.
- Exercise depths 1–8 and restoration: xframe_depth=1–8, xprior_depth=0–7, correct
  captured U/K origin, derived return mode, top-only access, and no link edits.
  Bootstrap changes only its marker/derived return mode, not origin/linkage.
  Failed prepare leaves provenance unchanged; failed IRET retains the rejected
  frame; overflow/failed entry retain host diagnostics with no guest emergency
  interface. Reset discards frames and E; inactive reads fault rather than expose
  an artificial zero frame or stale metadata.
- Install vector 7 at kernel selector 0 offsets `$001C–$001F` and verify cause 7
  leaves vectors/causes 0–6 intact. From U, exercise every assigned privileged
  operation and alias, checking exact instruction-start PC/CS, canonical saved
  I=1, physical legacy xaddr, correct operation class, and no ordinary effects.
  Cover classes 1–10, including MOVU user access. A missing
  operand or reserved encoding must win over privilege, with class zero.
- Check control/command data cannot select the privilege class: SETC command
  with invalid data remains CONTROL_WRITE in U. Saved flag/map values do not
  turn legal U context access into Invalid State before Privilege. All successful
  extended transfers preserve live N/Z/C/V/I/E.

| Case | Input / event | Required record or behavior |
|---|---|---|
| Saved map read | `1F 82 20`, top xprocess_map=7 | r2=7; frame and flags unchanged |
| Saved selector edit | `1F 97 22`, r7=`$1234` in K/E=1 | Store entire `$1234`; later IRET rejects noncanonical high byte |
| Provenance write | `1F 90 23`, in either mode/depth | Illegal Instruction before privilege/E checks |
| No legacy alias | `1F 80 14` | Illegal Instruction; legacy GETX xtrap remains `1F 34 00` |
| Reserved second byte | `1F 88` at `$FFFE` | Illegal Instruction without fetching a selector across the segment |
| Missing extended index | `1F 80` with index unavailable in U | USER/FETCH Memory Access, xpriv_class=0 |
| Legal U GETX | `1F 80 29`, no active frame before execution | Cause 7, CONTEXT_READ=5; xpc identifies this GETX |
| Legal U SETX | `1F 90 20`, source=`$FFFF` | Cause 7, CONTEXT_WRITE=6, before saved-map validity checks |
| Legal U CSR write | `10 93`, source has reserved bits | Cause 7, CSR_WRITE=1, before scalar validation |
| Legal U DI/EI | `02` / `03`, or CLI/SEI aliases | Cause 7, INTERRUPT_CONTROL=2; U I remains 1 before entry |
| Legal U HALT | `01` | Cause 7, HALT=3; no halted-state commit |
| Legal U stack-limit write | MOV to SB or ST | Cause 7, STACK_LIMIT_WRITE=4 |
| Legal U IRET | `1F 12`, no active frame | Cause 7, EXCEPTION_RETURN=7 |
| Legal U GETC | `1F A0 00` | Cause 7, CONTROL_READ=8 |
| Legal U SETC command | `1F B0 7F`, source=`$FFFF` | Cause 7, CONTROL_WRITE=9; command data not validated first |
| Inactive K GETX at invalid next | `1F 80 20` at `$FFFD`, E=0 | Invalid State wins over next-PC Memory Access |
| Prepared bootstrap frame | K-origin depth-1 frame before/after successful prepare | xorigin_mode stays 1, xreturn_mode 1→0, xbootstrap 0→1, prior/frame depth stay 0/1 |
| Negative widened start | Offset -1 during explicit access | xoffsetlo/hi=`$FFFF/$FFFF`, both address pairs invalid/zero, OFFSET_RANGE=1 |
| Positive widened overflow | Offset +65536 | xoffsetlo/hi=`$0000/$0001`, both address pairs invalid/zero, OFFSET_RANGE=1 |
| Missing second backing byte | UACCESS word at map 7, segment 3, frame `$23`, start `$FFFE`; `$FFFF` absent | origin=3, phase=2, access=2 or 3, reason=8, width=2; attempted offset `$0000FFFE`; virtual low/high words `$FFFF/$0103`, physical low/high words `$FFFF/$0123` |
| Nonmemory replacement | IRQ/SWI/Privilege/Illegal/Invalid State/Stack Boundary after a memory event | New `$30–$3A` all zero, new event-specific fields initialized; lower frame unchanged |

#### Conformance: Explicit user-access transfers

The assigned [MOVU forms](ISA.md#user-access-encoding) and
[copy/recovery ABI example](ISA.md#user-buffer-copy-and-recovery-abi) require the
following checks. These are specification requirements, not current execution
coverage; the production modes still reject `$27` at the primary byte.

- Enumerate all 256 second bytes: exactly 64 select MOVU forms and 192 reject
  before a base fetch. For each legal mode, accept bases 0–3 and reject the 252
  other base bytes before displacement fetch. Cover 128 short and 128 displaced
  structural forms, both widths/directions, all eight value registers, all B1
  bases, and signed displacement endpoints -32768/32767.
- Verify exact published bytes, source/destination order, unsuffixed word,
  default/zero displacement shortening, and valid raw long-zero canonicalization.
  Reject out-of-range displacement literals, internal/DP/SP/absolute/explicit
  segment operands, immediates, register-register/memory-memory source, bad widths,
  and wrong operand counts. No hidden GPR temporary or pointer update is allowed.
- Fully decoded U execution is Privilege cause 7/class 10 before disabled/map/
  offset checks. Reserved/missing bytes retain decode/fetch priority and use the
  actual execution map's origin (USER for a U instruction fetch). Legal K fetch
  stays in map 0 even when process_map, DS/CS/SS, and user-access map/segment differ.
- Use separate maps 7/8 with identical virtual addresses and distinct physical
  frames. MOVU must use uaccess_map/segment under U rights, independent of K rights
  and live process_map. Map zero is disabled; unpublished maps are rejected on
  use; selector configuration itself does not dereference the target.
- Byte loads clear the destination's upper byte, including B1 destinations; byte
  stores use only the original low byte. Words are little-endian and unaligned.
  Test base/load-destination and base/store-source aliases using original values.
  Preserve all other registers, controls, flags, and segments on success.
- Verify the exact fault sequence: offset/width, disabled/published map,
  presence, bounds, all U rights, device kind, increasing-byte backing, then
  following-PC. Exercise simultaneous failures at every adjacent priority step.
  Check complete saved attempted offset/width, relevant virtual/physical address,
  enum values and validity bits, including second-byte backing failures.
- All MMIO denial paths must produce zero callbacks and no device side effects.
  A legal MMIO descriptor lacks U rights, so RIGHTS can win before DEVICE; a
  U-readable RAM descriptor naming actual MMIO must fail DEVICE. Do not fabricate
  an unpublishable MMIO descriptor with U grants merely to test a later check.
- Check final byte `$FFFF`, last word `$FFFE`, rejected crossing word `$FFFF`,
  negative/too-large starts, and short/long code-segment endpoints. No register,
  flag, pointer, or store byte commits on fetch/operand/next-PC failure. In legacy
  modes `$27` remains illegal without fetching a descriptor.
- Run the published copy example at CS=2/PC=`$1000`; verify copy_byte=`$1022`,
  copied=`$103D`, bad_user_buffer=`$104F`, and registry entries generated from
  exact emitted layout. Counts 0/1/255 succeed as appropriate; 256 rejects before
  user access. Traverse the last user byte without post-increment overflow;
  crossing ranges return the explicit software error without wrapping.
  Input segment r1=5 is independent of saved DS=3; a missing arbitrary target
  selector must not be confused with an invalid caller return descriptor.
- Execute U→SWI→K MOVU fault→depth-2 handler→retry/fixup→K→U. On retry, publish
  a permitted complete descriptor pointing to initialized installed RAM, keep
  the lower frame unchanged, restore saved K controls via IRET, and retranslate
  the instruction start. Do not change immutable backing topology as a repair.
- On fixup, match exact PC/CS/cause/origin/phase/access/width/reason plus lease
  policy. Edit only the top K resume PC and permitted control words; clear both
  saved ua words for an abort that must return despite invalid target-map state.
  Test a distinct unpublished target map 8 while lower U process map 7 remains
  valid. Invalidating the lower process map must still prevent its U return;
  fixup is not a way to bypass map lifetime or complete IRET validation.
- Confirm memory handler faults, K buffer-store faults, unmatched PCs/reasons,
  and unexpected KERNEL-origin failures follow kernel bug policy rather than
  automatic pointer-error conversion. Previously copied RAM bytes remain committed
  on a later failure; no whole-copy rollback or implicit errno/skip exists.

| Case | Operation / setup | Required outcome |
|---|---|---|
| Byte load | `27 20 00`, r4=`$2000`, selected user byte=`$80`, r0=`$FFFF` | r0=`$0080`; r4 and all flags unchanged |
| Negative displaced store | `27 C4 01 FE FF`, r5=`$2002`, r1=`$1234` | User `$2000/$2001` become `34 12`; pointers/flags unchanged |
| Base-load alias | `27 30 00`, r4=`$12FF`, selected byte=`$80` | Read old `$12FF`, then r4=`$0080` |
| Base-store alias | `27 90 00`, r4=`$1234` | Store `34 12` at old `$1234`; r4 unchanged |
| Illegal descriptor | `27 01` at the code-segment end | Illegal Instruction before base fetch |
| Illegal base before displacement | `27 40 04` with displacement bytes unavailable | Illegal Instruction before displacement fetch |
| Missing displacement byte | `27 40 00 FE` with next byte absent | Fetch Memory Access before privilege/data checks |
| Privilege before disabled state | U executes `27 20 00`, disabled ua controls | Cause 7/class 10, no data transfer |
| Offset before disabled state | K word at base `$FFFF`, disabled ua controls | OFFSET_RANGE=1; valid virtual start, invalid physical address |
| Disabled map | K byte transfer with ua map=0 and a valid offset | ACCESS_DISABLED=2; no descriptor resolution or physical address |
| Invalid map | ua map=8 unpublished, lower process map 7 valid | MAP_INVALID=3; nested fixup can disable saved controls and return to valid process 7 |
| Direction rights | Selected descriptor U R only | Load succeeds; store fails RIGHTS=6 without changing either byte |
| Last valid word | ua word at `$FFFE`, both bytes installed | Success without segment carry |
| Missing second byte | ua word at `$FFFE`, second byte uninstalled | BACKING=8; relevant offset `$FFFF`, original start/width retained; first store byte unchanged |
| Short/long final next | Valid short at `$FFFD` / long at `$FFFB` | KERNEL/RESULT_PC/FETCH Memory Access; no ordinary effects |
| Operand and next both invalid | Same code position with ua map=0, valid offset | UACCESS/OPERAND/ACCESS_DISABLED wins over resulting-PC failure |

#### Conformance: PIC, timer and replay

These checks cover the assigned [minimal-v1 machine profile](ISA.md#minimum-machine-mmio-profile)
and [host/replay contract](ISA.md#deterministic-host-events-and-replay). They are
acceptance requirements for the implemented Phase 3 machine.
Install valid K vectors/entry and pinned recovery code to distinguish a device
fault from a subsequent failed exception entry.

- Verify the exact 256-byte MMIO window `$FF0000–$FF00FF`, holes through
  `$FFFFFF`, reset map-0 selector `$FF` flags `$00C3`, and all thirty assigned
  aligned word register starts. Test every reserved start, byte/odd/spanning
  access, direction, reserved write value and status bit. Reserved bytes inside
  the installed window report DEVICE, rather than BACKING. Check exact
  Memory Access origin/phase/access/address/width metadata and earlier
  rights/kind/backing failures. Ordinary U MOV is a memory fault; U MOVU is
  Privilege/class 10, and K MOVU cannot access the device.
- Instrument pure preflight versus commit. Successful word accesses cause one
  transaction; failed operand preflight or selected next-PC validation causes
  no irreversible callback, partial word, register/flag write, shadow/latch
  change, or retirement tick. An invalid timer bundle wins over a simultaneous
  invalid next PC. Boundary input after a fault is an independent event.
- Check all sixteen PIC enable/ACK bits, reset words and source mask `$0007`.
  Slots 3–15 accept enable storage but never generate requests. Reads are
  side-effect-free; wrong-direction writes fault. Repeated edges coalesce,
  pending survives masking/in-service, lowest eligible source wins vector 5,
  and xirq_source records 0/1/2 correctly. Entry alone clears selected pending
  and sets in-service; failed entry leaves both unchanged. Cover nested IRQs,
  I gating, HALT wakeup, ACK of inactive bits, and IRET without ACK.
- Exercise source 2 asserted/deasserted at one boundary and across entry/ACK.
  Resampling an asserted in-service level can leave one final sticky request
  after deassertion. ACK clears only in-service, never pending or the level.
  A timer/new edge at the boundary after ACK remains eligible. Neither
  DISABLE_TIMER nor successful timer reconfiguration withdraws pending work.
- SNAPSHOT_TIME captures time before its own retirement. Four words remain
  stable across subsequent instructions and reads until the next snapshot;
  reset returns zero/invalid. Check little-endian word order through all 64
  bits, shared-latch clobber/restart, and preservation of staged words across
  nested kernel use. Active readback must never substitute shadow fields.
- Exercise every timer command and legal mode 0/1/3; reject mode 2 and all
  unknown command/mode values. Disabled commit requires D=P=0; one-shot requires
  D>T and P=0; periodic requires D>T and P>0. Check exact validation order,
  all word endpoints, and full old active/shadow/snapshot/status preservation
  on failure. Success installs one complete bundle and retains shadows,
  pending, in-service and sticky overflow. DISABLE retains last active D/P;
  disabled commit clears them. Clearing overflow changes only status bit 2.
- Use the deadline vectors below for retirement and idle advancement. Periodic
  arithmetic must use sufficient precision for subtraction, k and multiplication,
  coalesce one pending bit, retain phase and detect overflow without wrapping.
  Expiry at maximum time is evaluated before timer disable; time saturates,
  while later instructions still retire. Time-exhausted is derived and cannot
  be guest-cleared; deadline-overflow is independently sticky. Neither status
  condition generates a separate interrupt.
- Track B separately from time: committed ordinary/MMIO/HALT/SWI/IRET instructions
  each advance both once; synchronous fault/terminal-entry and hardware IRQ
  transitions advance B only. Failed SWI entry has no retirement; halted/terminal
  no-progress polls advance neither. Reset begins B=0/T=0. No wall-clock input
  changes time; only an accepted idle_advance can jump it without retirement.
- Validate JSONL header/schema, exact keys/types (including duplicate keys),
  canonical decimal strings, run IDs and consecutive sequence numbers, full
  physical RAM/backing artifact lengths/tags and SHA-256 binding. Reject malformed
  or unsupported profiles before execution. Check error codes 1–7 and specified
  precedence; externally injecting timer source 0 or slots 3–15 is BAD_SOURCE.
  Validate batches against staged state and roll back everything, including
  sequence, if a later record fails. Empty batches are no-ops. Reset must be
  alone; a terminal machine accepts only reset.
- Idle advancement requires halted/nonterminal state and no eligible IRQ;
  non-increasing/out-of-range targets or jumps past a wake-enabled timer
  deadline are rejected. Advancing exactly to that deadline sets pending but
  does not enter an IRQ, retire, or increment B. The next step enters normally.
  Masked/in-service periods may be crossed with coalescing; I=0 may remain
  halted. Cover multiple batches at one halted B, no-progress polls, sequence
  continuation and deterministic final-level sampling before arbitration.
- Reset clears all PIC/timer/shadow/snapshot/line/frame/terminal state, cancels
  queued old-run input, preserves RAM, and verifies the next run header against
  that RAM/backing before new events. Invalid boot state produces the normal
  TerminalResetFault in the new run. Replay the same artifacts/events twice
  and compare registers, memory, frames, PIC, timer, B/time and terminal state
  at each transition and accepted batch, including faults and idle wakeup.
- Assemble the published timer setup/ACK example with existing MOV encodings;
  after HALT advance to 256, enter vector 5/source 0, ACK, and IRET to after_idle
  with DS restored. This execution test requires the future device implementation.

Here M denotes `$FFFFFFFFFFFFFFFF`. Each vector assumes an initially clear
source-0 pending bit and status unless explicitly stated.

| Fixture / operation | Required result |
|---|---|
| Commit one-shot D=T+1, P=0 | Commit succeeds; its retirement sets pending 0, active mode becomes 0 |
| Periodic D=10, P=3; evaluate at T=20 | One pending bit, next D=22, not 23 |
| Periodic D=M-2, P=3; evaluate at T=M-2 | Pending 0 set, active mode 0, D/P retained, deadline-overflow bit 2 set |
| One-shot D=M; tick from M-1 to M | Due request set, mode 0, time-exhausted bit 1 set; no arithmetic overflow bit solely from exhaustion |
| Read status after time exhaustion; no snapshot/periodic overflow | Value `$0002`; next successful instruction leaves time M |
| Snapshot at T=$0001000200030004 | Latch words `$0004,$0003,$0002,$0001`; time becomes T+1 |
| ACK bit 0 while another timer edge becomes due on its retirement | In-service 0 cleared; pending 0 retained/set for next arbitration |
| Halted T=20, I=1, enable0=1, not in-service, D=30 | idle target 31 fails BAD_TIME=6 unchanged; target 30 succeeds and next step takes IRQ |

#### Conformance: Far calls and returns

The [CALLF/RETF contract](ISA.md#far-call-and-return-contract) is assigned and
implemented for the RAM-based working subset. Legacy modes reject `$54/$55`
before operand fetch. Protected-mode permission/MMIO cases are covered by the additional protected suite;
this checklist continues to define the full acceptance scope.

- Check exact bytes `CALLF $010200 = 54 00 02 01 00`, `RETF = 55` and unchanged
  near CALL/RET/JL bytes. Cover target 0/$FFFF/$010000/$FFFFFF, flat-label sizing,
  all truncated lengths, default legacy rejection, disassembly round trips,
  unsupported suffixes, extra/missing operands, indirect/register forms, negative
  or oversized targets. Nonzero packed high bytes are post-fetch Invalid State,
  not early Illegal Instruction; a missing operand still wins before that check.
- With SS=2, SB=$1000, SP=ST=$1004, old CS=0, PC=$0100, successful CALLF writes
  `05 01 00 00` at `$021000`, changes SP to $1000 and target CS:PC together.
  RETF restores CS=0/PC=$0105/SP=$1004, leaving the bytes intact. Check both
  code-segment endpoints 0/$FF, every NZCV/I state, E with active context, all
  other registers, exception frames and map/mode association unchanged.
- Cover same-segment calls, cross-segment calls, recursive four-byte frames,
  near CALL inside a far entry and far CALLF inside a near entry. Match each
  return to its frame; no hardware frame-type tag or mismatched-return guarantee
  is specified. Check additional-argument base +2 near / +4 far and release of
  callee locals before return. Run the published caller/callee images with
  eight retired instructions, r0=6, final PC=$010E, SP=$FFFE, minimum SP=$FFF8.
- Exercise invalid SB/SP/ST configurations, underflow, insufficient push/pop
  room, partial allocation, odd frames and ST=$FFFF. The last stack byte is
  $FFFE. Missing each of the four frame bytes fails before any write/pop. Probe
  return-PC then saved-CS as separate width-2 RAM operands in increasing order;
  a hole at SS:$1003 reports attempted $1002/width 2 and relevant $1003.
- Try every nonzero reserved packed-target high byte and every nonzero saved-CS
  high byte. CALLF format validation precedes stack bounds. RETF probes both
  words before saved-CS validation; unavailable frame bytes win over malformed
  saved CS, which wins over target failures. No CS truncation/masking is allowed.
- CALLF at $FFFA saves the largest next=$FFFF and succeeds with a valid frame
  and target. At $FFFB, complete fetch followed by valid operand probes fails
  next=$10000 without touching the frame, before target probing; operand faults
  retain earlier priority. Raw CALLF at $FFFC–$FFFF fails instruction fetch
  without segment carry. RETF at $FFFF can succeed because next is unused.
- Target probes check only the first executable RAM byte, without decoding it.
  Target holes fail before any frame commit/pop; an installed illegal target
  opcode faults on the following step with the completed call retained. Verify
  all ordinary state and frame bytes unchanged on failure, and precise saved
  instruction PC/CS/SP/flags apart from normal exception entry effects.
- IRQ before CALLF sees the old unallocated frame; IRQ after CALLF sees all four
  committed bytes and the new CS:PC/SP. IRQ after RETF sees the complete pop and
  original caller CS:PC. IRET restores the appropriate boundary for resumption.
  Far calls/returns within a handler preserve its saved exception frame and E.
- On the protected runtime, run in both U and K with identical selectors
  resolving to different maps. Enforce U/K X rights on targets and corresponding
  stack R/W rights/bounds; reject nonpresent, nonexecutable, MMIO or missing
  target/stack backing. Frame faults use OPERAND READ/WRITE width 2; target
  faults use CONTROL_TARGET FETCH width 1, with origin/map from current mode.
  Neither far form changes mode, process_map, user-access state, IRQ policy or
  exception depth; a U far transfer to a K-only segment is denied. No failed
  frame probe or target check may invoke a device callback.

#### Conformance: TST and TSTI

The [assigned forms](ISA.md#tst-tsti--test-a-register-without-writing-it)
and [review](ISA_EXAMPLES.md#test-instruction-review-and-measurements) have implemented subset coverage in explicit
`working` mode. TST.W is an OR-self alias in all modes; the twelve new `$1A`
tags require working mode and are rejected in legacy modes before immediate
fetch. Protected U/K execution is enabled in working mode.

- Cover all twelve tags `$40–$4B`, r0–r3, both immediate widths, unary/default
  word aliases, canonical disassembly, negative literal aliases and endpoints.
  Reject other banks, memory, suffixes, arities, out-of-range literals and
  adjacent reserved tags. Schema/runtime gating must agree between Python and
  generated C++ tables; legacy instruction bytes and runtime behavior remain.
- Verify every unary input word, all byte value/mask pairs, word boundary and
  zero/all-ones/sign-only masks, all incoming NZCV/I states, and active-frame E.
  All sixteen register bits remain unchanged; N/Z use the temporary's selected
  width and C/V/I/E remain unchanged. Include `$FF00` as byte zero/word negative,
  `$1280 & $80` as byte negative/word positive, and nonzero upper-byte inputs.
- Verify BEQ/BNE/BMI/BPL after tests and all other predicates with deliberately
  retained C/V. Tests cannot manufacture fresh comparison carry/overflow.
  No implicit MMIO read, pointer update, memory write or frame edit occurs.
- Missing instruction/operand bytes and an unrepresentable following PC must
  leave registers and saved flags unchanged before ordinary exception entry.
  Cover every truncation of every new tag, final valid next PC `$FFFF`, the
  entire second-byte map, and legacy early rejection before operand reads.
- Check the measured one-sample RX/TX sequences and a read-to-consume model
  returning `$03` then `$00`; two tests of the snapshot preserve both flags,
  while a second device read consumes a different sample. This is a symbolic
  device fixture, not runtime UART/MMIO conformance.

#### Conformance: Immediate ALU

The assigned [immediate ALU forms](ISA.md#immediate-alu-instructions)
require these implementation checks; this list specifies requirements, not current implementation coverage:

- Cover all 48 primary opcodes $80–$AF, all four B0 destinations and both widths,
  in U/K modes. Reject B1/internal destinations, unsupported suffixes and operand
  counts. Unsuffixed source selects `.W`; there is no descriptor byte.
- Accept byte literals -128..255 and word literals -32768..65535; reject values
  outside those intervals. Verify negative/hex literal aliases and little-endian
  word bytes, including `86 34 12` for ADDI.W #$1234,r2.
- Check eight-bit operands/results/N/Z/C/V for `.B` despite nonzero original
  upper bytes. Every writing byte operation clears the upper byte; CMPI.B
  preserves all sixteen destination bits. Word arithmetic/logic flags match
  corresponding register operations. All forms preserve I/E; logic preserves CV.
- Check the boundary vectors below, aliases, zero masks and all-ones masks.
  Byte/word widths cannot be substituted solely because an immediate fits.
- Check missing first/second immediate bytes, code-segment boundaries and
  representable next-PC checks. No destination/flag effects commit on failure.
  An assigned immediate opcode with a missing operand produces a fetch fault;
  unassigned primary opcodes are rejected without fetching an operand.

Flags below show NZCV only; ADDI/SUBI/CMPI results ignore input NZCV, while logic
retains the stated input C/V. Higher original register bytes deliberately differ.

| Instruction | Original destination | Result destination | NZCV |
|---|---|---|---|
| ADDI.B #1,r0 | $12FF | $0000 | $5: Z,C |
| ADDI.B #1,r0 | $127F | $0080 | $A: N,V |
| SUBI.B #1,r0 | $1200 | $00FF | $2: N |
| SUBI.B #1,r0 | $1280 | $007F | $C: C,V |
| CMPI.B #1,r0 | $1280 | $1280, unchanged | $C: C,V |
| ANDI.B #$7F,r0 (input CV=$C) | $FF80 | $0000 | $D: Z,C,V |
| ORI.B #$80,r0 (input CV=$C) | $1200 | $0080 | $E: N,C,V |
| XORI.B #$FF,r0 (input CV=$C) | $12FF | $0000 | $D: Z,C,V |
| ADDI.W #1,r0 | $FFFF | $0000 | $5: Z,C |
| ADDI.W #1,r0 | $7FFF | $8000 | $A: N,V |
| SUBI.W #1,r0 | $0000 | $FFFF | $2: N |
| SUBI.W #1,r0 | $8000 | $7FFF | $C: C,V |

#### Conformance: Immediate-count shifts and rotates

The [assigned operations](ISA.md#immediate-count-shifts-and-rotates) use forty
tags `$1A $60–$87`, three bytes each, in working mode. These have real RAM-based
emulator/tool coverage alongside the implemented protected execution and time/PIC machinery. The vectors below are now implementation requirements and tests.

- Cover SHLI, SHRI, SARI, ROLI, and RORI at byte/word widths, all four B0
  destinations, and every count 0–16. Compare against an exact repeated-one-bit
  reference and independent boundary results; do not implement a count by
  masking to four bits. Counts 8 and 16 must remain distinguishable from zero.
- Count zero preserves all sixteen register bits and every flag. Positive byte
  counts zero-extend results; N/Z use the selected width and C is the final
  displaced bit. Preserve V/I/E. Input C does not feed true rotates, and a
  full-width positive rotate still updates N/Z/C.
- Verify byte shifts beyond eight positions: logical results/C become zero
  after nine or more steps; arithmetic right shift retains sign-fill and its
  final displaced sign bit. Avoid undefined host-language oversized shifts.
- Reject negative/out-of-range assembly counts, B1/internal destinations,
  unsupported suffixes, and missing/non-immediate count operands. Every encoded
  count 17–255 raises Invalid State after full fetch, before resulting-PC checks;
  check all 9,560 invalid streams. Cover all truncated positions,
  final-PC failures, compatibility modes, and precise unchanged state on fault.
- One immediate-count operation retires once and has one atomic commit; internal
  bit steps do not permit IRQ entry or expose intermediate values. Preserve all
  existing two-byte, single-bit word shift encodings and their flags.
- Cover all 680 legal tag/count streams with exact bytes and source/disassembly
  round trips. Unsuffixed operations use word width. Schema/runtime gating must
  preserve legacy 0.4/0.5 rejection at the secondary byte before count fetch;
  reserved neighbors `$5F`/`$88` are illegal before operand fetch in all modes.
- Test count zero and count sixteen at PC=$FFFD: a valid count gives Memory
  Access without writes, while raw count seventeen gives earlier Invalid State.
  At $FFFC, a valid three-byte instruction finishes at $FFFF; at $FFFE the count
  fetch crosses the segment and fails. Active frame/provenance/E must survive a
  successful shift; failed instructions save original registers and flags.
- Exhaust all byte inputs with both input C values and all counts/operations.
  Use a repeated-step bit-lane model across all widths/destinations/incoming
  flags and boundary inputs, plus a closed-form seeded word oracle and the
  independent numeric vectors below. The 89-byte published program must halt
  after 26 instructions at $0159 with all nine result words and final CSR=$0006.

| Assigned operation | Original destination | Result destination | N/Z/C |
|---|---|---|---|
| SHRI.W #8,r0 | `$12AB` | `$0012` | `0/0/1` |
| SHLI.W #8,r0 | `$12AB` | `$AB00` | `1/0/0` |
| SHLI.W #16,r0 | `$12AB` | `$0000` | `0/1/1` |
| SHRI.W #16,r0 | `$12AB` | `$0000` | `0/1/0` |
| SARI.W #16,r0 | `$80AB` | `$FFFF` | `1/0/1` |
| ROLI.W #8,r0 | `$12AB` | `$AB12` | `1/0/0` |
| RORI.W #8,r0 | `$12AB` | `$AB12` | `1/0/1` |
| ROLI.W #16,r0 | `$12AB` | `$12AB` | `0/0/1` |
| RORI.W #16,r0 | `$12AB` | `$12AB` | `0/0/0` |
| SHRI.B #9,r0 | `$1281` | `$0000` | `0/1/0` |
| SARI.B #16,r0 | `$1281` | `$00FF` | `1/0/1` |
| ROLI.B #8,r0 | `$1282` | `$0082` | `1/0/0` |
| RORI.B #16,r0 | `$1282` | `$0082` | `1/0/1` |
| SHLI.B #0,r0 | `$12AB` | `$12AB`, unchanged | Input N/Z/C preserved |

V/I/E retain their input values in every row, including zero-count operations.

### Instruction and execution vectors

Use these cases as the working specification’s conformance contract.
[README.md](../README.md#validation) records implementation coverage separately.
Unless a case specifies U mode, use K mode, valid kernel-entry state and a
published kernel map with the required RAM rights. Observe saved frames and live
state independently. Tests of nested faults must leave frame capacity and valid
entry state available unless they deliberately test terminal failure.

#### Conformance: Arithmetic and flag cases

In this table r0 is the destination and r1 the source. Exact encodings are ADD=`10 64`, ADC=`10 44`, SUB=`10 74`, SBC=`10 54`, and CMP=`1A 04`. Flags shown include only N/Z/C/V; all unrelated flags must be preserved.

| Operation | Original r0 | r1 | Input C | Result r0 | N/Z/C/V bits |
|---|---|---|---:|---|---|
| ADD | $FFFF | $0001 | Either | $0000 | $5: Z,C |
| ADD | $7FFF | $0001 | Either | $8000 | $A: N,V |
| ADC | $7FFF | $0000 | 1 | $8000 | $A: N,V |
| ADC | $FFFF | $0000 | 1 | $0000 | $5: Z,C |
| SUB | $0005 | $0003 | Either | $0002 | $4: C |
| SUB | $0003 | $0005 | Either | $FFFE | $2: N |
| SUB | $8000 | $0001 | Either | $7FFF | $C: C,V |
| SBC | $0000 | $FFFF | 0 | $0000 | $1: Z |
| SBC | $8000 | $7FFF | 0 | $0000 | $D: Z,C,V |
| CMP | $8000 | $7FFF | Either | $8000, unchanged | $C: C,V |

Also check self-operands, C=0/C=1 carry chains, CMP preserving both inputs, and preservation of I/E by every arithmetic instruction. With V initially set, `SHL $8001 -> $0002`, `SHR $8001 -> $4000`, and `SAR $8001 -> $C000` must all keep V set and set C. `SHR $0001` produces zero with Z/C set. Logical operations update N/Z while preserving C/V.

#### Conformance: Addressing, encoding, and PC cases

Unless overridden, use K mode, CS=DS=SS=0, PC=$0100, valid kernel RAM mappings and entry state, and I=0. For raw fault checks, install a valid corresponding vector and observe the saved context; the ordinary instruction effects listed below must not occur before entry.

| Case | Setup / bytes | Expected result |
|---|---|---|
| Little-endian immediate | `15 00 34 12` | r0=$1234; PC=$0104 |
| B1 to accumulator | r4=$1234; `10 A3` | r3=$1234; flags unchanged |
| Accumulator to B1 | r3=$1234; `10 BC` | r4=$1234; flags unchanged |
| Self-move pseudo | Assemble MOV rn,rn for each r0–r7 | Emit `00`, never an escape |
| Unsupported direct transfer | Assemble MOV r4,r5 | Reject; require explicit accumulator temporary |
| Byte load | r0=$FFFF; DS:$2000=$80; `15 18 00 20` | r0=$0080; flags unchanged |
| Byte store | r0=$ABCD; `15 58 00 20` | Only DS:$2000 becomes $CD |
| Signed DP displacement | DS=3, DP=$1202; `15 20 FE FF` | Load word from physical $031200 |
| Unaligned word | Word at DS:$2001 | Load/store succeeds |
| Word crosses segment | Store word at DS:$FFFF | Memory Access; first byte unchanged |
| Second byte unavailable | Word store at $FFFC, byte $FFFD uninstalled | Memory Access; $FFFC unchanged |
| DP underflow | DP=0, disp16=$FFFF | Memory Access; address-valid bit clear |
| Branch base | BNE at $1000, bytes `41 04 00`, Z=0 | PC=$1007 |
| Untaken branch | Same bytes, Z=1 | PC=$1003 |
| Branch truth table | All conditions, all 16 flag patterns | Match ISA predicates exactly; flags unchanged |
| Last-byte NOP | PC=$FFFF, byte `00` | Memory Access; saved PC=$FFFF |
| Last-byte extension | PC=$FFFF, byte `15` | Fetch Memory Access, never wrap |
| Widened branch base | PC=$FFFD, bytes `4E FD FF` | PC=$FFFD; intermediate next=$10000 |
| Illegal before operand fetch | PC=$FFFE, bytes `15 40` | Illegal Instruction, not a fetch across the boundary |
| Reserved primary | Byte `$FF` | Illegal Instruction |
| Reserved transfer mode | Bytes `10 E0` | Illegal Instruction |
| Reserved B1 selector bits | Bytes `15 80 04` | Illegal Instruction; B1 indirect forms are otherwise assigned |
| Reserved shift bits | Bytes `1A 14` | Illegal Instruction |
| Reserved context register-byte bits | Bytes `1F 28 08` | Illegal Instruction, even when E=0 |
| Far jump | `53 00 02 03 00` | CS=3, PC=$0200 |
| Invalid far-jump high byte | `53 00 02 03 01` | Invalid State; CS unchanged |
| Invalid segment write | r0=$0100; MOV r0,ds (`10 D2`) | Invalid State; DS unchanged |
| Reserved CSR bit | r0=$0010; MOV r0,csr (`10 93`) | Invalid State; CSR unchanged |
| Explicit CSR write | Write defined bits with E either zero or one | Update N/Z/C/V/I; retain actual active E |
| CS write | MOV r0,cs | Fetch operands in old CS; commit following offset in new CS after target validation |

#### Conformance: B1 pointer transfer and adjustment cases

Use independent CS and DS values so selecting the wrong segment cannot accidentally pass. Pointer offsets remain unsigned 16-bit values; memory displacements are signed 16-bit, while ADDP constants are signed eight-bit.

| Case | Setup / operation | Expected result |
|---|---|---|
| Independent data pointer | DS=3, r4=$2000; `15 80 00` | Load a word from physical $032000 into r0; DP unchanged |
| Explicit code pointer | CS=2, r6=$2000; `15 AA 02` | Load a byte from physical $022000 into r2; PC advances normally |
| Pointer displacement | `MOV.W r1,[ds:r5+2]` / `15 D1 01 02 00` | Store a word at DS:(old_r5+2); r5 unchanged |
| Negative displacement | `MOV.W [cs:r6-2],r2` / `15 B2 02 FE FF` | Subtract two from the base for this access only |
| Default segment and zero displacement | Assemble `[r4]` or `[r4+0]` | Select DS and emit the three-byte form |
| Base and destination alias | `MOV.W [ds:r4],r4` | Address uses old r4; successful load replaces r4 |
| Base and store source alias | `MOV.W r4,[ds:r4]` | Store the original r4 value at its original offset |
| Segment-boundary word | Base=$FFFF, word access | Memory Access; no partial transfer or wrap |
| Displaced underflow/overflow | Base=0 with -1, or base=$FFFF with +1 | Memory Access; base/value registers unchanged |
| Reserved descriptor before displacement | At PC=$FFFD, `15 90 04` | Illegal Instruction without fetching a displacement across the boundary |
| Positive byte stride | r4=$1000; `ADDP #1,r4` / `20 01` | r4=$1001; every flag unchanged |
| Positive word stride | r7=$1000; `ADDP #2,r7` / `23 02` | r7=$1002; every flag unchanged |
| Negative stride | r4=$1000; `ADDP #-1,r4` / `20 FF` | r4=$0FFF; every flag unchanged |
| Dedicated DP adjustment | DP=$2000; `ADDP #2,dp` / `24 02` | DP=$2002; DS, general registers, and every flag unchanged |
| DP dereference | DS=3, DP=$2000; ADDP #2,dp, then load `[dp+0]`, then ADDP #-2,dp | Read physical $032002 and finish with DP=$2000 |
| Constant endpoints | ADDP #-128 or #127 | Accepted and checked in a wider type |
| Zero adjustment | ADDP #0,r4 or ADDP #0,dp | Destination and flags unchanged; PC advances by two |
| Checked lower boundary | r4=0; ADDP #-1,r4 | Invalid State; r4 remains zero before entry; no wrapping |
| Checked upper boundary | r4=$FFFF; ADDP #1,r4 | Invalid State; r4 remains $FFFF before entry; no wrapping |
| No mapping probe | ADDP produces a valid offset pointing into unavailable RAM | Adjustment succeeds; a later dereference faults |
| Restricted destinations | Assemble ADDP targeting r0–r3, SP, SB, ST, CS, DS, SS, or CSR | Reject; only r4–r7 and DP are allowed |
| Strict signed constant | Assemble ADDP #128, #-129, or #$FF | Reject; use #-1 explicitly for a negative unit step |
| Aliases | INCP r4 / DECP r7 / INCP dp / DECP dp | Emit `20 01` / `23 FF` / `24 01` / `24 FF`; preserve flags |
| Old escaped ADDP forms | `1A 40` through `1A 43` | Illegal Instruction immediately; no constant fetch |
| Unassigned neighboring primary | `$28` | Illegal Instruction; $25 is ADJSP, $26 is SWI, and $27 is MOVU, not additional ADDP destinations |
| Final representable next | ADDP at $FFFD with a valid sum | Success; PC=$FFFF and destination updated |
| Final-PC overflow | ADDP at $FFFE with a valid sum | Memory Access; destination unchanged before entry |
| Flags between arithmetic | CMP, then ADDP, then a branch; ADD, then ADDP, then ADC | Branch sees CMP flags; ADC consumes ADD's carry |

For every failed instruction, check its original PC and registers in the saved context; exception entry itself sets E and clears I. ADDP range faults, for both B1 and DP, report Invalid State with the instruction address because adjustment accesses no memory. Direct SP-relative moves still prohibit slots below SP; B1 accesses remain ordinary memory accesses and do not acquire stack limits when their segment aliases SS.

#### Conformance: Stack and call cases

Use SS=2, SB=$1000, SP=$1000, ST=$1004, with words $1234 at SS:$1000 and $5678 at SS:$1002 unless stated otherwise.

| Case | Operation / setup | Expected result |
|---|---|---|
| Slot zero | `MOV.W [sp+0],r0` | r0=$1234; SP unchanged |
| Positive slot | `MOV.W [sp+2],r0` | r0=$5678; SP unchanged |
| Entire slot must fit | Word at `[sp+3]` or `[sp+4]` | Stack Boundary; no load/store effects |
| No negative slot | Assemble `[sp-2]` | Reject |
| Encoded large positive offset | `[sp+$FFFF]` | Stack Boundary; never interpret as -1 or wrap below SP |
| Empty-stack slot | SP=ST; load `[sp+0]` | Stack Boundary |
| Inclusive lower limit | SP=$1002, SB=$1000; PUSH r0 | Success at SS:$1000; SP=$1000 |
| Push below limit | SP=SB=$1000; PUSH r0 | Stack Boundary; SP and memory unchanged |
| Empty pop | SP=ST; POP r0 | Stack Boundary; r0 and SP unchanged |
| Allocate locals | Valid stack, ADJSP #-8 / `25 F8` | SP decreases by eight; flags and RAM unchanged |
| Release locals | Valid stack, ADJSP #8 / `25 08` | SP increases by eight within ST; flags and RAM unchanged |
| Exact limits | ADJSP lands exactly on SB or ST | Succeeds; lower limit inclusive and upper limit a valid empty SP |
| Zero adjustment | Valid stack, including SP=ST; `25 00` | Succeeds; only PC advances |
| Adjustment outside limits | Proposed SP below SB or above ST | Stack Boundary; SP and RAM unchanged; report SS:new_SP if representable |
| Widened SP underflow/overflow | SP=0 with -1, or SP=$FFFF with +1 | Stack Boundary; no wrapping; fault-address-valid clear |
| Invalid starting stack | Current invariant fails, even if an adjustment could repair it | Stack Boundary; no change; report SS:old_SP |
| Uninstalled local storage | Valid limits, ADJSP allocates unmapped bytes | Adjustment succeeds without memory access; later slot use faults |
| ADJSP next-PC overflow | At PC=$FFFE with valid bounds | Memory Access; no SP change |
| Mixed ADJSP failure | At PC=$FFFE with invalid starting stack | Stack Boundary wins over resulting-PC overflow |
| Invalid SP write | Candidate SP below SB or above ST | Stack Boundary; old SP unchanged |
| Configuration in progress | Write SB or ST so invariant temporarily fails | Write succeeds; subsequent stack use faults until repaired |
| Stack byte absent | POP with valid limits, second byte uninstalled | Memory Access; r0 and SP unchanged |
| Near call | CALL $0200 at $0100 with SP=$FFFE | Push bytes `04 01` at SS:$FFFC; SP=$FFFC; CS unchanged |
| Near return | RET from that callee | PC=$0104; SP=$FFFE |
| Call target absent | First target byte uninstalled | Memory Access; no return word written and SP unchanged |
| Return target absent | Top word names uninstalled target | Memory Access; no pop |
| Data alias | DS=SS; absolute access below SP within installed RAM | Ordinary data access succeeds; stack limits are not memory protection |

Also cover ST=$FFFF excluding stack byte $FFFF, odd stack boundaries, byte slot access at ST-1, direct allocation by lowering SP, and deallocation by raising SP. For combinations of faults, assert the ISA's specified priority rather than merely accepting any fault.

#### Conformance: Register-target JS cases

- `JS r4` through `JS r7` encode as `1F 60` through `1F 63`. Use CS=2, DS=3, and distinguish their memory contents. Only PC changes to the original selected register's offset; CS, SP, other registers, and every flag remain unchanged.
- Load a table entry with `MOV.W [cs:r4],r5`, then JS r5. There is no extra target-word read, stack push, or return address; the worked dispatch example must skip its $DEAD fallthrough and produce $BEEF.
- An unavailable first byte at CS:target causes Memory Access before effects and reports that physical target address.
- A mapped target containing an illegal opcode permits the jump to commit. The next instruction faults at the target, with its own saved PC, rather than at the JS.
- A complete two-byte JS at $FFFE may jump successfully; unused next=$10000 is allowed for an explicit target. A prefix at $FFFF still faults fetching the missing second byte.
- JS r0, JS dp, JS [r4], and indirect CALL are rejected by the assembler. `$1F $64` remains illegal rather than implying an indirect call.

#### Conformance: Exception, interrupt, and reset cases

- Install vector 0 as `00 01 00 00`. Reset produces CS:PC=0:$0100, zero general registers/DP/DS/SS/CSR, SB=0, SP=ST=$FFFE, no pending IRQ, and inactive context. RAM is preserved.
- Fault on PUSH with SB=SP=ST=0. For U entry, a valid protected kernel stack allows capture despite the invalid user stack. Vector 3 is read at kernel virtual $00000C. For K entry, an invalid retained kernel stack causes terminal entry failure after capture. The captured frame contains the original registers and faulting PC; successful U→K entry has live E=1 and I=0.
- Read xpc with `1F 28 00` (GETX xpc,r0), edit it using `1F 48 00` (SETX r0,xpc), and IRET with `1F 12`. The edited PC resumes; all other saved general/internal registers are restored. Live handler register changes are discarded.
- Repair an invalid saved stack configuration through SETX before IRET. An unrepaired configuration or invalid restored segment/CSR raises a nested validation fault, preserving the edited frame.
- GETX/SETX/IRET outside an active context raise Invalid State. Encoded writes to read-only cause/address/service context words raise Illegal Instruction.
- Fetch a later missing instruction byte: xpc names the instruction start, while the address words identify the missing byte. An out-of-range effective offset clears the address-valid bit. External IRQ address words are zero.
- Cause a second synchronous fault in a handler. Push a new frame without replacing the lower frame, then repair and return through both. Separately test depth-eight overflow and unavailable vector entries, malformed vectors or invalid handler/stack entry state: these cause terminal Double Fault with retained frames and diagnostics.
- Latch an IRQ with I=0; it stays pending. Execute EI; accept it at the following boundary, saving the following PC. An already eligible IRQ is accepted before DI is fetched.
- With E=1 and I=1, accept an enabled pending source not already in-service. Verify nested delivery, independent source ACK and restoration of the interrupted handler.
- HALT with I=1 advances PC and sleeps. An IRQ wakes it and saves that following PC; IRET resumes there. HALT with I=0 stays asleep on a pending IRQ. Multiple requests coalesce.
- Host reset exits HALT and terminal state, discards pending IRQ/context, and reloads vector 0. An invalid reset vector or unavailable reset target gives terminal Reset Fault with no active frame and CS:PC=0:0.

#### Conformance: Instruction-boundary recovery cases

These sequences test actual instruction boundaries, not an artificial transaction spanning a prologue. Install valid vectors and mapped handler code. For a fault, observe the saved context after entry, distinguishing its pre-entry flags from live E=1/I=0.

| Scenario | Setup and sequence | Expected result |
|---|---|---|
| Saved E/reserved bits: four cases | In a handler, SETX values $0040, $0010, $006F, or $8000 into xcsr, then GETX and IRET | SETX succeeds and GETX returns the exact word; live flags stay unchanged. IRET rejects the value with Invalid State, pushes a nested fault frame, and retains the lower edited frame. Repair via a registered recovery continuation; do not silently mask the value. |
| Valid saved CSR and known resume PC | A fixed image has illegal byte $FF at $0100 and NOP at $0101. Handler SETX writes xpc=$0101 and xcsr=$0005, then IRET | Resume at the known $0101 address with CSR=$0005/E=0; original general registers are restored. NOP advances to $0102. The handler obtains the address from the known layout, not new length metadata. |
| Partially allocated frame | SB=$1000, SP=ST=$10C8; execute ADJSP #-128 twice | First succeeds at $1048; second proposes $0FC8 and faults. Saved xsp=$1048 and xpc identifies the second instruction. No sequence rollback occurs. |
| ADDP upper boundary: two destinations | r4 or DP starts at $FF80; execute ADDP #127, then ADDP #1 | First reaches $FFFF; second faults with Invalid State and saves the already-advanced $FFFF value. |
| ADDP lower boundary: two destinations | r4 or DP starts at $007F; execute ADDP #-127, then ADDP #-1 | First reaches zero; second faults with Invalid State and saves zero. |
| IRQ between adjustments | Use the partially allocated-frame setup, accept an IRQ after the first ADJSP, and IRET without modifying the context | IRQ saves xsp=$1048 and the second instruction's PC. IRET resumes there; the second adjustment still faults with $1048 saved. |
| Invalid user stack repaired | Enter from U with an invalid numeric saved stack but valid protected kernel-entry state. Use saved-context edits to restore the descriptor-installed user bounds and a valid SP | Entry uses the kernel stack independently of the user stack. IRET restores the repaired image and canonical user I=1; capture performs no user-stack writes. |
| Invalid user stack unrepaired | Enter from the same U configuration, then IRET without repairing saved bounds | Nested Stack Boundary fault; retain the invalid lower image and allow the handler to recover. |
| Handler HALT and IRQ | Enter a K handler, enable interrupts and HALT with no eligible source. Assert another enabled source | Wake into a nested IRQ frame with the following PC saved. IRET resumes the halted handler after HALT. A masked or in-service source alone does not wake it. |

Failed IRET must not silently clear E/reserved bits or overwrite the rejected frame. A separate nested frame captures the failed return instruction. Software recovery from a multi-instruction allocation must account for completed instructions; prevalidation is not sequence atomicity. No decoded-length context field, wider adjustment encoding, or indirect call is assigned by these clarifications.

#### Conformance: Software-trap and system-call cases

These are the required software-trap cases:

| Case | Required behavior |
|---|---|
| Every service number | Encode unsigned 0–255 in exactly two bytes; xtrap is zero-extended and xcause=6, including service 0. |
| Alias and invalid operands | TRAP emits the same bytes; reject #-1, #256, register operands, missing operands, and size suffixes. |
| Masking and flags | Exercise all 32 K caller N/Z/C/V/I combinations and all 16 U NZCV combinations with I=1: enter regardless of K I; live NZCV retained, I=0/E=1; unchanged xcsr restored by IRET. |
| Exact continuation | SWI at $0106 saves $0108; return never executes SWI again. Restore nonzero caller CS after a handler in a different segment. |
| Context metadata | xtrap at index $14 is read-only; GETX accepts all eight GPR destinations, rejects upper register-byte bits before checking active context, and faults Invalid State outside a handler. SETX `$54` remains illegal before a third-byte fetch. |
| Ordinary preservation/results | Use distinct initial values in every register. Handler clobbers live registers; IRET restores them except explicit xr0/xr1 edits. Verify DP, segments, stack, caller flags and all unedited context words. |
| Unsupported service | Handler returns OS error normally, preserving xpc and caller interrupt policy; hardware never rejects an otherwise valid immediate. |
| Invalid caller stack | Entry succeeds without using/probing stack RAM; unrepaired IRET raises a nested Stack Boundary fault. Repair saved limits/SP and verify successful return. |
| Fetch failure | Missing immediate yields cause 2, fault-start xpc and xtrap=0. At PC=$FFFF, no wrap to offset zero. |
| Continuation boundary | SWI at $FFFD can save $FFFF; at $FFFE it faults cause 2 with xpc=$FFFE and no valid address. |
| Uninstalled continuation | SWI entry succeeds when next is uninstalled; IRET then raises a nested Memory Access fault unless xpc/xcs were repaired to an executable installed target. |
| Bad trap vector | Test each missing vector byte, reserved high byte, and missing handler target: terminal Double Fault retains SWI's following-PC context and xtrap. An illegal installed handler opcode fails only on its decode. |
| Nested operation and priority | K SWI with E=1 creates a nested frame. Missing immediate takes fetch-fault priority over next-PC validation; exhausted depth or failed entry is terminal Double Fault. |
| IRQ ordering | Eligible IRQ before SWI wins and saves SWI's start; masked/newly arriving IRQ is retained through SWI; IRET restoring I=1 permits delivery at the following-PC boundary. |
| Later exceptions/reset | After return, a later non-SWI exception sets xtrap=0. Reset discards the whole context and pending requests as before. K handler HALT obeys ordinary source eligibility. |
