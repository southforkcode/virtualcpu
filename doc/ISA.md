# ISA: Virtual 16-bit CPU, revisions 0.5 and 0.6

Revision **0.6 architectural direction is approved; its execution draft is not frozen**. It adds protected user/kernel
execution, process segment maps, bounded nested exceptions, safe kernel entry,
and deterministic preemption. The [revision 0.6 section](#revision-06-os-execution-architecture)
defines those semantics as an overlay on the frozen revision 0.5 baseline below.
Where an overlay rule differs, the selected revision determines the behavior.

| Revision | Specification status | Implementation status |
|---|---|---|
| 0.4 | Previous baseline; no SWI or GETX xtrap | Selectable in CPU and tools |
| 0.5 | Frozen instruction set; execution candidate pending complete conformance audit | CPU, assembler/disassembler and conformance tests implemented |
| 0.6 | Approved direction; revised execution contracts under review; no binary/control freeze | Not yet implemented or selectable |

[ISA_ENCODING.md](ISA_ENCODING.md) contains the frozen 0.5 machine-readable
binary table and the 0.6 allocation requirements. Architecture approval does not
turn experimental study bytes into assigned instructions. Existing assigned
instruction bytes and B0/B1 roles are preserved; 0.6 changes privilege,
translation and exception semantics only when that revision is selected.

[ISA_Examples.md](ISA_Examples.md) contains the executable baseline programs,
software ABI comparisons and retained density evidence. [README.md](../README.md)
describes the current library, tools and validation. These documents replace the
separate proposal, experiment reports and temporary implementation notes.

## Revision 0.5 baseline

The following conventions through the system-call ABI specify revision 0.5.
Revision 0.6 inherits them except for the explicit overrides later in this file.
Baseline statements about one execution mode, physical segment addressing,
single exception context, interrupt blocking while E=1, and unrestricted control
writes apply to 0.5 only. They must not be applied to protected 0.6 execution.

## Conventions

- Instructions use source, destination operand order. `$` denotes hexadecimal; `#` denotes an immediate.
- Assembly MOV byte immediates accept -128 through 255 and word immediates -32768 through 65535, encoding negatives in two's complement; other values are rejected rather than truncated. ADDP and ADJSP use strictly signed immediates from -128 through 127. SWI accepts only unsigned service numbers from 0 through 255. Absolute offsets are unsigned. A byte immediate still zero-extends on load, so `MOV.B #-1,r0` produces `$00FF`.
- Registers and arithmetic results are 16 bits unless stated otherwise. Arithmetic results are reduced modulo 65536; address calculations are checked before narrowing.
- Instruction operands and memory words are little-endian. A word `$1234` occupies bytes `34 12`. Unaligned word accesses are allowed.
- Instructions are 1–5 bytes long. There are no delay slots or implicit operand-size prefixes.
- Flags not explicitly updated remain unchanged. Ordinary moves, ADDP, ADJSP, stack operations, jumps, calls, and ordinary returns preserve flags. Explicit CSR writes, SWI/exception entry, and IRET have the effects specified below.
- All unassigned opcodes and reserved encoding fields are illegal in this revision. Later extensions require an explicitly selected architecture revision; software must not treat reserved encodings as NOPs.
- There is one execution mode, no memory protection, and no privileged instructions. Stack checks apply to stack addressing and operations, not to arbitrary aliases of the same memory.

## Registers

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

ADDP adjusts a B1 register or DP by a signed byte constant and preserves flags. General ADD/ADC/SUB/SBC, logic, CMP, and shifts remain B0-only. ADDP checks only the offset range; a later memory transfer checks the selected segment and installed RAM. See the two-byte `$20–$24` encodings below.

### Internal-register writes

| Destination | Rule |
|---|---|
| `dp` | Any 16-bit value; effective addresses are checked when used |
| `sp` | Require `SB <= new_SP <= ST`; otherwise Stack Boundary fault; no memory access or allocation initialization |
| `sb`, `st` | Any 16-bit value; may temporarily invalidate the stack while configuring it |
| `ss`, `ds` | Require high byte zero; changing the segment does not probe memory |
| `cs` | Require high byte zero; continue at the following instruction offset in the new CS, validating that target's first byte |
| `csr` | Reserved bits must be zero; write N/Z/C/V/I; ignore the supplied E bit |

Reading SP or other stack registers is allowed even when stack configuration is invalid. Stack use requires `SB <= SP <= ST`. For initialization or switching, mask interrupts, write SS/SB/ST, then write SP, then restore interrupt policy. Temporary invalid configuration does not itself fault; an intervening stack operation does. DI does not mask synchronous faults.

## CSR and arithmetic

| Bit | Name | Meaning |
|---|---|---|
| 0 | Z | Result is zero |
| 1 | N | Result bit 15 |
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

XOR, AND, OR, and complemented MOV update N/Z from the word result and preserve C/V/I/E. `OR r2, r2` tests zero/sign without changing r2. There is no dedicated TST instruction.

## Memory and address formation

```text
physical(segment, offset) = (segment << 16) | offset
```

Segments do not overlap. The address space is 24 bits, or 16 MiB. Offsets are unsigned 16-bit values. For example, `DS=$03, DP=$1200` addresses physical `$031200`.

The machine supplies an installed-RAM map, stable while an instruction executes; bytes outside it cause Memory Access faults. The baseline has ordinary byte-addressed RAM, no MMIO side effects, and no access permissions. Hosts may install less than 16 MiB. Memory images and installed RAM survive CPU reset.

| Access | Segment | Offset calculation |
|---|---|---|
| Instruction fetch | CS | PC and subsequent instruction-byte offsets |
| `[addr16]` | DS | Unsigned `addr16` |
| `[dp+disp16]` | DS | `DP + s16(disp16)` |
| `[ds:rp+disp16]` | DS | `rp + s16(disp16)`; rp is r4–r7 |
| `[cs:rp+disp16]` | CS | `rp + s16(disp16)`; rp is r4–r7 |
| `[sp+off16]` | SS | `SP + unsigned(off16)` |
| PUSH/POP/CALL/RET stack access | SS | As defined in Stack |

Calculate offsets in a wider signed or unsigned type as appropriate. Negative or greater-than-65535 offsets fault; they never wrap or carry into another segment. Every byte must fit within the same segment and be installed. Words may start at odd offsets but not at `$FFFF`. Validate the whole access before writing any byte.

DP and B1 memory displacements range from -32768 to 32767. SP offsets range from 0 to 65535 and additionally obey stack limits. Assemblers reject negative SP offsets and out-of-range displacements instead of truncating them. An encoded SP offset `$FFFF` means +65535, never -1. B1 forms select only CS or DS; they do not provide an SS-relative stack-slot mode.

A byte at offset `$FFFF` is accessible, but an offset `$10000` cannot be stored as a pointer or end sentinel. A traversal that includes the last byte must test its count/end condition before advancing past that byte. ADDP from `$FFFF` by +1 still faults; it never enters the next segment. See the count-controlled segment-end traversal in the worked examples.

### Instruction fetch and PC updates

Fetch all instruction bytes from the original CS, with increasing offsets and no wrapping. A fetch beyond `$FFFF` or from an uninstalled byte faults at that instruction. Fetch/decode does not architecturally advance PC before commit.

Let `next = original_PC + instruction_length`, computed without truncation:

- Ordinary instructions and HALT require `next <= $FFFF` and commit PC=next.
- Untaken conditional branches likewise require a representable next.
- Taken branches use `target = next + s16(rel16)` and require `0 <= target <= $FFFF`. Their intermediate next may equal `$10000` if the resulting target is valid.
- JS, JL, RET, and IRET use their explicit target; unused next need not be representable. CALL requires a representable next because it saves that return offset.
- SWI requires a representable next because it saves that resume offset in xpc. It transfers through vector 6 without checking whether the following instruction is installed; IRET checks the selected resume target later.
- A move to CS uses next in the new segment, so next must be representable.

Explicit transfers (taken branches, JS, JL, CALL, RET, IRET, and writes to CS) validate that the target's first byte is installed before committing. They do not decode the target instruction; a later decode fault belongs to that target instruction. Ordinary sequential execution checks the following instruction's memory on its next fetch.

Consequently, NOP at `$FFFF` faults without advancing PC, while a three-byte BRA at `$FFFD` may branch back within its segment. No instruction's bytes may themselves cross a segment boundary.

## Stack

The stack grows downward within SS. SB is the inclusive lower limit. ST is an exclusive upper limit and the empty-stack pointer. The valid configuration is:

```text
0 <= SB <= SP <= ST <= $FFFF
allocated stack bytes = [SP, ST)
```

ST occupies the previously unused B3 selector. It is a 16-bit exclusive bound: `$10000` cannot be represented, so offset `$FFFF` is never a stack byte. Reset uses `ST=SP=$FFFE`, `SB=0`. Odd bounds and pointers are allowed; words do not require alignment.

Direct stack-relative MOV computes `start = SP + unsigned(off16)` and requires a valid stack configuration, `SP <= start`, and `start + size <= ST`, with arithmetic checked before truncation. It uses SS and does not change SP. Stack slots below SP are inaccessible through this mode. Even `[sp+0]` faults on an empty stack. A register-to-SP move changes the allocation boundary; it neither clears nor writes allocated memory.

Absolute/DP-relative and B1 pointer accesses use ordinary memory rules even if their selected segment equals SS. Stack limits are bounds for stack operations, not general memory protection.

| Instruction | Checks and successful operation |
|---|---|
| `PUSH rn` | Require valid configuration and `SP-2 >= SB`; store the original register word at `SS:(SP-2)`, then set SP=SP-2 |
| `POP rn` | Require valid configuration and `SP+2 <= ST`; load the word at SS:SP into rn, then set SP=SP+2 |
| `ADJSP #simm8` | Require valid configuration and `SB <= SP+s8(simm8) <= ST`, calculated without narrowing; set SP to the checked result |
| `CALL addr16` | Require representable next, valid target in current CS, and valid stack access; push next as one word and set PC=addr16 |
| `RET` | Peek the word at SS:SP; validate it as a target in current CS; then set SP=SP+2 and PC=that word |

PUSH/POP accept r0–r7 only, not internal registers. POP is canonical; PULL is an alias for POP. PUSH.W, POP.W, and PULL.W are explicit word-transfer aliases for PUSH, POP, and POP respectively. There are no byte stack operations; ADJSP has no transfer-size suffix. CALL/RET preserve CS; return addresses are offsets, not far pointers. All checks, including availability of both stack bytes and any target, precede effects. A failed push/call writes nothing; a failed pop/return changes neither the register nor SP.

For example, with `SB=$1000, SP=$1002, ST=$1004`, PUSH succeeds at `$1000`; a second PUSH faults and leaves SP and memory unchanged. With SP=ST, POP faults.

ADJSP allocates locals with a negative constant and releases them with a positive constant. It preserves all flags, touches no memory, and neither initializes new slots nor probes their RAM mapping. Stack-relative MOV checks the resulting allocation and installed bytes when used. The existing stack must be valid before adjustment; use explicit configuration-register writes to repair an invalid stack. A failing adjustment leaves SP and memory unchanged before exception entry. If the initial configuration is invalid, report SS:old_SP; otherwise report SS:new_SP when representable, or an invalid fault address if the widened new_SP is outside 0–65535.

## Instruction encodings

### Instruction lengths

This summary includes real instruction forms; assembler aliases use the length of their expansion. The JSON metadata provides a fixed length for every selected encoding. An escape byte alone is not a complete instruction.

| Form | Total bytes |
|---|---:|
| NOP, HALT, DI, EI; distinct B0-to-B0 MOV | 1 |
| `$10` ALU or bank transfer; `$1A` CMP or shift | 2 |
| ADDP to r4–r7/DP; ADJSP; SWI | 2 |
| PUSH, POP, RET, IRET; JS r4–r7 | 2 |
| Conditional branch, BRA; JS addr16 | 3 |
| GETX, SETX; MOV.B immediate load | 3 |
| B1 indirect load/store without displacement, byte or word | 3 |
| MOV.W immediate load; absolute/DP/SP memory load/store, byte or word | 4 |
| CALL addr16 | 4 |
| JL abs32 | 5 |
| B1 indirect load/store with disp16, byte or word | 5 |

### Complete primary opcode map

Rows are the high nibble and columns the low nibble. `R` means reserved and raises Illegal Instruction immediately. `MV` is a one-byte accumulator move; `E10/E15/E1A/E1F` select extension tables below. `AP4–AP7` and `APD` are two-byte ADDP operations targeting r4–r7 and DP; `ASP` is two-byte ADJSP.

| High / low | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | A | B | C | D | E | F |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | NOP | HALT | DI | EI | R | R | R | R | R | R | R | R | R | R | R | R |
| 1 | E10 | MV | MV | MV | MV | E15 | MV | MV | MV | MV | E1A | MV | MV | MV | MV | E1F |
| 2 | AP4 | AP5 | AP6 | AP7 | APD | ASP | SWI | R | R | R | R | R | R | R | R | R |
| 3 | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R |
| 4 | BEQ | BNE | BCS | BCC | BMI | BPL | BVS | BVC | BHI | BLS | BGE | BLT | BGT | BLE | BRA | R |
| 5 | R | R | JS | JL | R | R | R | R | R | R | R | R | R | R | R | R |
| 6 | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R |
| 7 | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R |
| 8 | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R |
| 9 | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R |
| A | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R |
| B | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R |
| C | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R |
| D | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R |
| E | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R |
| F | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R | R |

The corresponding JSON tables have the following byte coverage. A selected encoding may still fail a reserved descriptor/register-byte check or a later operand/state/memory check. These counts describe matching byte values, not the number of instruction mnemonics or successful executions.

| JSON table | Inspected byte | Assigned values | Reserved/illegal values |
|---|---|---:|---:|
| `primary` | First byte | 44, including four escapes | 212 |
| `ext10` | Second byte after $10 | 224 | 32 |
| `ext15` | Second byte after $15 | 240 | 16 |
| `ext1a` | Second byte after $1A | 28 | 228 |
| `ext1f` | Second byte after $1F | 61 | 195 |

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

Bit 7 of the second byte selects the addressing family: zero retains the original immediate/absolute/DP/SP formats; one selects B1 indirect addressing, added in revision 0.2. Direction, size, and value-register fields retain their positions in both families.

#### Original forms (bit 7 = 0)

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

Immediate byte loads are three bytes; immediate word loads and all original memory forms are four bytes. Memory width never changes the address-operand width. d=1/mm=00 is Illegal Instruction, detected from the second byte before fetching an operand.

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

### `$1A`: comparison and single-bit shifts

CMP and shifts are two bytes and accept only B0 accumulators.

| Second byte | Syntax | Operation | Updated flags |
|---|---|---|---|
| `0000 ss dd` | `CMP rs,rd` | Flags for `rd-rs`; discard result | N,Z,C,V |
| `0001 00 dd` | `SHL rd` | `rd = (old_rd << 1) & $FFFF`; C=old bit 15 | N,Z,C |
| `0010 00 dd` | `SHR rd` | `rd = old_rd >> 1`; C=old bit 0 | N,Z,C |
| `0011 00 dd` | `SAR rd` | `rd = (old_rd >> 1) \| (old_rd & $8000)`; C=old bit 0 | N,Z,C |

Shifts set N/Z from the result and preserve V/I/E. Nonzero reserved `00` fields and all other second-byte values are illegal. No variable-count shifts or rotations are assigned.

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

The primary opcode identifies the destination, leaving the entire second byte for a signed two's-complement constant, -128 through 127. B1 uses `0010 00pp` with pp selecting r4–r7; `$24` is the DP form. `$25` is ADJSP, `$26` is SWI, and `$27–$2F` remain reserved. ADDP accepts only r4–r7 and DP, not B0 registers, SP, other internal registers, or memory.

Compute `sum = unsigned(old_destination) + s8(simm8)` in a wider type; require `0 <= sum <= $FFFF`. Otherwise raise Invalid State with the instruction address reported, leaving the destination and flags unchanged before fault entry. No segment or memory is accessed and no offset wrapping occurs. A valid sum may point to uninstalled RAM; that is checked on dereference. The DP form uses the same checks and flag behavior as the B1 forms and changes neither DS nor any other register. Alignment and access width do not affect adjustment: +1 advances one byte and +2 advances one word.

Examples: `ADDP #1,r4` is `20 01`; `ADDP #-1,r4` is `20 FF`; `ADDP #2,r7` is `23 02`; `ADDP #-2,dp` is `24 FE`. `INCP` and `DECP` are assembler aliases for ADDP #1 and ADDP #-1 respectively, accepting the same five destinations and using the same two-byte encodings. A signed constant makes a separate subtract-pointer opcode unnecessary. There is no 16-bit immediate form yet; larger updates use multiple ADDP instructions or explicit accumulator arithmetic followed by a transfer back. Only ADDP guarantees checked pointer adjustment; ordinary B0 arithmetic retains its modulo behavior.

Five primary opcodes cover `5 * 256` destination/constant combinations. This preserves the full signed-byte range while saving one byte per operation relative to the earlier escaped draft. The old `$1A $40–$43` encodings are reserved and raise Illegal Instruction immediately in revision 0.3; no constant byte is fetched for them.

ADDP preserves **all** CSR bits, including N/Z/C/V/I/E. It is not an arithmetic comparison or a loop-count test. For example, an intervening ADDP does not disturb CMP flags or a carry intended for ADC.

### `$25`: checked stack adjustment

`ADJSP #simm8` is `$25 simm8`, length two. The constant is signed two's complement, -128 through 127; assembly rejects values outside that range. Compute the new SP in a wider type, require the initial `SB <= SP <= ST` invariant and `SB <= new_SP <= ST`, then commit SP with every CSR bit preserved. Failure is Stack Boundary, not ADDP's Invalid State. For example, `ADJSP #-8` is `25 F8`, and `ADJSP #8` is `25 08`. ADJSP #0 is valid on a valid stack, including an empty one. Normal instruction fetch and following-PC checks still apply, and all effects are atomic. It does not change SS/SB/ST and does not save or restore registers.

### `$26`: software trap

`SWI #imm8` is `$26 imm8`, length two. The service number is unsigned 0–255; assembly rejects negative values, values above 255, omitted or register operands, and size suffixes. `TRAP #imm8` is an exact assembler alias. All 256 service numbers are hardware-valid and select the same vector 6; none invokes a built-in host service.

SWI is a deliberate synchronous transfer using the existing stack-independent saved exception context. It saves the **following PC**, unlike a fault, which saves the instruction start. The handler reads the service number through `GETX xtrap,rn`, returns results by editing saved registers with SETX, and resumes through IRET. SWI pushes no ordinary stack frame or return word and does not require a valid caller stack.

Entry follows these rules in order:

1. Accept an already eligible external IRQ at the instruction boundary before fetching SWI. Otherwise fetch both bytes from the original CS without wrapping or advancing architectural PC. A missing immediate is a Memory Access fault at the instruction-start PC, not a software trap.
2. Require E=0 after complete fetch. I may be zero or one: DI cannot suppress SWI. E=1 raises Invalid State at the instruction start and therefore terminal Double Fault, preserving the original active context. Do not create a new cause-6 context.
3. Compute `next = old_PC + 2` without truncation and require `next <= $FFFF`. Failure is Memory Access, saving the instruction-start xpc with no valid relevant address. E validation precedes this check. Do not probe the byte at next; it need only be installed when IRET later selects it.
4. Snapshot ordinary registers as for exception entry, except set xpc=next. Record xcs=old_CS, xcsr=old_CSR with E=0, xcause=6, xtrap=zero-extended imm8, and xaddrlo=xaddrhi=0. Capture all ordinary values unchanged, including any invalid caller stack configuration.
5. Activate the context, set live E=1, clear I/HALT, and validate vector 6 at physical `$000018–$00001B` using the normal packed-address entry rules. General registers, DP, DS, SS, SP, SB, ST, and N/Z/C/V otherwise retain their entry values. Install handler CS:PC only after validation. SWI performs no guest stack/data writes.
6. Failure to read a vector byte, a nonzero reserved vector high byte, or an unavailable handler first byte produces terminal Double Fault. Retain the new cause-6 context with its following xpc and service number. Live CS:PC stay at the pre-entry SWI location on vector failure; terminal diagnostics describe that failure under the normal entry rules. An installed but illegal handler instruction fails only on its subsequent decode.

SWI entry has no intermediate interruptible state. An IRQ arriving during the instruction remains latched; it cannot replace synchronous SWI entry or a SWI fetch/state/PC fault. Successful SWI does not consume a pending IRQ. While E=1, even EI does not enable external delivery. After IRET restores I=1/E=0, a pending IRQ may be accepted before fetching the resumed instruction.

IRET validates and restores the saved context under the ordinary rules, including its stack invariant and installed resume target. Normal return resumes after SWI without software decoding or advancing xpc. A handler may explicitly edit xpc/xcs to redirect execution. An invalid saved stack must be repaired before return even though it did not prevent entry. RET is not a trap return, and results left only in live handler registers are discarded by IRET.

Examples: `SWI #1` and `TRAP #1` both encode as `26 01`. SWI at `$FFFD` may save `$FFFF`; SWI at `$FFFE` faults because next is `$10000`. An absent immediate at `$FFFF` is a fetch fault, taking precedence over state/next checks. See the system-call ABI below and [the encoded syscall example](ISA_Examples.md#software-trap-and-system-call-example).

### Branches and absolute jumps

Branches are three bytes: `$40+condition`, then little-endian rel16. The assembler encodes `label - next` as a signed 16-bit displacement and rejects out-of-range labels. At `$1000`, `BNE +4` targets `$1007` when taken.

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
| All others | Reserved | — | Illegal Instruction |

GETX/SETX use a third byte `0000 0rrr`; upper five bits must be zero. GETX copies a context word to any general register; SETX copies a general register to a writable context word. They preserve flags and require E=1. With E=0 they raise Invalid State. SETX validates values on IRET, allowing edits of related fields in any order. Encoded writes to read-only context words are illegal.

SETX writes the entire saved word; it does not apply the live CSR-write mask to xcsr. Writing xcsr does not change live CSR or exception activity. The saved-CSR validity rules under IRET still apply when the context is restored.

`GETX xtrap,rn` uses `$1F $34 00000rrr`. xtrap is read-only, as are cause/address metadata: `$1F $54` remains illegal and is rejected before fetching a register byte.

`JS r4` through `JS r7` sample the selected B1 register as the target offset, validate the first byte at current CS:target, then commit PC. They preserve CS, SP, all general/internal registers other than PC, and all flags. They do not dereference a target word in memory and do not push a return address. For example, `JS r4` is `1F 60`; `JS r7` is `1F 63`. A table dispatch first loads the target word into B1, then uses JS. `JS [r4]`, `JS r0`, `JS dp`, and indirect CALL have no encoding. The instruction may end at `$FFFF` because unused next need not be representable, as for absolute JS. An unavailable target faults before effects; an illegal instruction at an installed target faults only when that next instruction is decoded.

## Exceptions, software traps, interrupts, and reset

### Vectors and causes

The fixed vector table is in physical segment zero at byte address `4 * vector`. Each entry is the same little-endian packed 32-bit address as JL, including a zero reserved high byte. The vector table is ordinary writable RAM. The host installs a valid reset vector and any needed handler vectors before reset.

| Vector / cause | Name | Trigger |
|---:|---|---|
| 0 | Reset | Host reset; not an exception context |
| 1 | Illegal Instruction | Unassigned opcode or reserved encoding field |
| 2 | Memory Access | Fetch/data/control-target unavailable or outside its segment |
| 3 | Stack Boundary | Invalid stack configuration, direct SP-write or ADJSP violation, or stack access outside allocation/limits |
| 4 | Invalid State | Bad segment/CSR/JL value, ADDP result outside the offset range, inactive-context operation, or invalid IRET state |
| 5 | External Interrupt | Pending maskable interrupt accepted at an instruction boundary |
| 6 | Software Trap | SWI with E=0 and a representable following PC; independent of I |

### Precise entry and saved context

A synchronous fault leaves the instruction's ordinary registers and memory unchanged, then enters its vector. Saved PC identifies its first byte, even if a later instruction byte failed to fetch. An external interrupt saves the boundary PC where execution would resume. A successful SWI instead saves the following PC and service number; a SWI fetch/state/PC fault retains ordinary fault semantics.

Entry snapshots all ordinary registers into a dedicated CPU context, sets active E=1, clears I, clears HALT, and loads handler CS:PC. The handler otherwise starts with the interrupted general registers, pointers, stack limits, SS, and DS. The context requires no ordinary stack or memory writes, so entry can handle stack faults. Saving the full register set lets a handler repair its stack without losing an accumulator during bootstrap.

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

The context contains 21 words; its first 17 are writable. xtrap is appended without renumbering existing fields. Every non-SWI entry sets xtrap=0, including a fault while fetching or validating SWI. Cause 6 distinguishes SWI #0 from those other entries. IRET does not restore read-only metadata as live registers. The saved service number avoids consuming an argument register or rereading mutable instruction memory.

The saved CSR describes the state to restore **outside** exception context. Exception entry records xcsr with E=0, and a valid restore value must keep E=0 and every reserved bit zero. SETX may temporarily store an invalid xcsr, including E=1, and GETX reads that stored word back unchanged. IRET rejects it; it does not mask or silently clear invalid bits. Because IRET executes with an active context, this validation failure becomes terminal Double Fault, retaining the saved context including prior SETX edits.

The relevant address is the first unavailable instruction/data byte, a representable start address of an invalid access, or an explicit control target. For Illegal Instruction and Invalid State it is the instruction start. If an access starts inside the segment but its width crosses the end, report the start; if the computed start is outside the offset range, both address words are zero and the valid bit is clear. An instruction fetch crossing the segment end also has no representable missing-byte address. External interrupts and successful software traps have no fault address: both address words are zero. A failed direct SP write reports SS:new_SP; a failed IRET stack invariant reports xss:xsp. For ADJSP, an invalid initial stack reports SS:old_SP; otherwise a rejected new_SP reports SS:new_SP if representable, with both address words zero and valid clear if not. Other stack failures report the attempted access start when representable. Fault priority below determines the reported address when multiple checks fail.

### IRET and nesting

IRET requires an active context. Validate saved segment high bytes, `(xcsr & ~$002F) == 0`, saved `xsb <= xsp <= xst`, and availability of the saved target's first byte. Restore all writable context registers atomically, deactivate E, and resume at xcs:xpc. Current handler registers and flags are discarded. IRET does not pop the ordinary stack. Context words become inaccessible until the next entry, which overwrites them.

There is no saved decoded-instruction-length field. A handler that changes xpc to skip a fault must know a valid resume address from the program layout or obtain one through a safe software decode. A fault can occur before a complete instruction has been fetched or decoded, so neither nominal instruction length nor the amount already fetched is an implicit resume offset. No additional context word or encoding is assigned for this purpose.

The baseline supports one active context. Maskable interrupts are held pending while E=1, even if a handler sets I. Any synchronous fault during a handler, including invalid IRET, causes terminal Double Fault without overwriting the original context. Failure to read a vector, a nonzero vector high byte, or an unavailable handler target also causes terminal Double Fault. Entry captures context, sets E, clears I/HALT, and then validates the vector; on vector failure, live CS:PC remain at the pre-entry location. A fault within an established handler leaves its live state at the faulting instruction, apart from entering terminal state. Terminal state stops execution and interrupt delivery until host reset; the host can inspect the original context and terminal cause/address. It is distinct from HALT.

### Fault priority and commit

Atomicity applies to **one instruction**, not an instruction sequence. Each successful instruction remains committed if a later instruction faults or an IRQ is accepted between them. Fault entry snapshots the state immediately before the faulting instruction; IRQ entry snapshots the interrupted boundary. Successful SWI snapshots its input state with the following PC as xpc. None rolls back earlier instructions. This applies equally to sequences of ADJSP, ADDP, or ordinary memory/register operations.

For example, with `SB=$1000` and `SP=ST=$10C8` (200 bytes available), the first `ADJSP #-128` succeeds with SP=$1048. A second `ADJSP #-128` proposes $0FC8 and faults. The saved xsp is $1048, and xpc identifies the second adjustment. The first allocation remains in effect. Similarly, ADDP #127 from $FF80 succeeds at $FFFF; a following ADDP #1 faults with $FFFF saved, not the pointer value from before the sequence. Software must account for completed steps and explicitly undo them when recovery requires it. Prevalidating a whole allocation can avoid an expected limit failure, but it does not make several instructions atomic or prevent an IRQ from observing an intermediate state.

Checks occur in this order; the first failure wins:

1. Fetch/decode bytes in order. Detect reserved primary/secondary opcodes immediately when known; do not fetch operands of illegal forms. For valid forms, fetch the complete instruction, checking reserved register-byte bits as soon as fetched.
2. Validate instruction-specific input state: segment/CSR/JL values, ADDP's widened result, active context (including SWI's E=0 requirement), and IRET saved-state fields. Direct SP writes and IRET's saved stack invariant use Stack Boundary; other invalid values use Invalid State.
3. Validate stack configuration and stack bounds, including ADJSP's initial invariant and widened new_SP, or ordinary effective-address arithmetic and complete access range. Stack and ADJSP range failures use Stack Boundary; data/fetch segment failures use Memory Access.
4. Validate availability of all operand-memory bytes in increasing address order. Read operands or peek a return word without committing effects.
5. Validate the selected resulting PC offset and any explicit target's first byte. Out-of-range PC or unavailable target uses Memory Access.
6. Compute results and commit all effects together. For SWI, enter vector 6 with the following PC saved after validating that offset at step 5; vector validation follows the entry rules. Process an eligible external interrupt at the following instruction boundary only if I=1/E=0.

An IRET target fault while E=1 becomes terminal Double Fault. Reset/vector reads are entry operations outside normal decoding. Interrupt requests arriving during an instruction are observed only after completion or synchronous entry; a synchronous fault or successful SWI entry takes precedence.

### Interrupt delivery and HALT

The host can latch one external interrupt request. Multiple requests before acceptance coalesce. Accept it before the next instruction when I=1 and E=0; successful entry consumes the request. DI/EI take effect on commit: an interrupt already eligible at a boundary is accepted before fetching DI, and one pending when EI commits is eligible immediately afterward. Requests remain pending while masked or while E=1.

This is a single notification mechanism, not an interrupt-controller or device-source interface. The guest receives cause 5 without a source identifier. Multiple vectors, device status, and host interrupt metadata require a future device specification.

HALT advances PC to next and stops instruction fetch. An eligible interrupt wakes it and enters vector 5 with that following PC saved. A masked pending interrupt does not wake it. IRET resumes after HALT instead of restoring halted state. A handler executing HALT cannot be woken by a maskable interrupt while E=1. Host reset always exits HALT and terminal state.

HALT inside an exception handler is intentionally a host-reset-only stop, even if the handler sets I=1 and an IRQ is pending. The CPU remains Halted with its active context retained; it does not become TerminalDoubleFault or TerminalResetFault. No further guest instruction, including IRET, executes until reset. This is a deliberate recovery/diagnostic behavior, not an implicit nested-interrupt mechanism.

### Reset

Reset clears general registers, DP, CSR, CS, PC, DS, SS, exception-active/context state, pending interrupt, HALT, and terminal state. It sets SB=0 and SP=ST=$FFFE, then reads vector 0 and installs its CS:PC only after validating the vector and the target's first byte. Failure is terminal Reset Fault with CS:PC=0:0 and no saved exception context. Reset does not clear RAM.

### Execution states

The following classification summarizes the behavior above and matches the names used in the implementation handoff. It does not prescribe a C++ storage layout or add guest-visible status bits.

| State | Saved exception context / E | Execution and exit |
|---|---|---|
| Running | Absent / 0, or active / 1 while handling an exception | Execute instructions; enter HALT, fault entry, or a terminal state according to the rules above |
| Halted | Absent / 0, or active / 1 if a handler halted | No fetch; only an eligible IRQ or host reset exits this state; IRQ eligibility requires I=1 and E=0 |
| TerminalResetFault | Absent / 0 | No fetch or IRQ delivery; host reset is required |
| TerminalDoubleFault | Original context retained / 1 | No fetch or IRQ delivery; host reset is required |

E reflects active saved-context availability independently of Running/Halted state. IRET deactivates the context and resumes Running; it does not restore a prior Halted state. A deferred IRQ remains pending until successful interrupt entry consumes it or host reset clears it.

## Calling convention

This is a software convention, not hardware enforcement:

- Calls are near; caller and callee execute in the same CS.
- r0–r2 carry up to three word arguments. r0 returns a word; a double word returns low half in r0 and high half in r1.
- r0–r3, DP, and N/Z/C/V are caller-saved. r4–r7, DS, SS, SB, and ST are callee-preserved. A callee returns with the caller's SP restored and does not change interrupt policy.
- On entry, `[sp+0]` is the return offset. Additional arguments start at `[sp+2]`; the caller removes them after return. A callee accounts for its pushes/local allocation when accessing these slots.
- Allocate locals with ADJSP using a negative constant, then use nonnegative slot offsets. Release them with a positive adjustment before RET. PUSH/POP preserve individual registers. Larger adjustments can use several ADJSP instructions, each with its own checks and commit; a later failure leaves earlier adjustments in effect. This is not an atomic whole-frame allocation.
- Exception handlers use saved context and IRET instead of this call convention; live-register changes are discarded unless copied into context with SETX.

## System-call ABI v1

The following is the baseline software convention, separate from instruction semantics and the ordinary CALL ABI:

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

Handlers may use any live registers because IRET restores the saved caller state. SETX edits only the fields that the service intends to return. Entry does not switch DS or select a kernel stack: install any handler DS/SS/SB/ST/SP explicitly, preserving caller state in the dedicated context. A handler that uses PUSH/CALL/locals needs a valid mapped stack of its own or a deliberately accepted caller stack. The companion syscall example needs no stack.

The one-context model also applies to system calls. External IRQs remain deferred throughout a handler, even after EI; a synchronous fault or another SWI is terminal Double Fault. Initial services should complete synchronously without relying on interrupt delivery while E=1. HALT in a handler remains a host-reset-only stop. Calling another kernel routine with ordinary CALL is possible with a valid handler stack; it does not create another exception context. The approved [revision 0.6 architecture](#revision-06-os-execution-architecture) adds preemptible kernel entry, fault recovery and nested interrupts; the rules in this paragraph remain the revision 0.5 behavior.


## Revision 0.6 OS execution architecture

This is the revised execution draft for the approved direction. It requires
prototype validation before an architectural freeze. Preserve 16-bit B0 arithmetic and B1
address/storage registers, little-endian values, 16-bit segment offsets,
24-bit physical memory, checked pointer/stack arithmetic and precise per-instruction
commit. No operating system, Linux binary compatibility, software errno values,
or device protocol is defined by this ISA.

| Mechanism | Architectural decision |
|---|---|
| Privilege | U/K modes outside CSR; saved return mode is protected; bootstrap can only drop K→U |
| Memory | 256 process-map IDs, 256 segment descriptors each, 64 KiB physical frames, per-mode R/W/X and bounds |
| Kernel mapping | Map 0 selected on entry; explicit protected access to user maps |
| Contexts | Eight ordinary hardware frames; failed entry or overflow gives terminal host diagnostics; no executable emergency environment |
| Kernel stack | Protected entry stack for U→K; interrupted kernel stack retained for K→K |
| Scheduling | Prevalidate a complete incoming U image, then replace an outermost U-origin frame under DI; no bulk-switch opcode |
| Interrupts | 16 pending/enable/in-service sources; deterministic lowest-source arbitration; independent ACK |
| Timer | 64-bit retired-instruction time, one-shot/periodic deadline, recorded idle advancement |
| Devices | K-controlled MMIO; pure preflight and infallible instruction commit |

The original context indices 0x00–0x14, CSR flag assignments, SWI imm8 and IRET
instruction bytes remain fixed. Additional context/control words, fault/vector
numbers and MMIO addresses have no assigned binary layout yet. The five-bit
GETX/SETX context selector must not silently grow; a new escaped context/control
access family can expose additional state. See the
[encoding requirements](ISA_ENCODING.md#revision-06-encoding-requirements).

### Architectural decision report

Status: design-review revision, **not an execution freeze**. The approved B0/B1,
segmented isolation and deterministic-time direction remains. The following
changes revise the unimplemented 0.6 draft; 0.5 bytes, metadata and behavior do
not change. New logical words/encoding layouts below have no assigned numeric IDs.

| Decision / normative draft change | Implementation consequence | Required conformance |
|---|---|---|
| Prevalidate a complete U image before outer-frame replacement; never replace provenance | Shared documented return validator, readable committed descriptors, pinned software backup; no bulk-switch opcode | Invalid candidate changes no outgoing field; inject faults after every SETX and verify rollback/fatal handling |
| IRQs disabled while frame and scheduler state are inconsistent | Kernel scheduling ABI; pending sources stay latched, DI does not mask faults | Boundary IRQ injection; timer/device nesting defers switching |
| Fault record identifies origin, phase, map, virtual start/relevant byte, physical address and reason | More read-only context words; copy-fault fixups match explicit UACCESS and known K PC | Rights vs backing/width failures; failed second byte changes nothing; K fetch failures never mislabeled as copy faults |
| Eight ordinary frames, terminal diagnostics on failed entry/overflow; remove executable emergency recovery | One entry path, no alternate map/stack/vector/retry or special IRET | Every failed entry stage and depth-8 overflow preserves frames and diagnostics until reset |
| Published-map validity plus software generation/lease protocol | Eight-bit IDs retained; kernel drains all live/saved references before invalidation/reuse | Invalid map rejected; stale software handle blocked before republishing/use; nested references prevent premature reuse |
| U I is fixed enabled, readable but not user-owned | U returns reject I=0; K I remains saveable/maskable | All U flag combinations; DI/EI/CSR attempts; no silent I correction |
| rel8, B1 simm6 and SP uimm8 layouts specified symbolically; ADDI/CMPI stay prototype candidates | No assigned opcode, no change to 0.5 decoder; protected execution and density milestones separated | Range/relaxation/reserved-bit tests plus baseline compatibility before allocating bytes |
| Three-byte extended context/control access is a layout candidate; bulk transfer deferred | Full eight-bit word selector with GPR in subopcode; timer/PIC remain MMIO | Compare complete context/IRQ paths after prototype; no speculative multiword atomic operation |

Executable emergency recovery is deferred because the ordinary nesting path
already handles expected invalid pointers, while broken entry configuration
cannot establish a trustworthy ordinary handler. This review chooses terminal
host diagnostics for the first implementation. A SWI retires only when its
live handler-entry bundle commits; capture alone does not retire it. The decision can be revisited
with a demonstrated recovery workload; it is not hidden implementation behavior.

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
| B0/B1, DP, checked ADDP, SP/ADJSP, ordinary CALL/RET | Existing checks and effects | Same |
| Read CSR, CS/DS/SS, SP/SB/ST | Allowed; exposes only current live state | Allowed |
| Direct CSR write, DI/EI, HALT | Privilege exception, no effects | Existing effects, E remains derived |
| Write DS | Select any present user data-capable descriptor; no memory probe | Select kernel descriptor |
| Write CS, JL | Validate user executable descriptor and target; remain U | Select kernel executable descriptor; remain K |
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
can use a kernel-installed second descriptor. A U-mode SS write differs from its
v0.5 behavior; this semantic change is confined to explicitly selected v0.6 mode.

Decode/reserved-byte failures precede privilege checking. After complete fetch,
a privileged opcode in U faults before operand-state, stack, data, or target
checks. For allowed segment writes, an illegal high byte is Invalid State;
missing descriptor/permissions are memory-protection faults. Privilege faults
save the instruction start and operation class; the instruction changes nothing.
U cannot write a frame containing its own return mode, alter live maps, change
interrupt masks, or configure vector/entry state.

Encoding: protect existing operations by mode checks; expose protected control
state through a new escaped control access family. No CSR bit or old register
selector is reassigned. Complexity: low for mode checks, moderate for validated
segment selection. Conformance: attempt every privileged operation from U and K,
including malformed encoding, invalid operand, inactive frame, and absent memory;
verify fault priority, no effects, useful user SP/DP/segment access, and no mode
change through JL or CSR writes. v0.5 remains its original single-mode machine.

### Protected segmented memory

The current 24-bit formula becomes a **guest virtual address**: `selector:offset`.
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
all mappings before reusing a frame is kernel responsibility; DMA is outside revision 0.6.

Translation uses a small protected segment-mapping table with no page walker
or TLB requirement. A descriptor lookup on every access is
sufficient. Whole-segment allocation and limited per-segment bounds are a real
cost: arbitrary 4 KiB sharing, sparse mappings within one segment, transparent
page-sized copy-on-write, and traditional Linux memory layouts are not supplied.
A kernel using segment-sized allocation can provide process isolation and
recoverable memory faults. Page-based translation and an unmodified Linux port are outside this revision.

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
resets in 0.6. Host debugger mutations are outside that execution contract.

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
references are rejected during return validation. User-access encodings and new
saved-field indices remain unassigned.

#### User-access fault contract

All mapping, protection, backing and disabled-user-access failures below use the
existing Memory Access cause/vector 2; no separate user-access vector is needed.
Every such fault records the following **logical words**, read-only through
context access. New context indices and numeric enum discriminants remain
unassigned; the word widths, meanings, validity rules and reset-on-entry behavior
below are the execution contract. On nonmemory entries, scalar/validity words
clear and semantic tags are NONE (their numeric codes remain pending); legacy
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
NOT_PRESENT, RIGHTS or DEVICE as appropriate. All numeric enum codes are pending.

Origin follows the **failed access**, not the opcode. A user-access data fault has
UACCESS / operand / READ-or-WRITE and identifies its selected process map. Failure
to fetch that instruction or represent its following K PC is KERNEL / FETCH.
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
6. Selected resulting PC/target validation follows operand preflight, as in 0.5;
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
before granting them to a different owner. Numeric control layouts are pending.

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

Encoding: existing memory bytes unchanged; new privileged descriptor/control and
user-access forms only. Complexity: moderate, one lookup and complete preflight
per access; no page walks. Conformance: two maps with identical virtual pointers
and different data; shared frame with differing rights; X-only/R-only/W-only and
U/K matrices; CS data rights; user mapping of a kernel physical frame denied by
kernel map ownership policy; holes/limits/unaligned words; failed second-byte
store; map edits; user-access faults while kernel code/stack remain available.
The CPU enforces descriptors; the kernel must never grant U rights to its private
frames. No hardware ownership tag can repair a malicious kernel map policy.

### Nested contexts, entry, and terminal diagnostics

There are exactly eight protected ordinary frames. Capture is independent of
user or kernel stack memory. GETX/SETX address only the top frame. E is derived
from ordinary depth; it does not block IRQs in 0.6. A K handler can set I after
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
configuration, special IRET or retry path exists in this revision. Host reset is
the recovery path. Such recovery can be reconsidered in a later revision only
with a demonstrated need and a separately validated state machine.

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
through the original syscall frame. A fault in K code no longer implies terminal
state merely because E=1. Conformance must cover depths 1–8, nested fault/IRQ
restoration, bad IRET recovery, each failed-entry stage and depth-8 overflow with
retained frames/diagnostics. No conformance test may expect guest emergency retry.

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
3. No terminal/entry transition is active; this revision has no emergency context.
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

Conformance: invalid candidates for every validation row leave A unchanged;
valid A/B switches preserve registers/maps/stacks; injected faults after each
SETX retain precise partial edits and take the documented rollback/fatal path;
IRQs injected at every boundary remain pending during DI; nested timer/device
handlers defer scheduling; rejected map generations never authorize reuse. The
0.5 single-context image remains unchanged and supplies no isolation.

### Interrupts and deterministic time

Use 16 sources with sticky PENDING, ENABLE and IN_SERVICE bitmaps. K reads status,
writes enable, and acknowledges each source with a write-one bit. A host/device
assertion sets pending; repeated edges coalesce. At a boundary, eligibility is
`pending & enable & ~in_service`, I=1, and not terminal. Choose the
lowest source index. Entry records source in the frame; only successful entry
clears its pending bit and sets its in-service bit. Failed entry leaves it pending.
Acknowledgment clears in-service only, not a later pending assertion. A level
source reasserts at the next boundary if still active. Document devices as edge
or level sources. Timer source 0 is fixed; the machine configuration assigns other sources.

No programmable priority or priority ceiling initially. A handler must acknowledge
or mask its source before enabling reentry of the same source. Other sources can
nest when I=1. Restoring I with IRET is separate from device acknowledgment.
PIC writes and newly asserted requests take effect at defined boundaries; events
with identical timestamps are batched before source arbitration. Acknowledgment
and a new edge at the same boundary preserve the new pending edge. Source numbers
are not vectors: a shared IRQ vector plus saved source is sufficient.

A 64-bit retired-instruction counter advances by one for every committed guest
instruction, including SWI, IRET and HALT. Faulting instructions, hardware entry,
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

Encoding: protected MMIO/control registers, no timer/PIC instruction family.
Complexity: low/moderate, bitmaps and integer deadlines. Conformance: distinguish
timer/device source, acknowledge independently, simultaneous tie order, pending
while masked/active, nested sources, new edge during service/ACK, failed entry
retains pending, exact retired-instruction deadlines, SWI/EI/IRET/HALT boundaries,
periodic overrun/overflow, recorded idle advance and reproducible two-process runs.
v0.5 retains its coalescing single IRQ and has no implicit timer.

### Device architecture and ordering

Devices use protected MMIO. Reserve kernel map
descriptors for device windows; never grant U rights or user-access mappings to
them. Ordinary MOV can access permitted MMIO. Console, storage, input, timer and
PIC register layouts are subsequent machine/device contracts. Devices expose
source IDs and status; kernel software translates OS calls to those registers.

All instruction fetches, vector targets, stacks, and execution must be RAM, never
MMIO. MMIO data instructions perform exactly one byte or word transaction; a
word is not two separate byte callbacks. Alignment/width/register permissions and
all CPU checks, including next/target checks, are completed before any irreversible
read or write. A transfer spanning devices or RAM/device boundaries faults before
side effects. No instruction has two device transactions in this baseline.

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

Complexity: moderate, principally transactional device contracts. Conformance:
width/alignment faults, rejected next-PC and pointer update before console input
consumption, word atomicity, no execute/stack MMIO, U denial, ordered writes,
replayable input and per-source completion. Production device protocols and host
integration require separate machine specifications.

### Reset and boot contract

Host reset clears all ordinary frames and terminal diagnostics, pending/in-service
sources and PIC enables; disables the timer with counter zero; clears live GPRs,
DP, flags and process/user-access selectors; and enters K with the v0.5 reset
stack values. User maps become invalid. Kernel map 0 resets to identity frame
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

### Code-density decisions

The measured [density evidence](ISA_Examples.md#code-density-evidence) supports
the following extension directions. The [symbolic encoding proposals](ISA_ENCODING.md#revision-06-encoding-proposals)
define operand layouts and selection rules without assigning private study bytes.
The 0.5 decoder continues to reject their new opcodes/reserved descriptor bits.

| Form | Disposition and semantics | Compatibility and conformance |
|---|---|---|
| rel8 branches | Approved extension direction: two bytes, all fifteen conditions, signed -128..127 from the selected instruction end | Retain rel16. Relax forward/backward branches iteratively; support forced short/long forms; test endpoints, cascading shrink, segment ends and execute faults. New opcode family remains unassigned. |
| B1 simm6 | Approved extension direction: a=0 descriptor `ddddddpp` carries signed -32..31; a=1 retains disp16 and zero upper descriptor bits | Existing zero-displacement bytes are identical. Test -32/31, long fallback -33/32, CS/DS selection, base/value aliases and atomic protection faults. Revision-specific metadata remains to be added. |
| SP uimm8 | Approved extension direction: three-byte byte/word load/store with unsigned 0..255 slot offset | Retain off16 and identical allocation/SS/protection checks. Test 0/255/256, negative rejection and failed-word-store atomicity. New family remains unassigned. |
| B0 ADDI/CMPI imm4 | Measured candidates: two bytes, signed -8..7 sign-extended to a word; results/flags match register ADD/CMP | B0 only, no hidden temporary. CMPI does not write its destination. Further encoding allocation is separate from architecture approval. |
| SUBI imm4 | Deferred: ADDI+CMPI matched the three-operation study sizes | ADDI #-1 matches SUBI #1 under no-borrow carry; SUBI #0 and SUBI #-8 have distinct cases, but this corpus does not justify an opcode. |
| B1 post-increment | Deferred pending compiler/runtime evidence despite copy-loop savings | If adopted, update by width; reject a load whose destination is its base; stores sample the old source/base. Validate pointer adjustment before transfer, commit both together, preserve flags, and never permit segment wrapping or an unrepresentable final pointer. |

General B1 ALU, multiply/divide, variable shifts, rotations, far/indirect CALL,
and wider pointer/stack immediates remain deferred. The existing B1 callee-save
ABI remains a software convention. Code relocation, compact forms and instruction
fusion can change fault PCs and interrupt observation points; matching successful
outputs is not equivalence of those observation points.

### Implementation conformance for revision 0.6

The following are required before a 0.6 implementation can claim conformance:

- Add versioned binary/control layouts in ISA_ENCODING.md, retaining the 0.5
  table and explicit image/host revision selection. An image cannot change its
  revision by executing an opcode.
- Update CPU, assembler/disassembler and conformance tests together. Preserve
  all 0.5 bytes and behavior in 0.5 mode, including its terminal nesting policy.
- Exercise U→SWI→nested user-access fault→known fixup→U; invalid user stacks;
  protected return mode and descriptor bounds; every ordinary depth and terminal
  entry-failure/overflow stage; all privilege and fault-priority combinations.
- Alternate two isolated processes at identical virtual addresses using timer
  interrupts and software context saves. Verify registers, flags, maps and stacks,
  and defer switching under nested kernel frames.
- Verify independent device-source ACK, MMIO width and commit ordering,
  deterministic timer deadlines/idle events and reset from every terminal state.

Prototype privilege, maps, ordinary entry/nesting, software switching, PIC, timer
and MMIO in that order. Each milestone needs its own conformance checks. Compact
encodings follow as a separate optimization milestone after protected execution;
none is required to validate isolation or scheduling. Map-lifetime and scheduler
critical sections are kernel ABI requirements, not additional CPU instructions.
No complete operating system is required.

## Revision history

| Revision | Change |
|---|---|
| 0.2 | B1 CS/DS indirection and signed displacement |
| 0.3 | Two-byte ADDP at $20–$24, including DP; old escaped draft forms illegal |
| 0.4 | ADJSP at $25 and register-target JS at $1F $60–$63 |
| 0.5 | SWI at $26, cause 6, GETX xtrap at $1F $34 and the separate syscall ABI |
| 0.6 | Approved protected-segment direction; revised execution draft with prevalidated software switching, explicit fault origin, terminal entry failures and map-lifetime ABI; not frozen or implemented |

## Conformance requirements

Use these cases as the emulator test contract for revision 0.5. [README.md](../README.md#validation) maps current real-CPU checks to these areas. Observe actual CPU state and memory, rather than treating the temporary design model as the implementation oracle. Revision 0.4 images retain their older decoder rules; the software-trap cases explicitly test revision isolation. Reset failures, terminal diagnostics, mixed-fault priority, and installed-RAM holes require real-emulator coverage, independently of prior design-model results.

### Arithmetic and flag cases

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

### Addressing, encoding, and PC cases

Unless overridden, use CS=DS=SS=0, PC=$0100, valid installed RAM, and I=0. For raw fault checks, install a valid corresponding vector and observe the saved context; the ordinary instruction effects listed below must not occur before entry.

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
| Reserved B1 selector bits | Bytes `15 80 04` | Illegal Instruction; bit-7-one forms themselves are assigned in revision 0.2 |
| Reserved shift bits | Bytes `1A 14` | Illegal Instruction |
| Reserved context register-byte bits | Bytes `1F 28 08` | Illegal Instruction, even when E=0 |
| Far jump | `53 00 02 03 00` | CS=3, PC=$0200 |
| Invalid far-jump high byte | `53 00 02 03 01` | Invalid State; CS unchanged |
| Invalid segment write | r0=$0100; MOV r0,ds (`10 D2`) | Invalid State; DS unchanged |
| Reserved CSR bit | r0=$0010; MOV r0,csr (`10 93`) | Invalid State; CSR unchanged |
| Explicit CSR write | Write defined bits with E either zero or one | Update N/Z/C/V/I; retain actual active E |
| CS write | MOV r0,cs | Fetch operands in old CS; commit following offset in new CS after target validation |

### B1 pointer transfer and adjustment cases

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
| Unassigned neighboring primary | `$27` | Illegal Instruction; $25 is ADJSP and $26 is SWI, not additional ADDP destinations |
| Final representable next | ADDP at $FFFD with a valid sum | Success; PC=$FFFF and destination updated |
| Final-PC overflow | ADDP at $FFFE with a valid sum | Memory Access; destination unchanged before entry |
| Flags between arithmetic | CMP, then ADDP, then a branch; ADD, then ADDP, then ADC | Branch sees CMP flags; ADC consumes ADD's carry |

For every failed instruction, check its original PC and registers in the saved context; exception entry itself sets E and clears I. ADDP range faults, for both B1 and DP, report Invalid State with the instruction address because adjustment accesses no memory. Direct SP-relative moves still prohibit slots below SP; B1 accesses remain ordinary memory accesses and do not acquire stack limits when their segment aliases SS.

### Stack and call cases

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

### Register-target JS cases

- `JS r4` through `JS r7` encode as `1F 60` through `1F 63`. Use CS=2, DS=3, and distinguish their memory contents. Only PC changes to the original selected register's offset; CS, SP, other registers, and every flag remain unchanged.
- Load a table entry with `MOV.W [cs:r4],r5`, then JS r5. There is no extra target-word read, stack push, or return address; the worked dispatch example must skip its $DEAD fallthrough and produce $BEEF.
- An unavailable first byte at CS:target causes Memory Access before effects and reports that physical target address.
- A mapped target containing an illegal opcode permits the jump to commit. The next instruction faults at the target, with its own saved PC, rather than at the JS.
- A complete two-byte JS at $FFFE may jump successfully; unused next=$10000 is allowed for an explicit target. A prefix at $FFFF still faults fetching the missing second byte.
- JS r0, JS dp, JS [r4], and indirect CALL are rejected by the assembler. `$1F $64` remains illegal rather than implying an indirect call.

### Exception, interrupt, and reset cases

- Install vector 0 as `00 01 00 00`. Reset produces CS:PC=0:$0100, zero general registers/DP/DS/SS/CSR, SB=0, SP=ST=$FFFE, no pending IRQ, and inactive context. RAM is preserved.
- Fault on PUSH with SB=SP=ST=0. Entry succeeds without using the stack. Vector 3 is read at physical $00000C. Saved context contains the original registers and faulting PC; live E=1 and I=0.
- Read xpc with `1F 28 00` (GETX xpc,r0), edit it using `1F 48 00` (SETX r0,xpc), and IRET with `1F 12`. The edited PC resumes; all other saved general/internal registers are restored. Live handler register changes are discarded.
- Repair an invalid saved stack configuration through SETX before IRET. An unrepaired configuration or invalid restored segment/CSR causes terminal Double Fault, preserving the original context.
- GETX/SETX/IRET outside an active context raise Invalid State. Encoded writes to read-only cause/address/service context words raise Illegal Instruction.
- Fetch a later missing instruction byte: xpc names the instruction start, while the address words identify the missing byte. An out-of-range effective offset clears the address-valid bit. External IRQ address words are zero.
- Cause a second synchronous fault in a handler. Enter terminal Double Fault without replacing the first context. An unavailable vector entry, nonzero vector high byte, or unavailable handler target likewise terminates.
- Latch an IRQ with I=0; it stays pending. Execute EI; accept it at the following boundary, saving the following PC. An already eligible IRQ is accepted before DI is fetched.
- Set E=1 and I=1 with a pending IRQ; delivery remains deferred. After a valid IRET restoring I=1, accept it before the resumed instruction.
- HALT with I=1 advances PC and sleeps. An IRQ wakes it and saves that following PC; IRET resumes there. HALT with I=0 stays asleep on a pending IRQ. Multiple requests coalesce.
- Host reset exits HALT and terminal state, discards pending IRQ/context, and reloads vector 0. An invalid reset vector or unavailable reset target gives terminal Reset Fault with no active context.

### Instruction-boundary recovery cases

These sequences test actual instruction boundaries, not an artificial transaction spanning a prologue. Install valid vectors and mapped handler code. For a fault, observe the saved context after entry, distinguishing its pre-entry flags from live E=1/I=0.

| Scenario | Setup and sequence | Expected result |
|---|---|---|
| Saved E/reserved bits: four cases | In a handler, SETX values $0040, $0010, $006F, or $8000 into xcsr, then GETX and IRET | SETX succeeds and GETX returns the exact word; live flags stay unchanged. IRET rejects the value with Invalid State, escalates to TerminalDoubleFault, and retains the context including that SETX edit. |
| Valid saved CSR and known resume PC | A fixed image has illegal byte $FF at $0100 and NOP at $0101. Handler SETX writes xpc=$0101 and xcsr=$0005, then IRET | Resume at the known $0101 address with CSR=$0005/E=0; original general registers are restored. NOP advances to $0102. The handler obtains the address from the known layout, not new length metadata. |
| Partially allocated frame | SB=$1000, SP=ST=$10C8; execute ADJSP #-128 twice | First succeeds at $1048; second proposes $0FC8 and faults. Saved xsp=$1048 and xpc identifies the second instruction. No sequence rollback occurs. |
| ADDP upper boundary: two destinations | r4 or DP starts at $FF80; execute ADDP #127, then ADDP #1 | First reaches $FFFF; second faults with Invalid State and saves the already-advanced $FFFF value. |
| ADDP lower boundary: two destinations | r4 or DP starts at $007F; execute ADDP #-127, then ADDP #-1 | First reaches zero; second faults with Invalid State and saves zero. |
| IRQ between adjustments | Use the partially allocated-frame setup, accept an IRQ after the first ADJSP, and IRET without modifying the context | IRQ saves xsp=$1048 and the second instruction's PC. IRET resumes there; the second adjustment still faults with $1048 saved. |
| Stack initialization repaired without stack use | Set ST=$1000 while SP=$FFFE, then trigger a synchronous fault. Handler uses GETX/SETX and B0 only to set xsp=xst and xpc to a known resume address | Entry succeeds despite invalid live stack configuration. IRET restores SP=ST=$1000 and original general registers/flags; no guest-stack writes occur. |
| Stack initialization unrepaired | Enter from the same invalid configuration, then IRET without repairing saved stack bounds | TerminalDoubleFault; retain the original invalid saved stack and context. |
| Handler HALT with pending IRQ | Enter a handler; latch an IRQ; execute EI then HALT | Remain Halted with E=I=1, context retained, request pending, and no terminal fault. Repeated execution steps do not fetch, deliver the IRQ, or run IRET. Only host reset exits; verify reset against the actual emulator. |

The parameterized rows define fourteen recovery scenarios. Failed IRET must not silently clear E/reserved bits or replace the saved context with another snapshot. Software recovery from a multi-instruction allocation must account for completed instructions; prevalidation is not sequence atomicity. No decoded-length context field, wider adjustment encoding, or indirect call is assigned by these clarifications.

### Software-trap and system-call cases

These are the required revision 0.5 software-trap cases:

| Case | Required behavior |
|---|---|
| Every service number | Encode unsigned 0–255 in exactly two bytes; xtrap is zero-extended and xcause=6, including service 0. |
| Alias and invalid operands | TRAP emits the same bytes; reject #-1, #256, register operands, missing operands, and size suffixes. |
| Masking and flags | Exercise all 32 valid caller N/Z/C/V/I combinations: enter regardless of I; live NZCV retained, I=0/E=1; unchanged xcsr restored by IRET. |
| Exact continuation | SWI at $0106 saves $0108; return never executes SWI again. Restore nonzero caller CS after a handler in a different segment. |
| Context metadata | xtrap at index $14 is read-only; GETX accepts all eight GPR destinations, rejects upper register-byte bits before checking active context, and faults Invalid State outside a handler. SETX `$54` remains illegal before a third-byte fetch. |
| Ordinary preservation/results | Use distinct initial values in every register. Handler clobbers live registers; IRET restores them except explicit xr0/xr1 edits. Verify DP, segments, stack, caller flags and all unedited context words. |
| Unsupported service | Handler returns OS error normally, preserving xpc and caller interrupt policy; hardware never rejects an otherwise valid immediate. |
| Invalid caller stack | Entry succeeds without using/probing stack RAM; unrepaired IRET double faults. Repair saved limits/SP and verify successful return. |
| Fetch failure | Missing immediate yields cause 2, fault-start xpc and xtrap=0. At PC=$FFFF, no wrap to offset zero. |
| Continuation boundary | SWI at $FFFD can save $FFFF; at $FFFE it faults cause 2 with xpc=$FFFE and no valid address. |
| Uninstalled continuation | SWI entry succeeds when next is uninstalled; IRET then double faults unless xpc/xcs were repaired to an installed target. |
| Bad trap vector | Test each missing vector byte, reserved high byte, and missing handler target: terminal Double Fault retains SWI's following-PC context and xtrap. An illegal installed handler opcode fails only on its decode. |
| Nested operation and priority | E=1 SWI causes Invalid State/Double Fault, preserving the old context. Missing immediate takes fetch-fault priority over E=1; E=1 takes priority over nonrepresentable next. |
| IRQ ordering | Eligible IRQ before SWI wins and saves SWI's start; masked/newly arriving IRQ is retained through SWI; IRET restoring I=1 permits delivery at the following-PC boundary. |
| Later exceptions/reset | After return, a later non-SWI exception sets xtrap=0. Reset discards the whole context and pending requests as before. Handler HALT still requires host reset. |
| Revision isolation | Revision 0.4 rejects `$26` immediately without reading an immediate, and rejects `$1F $34` without reading a register byte. Revision 0.5 accepts these forms; do not silently enable them in a 0.4 image. |
