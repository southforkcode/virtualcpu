# ISA Review: Virtual 16-bit CPU

I've reviewed your current ISA specification, including the register architecture, instruction encodings, memory addressing modes, condition flags, branching model, and Fibonacci example.

**My overall assessment: This is a promising foundation for a compact, relatively elegant 16-bit virtual CPU.** The design has a recognizable architectural philosophy, and several decisions are particularly well suited to a software-emulated processor.

However, there are some architectural gaps worth addressing before expanding the instruction set. A few are significant enough that they could force changes to your existing encodings.

I'll approach this as an ISA design review rather than a critique of the documentation itself.

## 1. Architectural assessment

Your ISA currently combines several interesting characteristics:

| Feature | Current design | Assessment |
|---|---|---|
| Architecture | 16-bit, register-based | Good foundation |
| General registers | 8 × 16-bit | Reasonable for a compact CPU |
| Register classes | Accumulator, scratch, internal | Interesting, but introduces constraints |
| Address space | Segmented, with 16-bit offsets | Promising, needs formalization |
| Instruction width | Variable, 1–4 bytes | Good for code density |
| ALU operations | Register-to-register | Simple and consistent |
| Memory operations | Register-to-memory, immediate-to-register | Clean separation |
| Addressing | Absolute, DP-relative, SP-relative | Useful initial selection |
| Condition flags | Z, N, C, V, I, E | Conventional and sufficient for basic operations |
| Branching | 16-bit relative, condition-code based | Comprehensive |
| Stack | Downward-growing, boundary checked | Good safety consideration |
| Exceptions | Partially defined | Needs architectural specification |

The most important design choice is your separation between **accumulator registers (`r0–r3`) and scratch registers (`r4–r7`)**.

This creates an architecture somewhat reminiscent of older accumulator-oriented processors, but with more flexible register transfers.

Your instruction encoding also suggests that you're optimizing for:

- Compact instruction representations.
- Relatively simple decoding.
- Small register selectors.
- A limited number of addressing modes.
- Extensibility through secondary opcodes.

These are sensible goals for a virtual processor.

One question I would keep in mind throughout the design is:

**Are you optimizing primarily for instruction density, implementation simplicity, or the quality of the assembly programming experience?**

You can balance all three, but some decisions will favor one over the others.

---

# 2. Register architecture

## 2.1 Accumulator versus scratch registers

You define:

| Class | Registers | Intended role |
|---|---|---|
| Accumulator | `r0–r3` | Arithmetic and working values |
| Scratch | `r4–r7` | Temporary storage |
| Internal B0 | `sp`, `sb`, `dp`, `csr` | Processor state |
| Internal B1 | `ss`, `cs`, `ds`, reserved | Segmentation |

Your ALU encoding is:

```text
$10 0ooo ss dd
```

This provides two-bit source and destination selectors.

Consequently, ALU operations appear restricted to `r0–r3`.

For example:

```asm
ADD r0, r1    ; valid
ADD r4, r1    ; not directly encodable
```

This is a meaningful architectural tradeoff.

### Advantages

The restriction makes your ALU encoding compact.

With two bits per register, you can fit:

- Three bits of ALU operation selection.
- Two bits for the source.
- Two bits for the destination.
- One bit for opcode classification.

All within the second instruction byte.

It also gives the CPU a clear distinction between working registers and storage registers.

### Disadvantages

The restriction increases register movement.

Consider your Fibonacci example:

```asm
MOV r4, r3
SUB r3, r2
```

You're forced to transfer the constant from a scratch register into an accumulator before performing arithmetic.

This costs an additional instruction.

The effect becomes more pronounced when implementing algorithms with several simultaneously active values.

For example, an algorithm requiring five arithmetic operands must continually move values between scratch and accumulator registers.

### Recommendation

**I would retain the accumulator/scratch distinction for now.**

It is one of the more distinctive aspects of your architecture, and the encoding benefits are substantial.

However, I would explicitly document that:

> ALU operations are restricted to accumulator registers `r0–r3`. Scratch registers `r4–r7` are accessible through register transfer and memory instructions.

This distinction is currently implied by the encoding rather than stated as an architectural rule.

I would also consider eventually introducing a limited number of operations that work directly on scratch registers, but only if actual programs demonstrate the need.

---

## 2.2 The register-transfer encoding deserves closer examination

Your primary register move instruction uses:

```text
00 01 ss dd
```

The four-bit combination of `ss` and `dd` provides 16 possible encodings.

You reserve the cases where `ss == dd` for extended instructions:

```text
$10
$15
$1A
$1F
```

This is clever.

It effectively repurposes register-to-self operations as escape opcodes.

However, it introduces a subtle complication.

Normally:

```asm
MOV r0, r0
```

would be a valid instruction, even if it accomplishes nothing.

In your architecture, the corresponding encoding instead introduces an extended instruction.

This is not inherently problematic, but the assembler must understand that these four encodings are unavailable for ordinary moves.

More importantly, your extended move operations include transfers between different register banks.

I would explicitly document the relationship between the primary move encoding and the extended transfer modes.

There is also a naming ambiguity in the current extended move table:

```text
000  Internal B0 -> regular
001  Regular -> internal
010  Scratch -> regular
011  Regular -> scratch
100  Internal B1 -> regular
101  Regular -> internal B1
```

Does *regular* always mean an accumulator register?

If so, say so.

Otherwise, the two-bit register selectors are insufficient to identify all eight general registers without additional interpretation.

**Recommendation:** Use the terms `accumulator`, `scratch`, `internal B0`, and `internal B1` consistently throughout the encoding specification.

This will make implementing the decoder much easier.

---

## 2.3 Internal registers and architectural state

Your internal registers include:

```text
sp
sb
dp
csr
pc
cs
ds
ss
```

I like the decision to expose `sp`, `sb`, and `dp` through register transfers.

This allows programs to manipulate stack and data pointers without requiring specialized instructions for every operation.

However, it raises an important question:

**What happens when a program writes directly to `sp`, `sb`, or `csr`?**

For example:

```asm
MOV r0, sp
```

Does this operation immediately validate the new stack pointer against the stack boundary?

Similarly:

```asm
MOV r0, csr
```

Can software directly modify interrupt and exception flags?

These behaviors need to be specified.

I would distinguish between:

1. Registers that are freely writable.
2. Registers that have restricted bits.
3. Registers whose writes trigger validation or other processor behavior.

You may eventually need privilege rules, but you do not necessarily need a privileged execution mode yet.

---

# 3. Memory architecture and segmentation

This is one of the areas where I would spend the most design effort.

Your CPU has three segment registers:

```text
cs   code segment
ds   data segment
ss   stack segment
```

Each is physically represented as a 16-bit register, but currently only its low eight bits are used.

You also have a 16-bit program counter.

This suggests a segmented address model with up to 256 segments of 64 KiB each.

That gives a potential physical address space of:

\[
2^8 \times 2^{16} = 2^{24}
\]

Or **16 MiB of addressable memory**.

This is a reasonable arrangement for a virtual 16-bit processor.

However, the specification doesn't yet define the address translation formula.

## 3.1 Define address formation explicitly

One possible model is:

\[
\text{PhysicalAddress} = (\text{Segment} \ll 16) \;|\; \text{Offset}
\]

For example:

```text
DS = $03
DP = $1200

Physical address = $031200
```

This gives you nonoverlapping 64 KiB segments.

It is substantially simpler than the overlapping segment model used by the original x86 architecture.

For a virtual CPU, I would strongly prefer the simpler model.

It also means:

```text
CS:PC  -> instruction address
DS:DP  -> data address
SS:SP  -> stack address
```

You should explicitly state which segment each addressing mode uses.

For example:

| Addressing mode | Default segment |
|---|---|
| Instruction fetch | `cs` |
| Absolute `[addr16]` | `ds` |
| `[dp+off16]` | `ds` |
| `[sp-rel16]` | `ss` |

This would make your memory model much more predictable.

## 3.2 Segment boundary behavior

Consider:

```text
DS = $02
DP = $FFFF
```

Now execute a 16-bit load:

```asm
MOV.W [dp+0], r0
```

The operation requires two bytes.

Where does the second byte come from?

Possible behaviors include:

- Wrap to offset `$0000` in the same segment.
- Continue into the next physical segment.
- Raise a memory boundary exception.

All three are implementable.

My preference would be **a boundary exception for multi-byte accesses crossing a segment boundary**.

This avoids implicit segment transitions and makes memory protection easier to implement later.

However, if your goal is compatibility with familiar 16-bit wraparound behavior, wrapping within the segment is also defensible.

The important thing is to make the behavior explicit.

## 3.3 Signed versus unsigned displacements

Your memory addressing modes include:

```text
[addr16]
[dp+off16]
[sp-rel16]
```

You explicitly describe branch displacements as signed, but the memory displacement semantics are less clear.

I would recommend:

```text
addr16    unsigned absolute offset
off16     signed displacement
rel16     signed displacement
```

That would permit:

```asm
MOV.W [dp+4], r0
MOV.W [dp-4], r0
MOV.W [sp-2], r0
```

One concern: your stack addressing mode uses subtraction rather than addition.

```text
[sp-rel16]
```

If `rel16` is signed, subtraction is unnecessary to support negative displacements.

For example:

```text
SP = $1000
rel16 = -4

SP - (-4) = $1004
```

That can be perfectly valid, but it makes the addressing model asymmetric.

**I would favor `[sp+disp16]` and `[dp+disp16]`**, both using signed displacements.

The stack can still grow downward; addressing syntax does not need to encode stack growth direction.

---

# 4. Instruction encoding

Your instruction encoding is compact and mostly regular.

There are nevertheless some important inconsistencies and opportunities.

## 4.1 The `$10` extended opcode is well structured

You use:

```text
$10 0ooo ss dd
$10 1mmm ss dd
```

This gives you:

- Eight ALU operations.
- Eight register-transfer modes.
- Four source selectors.
- Four destination selectors.

The resulting instructions are two bytes long.

That is efficient.

For example:

```asm
ADD r1, r2
```

requires only two instruction bytes.

One small correction to the documentation:

The field `0ooo` is four bits, but the actual ALU operation selector is only three bits.

I would describe the format as:

```text
0 ooo ss dd
│ │   │  │
│ │   │  └── Destination accumulator
│ │   └───── Source accumulator
│ └───────── ALU operation
└─────────── Instruction group selector
```

This makes the decoder layout unambiguous.

## 4.2 The `$15` load/store encoding is a good design

Your memory-transfer format is:

```text
$15 xdmm zrrr operand
```

I particularly like the separation between:

- Direction.
- Addressing mode.
- Operand size.
- Register selection.

It is compact and relatively straightforward to decode.

For example, the same instruction family supports:

```asm
MOV.B #10, r0
MOV.W #1000, r0

MOV.B [$2000], r0
MOV.W [$2000], r0

MOV.B r0, [$2000]
MOV.W r0, [$2000]
```

The format also allows all eight general-purpose registers to participate in memory transfers.

That is a good decision.

### One concern: the reserved `x` bit

You currently require:

```text
x = 0
```

Setting it raises an illegal instruction exception.

This leaves a useful expansion opportunity.

I would keep it reserved.

However, document whether future extensions may reinterpret that bit while preserving the rest of the instruction format.

For example, it could eventually distinguish between different addressing families.

There is no reason to assign it a function prematurely.

## 4.3 Byte-order is unspecified

Your instruction formats contain 16-bit and 32-bit operands, but the specification does not define how those values are stored.

For example:

```asm
MOV.W #$1234, r0
```

Does the immediate appear as:

```text
12 34
```

or:

```text
34 12
```

I would choose one endianness for both instruction operands and memory data.

Little-endian would be a conventional choice, but either is fine for a new virtual architecture.

The important thing is consistency.

---

# 5. Condition flags and arithmetic

Your condition flag design is conventional:

```text
Z  Zero
N  Negative
C  Carry
V  Overflow
I  Interrupt enable
E  Exception
```

The branch conditions are also well chosen.

In particular, you have the full set of useful signed and unsigned comparisons.

However, there is a significant missing operation.

## 5.1 You need a comparison mechanism

Currently, you have:

```asm
SUB rs, rd
```

which updates:

```text
N Z C V
```

But subtraction also modifies the destination register.

Suppose you want to compare two values:

```asm
; Is r0 greater than r1?
```

Without a comparison instruction, you must preserve one operand.

For example:

```asm
MOV r0, r2
SUB r1, r2
BGT greater
```

This is functional but inefficient.

I would strongly recommend adding:

```asm
CMP rs, rd
```

Semantically:

```text
temp = rd - rs
update N, Z, C, V
discard temp
```

This gives you comparisons without destroying register contents.

It also fits naturally into your existing ALU design.

### The encoding problem

Unfortunately, your current ALU operation selector is fully occupied.

You have:

```text
XOR
AND
OR
NOT/MOV complement
ADC
SBC
ADD
SUB
```

All eight available combinations are assigned.

This is the first significant indication that your ALU encoding may be too restrictive.

You have several options:

1. Introduce another extended opcode group.
2. Reassign one existing ALU operation.
3. Add an additional instruction prefix.
4. Expand the ALU encoding.

I would not redesign the entire encoding immediately.

Instead, I would investigate whether a second ALU opcode group could accommodate `CMP`, shifts, and other future operations.

---

## 5.2 Consider a `TST` instruction

You currently use:

```asm
OR r2, r2
```

to test whether a register is zero.

This works because OR updates `N` and `Z`.

Your Fibonacci example demonstrates this technique.

A dedicated instruction could be:

```asm
TST r2
```

Semantically:

```text
N = bit15(r2)
Z = (r2 == 0)
```

This is not essential.

In fact, if code density is a priority, reusing:

```asm
OR r2, r2
```

is perfectly reasonable.

I would prioritize `CMP` over `TST`.

---

## 5.3 Carry semantics need to be explicit

Your `SBC` instruction uses:

```text
rd = rd - rs - (1-C)
```

This means:

```text
C = 1  -> no borrow
C = 0  -> borrow
```

That is a conventional and useful choice.

It also matches your unsigned branch conditions:

```text
BCS -> unsigned higher or same
BCC -> unsigned lower
```

However, your `SUB` documentation should explicitly state that carry is set when no unsigned borrow occurs.

For example:

```text
SUB 3, 5
```

means:

```text
5 - 3 = 2

C = 1
Z = 0
N = 0
V = 0
```

Whereas:

```text
SUB 5, 3
```

means:

```text
3 - 5 = $FFFE

C = 0
Z = 0
N = 1
V = 0
```

This behavior is implied by your branch definitions, but making it normative will prevent emulator bugs.

---

## 5.4 Flag preservation

Your logical operations update:

```text
N, Z
```

but not:

```text
C, V
```

That means an operation such as:

```asm
AND r0, r1
```

preserves the carry and overflow flags from earlier arithmetic.

This can be useful.

It can also be surprising.

For example:

```asm
ADD r0, r1
AND r2, r3
BCS somewhere
```

The branch examines the carry generated by `ADD`, not `AND`.

I would keep this behavior, but document explicitly that flags not listed in the updated-flags column remain unchanged.

That should be a general ISA rule.

---

# 6. Missing instructions and programming capabilities

This is where I see the largest gap between your current ISA and a processor capable of supporting general-purpose programs.

Your current instruction set can execute arithmetic, transfer data, and perform conditional branches. However, several fundamental programming operations remain unavailable.

## 6.1 Shifts and rotations — high priority

You currently have no shift instructions.

I would consider these essential.

At minimum:

| Instruction | Operation | Flags |
|---|---|---|
| `SHL` | Shift left, inserting zero | N, Z, C |
| `SHR` | Logical shift right, inserting zero | N, Z, C |
| `SAR` | Arithmetic shift right, preserving sign | N, Z, C |
| `ROL` | Rotate left | N, Z, C |
| `ROR` | Rotate right | N, Z, C |

These enable efficient multiplication and division by powers of two, bit-field manipulation, and many other common operations.

For example, without shifts, even extracting an arbitrary bit field becomes unnecessarily complicated.

I would initially implement single-bit shifts.

You can introduce variable-count shifts later if real programs justify them.

One decision to make is whether shifts should preserve or clear `V`. I would favor preserving it unless you define a specific overflow interpretation.

## 6.2 Stack operations — high priority

Your specification defines a stack pointer, stack base, and stack segment.

However, it does not yet define instructions for pushing or popping values.

I would add:

```asm
PUSH r0
POP  r0
```

For a downward-growing stack, one possible convention is:

```text
PUSH.W:
    SP = SP - 2
    memory[SS:SP] = value

POP.W:
    value = memory[SS:SP]
    SP = SP + 2
```

These operations should not modify arithmetic flags.

You also need to specify when stack boundary checks occur and whether failed operations leave the architectural state unchanged.

**One important ambiguity:** Your `sb` register is described as the stack base, while the specification says extending `sp` past `sb` causes an exception.

For a downward-growing stack, the initial stack pointer is typically near the upper end of the stack, and overflow occurs at its lower boundary.

If `sb` represents that lower boundary, I would call it the *stack limit* rather than the *stack base*.

You may also eventually want an upper boundary to detect stack underflow.

## 6.3 Subroutine calls and returns — high priority

You currently have:

```asm
JS addr16
JL addr32
```

These are jumps, not subroutine calls.

There is no instruction that automatically preserves a return address.

You could implement subroutine calls through explicit register and stack manipulation, but that is awkward with the current instruction set.

I would recommend introducing:

```asm
CALL addr16
RET
```

Eventually, you may also want a far-call variant.

This raises several architectural questions:

- Does `CALL` push only `PC`, or both `CS` and `PC`?
- Does `RET` restore the code segment?
- Is the return address the instruction immediately following the call?
- How does a stack overflow during `CALL` behave?

I would initially define near calls that preserve only `PC`.

A far-call mechanism can be added when necessary.

## 6.4 Increment and decrement — medium priority

Your Fibonacci example uses:

```asm
MOV r4, r3
SUB r3, r2
```

to decrement a counter.

A dedicated instruction would be simpler:

```asm
DEC r2
```

Similarly:

```asm
INC r2
```

These are convenient, but I would not necessarily prioritize them over `CMP`, shifts, and stack operations.

A useful alternative would be to introduce a compact immediate arithmetic instruction:

```asm
ADD #1, r2
SUB #1, r2
```

This would be more general than dedicated increment/decrement operations.

The tradeoff is instruction size.

For your architecture, a small immediate arithmetic format might be especially valuable because only four registers can participate directly in ALU operations.

---

# 7. Branching and control flow

Your conditional branch family is comprehensive.

I particularly like that you support both signed and unsigned comparisons.

The branch encoding is also straightforward:

```text
0100 cccc
```

This provides 16 possible branch conditions.

You use 15 and reserve one.

That is reasonable.

## 7.1 Define the PC-relative base

Your branch semantics are written as:

```text
PC <- PC + rel16
```

However, this is ambiguous unless you define the value of `PC` when the calculation occurs.

For example:

```text
Address $1000: BNE +4
```

Does the offset apply relative to:

- The beginning of the branch instruction?
- The address following the opcode?
- The address following the entire instruction?

I recommend using the address immediately following the complete instruction.

Thus:

```text
PC = $1000
Instruction length = 3
Displacement = +4

Target = $1007
```

This is convenient for assemblers and relocation.

You should also define whether the resulting 16-bit PC offset wraps within `CS` or raises an exception.

## 7.2 Short branches could improve code density

Every conditional branch currently requires three bytes.

For a compact virtual CPU, this is acceptable.

However, most branches in typical programs target nearby instructions.

Consider:

```asm
loop:
    ADD r0, r1
    SUB r2, r3
    BNE loop
```

A signed 8-bit displacement would be sufficient.

You could eventually introduce short branches:

```asm
BNE.S loop
BNE.W loop
```

With sizes:

| Form | Size |
|---|---:|
| Short relative branch | 2 bytes |
| Existing relative branch | 3 bytes |

I would treat this as a future optimization rather than an immediate requirement.

Your existing 16-bit branches are sufficient for initial implementation.

---

# 8. Exceptions and interrupts

Your specification defines two status bits:

```text
I = Interrupt enable
E = Exception
```

It also references:

- Illegal instruction exceptions.
- Stack boundary exceptions.

However, it does not yet describe how exceptions are handled.

This is a significant architectural omission.

## 8.1 Define exception entry

At minimum, the architecture needs to answer:

1. Where is the exception handler located?
2. How is the previous execution state preserved?
3. Are interrupts automatically disabled during exception handling?
4. How does execution return to the interrupted program?
5. What happens if an exception occurs while handling another exception?

One possible approach is a fixed exception-vector table.

For example:

```text
Vector 0: Reset
Vector 1: Illegal instruction
Vector 2: Stack boundary
Vector 3: Memory access fault
Vector 4: External interrupt
```

This is only a proposed design, not something established in your current specification.

You could also use a single exception entry point and expose the exception reason through a register.

For a small virtual CPU, that might be simpler.

## 8.2 Define the interrupt-enable flag

You currently specify:

```asm
CLI    ; disable interrupts
SEI    ; enable interrupts
```

This is understandable, although the traditional meanings of these mnemonics on some processors are reversed relative to your definitions.

I would consider using:

```asm
DI     ; disable interrupts
EI     ; enable interrupts
```

These are less ambiguous.

Alternatively, retain your existing mnemonics and clearly document their semantics.

## 8.3 Exception return

You will eventually need an instruction such as:

```asm
IRET
```

This would restore the processor state saved during exception entry.

The exact behavior depends on your exception model.

I would define the exception mechanism before assigning its instruction encoding.

---

# 9. Important encoding and documentation inconsistencies

There are several specific issues I would resolve before implementing an assembler or emulator.

### 9.1 `$15` is described as an extended instruction but its escape mechanism is not fully documented

Your primary opcode map reserves self-moves for extended instructions:

```text
$10
$15
$1A
$1F
```

However, the specification does not fully explain how all four values arise from the primary register-move encoding.

I would include an explicit opcode map covering `$00–$FF`, even if most entries are reserved.

This will help establish that all instruction formats decode unambiguously.

### 9.2 `JL` has inconsistent address terminology

You describe:

```text
JL abs32
```

as a jump to a 24-bit address.

This can be perfectly reasonable if the instruction contains a 32-bit operand with only 24 meaningful bits.

However, you need to define the layout.

For example:

```text
bits 31–24: reserved
bits 23–16: code segment
bits 15–0:  program counter
```

Or perhaps a different arrangement.

You should also specify whether nonzero reserved bits cause an exception.

### 9.3 `MOV` flag behavior is ambiguous

Your instruction table uses:

```text
MOV reg,reg    —*    —*
```

But the meaning of the asterisk is not explained in the provided specification.

This matters particularly when the destination is `csr`.

For example:

```asm
MOV r0, csr
```

would directly change status flags.

I would distinguish between ordinary instructions that calculate flags and register transfers that explicitly write `csr`.

### 9.4 The Fibonacci example reveals an architectural inconsistency

Your example contains:

```asm
MOV r4, r3
```

However, the ordinary register move format appears limited to accumulator registers.

This instruction presumably uses the extended scratch-to-accumulator transfer encoding.

That is fine, but the assembler needs a rule for selecting the correct encoding based on operand classes.

The example is a useful test case for that rule.

### 9.5 The stack boundary rule needs a precise comparison

The current statement that extending `sp` past `sb` raises an exception is insufficient.

You need to define whether the boundary is inclusive and whether the comparison is performed before or after a stack operation.

For example, with:

```text
SP = $1002
SB = $1000
```

Is a 16-bit push valid?

I would make the lower boundary inclusive, meaning a push to `$1000` succeeds, while a subsequent push fails.

---

# 10. An additional concern: atomicity

Because this is a virtual CPU, you have an opportunity to define exceptionally clean execution semantics.

I would recommend establishing the following architectural principle:

**An instruction either completes successfully or leaves the processor in a precisely defined fault state.**

Consider:

```asm
PUSH r0
```

Suppose the stack boundary check fails.

Ideally:

- The stack pointer remains unchanged.
- No memory is modified.
- The exception handler receives a well-defined fault address.
- The faulting instruction can be identified.

The same principle applies to multi-byte memory operations.

For example:

```asm
MOV.W r0, [$FFFF]
```

If the second byte crosses a segment boundary and causes an exception, you should avoid writing the first byte before discovering the fault.

This is particularly valuable if you eventually want debugging, deterministic replay, or precise exceptions.

You do not need sophisticated hardware to achieve this in a virtual machine.

---

# 11. Prioritized recommendations

I would organize the next phase of ISA development as follows.

| Priority | Area | Recommendation | Reason |
|---|---|---|---|
| **Critical** | Memory | Define segment address translation | Fundamental execution semantics |
| **Critical** | Encoding | Complete primary and extended opcode maps | Prevent encoding conflicts |
| **Critical** | Arithmetic | Define exact carry, overflow, and flag behavior | Ensure correct conditional branches |
| **Critical** | Addressing | Define displacement signs, wraparound, and endianness | Prevent emulator ambiguity |
| **High** | Arithmetic | Add `CMP` | Non-destructive comparisons |
| **High** | Bit operations | Add `SHL`, `SHR`, `SAR` | Fundamental integer operations |
| **High** | Stack | Add `PUSH`, `POP` | Practical stack usage |
| **High** | Control flow | Add `CALL`, `RET` | Subroutines and reusable code |
| **High** | Exceptions | Define exception entry and return | Complete execution model |
| **Medium** | Arithmetic | Add immediate arithmetic or `INC`/`DEC` | Reduce instruction overhead |
| **Medium** | Branching | Consider 8-bit relative branches | Improve code density |
| **Medium** | Registers | Clarify internal-register write semantics | Predictable processor state |
| **Low** | Arithmetic | Add multiply/divide | Can initially be implemented in software |
| **Low** | Optimization | Add specialized addressing modes | Evaluate after writing programs |

---

# 12. What I would preserve

Despite the recommendations above, there are several aspects of your design I would avoid changing prematurely.

**First, keep the eight general-purpose registers.** Four accumulators and four scratch registers provide a reasonable balance between a small register file and useful programming flexibility.

**Second, preserve the two-byte ALU instruction format.** It is compact, regular, and easy to decode. Introduce additional instruction families rather than immediately expanding every ALU instruction.

**Third, retain the unified load/store instruction family.** The `$15` encoding is one of the stronger parts of the specification. Direction, size, register, and addressing mode are cleanly represented.

**Fourth, retain the comprehensive condition-code branches.** Your signed and unsigned branch semantics are conventional and expressive.

**Fifth, keep the segmented memory architecture if you intend to support programs larger than 64 KiB.** With nonoverlapping segments, it provides a straightforward way to extend the address space without making every pointer 24 bits wide.

---

# 13. The next architectural decision

Before adding many more instructions, I would resolve one central question:

## Should scratch registers remain storage-only, or should they participate directly in arithmetic?

This decision affects almost every subsequent instruction family.

Your current design creates an interesting tension:

- Four accumulator registers simplify instruction encoding.
- Four scratch registers provide additional storage.
- Moving values between the two groups costs instructions.
- The existing ALU opcode space is already fully allocated.

There is a possible middle ground worth exploring.

You could preserve the existing two-byte accumulator ALU operations while introducing a second, less compact instruction family capable of operating on all eight registers.

For example, conceptually:

```asm
ADD r0, r1    ; compact accumulator operation
ADD r4, r1    ; extended general-register operation
```

The assembler would select the appropriate encoding.

This would preserve the compact common case without making scratch registers permanently inaccessible to arithmetic.

However, I would not introduce this until you have evaluated several realistic algorithms.

Your Fibonacci example is a good start. I would next test the ISA against:

1. **Array traversal and summation** — exercises memory addressing and pointer arithmetic.
2. **Insertion sort** — exercises comparisons, conditional branches, and indexed memory access.
3. **Recursive factorial** — exercises subroutines, stack management, and register preservation.
4. **String copying** — exercises byte operations and pointer manipulation.
5. **Multiword arithmetic** — exercises carry propagation and register pressure.

These will reveal whether the accumulator/scratch distinction is a useful architectural feature or an unnecessary programming burden.

## Final assessment

I think the ISA has a coherent direction, and I would resist the temptation to turn it into a conventional general-purpose register architecture simply because that would make certain algorithms easier.

The more interesting challenge is determining whether your register partitioning and compact encodings can produce an architecture that is both pleasant to program and efficient to emulate.

**My strongest recommendation is to finish defining the execution semantics and memory model before significantly expanding the instruction set.**

After that, I would concentrate on `CMP`, shifts, stack operations, and subroutine support.

Those additions would move the design from a processor capable of executing small arithmetic programs toward one capable of supporting structured, general-purpose software.

The current ISA is already sufficient to begin experimenting with that distinction.