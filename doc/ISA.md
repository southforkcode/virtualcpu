# ISA

## Registers

- 8 16-bit accumulator and scratch registers: `r0`..`r7`
  - `r0`..`r3` are accumulator/working registers
  - `r4`..`r7` are scratch/storage registers
- 4 16-bit internal, accessible registers
  - `sp` : stack pointer
  - `sb` : stack bottom pointer
  - `dp` : data pointer
  - `csr` : cpu status register
- `pc` : 24-bit program counter

These registers are partitioned into register classes:

| Class/Selector | `00` | `01` | `10` | `11` |
|---|---|---|---|---|
| Accumulator | `r0` | `r1` | `r2` | `r3` |
| Scratch | `r4` | `r5` | `r6` | `r7` |
| Internal | `sp` | `sb` | `dp` | `csr` |
| Future | *tbd* | *tbd* | *tbd* | *tbd* |

## `csr` Flags

`csr` register is a 16-bit field with status flags

 | Bit | Description |
 | --- | --- |
 | 0 | Zero flag |
 | 1 | Negative/Sign flag |
 | 2 | Carry flag |
 | 3 | Overflow flag |
 | 5 | Interrupt flag |
 | 6 | Exception flag |

## Stack

Conventionally, the stack grows downward, from a higher address to a lower address. Trying to extend `sp` past `sb` will raise a *Stack Boundary Exceeded* exception.

## Instructions
| Instruction | Modes | Operation | Source flags | Updated flags |
|---|---|---|---|---|
| `ADC` | reg,reg | adds source and carry-in to destination | C | N, Z, C, V |
| `ADD` | reg,reg | adds source to destination | — | N, Z, C, V |
| `AND` | reg,reg | bitwise AND | — | N, Z |
| `BCC` | rel16 | branches if carry clear (`C=0`; unsigned lower) | C | — |
| `BCS` | rel16 | branches if carry set (`C=1`; unsigned higher or same) | C | — |
| `BEQ` | rel16 | branches if equal (`Z=1`) | Z | — |
| `BGE` | rel16 | branches if signed greater or equal (`N=V`) | N, V | — |
| `BGT` | rel16 | branches if signed greater (`Z=0 AND N=V`) | Z, N, V | — |
| `BHI` | rel16 | branches if unsigned higher (`C=1 AND Z=0`) | C, Z | — |
| `BLE` | rel16 | branches if signed less or equal (`Z=1 OR N!=V`) | Z, N, V | — |
| `BLS` | rel16 | branches if unsigned lower or same (`C=0 OR Z=1`) | C, Z | — |
| `BLT` | rel16 | branches if signed less (`N!=V`) | N, V | — |
| `BMI` | rel16 | branches if negative (`N=1`) | N | — |
| `BNE` | rel16 | branches if not equal (`Z=0`) | Z | — |
| `BPL` | rel16 | branches if negative flag clear (`N=0`) | N | — |
| `BRA` | rel16 | branches unconditionally | — | — |
| `BVC` | rel16 | branches if overflow clear (`V=0`) | V | — |
| `BVS` | rel16 | branches if overflow set (`V=1`) | V | — |
| `CLI` | implied | disables maskable interrupts | — | I |
| `HALT` | implied | halts instruction execution | — | — |
| `JMP` | abs24 | jumps to an absolute 24-bit address | — | — |
| `MOV` | reg,reg | copies source to destination | —* | —* |
| `MOV` | ~reg,reg | copies the bitwise complement of source to destination | — | N, Z |
| `MOV` | imm16,reg | loads a 16-bit immediate into a register | — | — |
| `MOV` | imm8,reg | loads an 8-bit immediate, zero-extended to 16 bits | — | — |
| `MOV` | mem16,reg | loads a 16-bit value from memory | — | — |
| `MOV` | mem8,reg | loads an 8-bit value from memory, zero-extended to 16 bits | — | — |
| `MOV` | reg,mem16 | stores all 16 register bits to memory | — | — |
| `MOV` | reg,mem8 | stores register bits 7–0 to memory | — | — |
| `NOP` | implied | no operation | — | — |
| `OR` | reg,reg | bitwise OR | — | N, Z |
| `SBC` | reg,reg | subtracts source and borrow-in (`1−C`) from destination | C | N, Z, C, V |
| `SEI` | implied | enables maskable interrupts | — | I |
| `SUB` | reg,reg | subtracts source from destination | — | N, Z, C, V |
| `XOR` | reg,reg | bitwise XOR | — | N, Z |
## Instruction Formats
- `0000 00 00` - nop
- `0000 00 01` - halt
- `0000 00 10` - cli : disable interrupts
- `0000 00 11` - sei : enable interrupts
- `00 01 ss dd` - mov rs, rd
    - if rs == rd, see `$10`, `$15`, `$1A`, `$1F` extended instructions
- `0100 cccc` - `Bxx` : conditional 16-bit relative branches
- `0101 0000` - `JMP` : unconditional 24-bit absolute branch
### `$10` instructions
`$10` requires a second 1-byte fetch for the extended opcode. The extended opcode is partitioned into 2 distinct groups based on the 7th bit of the second opcode:
- `$10 0ooo ss dd` : alu/logic reg,reg operations
- `$10 1mmm ss dd` : extended mov operations for internal registers 
#### ALU/logic reg,reg operations
`$10 0ooo ss dd`
| `0ooo` | assembly syntax | operation |
| ---- | --------- | ------ |
| `0000` | `xor rs, rd` | `rd <- rs ^ rd` |
| `0001` | `and rs, rd` | `rd <- rs & rd` |
| `0010` | `or rs, rd` | `rd <- rs \| rd` |
| `0011` | `mov ~rs, rd` | `rd <- ~rs` |
| `0100` | `adc rs, rd` | `rd <- rs + rd + c` |
| `0101` | `sbc rs, rd` | `rd <- rd - rs - ~c` |
| `0110` | `add rs, rd` | `rd <- rs + rd` |
| `0111` | `sub rs, rd` | `rd <- rd - rs` |
#### Extended register move operations
This is a mov operation using the `mmm` mode selector for determing
the register source and destination.
`$10 1mmm ss dd`
| `mmm` | description |
| ----- | ----------- |
| `000` | `rs` is an internal register, `rd` is a regular register |
| `001` | `rs` is a regular register, `rd` is an internal register |
| `010` | `rs` is a scratch register, `rd` is a regular register |
| `011` | `rs` is a regular register, `rd` is a scratch register |
| `1--` | reserved |
| Internal Register | Description |
| --- | --- |
| `00` | stack pointer |
| `01` | stack bottom |
| `10` | data pointer |
| `11` | status register |
### `$15` instructions : load/store value to/from memory
`$15` transfers an immediate value to a register, or transfers data between memory and a register.
`$15 xdmm zrrr operand`
| Field | Meaning |
|---|---|
| `x` | Reserved; must be zero; one currently raises *Illegal Instruction* exception |
| `d` | Direction: `0` loads a register, `1` stores a register |
| `mm` | Operand mode |
| `z` | Transfer size: `0` = 16 bits, `1` = 8 bits |
| `rrr` | Register number, `r0–r7` |
When the `z` is `1`:
- register destination receives : `$00:<8-bit value>`
- memory destination receives the right-most 8 bits of the source.
`d` (direction) table:
| `d` | Meaning |
| --- | ------- |
| `0` | register is destination, operand is value source |
| `1` | register is source, operand is destination |
`z`, `mm` (size and operand mode) table:
| `z` | `mm` | Operand | Meaning |
| --- |---|---|---|
| `0` | `00` | `imm16` | Immediate 16-bit value |
| `1` | `00` | `imm8` | Immediate 8-bit value |
| `0` | `01` | `[addr16]` | 16-bit value at memory at virtual address |
| `1` | `01` | `[addr16]` | 8-bit value at memory at virtual address |
| `0` | `10` | `[dp+off16]` | 16-bit value at memory at DP plus an offset |
| `1` | `10` | `[dp+off16]` | 8-bit value at memory at DP plus an offset |
| `0` | `11` | `[sp-rel16]` | 16-bit value at memory at SP minus a displacement |
| `1` | `11` | `[sp-rel16]` | 8-bit value at memory at SP minus a displacement |
Notes:
- If `d`=`1` and `mm`=`00`, an *Illegal Instruction* exception is raised.
`rrr` (register) table:
| `rrr` | register |
| --- | --------- |
| `000` | `r0` |
| `001` | `r1` |
| `010` | `r2` |
| `011` | `r3` |
| `100` | `r4` |
| `101` | `r5` |
| `110` | `r6` |
| `111` | `r7` |
Instruction lengths:
| Form | Total bytes |
|---|---:|
| Immediate 8-bit load | 3 |
| Immediate 16-bit load | 4 |
| Any memory load/store | 4 |
### `Bxx`: conditional branches
`Bxx` operations require a 16-bit operand, *rel16*. *rel16* is a 16-bit signed offset value fetched as part of completing the fetching of the operation.
| opcode | assembly syntax | operation |
| --- | --- | --- |
| `$40` | `BEQ rel16` | `IF Z=1, pc <- pc + rel16` |
| `$41` | `BNE rel16` | `IF Z=0, pc <- pc + rel16` |
| `$42` | `BCS rel16` | `IF C=1, pc <- pc + rel16` |
| `$43` | `BCC rel16` | `IF C=0, pc <- pc + rel16` |
| `$44` | `BMI rel16` | `IF N=1, pc <- pc + rel16` |
| `$45` | `BPL rel16` | `IF N=0, pc <- pc + rel16` |
| `$46` | `BVS rel16` | `IF V=1, pc <- pc + rel16` |
| `$47` | `BVC rel16` | `IF V=0, pc <- pc + rel16` |
| `$48` | `BHI rel16` | `IF C=1 AND Z=0, pc <- pc + rel16` |
| `$49` | `BLS rel16` | `IF C=0 OR Z=1, pc <- pc + rel16` |
| `$4A` | `BGE rel16` | `IF N=V, pc <- pc + rel16` |
| `$4B` | `BLT rel16` | `IF N!=V, pc <- pc + rel16` |
| `$4C` | `BGT rel16` | `IF Z=0 AND N=V, pc <- pc + rel16` |
| `$4D` | `BLE rel16` | `IF Z=1 OR N!=V, pc <- pc + rel16` |
| `$4E` | `BRA rel16` | `pc <- pc + rel16` |
| `$4F` | reserved | \- |
## Examples
### Example 1: Fibonacci
```
; Compute F(10) and store the 16-bit result at virtual address $2000.
;
; r0 = a: current Fibonacci value
; r1 = b: next Fibonacci value
; r2 = iterations remaining
; r3 = temporary working register
; r4 = constant 1
        MOV.B #0,  r0
        MOV.B #1,  r1
        MOV.B #10, r2
        MOV.B #1,  r4
        OR    r2, r2        ; Test n without changing its value
        BEQ   done          ; F(0) is already in r0
loop:
        MOV   r0, r3        ; temporary = a
        MOV   r1, r0        ; a = b
        ADD   r3, r1        ; b = b + temporary
        MOV   r4, r3        ; Bring constant 1 into an accumulator
        SUB   r3, r2        ; --iterations
        BNE   loop
done:
        MOV.W r0, [$2000]
        HALT
```