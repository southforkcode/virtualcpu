# Machine-readable ISA encodings

The [machine-readable table](#machine-readable-table) embedded below defines the binary layouts for [revision 0.5 baseline](ISA.md#revision-05-baseline). The assembler/disassembler reads this table, and the C++ emulator generates its matching, length, revision and reserved-byte tables from it. Execution semantics remain implemented in C++; [README.md](../README.md#validation) describes validation. JSON specifies matching and operand layout; ISA.md specifies execution, faults, assembly restrictions, and commit behavior. Any discrepancy is a specification defect.

The JSON block is the sole machine-readable source. There is no separate JSON documentation file. Preserve the payload and the `ISA_ENCODING_JSON_BEGIN` / `ISA_ENCODING_JSON_END` markers when editing this document.

## Revision 0.6 encoding requirements

The [0.6 direction](ISA.md#revision-06-os-execution-architecture) is approved,
but its revised execution draft is **not frozen**. The
[architectural decision report](ISA.md#architectural-decision-report) records the
current review changes. The embedded schema-version-1 payload below is still
**0.5**; it must not be relabeled or used as a partial 0.6 decoder. CPU/tools
continue to reject 0.6. No new opcode, control/extended-context index, vector,
MMIO address or enum numeric discriminant is assigned by these proposals.

| Area | Revised draft requirement | Allocation status |
|---|---|---|
| Existing instructions | Preserve valid 0.5 bytes, CSR bits, B0/B1 banks, old context indices 0x00–0x14 | Fixed baseline; protected semantics apply only in 0.6 |
| Mode/entry/return | U I=1; protected provenance; eight ordinary frames; failed entry/overflow is terminal | SWI imm8/IRET bytes retained; no executable emergency interface |
| Context/control access | Readable committed descriptors, published-map state, user-access controls and one-way bootstrap action | Three-byte extended access candidate below; indices unassigned |
| Faults/vectors | Origin/phase/access/map/start/relevant virtual and physical address/reason record | Logical word layout in ISA.md; new word IDs, enum codes and any new cause/vector numbers unassigned |
| Memory | Published/invalidate map control; whole-descriptor publication; U-rights user-access transfer | Control layouts and user-access family unassigned; software generations are not hardware map bits |
| PIC/timer/MMIO | 16 sources, timer source 0, atomic snapshot/configuration/ACK | MMIO layouts remain machine specification work; not duplicated as CPU controls |
| Compact instructions | rel8, B1 simm6, SP uimm8; prototype ADDI/CMPI; defer SUBI/post-increment | Symbolic formats below, not assigned byte values |

A later allocation must publish complete revision-specific metadata and retain
0.5 as a separately selectable binary table. Execution contracts, kernel ABI
critical sections and opcode matching are distinct: matching a word does not
make an invalid return image or unauthorized access valid.

## Revision 0.6 encoding proposals

These proposals fix operand shape and selection rules for review, while keeping
all new opcode/family tags symbolic. They are not another machine-readable
encoding payload and are not enabled by tools. Protected execution is prototyped
before compact code generation; either milestone can be reviewed independently.

| Form | Bytes | Proposed byte layout (symbolic tags) |
|---|---:|---|
| Short branch | 2 | `BR8_BASE + condition`, `rel8`; condition order matches all fifteen existing rel16 predicates; family base unassigned |
| B1 simm6 | 3 | Existing `$15`, existing `1ds0zrrr`, `ddddddpp`; upper six descriptor bits are signed displacement, low two B1 base |
| Long B1 | 5 | Existing `$15`, existing `1ds1zrrr`, `000000pp`, `disp_lo`, `disp_hi` |
| Short SP memory | 3 | `SP8`, `0d11zrrr`, `off8`; new primary tag SP8 unassigned; d/z/rrr retain their existing meanings |
| ADDI / CMPI | 2 each | `IMM4_ADD` or `IMM4_CMP`, `00rrIIII`; symbolic primary tags unassigned, rr selects B0 and IIII is signed imm4 |
| Extended context/control access | 3 | Existing context escape, `ffkk0rrr`, `index8`; ff is an unassigned secondary family tag, kk selects context read/write or CPU-control read/write, rrr selects any GPR |

In the extended-access candidate, ff/kk bit assignments are unassigned; choosing
an unused secondary block avoids spending a primary family on rare control
access. Old GETX/SETX byte layouts and selectors remain unchanged. The eight-bit
third-byte index is a new form, not an expansion of the old five-bit field.
Unknown selectors and reserved fields are illegal as soon as fetched; a known
read-only selector used by a write form is illegal. Complete legal fetch/decode
precedes K-only privilege checking and active-context/state validation. All such
transfers preserve flags except an explicitly defined control effect. Common
reads such as active process-map ID need one three-byte transfer and no pointer
or hidden temporary. Actual index assignments remain pending.

### Compact-form selection and exact semantics

For branches, `.S` forces rel8 and `.W` forces rel16; unsuffixed forms start long.
Compute displacement from the end of the **selected** instruction in a wide type.
With no alignment-dependent directives, repeatedly shrink every eligible long
branch, accounting for its own shrink and relocating forward/backward targets,
until no sizes change. Validate forced-short ranges after convergence, so another
branch's shrink can make a forced-short target fit. Forced-long forms never shrink.
Reject out-of-range final displacement rather than truncate. The unused condition
slot is illegal before any displacement fetch. Fetch never wraps; a taken branch
may use intermediate next=$10000 only if all instruction bytes fit and the final
target is representable/executable. Untaken next must itself be representable.
Flags and all fifteen predicates are identical to rel16.

For B1, choose a=0 for -32..31 inclusive, including zero; sign-extend descriptor
bits 7–2 before adding the original base in a wide type. Use a=1 outside that
range, retaining signed disp16 and its early zero-upper-descriptor check. Long
zero displacement remains a valid noncanonical encoding. In 0.5 every nonzero
upper descriptor field remains illegal. Width, CS/DS selection, base/value alias
sampling, protection checks and atomic stores do not change.

For SP, choose the new form only for unsigned 0..255; otherwise retain off16.
A byte transfer still carries an unsigned slot offset. Negative offsets are
rejected. Both forms check the same SB≤SP≤ST invariant, allocated slot interval,
SS translation, rights, backing and final PC before any effect. Slot 255 must fit
the entire byte/word. Offset 255 means +255, not -1; offset 256 selects the long
form and cannot truncate to zero. Reserved
second-byte fields of SP8 are illegal without fetching a third byte.

For immediates, accept -8..7 only and sign-extend to the 16-bit source word.
ADDI uses the exact ADD result/NZCV definition, including unsigned carry from
that sign-extended word; CMPI uses CMP flags and preserves the destination.
I/E are preserved. The high two operand bits are reserved. SUBI is not proposed
for allocation: ADDI #-1 implements decrement with the same flags, and the
measured corpus did not justify distinct zero/+8 cases. The assembler must not
remove a constant setup unless register liveness and all visible flag effects
are proven; no implicit constant/scratch register is consumed.

Tests required before allocation/implementation: all branch predicates and rel8
endpoints/cascading layouts/segment ends; all B1 bases, signed endpoints and long
fallback/early reserved-bit failure; SP offsets 0/255/256 plus allocation/protection
atomicity; imm4 signed/carry/overflow boundary oracles and reserved bits; all GPRs,
selector classes, privilege precedence and index limits for extended accesses.
Every newly used byte/descriptor bit must remain illegal in 0.5 mode.

### Kernel context and control-access cost

The retained [context-frame study](ISA_Examples.md#os-oriented-workloads) is a
17-word 0.5 image, not an implemented 0.6 scheduler: it uses 34 three-byte GETX/
SETX transfers (**102 bytes**). Its total is 281 baseline bytes, or 221 with
compact B1 memory accesses; those totals include memory, setup and entry/return.

A hypothetical 20-word spill plus reload costs 40 context transfers, **120 bytes**
at the proposed three-byte access size, before memory operations. This is a size
calculation, not a new benchmark. Canonical U user-access selectors are zero and
may be rematerialized instead of saved under the kernel ABI. The new full-index
candidate keeps the cost per context transfer at three bytes without introducing
bulk memory operations or variable-count atomic instructions. Measure the real
two-process scheduler, its faults, and register liveness before reconsidering a
multiword context transfer.

CPU-only controls (process-map ID, user-access selectors, descriptor publication,
entry configuration) use the generic candidate access. Timer/PIC remain protected
MMIO, accessed with ordinary MOV; existing CSR moves remain their existing size.
No one-byte fast-control opcode, general-purpose encoding sacrifice, port-I/O
instruction or duplicate timer/PIC register path is justified by current evidence.
Measure initialization, syscall and IRQ paths after the protected CPU prototype;
control-family selection and index values are still review decisions.

## Format, schema version 1

All numeric values in JSON are decimal integers. Byte positions are zero-based from the instruction's first byte. Multibyte operands are little-endian. `isa_revision` identifies the guest architecture; `schema_version` identifies this metadata format. A consumer must explicitly support both versions, including revision 0.5's candidate execution status pending its conformance audit.

The overall `status` remains `candidate` while execution validation is pending. `instruction_set_status` is `frozen_for_implementation`; `execution_specification_status` is `provisional_until_emulator_validation`. These are review/implementation milestones, not guest execution modes or new encoding fields. The revision 0.4 execution clarifications do not alter matching tables, register/context layouts, flags, masks, aliases, or instruction lengths.

Revision 0.5 adopts SWI at `$26`, GETX xtrap at `$1F $34`, and read-only context word xtrap at index `$14`. The JSON includes these assignments and the TRAP alias. SWI has an unsigned service byte and uses exception-entry effects to save the following PC, enter vector 6, set E and clear I. Revision 0.4 images still reject the newly assigned bytes under that earlier revision. [ISA_Examples.md](ISA_Examples.md#software-trap-and-system-call-example) supplies the worked caller/handler.

Each decoding table has an `inspect_byte` and an `entries` array. Match an entry using either:

- `(instruction_byte & match.mask) == match.value`, or
- membership in `match.values`.

Exactly one entry may match. No match is Illegal Instruction immediately, without reading further bytes. A primary entry with `dispatch` selects another named table, which examines byte 1. An unavailable inspected byte is a fetch fault, not an illegal encoding. Dispatch entries have no final length; the selected instruction entry provides it.

| Entry property | Meaning |
|---|---|
| `id` | Unique encoding identifier, independent of overloaded assembly mnemonics |
| `mnemonic` | Canonical instruction mnemonic; operand classes determine the encoding |
| `length` | Complete instruction length in bytes |
| `fields` | Bit fields extracted from instruction bytes; includes source/destination register-bank identity |
| `operands` | Byte-aligned immediate/address/displacement fields with width and signed interpretation |
| `encoding_checks` | Reserved-bit checks performed as soon as that byte is fetched; failure is Illegal Instruction |
| `state_constraints` | Constraints on decoded operand values; apply after full instruction fetch with the stated fault kind |
| `destination` | A fixed register selected by the primary opcode, as with ADDP/ADJSP |
| `address` | Memory base class, segment, and whether stack-allocation checks apply |
| `transfer_bits` | Value width, independently of address/displacement width |
| `flags_read`, `flags_written` | Flag inputs and updates; absent `flags_read` means none |
| `special_effects` | Conditional CSR effects or context restoration not expressible as ordinary calculated flags |
| `semantic` | Operation identifier for a consumer's execution or formatting implementation |

A field is extracted as `(bytes[field.byte] >> field.lsb) & ((1 << field.width) - 1)`. Its `bank` selects a named register array in `register_banks`. For example, the two-bit B1 selector zero means r4, not r0. GETX/SETX extract the low five bits of byte 1 as a context index; their match-value sets restrict the valid indices.

Operands are assembled from `width_bytes` starting at `byte`, then interpreted according to `signed`. This describes bits, not all assembly literal rules: for example, a MOV immediate can be written using a negative two's-complement literal even though the loaded value zero-extends. ADDP/ADJSP accept strictly signed literals, SWI accepts only unsigned 0–255, and SP-relative operands are unsigned.

For B1 memory forms, the base-selector byte is fetched and validated before any optional displacement. Its upper six bits must be zero. GETX/SETX similarly validate their register byte's upper five bits. These checks differ from JL's nonzero reserved operand byte, which is an Invalid State fault after complete fetch.

Instruction families with multiple lengths have separate entries. B1 direct and displaced transfers therefore have distinct three-byte/five-byte entries. Assemblers select the direct form for zero displacement; decoders still accept an explicitly encoded zero displacement in the longer form.

## Register state and aliases

The register-bank arrays include B0–B3, all eight general registers, and the saved exception context. `context_writable_indices` excludes read-only cause/address/service metadata. `csr_masks` contains the defined and software-writable masks, with E derived from active context rather than an ordinary software-writable flag.

Aliases expand to real instructions before encoding. INCP/DECP accept r4–r7 or DP; they do not target SP. ADJSP is a separate checked stack operation with no transfer-size suffix. PULL, PUSH.W, POP.W, and PULL.W expand to the corresponding word PUSH/POP instruction as listed in JSON. General-register self-MOV expands to NOP. TRAP #imm8 expands to SWI #imm8 without changing its operand range or length. No alias may silently consume an accumulator temporary. Existing MOV suffix and operand-class rules remain defined by ISA.md.

`flags_written` lists ordinary updates; conditional CSR destinations use `special_effects`. IRET restores CSR/context and clears active E as specified by its context-restoration effect. SETX edits saved state and does not immediately update live flags. SWI likewise lists no ordinary calculated flags; its special effects perform exception entry, which preserves NZCV, clears I, and activates E. Its xpc is the following instruction, unlike the instruction-start PC saved for a fault.

SETX writes the complete saved xcsr word without the live CSR mask. The encoding is legal even when its stored value is temporarily invalid. A valid IRET restore requires `(xcsr & ~$002F) == 0`, including E=0; invalid values cause terminal Double Fault without discarding prior saved-context edits. This is semantic validation at return, not an illegal SETX encoding or a new table match condition.

The saved context now has 21 words. GETX selects indices `$00–$14`; SETX remains limited to `$00–$10`. xtrap is zero-extended from the SWI service byte and reset to zero on every non-SWI entry. SWI #0 is distinguished by cause 6. `$1F $54` remains illegal before reading a third byte.

## Validation and implementation use

During documentation review, all 256 values in each of the five decoding tables were checked for overlapping matches and expected coverage. The maps assign 44 primary bytes and 224/240/28/61 second-byte values in the `$10`/`$15`/`$1A`/`$1F` tables. Every instruction in the ten worked listings—164 instructions—was independently decoded through the JSON table and checked for length, reserved bits, and field/operand bounds. CSR masks were checked against their constituent flag bits, including exclusion of bit 4.

These historical checks establish agreement between the draft encodings and examples. The implemented tools now additionally validate matching-table coverage, reproduce published assembly bytes, and execute the supplied programs on the real CPU; see [README.md](../README.md#validation). The metadata payload below is unchanged by that implementation.

## Reading the embedded table

Read the JSON code block between the two marker lines; parse it as schema version 1. For example, from the repository root:

```python
import json
from pathlib import Path

text = Path("doc/ISA_ENCODING.md").read_text()
section = text.split("\n<!-- ISA_ENCODING_JSON_BEGIN -->\n", 1)[1]
section = section.split("\n<!-- ISA_ENCODING_JSON_END -->", 1)[0]
payload = section.split("```json\n", 1)[1].split("\n```", 1)[0]
metadata = json.loads(payload)
```

Compare `metadata["isa_revision"]` and `metadata["schema_version"]` with the versions the consumer supports. Generated runtime files may be derived from this payload; edit the embedded source when changing the ISA.

## Machine-readable table

<!-- ISA_ENCODING_JSON_BEGIN -->
```json
{
  "schema_version": 1,
  "architecture": "virtualcpu",
  "isa_revision": "0.5",
  "status": "candidate",
  "instruction_set_status": "frozen_for_implementation",
  "execution_specification_status": "provisional_until_emulator_validation",
  "byte_order": "little",
  "instruction_bytes": {
    "min": 1,
    "max": 5
  },
  "number_format": "JSON integers are decimal; matching uses (byte & mask) == value, or membership in values.",
  "unmatched_encoding": "illegal_instruction",
  "flags": {
    "Z": 1,
    "N": 2,
    "C": 4,
    "V": 8,
    "I": 32,
    "E": 64
  },
  "csr_masks": {
    "defined": 111,
    "software_writable": 47
  },
  "register_banks": {
    "general": [
      "r0",
      "r1",
      "r2",
      "r3",
      "r4",
      "r5",
      "r6",
      "r7"
    ],
    "B0": [
      "r0",
      "r1",
      "r2",
      "r3"
    ],
    "B1": [
      "r4",
      "r5",
      "r6",
      "r7"
    ],
    "B2": [
      "sp",
      "sb",
      "dp",
      "csr"
    ],
    "B3": [
      "ss",
      "cs",
      "ds",
      "st"
    ],
    "context": [
      "xr0",
      "xr1",
      "xr2",
      "xr3",
      "xr4",
      "xr5",
      "xr6",
      "xr7",
      "xpc",
      "xcs",
      "xcsr",
      "xsp",
      "xsb",
      "xst",
      "xss",
      "xdp",
      "xds",
      "xcause",
      "xaddrlo",
      "xaddrhi",
      "xtrap"
    ]
  },
  "context_writable_indices": [
    0,
    1,
    2,
    3,
    4,
    5,
    6,
    7,
    8,
    9,
    10,
    11,
    12,
    13,
    14,
    15,
    16
  ],
  "tables": {
    "primary": {
      "inspect_byte": 0,
      "entries": [
        {
          "id": "nop",
          "mnemonic": "NOP",
          "match": {
            "value": 0,
            "mask": 255
          },
          "length": 1,
          "fields": [],
          "operands": [],
          "flags_written": [],
          "semantic": "nop"
        },
        {
          "id": "halt",
          "mnemonic": "HALT",
          "match": {
            "value": 1,
            "mask": 255
          },
          "length": 1,
          "fields": [],
          "operands": [],
          "flags_written": [],
          "semantic": "halt"
        },
        {
          "id": "di",
          "mnemonic": "DI",
          "match": {
            "value": 2,
            "mask": 255
          },
          "length": 1,
          "fields": [],
          "operands": [],
          "flags_written": [
            "I"
          ],
          "semantic": "di"
        },
        {
          "id": "ei",
          "mnemonic": "EI",
          "match": {
            "value": 3,
            "mask": 255
          },
          "length": 1,
          "fields": [],
          "operands": [],
          "flags_written": [
            "I"
          ],
          "semantic": "ei"
        },
        {
          "id": "mov_acc",
          "mnemonic": "MOV",
          "match": {
            "values": [
              17,
              18,
              19,
              20,
              22,
              23,
              24,
              25,
              27,
              28,
              29,
              30
            ]
          },
          "length": 1,
          "fields": [
            {
              "name": "source",
              "byte": 0,
              "lsb": 2,
              "width": 2,
              "bank": "B0"
            },
            {
              "name": "destination",
              "byte": 0,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [],
          "flags_written": [],
          "semantic": "mov_acc"
        },
        {
          "id": "escape_10",
          "match": {
            "value": 16,
            "mask": 255
          },
          "dispatch": "ext10"
        },
        {
          "id": "escape_15",
          "match": {
            "value": 21,
            "mask": 255
          },
          "dispatch": "ext15"
        },
        {
          "id": "escape_1a",
          "match": {
            "value": 26,
            "mask": 255
          },
          "dispatch": "ext1a"
        },
        {
          "id": "escape_1f",
          "match": {
            "value": 31,
            "mask": 255
          },
          "dispatch": "ext1f"
        },
        {
          "id": "addp_r4",
          "mnemonic": "ADDP",
          "match": {
            "value": 32,
            "mask": 255
          },
          "length": 2,
          "fields": [],
          "operands": [
            {
              "name": "adjustment",
              "byte": 1,
              "width_bytes": 1,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "destination": "r4",
          "semantic": "checked_pointer_adjust"
        },
        {
          "id": "addp_r5",
          "mnemonic": "ADDP",
          "match": {
            "value": 33,
            "mask": 255
          },
          "length": 2,
          "fields": [],
          "operands": [
            {
              "name": "adjustment",
              "byte": 1,
              "width_bytes": 1,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "destination": "r5",
          "semantic": "checked_pointer_adjust"
        },
        {
          "id": "addp_r6",
          "mnemonic": "ADDP",
          "match": {
            "value": 34,
            "mask": 255
          },
          "length": 2,
          "fields": [],
          "operands": [
            {
              "name": "adjustment",
              "byte": 1,
              "width_bytes": 1,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "destination": "r6",
          "semantic": "checked_pointer_adjust"
        },
        {
          "id": "addp_r7",
          "mnemonic": "ADDP",
          "match": {
            "value": 35,
            "mask": 255
          },
          "length": 2,
          "fields": [],
          "operands": [
            {
              "name": "adjustment",
              "byte": 1,
              "width_bytes": 1,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "destination": "r7",
          "semantic": "checked_pointer_adjust"
        },
        {
          "id": "addp_dp",
          "mnemonic": "ADDP",
          "match": {
            "value": 36,
            "mask": 255
          },
          "length": 2,
          "fields": [],
          "operands": [
            {
              "name": "adjustment",
              "byte": 1,
              "width_bytes": 1,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "destination": "dp",
          "semantic": "checked_pointer_adjust"
        },
        {
          "id": "adjsp",
          "mnemonic": "ADJSP",
          "match": {
            "value": 37,
            "mask": 255
          },
          "length": 2,
          "fields": [],
          "operands": [
            {
              "name": "adjustment",
              "byte": 1,
              "width_bytes": 1,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "destination": "sp",
          "semantic": "checked_stack_adjust"
        },
        {
          "id": "swi",
          "mnemonic": "SWI",
          "match": {
            "value": 38,
            "mask": 255
          },
          "length": 2,
          "fields": [],
          "operands": [
            {
              "name": "service",
              "byte": 1,
              "width_bytes": 1,
              "signed": false,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "special_effects": [
            "enter_vector_6_with_following_pc_and_zero_extended_xtrap",
            "set_active_e_and_clear_i_without_guest_stack_access"
          ],
          "semantic": "software_trap"
        },
        {
          "id": "beq",
          "mnemonic": "BEQ",
          "match": {
            "value": 64,
            "mask": 255
          },
          "length": 3,
          "fields": [],
          "operands": [
            {
              "name": "displacement",
              "byte": 1,
              "width_bytes": 2,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "flags_read": [
            "Z"
          ],
          "semantic": "branch_eq"
        },
        {
          "id": "bne",
          "mnemonic": "BNE",
          "match": {
            "value": 65,
            "mask": 255
          },
          "length": 3,
          "fields": [],
          "operands": [
            {
              "name": "displacement",
              "byte": 1,
              "width_bytes": 2,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "flags_read": [
            "Z"
          ],
          "semantic": "branch_ne"
        },
        {
          "id": "bcs",
          "mnemonic": "BCS",
          "match": {
            "value": 66,
            "mask": 255
          },
          "length": 3,
          "fields": [],
          "operands": [
            {
              "name": "displacement",
              "byte": 1,
              "width_bytes": 2,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "flags_read": [
            "C"
          ],
          "semantic": "branch_cs"
        },
        {
          "id": "bcc",
          "mnemonic": "BCC",
          "match": {
            "value": 67,
            "mask": 255
          },
          "length": 3,
          "fields": [],
          "operands": [
            {
              "name": "displacement",
              "byte": 1,
              "width_bytes": 2,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "flags_read": [
            "C"
          ],
          "semantic": "branch_cc"
        },
        {
          "id": "bmi",
          "mnemonic": "BMI",
          "match": {
            "value": 68,
            "mask": 255
          },
          "length": 3,
          "fields": [],
          "operands": [
            {
              "name": "displacement",
              "byte": 1,
              "width_bytes": 2,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "flags_read": [
            "N"
          ],
          "semantic": "branch_mi"
        },
        {
          "id": "bpl",
          "mnemonic": "BPL",
          "match": {
            "value": 69,
            "mask": 255
          },
          "length": 3,
          "fields": [],
          "operands": [
            {
              "name": "displacement",
              "byte": 1,
              "width_bytes": 2,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "flags_read": [
            "N"
          ],
          "semantic": "branch_pl"
        },
        {
          "id": "bvs",
          "mnemonic": "BVS",
          "match": {
            "value": 70,
            "mask": 255
          },
          "length": 3,
          "fields": [],
          "operands": [
            {
              "name": "displacement",
              "byte": 1,
              "width_bytes": 2,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "flags_read": [
            "V"
          ],
          "semantic": "branch_vs"
        },
        {
          "id": "bvc",
          "mnemonic": "BVC",
          "match": {
            "value": 71,
            "mask": 255
          },
          "length": 3,
          "fields": [],
          "operands": [
            {
              "name": "displacement",
              "byte": 1,
              "width_bytes": 2,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "flags_read": [
            "V"
          ],
          "semantic": "branch_vc"
        },
        {
          "id": "bhi",
          "mnemonic": "BHI",
          "match": {
            "value": 72,
            "mask": 255
          },
          "length": 3,
          "fields": [],
          "operands": [
            {
              "name": "displacement",
              "byte": 1,
              "width_bytes": 2,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "flags_read": [
            "C",
            "Z"
          ],
          "semantic": "branch_hi"
        },
        {
          "id": "bls",
          "mnemonic": "BLS",
          "match": {
            "value": 73,
            "mask": 255
          },
          "length": 3,
          "fields": [],
          "operands": [
            {
              "name": "displacement",
              "byte": 1,
              "width_bytes": 2,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "flags_read": [
            "C",
            "Z"
          ],
          "semantic": "branch_ls"
        },
        {
          "id": "bge",
          "mnemonic": "BGE",
          "match": {
            "value": 74,
            "mask": 255
          },
          "length": 3,
          "fields": [],
          "operands": [
            {
              "name": "displacement",
              "byte": 1,
              "width_bytes": 2,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "flags_read": [
            "N",
            "V"
          ],
          "semantic": "branch_ge"
        },
        {
          "id": "blt",
          "mnemonic": "BLT",
          "match": {
            "value": 75,
            "mask": 255
          },
          "length": 3,
          "fields": [],
          "operands": [
            {
              "name": "displacement",
              "byte": 1,
              "width_bytes": 2,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "flags_read": [
            "N",
            "V"
          ],
          "semantic": "branch_lt"
        },
        {
          "id": "bgt",
          "mnemonic": "BGT",
          "match": {
            "value": 76,
            "mask": 255
          },
          "length": 3,
          "fields": [],
          "operands": [
            {
              "name": "displacement",
              "byte": 1,
              "width_bytes": 2,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "flags_read": [
            "Z",
            "N",
            "V"
          ],
          "semantic": "branch_gt"
        },
        {
          "id": "ble",
          "mnemonic": "BLE",
          "match": {
            "value": 77,
            "mask": 255
          },
          "length": 3,
          "fields": [],
          "operands": [
            {
              "name": "displacement",
              "byte": 1,
              "width_bytes": 2,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "flags_read": [
            "Z",
            "N",
            "V"
          ],
          "semantic": "branch_le"
        },
        {
          "id": "bra",
          "mnemonic": "BRA",
          "match": {
            "value": 78,
            "mask": 255
          },
          "length": 3,
          "fields": [],
          "operands": [
            {
              "name": "displacement",
              "byte": 1,
              "width_bytes": 2,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "flags_read": [],
          "semantic": "branch_ra"
        },
        {
          "id": "js_absolute",
          "mnemonic": "JS",
          "match": {
            "value": 82,
            "mask": 255
          },
          "length": 3,
          "fields": [],
          "operands": [
            {
              "name": "target",
              "byte": 1,
              "width_bytes": 2,
              "signed": false,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "semantic": "jump_near_absolute"
        },
        {
          "id": "jl",
          "mnemonic": "JL",
          "match": {
            "value": 83,
            "mask": 255
          },
          "length": 5,
          "fields": [],
          "operands": [
            {
              "name": "target",
              "byte": 1,
              "width_bytes": 4,
              "signed": false,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "semantic": "jump_far_absolute",
          "state_constraints": [
            {
              "operand": "target",
              "mask": 4278190080,
              "equals": 0,
              "fault": "invalid_state"
            }
          ]
        }
      ]
    },
    "ext10": {
      "inspect_byte": 1,
      "entries": [
        {
          "id": "xor",
          "mnemonic": "XOR",
          "match": {
            "value": 0,
            "mask": 240
          },
          "length": 2,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 2,
              "width": 2,
              "bank": "B0"
            },
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [],
          "flags_written": [
            "N",
            "Z"
          ],
          "flags_read": [],
          "semantic": "xor"
        },
        {
          "id": "and",
          "mnemonic": "AND",
          "match": {
            "value": 16,
            "mask": 240
          },
          "length": 2,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 2,
              "width": 2,
              "bank": "B0"
            },
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [],
          "flags_written": [
            "N",
            "Z"
          ],
          "flags_read": [],
          "semantic": "and"
        },
        {
          "id": "or",
          "mnemonic": "OR",
          "match": {
            "value": 32,
            "mask": 240
          },
          "length": 2,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 2,
              "width": 2,
              "bank": "B0"
            },
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [],
          "flags_written": [
            "N",
            "Z"
          ],
          "flags_read": [],
          "semantic": "or"
        },
        {
          "id": "mov_not",
          "mnemonic": "MOV",
          "match": {
            "value": 48,
            "mask": 240
          },
          "length": 2,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 2,
              "width": 2,
              "bank": "B0"
            },
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [],
          "flags_written": [
            "N",
            "Z"
          ],
          "flags_read": [],
          "semantic": "mov_not"
        },
        {
          "id": "adc",
          "mnemonic": "ADC",
          "match": {
            "value": 64,
            "mask": 240
          },
          "length": 2,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 2,
              "width": 2,
              "bank": "B0"
            },
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [],
          "flags_written": [
            "N",
            "Z",
            "C",
            "V"
          ],
          "flags_read": [
            "C"
          ],
          "semantic": "adc"
        },
        {
          "id": "sbc",
          "mnemonic": "SBC",
          "match": {
            "value": 80,
            "mask": 240
          },
          "length": 2,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 2,
              "width": 2,
              "bank": "B0"
            },
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [],
          "flags_written": [
            "N",
            "Z",
            "C",
            "V"
          ],
          "flags_read": [
            "C"
          ],
          "semantic": "sbc"
        },
        {
          "id": "add",
          "mnemonic": "ADD",
          "match": {
            "value": 96,
            "mask": 240
          },
          "length": 2,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 2,
              "width": 2,
              "bank": "B0"
            },
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [],
          "flags_written": [
            "N",
            "Z",
            "C",
            "V"
          ],
          "flags_read": [],
          "semantic": "add"
        },
        {
          "id": "sub",
          "mnemonic": "SUB",
          "match": {
            "value": 112,
            "mask": 240
          },
          "length": 2,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 2,
              "width": 2,
              "bank": "B0"
            },
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [],
          "flags_written": [
            "N",
            "Z",
            "C",
            "V"
          ],
          "flags_read": [],
          "semantic": "sub"
        },
        {
          "id": "mov_b2_b0",
          "mnemonic": "MOV",
          "match": {
            "value": 128,
            "mask": 240
          },
          "length": 2,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 2,
              "width": 2,
              "bank": "B2"
            },
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [],
          "flags_written": [],
          "semantic": "move_register",
          "special_effects": []
        },
        {
          "id": "mov_b0_b2",
          "mnemonic": "MOV",
          "match": {
            "value": 144,
            "mask": 240
          },
          "length": 2,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 2,
              "width": 2,
              "bank": "B0"
            },
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B2"
            }
          ],
          "operands": [],
          "flags_written": [],
          "semantic": "move_register",
          "special_effects": [
            "explicit_csr_write_if_destination"
          ]
        },
        {
          "id": "mov_b1_b0",
          "mnemonic": "MOV",
          "match": {
            "value": 160,
            "mask": 240
          },
          "length": 2,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 2,
              "width": 2,
              "bank": "B1"
            },
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [],
          "flags_written": [],
          "semantic": "move_register",
          "special_effects": []
        },
        {
          "id": "mov_b0_b1",
          "mnemonic": "MOV",
          "match": {
            "value": 176,
            "mask": 240
          },
          "length": 2,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 2,
              "width": 2,
              "bank": "B0"
            },
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B1"
            }
          ],
          "operands": [],
          "flags_written": [],
          "semantic": "move_register",
          "special_effects": []
        },
        {
          "id": "mov_b3_b0",
          "mnemonic": "MOV",
          "match": {
            "value": 192,
            "mask": 240
          },
          "length": 2,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 2,
              "width": 2,
              "bank": "B3"
            },
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [],
          "flags_written": [],
          "semantic": "move_register",
          "special_effects": []
        },
        {
          "id": "mov_b0_b3",
          "mnemonic": "MOV",
          "match": {
            "value": 208,
            "mask": 240
          },
          "length": 2,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 2,
              "width": 2,
              "bank": "B0"
            },
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B3"
            }
          ],
          "operands": [],
          "flags_written": [],
          "semantic": "move_register",
          "special_effects": []
        }
      ]
    },
    "ext15": {
      "inspect_byte": 1,
      "entries": [
        {
          "id": "mov_immediate_8",
          "mnemonic": "MOV",
          "match": {
            "value": 8,
            "mask": 248
          },
          "length": 3,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            }
          ],
          "operands": [
            {
              "name": "immediate",
              "byte": 2,
              "width_bytes": 1,
              "signed": false,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "transfer_bits": 8,
          "semantic": "move_immediate_zero_extend"
        },
        {
          "id": "mov_absolute_load_8",
          "mnemonic": "MOV",
          "match": {
            "value": 24,
            "mask": 248
          },
          "length": 4,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            }
          ],
          "operands": [
            {
              "name": "offset",
              "byte": 2,
              "width_bytes": 2,
              "signed": false,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "transfer_bits": 8,
          "address": {
            "base": "absolute",
            "segment": "ds",
            "stack_bounds": false
          },
          "semantic": "load_memory_zero_extend"
        },
        {
          "id": "mov_absolute_store_8",
          "mnemonic": "MOV",
          "match": {
            "value": 88,
            "mask": 248
          },
          "length": 4,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            }
          ],
          "operands": [
            {
              "name": "offset",
              "byte": 2,
              "width_bytes": 2,
              "signed": false,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "transfer_bits": 8,
          "address": {
            "base": "absolute",
            "segment": "ds",
            "stack_bounds": false
          },
          "semantic": "store_memory"
        },
        {
          "id": "mov_dp_load_8",
          "mnemonic": "MOV",
          "match": {
            "value": 40,
            "mask": 248
          },
          "length": 4,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            }
          ],
          "operands": [
            {
              "name": "offset",
              "byte": 2,
              "width_bytes": 2,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "transfer_bits": 8,
          "address": {
            "base": "dp",
            "segment": "ds",
            "stack_bounds": false
          },
          "semantic": "load_memory_zero_extend"
        },
        {
          "id": "mov_dp_store_8",
          "mnemonic": "MOV",
          "match": {
            "value": 104,
            "mask": 248
          },
          "length": 4,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            }
          ],
          "operands": [
            {
              "name": "offset",
              "byte": 2,
              "width_bytes": 2,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "transfer_bits": 8,
          "address": {
            "base": "dp",
            "segment": "ds",
            "stack_bounds": false
          },
          "semantic": "store_memory"
        },
        {
          "id": "mov_sp_load_8",
          "mnemonic": "MOV",
          "match": {
            "value": 56,
            "mask": 248
          },
          "length": 4,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            }
          ],
          "operands": [
            {
              "name": "offset",
              "byte": 2,
              "width_bytes": 2,
              "signed": false,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "transfer_bits": 8,
          "address": {
            "base": "sp",
            "segment": "ss",
            "stack_bounds": true
          },
          "semantic": "load_memory_zero_extend"
        },
        {
          "id": "mov_sp_store_8",
          "mnemonic": "MOV",
          "match": {
            "value": 120,
            "mask": 248
          },
          "length": 4,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            }
          ],
          "operands": [
            {
              "name": "offset",
              "byte": 2,
              "width_bytes": 2,
              "signed": false,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "transfer_bits": 8,
          "address": {
            "base": "sp",
            "segment": "ss",
            "stack_bounds": true
          },
          "semantic": "store_memory"
        },
        {
          "id": "mov_b1_ds_direct_load_8",
          "mnemonic": "MOV",
          "match": {
            "value": 136,
            "mask": 248
          },
          "length": 3,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            },
            {
              "name": "base",
              "byte": 2,
              "lsb": 0,
              "width": 2,
              "bank": "B1"
            }
          ],
          "operands": [],
          "flags_written": [],
          "transfer_bits": 8,
          "address": {
            "base": "B1",
            "segment": "ds",
            "stack_bounds": false
          },
          "encoding_checks": [
            {
              "byte": 2,
              "mask": 252,
              "equals": 0,
              "fault": "illegal_instruction"
            }
          ],
          "semantic": "load_memory_zero_extend"
        },
        {
          "id": "mov_b1_ds_disp16_load_8",
          "mnemonic": "MOV",
          "match": {
            "value": 152,
            "mask": 248
          },
          "length": 5,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            },
            {
              "name": "base",
              "byte": 2,
              "lsb": 0,
              "width": 2,
              "bank": "B1"
            }
          ],
          "operands": [
            {
              "name": "displacement",
              "byte": 3,
              "width_bytes": 2,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "transfer_bits": 8,
          "address": {
            "base": "B1",
            "segment": "ds",
            "stack_bounds": false
          },
          "encoding_checks": [
            {
              "byte": 2,
              "mask": 252,
              "equals": 0,
              "fault": "illegal_instruction"
            }
          ],
          "semantic": "load_memory_zero_extend"
        },
        {
          "id": "mov_b1_cs_direct_load_8",
          "mnemonic": "MOV",
          "match": {
            "value": 168,
            "mask": 248
          },
          "length": 3,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            },
            {
              "name": "base",
              "byte": 2,
              "lsb": 0,
              "width": 2,
              "bank": "B1"
            }
          ],
          "operands": [],
          "flags_written": [],
          "transfer_bits": 8,
          "address": {
            "base": "B1",
            "segment": "cs",
            "stack_bounds": false
          },
          "encoding_checks": [
            {
              "byte": 2,
              "mask": 252,
              "equals": 0,
              "fault": "illegal_instruction"
            }
          ],
          "semantic": "load_memory_zero_extend"
        },
        {
          "id": "mov_b1_cs_disp16_load_8",
          "mnemonic": "MOV",
          "match": {
            "value": 184,
            "mask": 248
          },
          "length": 5,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            },
            {
              "name": "base",
              "byte": 2,
              "lsb": 0,
              "width": 2,
              "bank": "B1"
            }
          ],
          "operands": [
            {
              "name": "displacement",
              "byte": 3,
              "width_bytes": 2,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "transfer_bits": 8,
          "address": {
            "base": "B1",
            "segment": "cs",
            "stack_bounds": false
          },
          "encoding_checks": [
            {
              "byte": 2,
              "mask": 252,
              "equals": 0,
              "fault": "illegal_instruction"
            }
          ],
          "semantic": "load_memory_zero_extend"
        },
        {
          "id": "mov_b1_ds_direct_store_8",
          "mnemonic": "MOV",
          "match": {
            "value": 200,
            "mask": 248
          },
          "length": 3,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            },
            {
              "name": "base",
              "byte": 2,
              "lsb": 0,
              "width": 2,
              "bank": "B1"
            }
          ],
          "operands": [],
          "flags_written": [],
          "transfer_bits": 8,
          "address": {
            "base": "B1",
            "segment": "ds",
            "stack_bounds": false
          },
          "encoding_checks": [
            {
              "byte": 2,
              "mask": 252,
              "equals": 0,
              "fault": "illegal_instruction"
            }
          ],
          "semantic": "store_memory"
        },
        {
          "id": "mov_b1_ds_disp16_store_8",
          "mnemonic": "MOV",
          "match": {
            "value": 216,
            "mask": 248
          },
          "length": 5,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            },
            {
              "name": "base",
              "byte": 2,
              "lsb": 0,
              "width": 2,
              "bank": "B1"
            }
          ],
          "operands": [
            {
              "name": "displacement",
              "byte": 3,
              "width_bytes": 2,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "transfer_bits": 8,
          "address": {
            "base": "B1",
            "segment": "ds",
            "stack_bounds": false
          },
          "encoding_checks": [
            {
              "byte": 2,
              "mask": 252,
              "equals": 0,
              "fault": "illegal_instruction"
            }
          ],
          "semantic": "store_memory"
        },
        {
          "id": "mov_b1_cs_direct_store_8",
          "mnemonic": "MOV",
          "match": {
            "value": 232,
            "mask": 248
          },
          "length": 3,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            },
            {
              "name": "base",
              "byte": 2,
              "lsb": 0,
              "width": 2,
              "bank": "B1"
            }
          ],
          "operands": [],
          "flags_written": [],
          "transfer_bits": 8,
          "address": {
            "base": "B1",
            "segment": "cs",
            "stack_bounds": false
          },
          "encoding_checks": [
            {
              "byte": 2,
              "mask": 252,
              "equals": 0,
              "fault": "illegal_instruction"
            }
          ],
          "semantic": "store_memory"
        },
        {
          "id": "mov_b1_cs_disp16_store_8",
          "mnemonic": "MOV",
          "match": {
            "value": 248,
            "mask": 248
          },
          "length": 5,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            },
            {
              "name": "base",
              "byte": 2,
              "lsb": 0,
              "width": 2,
              "bank": "B1"
            }
          ],
          "operands": [
            {
              "name": "displacement",
              "byte": 3,
              "width_bytes": 2,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "transfer_bits": 8,
          "address": {
            "base": "B1",
            "segment": "cs",
            "stack_bounds": false
          },
          "encoding_checks": [
            {
              "byte": 2,
              "mask": 252,
              "equals": 0,
              "fault": "illegal_instruction"
            }
          ],
          "semantic": "store_memory"
        },
        {
          "id": "mov_immediate_16",
          "mnemonic": "MOV",
          "match": {
            "value": 0,
            "mask": 248
          },
          "length": 4,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            }
          ],
          "operands": [
            {
              "name": "immediate",
              "byte": 2,
              "width_bytes": 2,
              "signed": false,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "transfer_bits": 16,
          "semantic": "move_immediate_zero_extend"
        },
        {
          "id": "mov_absolute_load_16",
          "mnemonic": "MOV",
          "match": {
            "value": 16,
            "mask": 248
          },
          "length": 4,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            }
          ],
          "operands": [
            {
              "name": "offset",
              "byte": 2,
              "width_bytes": 2,
              "signed": false,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "transfer_bits": 16,
          "address": {
            "base": "absolute",
            "segment": "ds",
            "stack_bounds": false
          },
          "semantic": "load_memory_zero_extend"
        },
        {
          "id": "mov_absolute_store_16",
          "mnemonic": "MOV",
          "match": {
            "value": 80,
            "mask": 248
          },
          "length": 4,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            }
          ],
          "operands": [
            {
              "name": "offset",
              "byte": 2,
              "width_bytes": 2,
              "signed": false,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "transfer_bits": 16,
          "address": {
            "base": "absolute",
            "segment": "ds",
            "stack_bounds": false
          },
          "semantic": "store_memory"
        },
        {
          "id": "mov_dp_load_16",
          "mnemonic": "MOV",
          "match": {
            "value": 32,
            "mask": 248
          },
          "length": 4,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            }
          ],
          "operands": [
            {
              "name": "offset",
              "byte": 2,
              "width_bytes": 2,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "transfer_bits": 16,
          "address": {
            "base": "dp",
            "segment": "ds",
            "stack_bounds": false
          },
          "semantic": "load_memory_zero_extend"
        },
        {
          "id": "mov_dp_store_16",
          "mnemonic": "MOV",
          "match": {
            "value": 96,
            "mask": 248
          },
          "length": 4,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            }
          ],
          "operands": [
            {
              "name": "offset",
              "byte": 2,
              "width_bytes": 2,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "transfer_bits": 16,
          "address": {
            "base": "dp",
            "segment": "ds",
            "stack_bounds": false
          },
          "semantic": "store_memory"
        },
        {
          "id": "mov_sp_load_16",
          "mnemonic": "MOV",
          "match": {
            "value": 48,
            "mask": 248
          },
          "length": 4,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            }
          ],
          "operands": [
            {
              "name": "offset",
              "byte": 2,
              "width_bytes": 2,
              "signed": false,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "transfer_bits": 16,
          "address": {
            "base": "sp",
            "segment": "ss",
            "stack_bounds": true
          },
          "semantic": "load_memory_zero_extend"
        },
        {
          "id": "mov_sp_store_16",
          "mnemonic": "MOV",
          "match": {
            "value": 112,
            "mask": 248
          },
          "length": 4,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            }
          ],
          "operands": [
            {
              "name": "offset",
              "byte": 2,
              "width_bytes": 2,
              "signed": false,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "transfer_bits": 16,
          "address": {
            "base": "sp",
            "segment": "ss",
            "stack_bounds": true
          },
          "semantic": "store_memory"
        },
        {
          "id": "mov_b1_ds_direct_load_16",
          "mnemonic": "MOV",
          "match": {
            "value": 128,
            "mask": 248
          },
          "length": 3,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            },
            {
              "name": "base",
              "byte": 2,
              "lsb": 0,
              "width": 2,
              "bank": "B1"
            }
          ],
          "operands": [],
          "flags_written": [],
          "transfer_bits": 16,
          "address": {
            "base": "B1",
            "segment": "ds",
            "stack_bounds": false
          },
          "encoding_checks": [
            {
              "byte": 2,
              "mask": 252,
              "equals": 0,
              "fault": "illegal_instruction"
            }
          ],
          "semantic": "load_memory_zero_extend"
        },
        {
          "id": "mov_b1_ds_disp16_load_16",
          "mnemonic": "MOV",
          "match": {
            "value": 144,
            "mask": 248
          },
          "length": 5,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            },
            {
              "name": "base",
              "byte": 2,
              "lsb": 0,
              "width": 2,
              "bank": "B1"
            }
          ],
          "operands": [
            {
              "name": "displacement",
              "byte": 3,
              "width_bytes": 2,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "transfer_bits": 16,
          "address": {
            "base": "B1",
            "segment": "ds",
            "stack_bounds": false
          },
          "encoding_checks": [
            {
              "byte": 2,
              "mask": 252,
              "equals": 0,
              "fault": "illegal_instruction"
            }
          ],
          "semantic": "load_memory_zero_extend"
        },
        {
          "id": "mov_b1_cs_direct_load_16",
          "mnemonic": "MOV",
          "match": {
            "value": 160,
            "mask": 248
          },
          "length": 3,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            },
            {
              "name": "base",
              "byte": 2,
              "lsb": 0,
              "width": 2,
              "bank": "B1"
            }
          ],
          "operands": [],
          "flags_written": [],
          "transfer_bits": 16,
          "address": {
            "base": "B1",
            "segment": "cs",
            "stack_bounds": false
          },
          "encoding_checks": [
            {
              "byte": 2,
              "mask": 252,
              "equals": 0,
              "fault": "illegal_instruction"
            }
          ],
          "semantic": "load_memory_zero_extend"
        },
        {
          "id": "mov_b1_cs_disp16_load_16",
          "mnemonic": "MOV",
          "match": {
            "value": 176,
            "mask": 248
          },
          "length": 5,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            },
            {
              "name": "base",
              "byte": 2,
              "lsb": 0,
              "width": 2,
              "bank": "B1"
            }
          ],
          "operands": [
            {
              "name": "displacement",
              "byte": 3,
              "width_bytes": 2,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "transfer_bits": 16,
          "address": {
            "base": "B1",
            "segment": "cs",
            "stack_bounds": false
          },
          "encoding_checks": [
            {
              "byte": 2,
              "mask": 252,
              "equals": 0,
              "fault": "illegal_instruction"
            }
          ],
          "semantic": "load_memory_zero_extend"
        },
        {
          "id": "mov_b1_ds_direct_store_16",
          "mnemonic": "MOV",
          "match": {
            "value": 192,
            "mask": 248
          },
          "length": 3,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            },
            {
              "name": "base",
              "byte": 2,
              "lsb": 0,
              "width": 2,
              "bank": "B1"
            }
          ],
          "operands": [],
          "flags_written": [],
          "transfer_bits": 16,
          "address": {
            "base": "B1",
            "segment": "ds",
            "stack_bounds": false
          },
          "encoding_checks": [
            {
              "byte": 2,
              "mask": 252,
              "equals": 0,
              "fault": "illegal_instruction"
            }
          ],
          "semantic": "store_memory"
        },
        {
          "id": "mov_b1_ds_disp16_store_16",
          "mnemonic": "MOV",
          "match": {
            "value": 208,
            "mask": 248
          },
          "length": 5,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            },
            {
              "name": "base",
              "byte": 2,
              "lsb": 0,
              "width": 2,
              "bank": "B1"
            }
          ],
          "operands": [
            {
              "name": "displacement",
              "byte": 3,
              "width_bytes": 2,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "transfer_bits": 16,
          "address": {
            "base": "B1",
            "segment": "ds",
            "stack_bounds": false
          },
          "encoding_checks": [
            {
              "byte": 2,
              "mask": 252,
              "equals": 0,
              "fault": "illegal_instruction"
            }
          ],
          "semantic": "store_memory"
        },
        {
          "id": "mov_b1_cs_direct_store_16",
          "mnemonic": "MOV",
          "match": {
            "value": 224,
            "mask": 248
          },
          "length": 3,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            },
            {
              "name": "base",
              "byte": 2,
              "lsb": 0,
              "width": 2,
              "bank": "B1"
            }
          ],
          "operands": [],
          "flags_written": [],
          "transfer_bits": 16,
          "address": {
            "base": "B1",
            "segment": "cs",
            "stack_bounds": false
          },
          "encoding_checks": [
            {
              "byte": 2,
              "mask": 252,
              "equals": 0,
              "fault": "illegal_instruction"
            }
          ],
          "semantic": "store_memory"
        },
        {
          "id": "mov_b1_cs_disp16_store_16",
          "mnemonic": "MOV",
          "match": {
            "value": 240,
            "mask": 248
          },
          "length": 5,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            },
            {
              "name": "base",
              "byte": 2,
              "lsb": 0,
              "width": 2,
              "bank": "B1"
            }
          ],
          "operands": [
            {
              "name": "displacement",
              "byte": 3,
              "width_bytes": 2,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "transfer_bits": 16,
          "address": {
            "base": "B1",
            "segment": "cs",
            "stack_bounds": false
          },
          "encoding_checks": [
            {
              "byte": 2,
              "mask": 252,
              "equals": 0,
              "fault": "illegal_instruction"
            }
          ],
          "semantic": "store_memory"
        }
      ]
    },
    "ext1a": {
      "inspect_byte": 1,
      "entries": [
        {
          "id": "cmp",
          "mnemonic": "CMP",
          "match": {
            "value": 0,
            "mask": 240
          },
          "length": 2,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 2,
              "width": 2,
              "bank": "B0"
            },
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [],
          "flags_written": [
            "N",
            "Z",
            "C",
            "V"
          ],
          "semantic": "compare"
        },
        {
          "id": "shl",
          "mnemonic": "SHL",
          "match": {
            "value": 16,
            "mask": 252
          },
          "length": 2,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [],
          "flags_written": [
            "N",
            "Z",
            "C"
          ],
          "semantic": "shl"
        },
        {
          "id": "shr",
          "mnemonic": "SHR",
          "match": {
            "value": 32,
            "mask": 252
          },
          "length": 2,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [],
          "flags_written": [
            "N",
            "Z",
            "C"
          ],
          "semantic": "shr"
        },
        {
          "id": "sar",
          "mnemonic": "SAR",
          "match": {
            "value": 48,
            "mask": 252
          },
          "length": 2,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [],
          "flags_written": [
            "N",
            "Z",
            "C"
          ],
          "semantic": "sar"
        }
      ]
    },
    "ext1f": {
      "inspect_byte": 1,
      "entries": [
        {
          "id": "push",
          "mnemonic": "PUSH",
          "match": {
            "value": 0,
            "mask": 248
          },
          "length": 2,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            }
          ],
          "operands": [],
          "flags_written": [],
          "semantic": "push"
        },
        {
          "id": "pop",
          "mnemonic": "POP",
          "match": {
            "value": 8,
            "mask": 248
          },
          "length": 2,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            }
          ],
          "operands": [],
          "flags_written": [],
          "semantic": "pop"
        },
        {
          "id": "call",
          "mnemonic": "CALL",
          "match": {
            "value": 16,
            "mask": 255
          },
          "length": 4,
          "fields": [],
          "operands": [
            {
              "name": "target",
              "byte": 2,
              "width_bytes": 2,
              "signed": false,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "semantic": "call_near_absolute"
        },
        {
          "id": "ret",
          "mnemonic": "RET",
          "match": {
            "value": 17,
            "mask": 255
          },
          "length": 2,
          "fields": [],
          "operands": [],
          "flags_written": [],
          "semantic": "return_near"
        },
        {
          "id": "iret",
          "mnemonic": "IRET",
          "match": {
            "value": 18,
            "mask": 255
          },
          "length": 2,
          "fields": [],
          "operands": [],
          "flags_written": [],
          "semantic": "return_exception",
          "special_effects": [
            "restore_context_including_csr_and_clear_active_e"
          ]
        },
        {
          "id": "getx",
          "mnemonic": "GETX",
          "match": {
            "values": [
              32,
              33,
              34,
              35,
              36,
              37,
              38,
              39,
              40,
              41,
              42,
              43,
              44,
              45,
              46,
              47,
              48,
              49,
              50,
              51,
              52
            ]
          },
          "length": 3,
          "fields": [
            {
              "name": "context",
              "byte": 1,
              "lsb": 0,
              "width": 5,
              "bank": "context"
            },
            {
              "name": "destination",
              "byte": 2,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            }
          ],
          "operands": [],
          "flags_written": [],
          "encoding_checks": [
            {
              "byte": 2,
              "mask": 248,
              "equals": 0,
              "fault": "illegal_instruction"
            }
          ],
          "semantic": "getx"
        },
        {
          "id": "setx",
          "mnemonic": "SETX",
          "match": {
            "values": [
              64,
              65,
              66,
              67,
              68,
              69,
              70,
              71,
              72,
              73,
              74,
              75,
              76,
              77,
              78,
              79,
              80
            ]
          },
          "length": 3,
          "fields": [
            {
              "name": "context",
              "byte": 1,
              "lsb": 0,
              "width": 5,
              "bank": "context"
            },
            {
              "name": "source",
              "byte": 2,
              "lsb": 0,
              "width": 3,
              "bank": "general"
            }
          ],
          "operands": [],
          "flags_written": [],
          "encoding_checks": [
            {
              "byte": 2,
              "mask": 248,
              "equals": 0,
              "fault": "illegal_instruction"
            }
          ],
          "semantic": "setx"
        },
        {
          "id": "js_register",
          "mnemonic": "JS",
          "match": {
            "value": 96,
            "mask": 252
          },
          "length": 2,
          "fields": [
            {
              "name": "target",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B1"
            }
          ],
          "operands": [],
          "flags_written": [],
          "semantic": "jump_near_register"
        }
      ]
    }
  },
  "aliases": [
    {
      "mnemonic": "CLI",
      "expands_to": "DI"
    },
    {
      "mnemonic": "SEI",
      "expands_to": "EI"
    },
    {
      "mnemonic": "PULL",
      "expands_to": "POP"
    },
    {
      "mnemonic": "INCP",
      "expands_to": "ADDP",
      "adjustment": 1,
      "destinations": [
        "r4",
        "r5",
        "r6",
        "r7",
        "dp"
      ]
    },
    {
      "mnemonic": "DECP",
      "expands_to": "ADDP",
      "adjustment": -1,
      "destinations": [
        "r4",
        "r5",
        "r6",
        "r7",
        "dp"
      ]
    },
    {
      "mnemonic": "MOV",
      "same_general_register_operands": true,
      "expands_to": "NOP"
    },
    {
      "mnemonic": "PUSH.W",
      "expands_to": "PUSH"
    },
    {
      "mnemonic": "POP.W",
      "expands_to": "POP"
    },
    {
      "mnemonic": "PULL.W",
      "expands_to": "POP"
    },
    {
      "mnemonic": "TRAP",
      "expands_to": "SWI"
    }
  ],
  "assembler_rules": {
    "default_memory_width_bits": 16,
    "default_b1_segment": "ds",
    "omit_zero_b1_displacement": true,
    "stack_offset_signed": false,
    "adjsp_signed_range": [
      -128,
      127
    ],
    "addp_signed_range": [
      -128,
      127
    ],
    "indirect_js_bank": "B1",
    "memory_indirect_js": false,
    "indirect_call": false,
    "swi_unsigned_range": [
      0,
      255
    ]
  },
  "semantic_reference": "ISA.md",
  "format_reference": "ISA_ENCODING.md"
}
```
<!-- ISA_ENCODING_JSON_END -->
