# Machine-readable ISA encodings

The [machine-readable table](#machine-readable-table) embedded below defines the binary layouts currently consumed by the tools. The payload includes all currently assigned instructions in the [working ISA](ISA.md), including immediate ALU, protected controls, extended context and MOVU. The assembler/disassembler reads this table, and the C++ emulator generates its matching, length and reserved-byte tables from it. Execution semantics remain implemented in C++; [ISA.md Appendix C](ISA.md#implementation-status-and-remaining-work) is the authoritative runtime/profile and validation-status record. JSON specifies matching and operand layout; ISA.md specifies execution, faults, assembly restrictions, and commit behavior. Any discrepancy is a specification defect.

The JSON block is the sole machine-readable source. There is no separate JSON documentation file. Preserve the payload and the `ISA_ENCODING_JSON_BEGIN` / `ISA_ENCODING_JSON_END` markers when editing this document.

Documentation: [Architecture and conformance](ISA.md) · [Examples and design measurements](ISA_EXAMPLES.md).

## Encoding requirements

The six byte/word immediate ALU families are assigned at $80–$AF in
[ISA.md](ISA.md#immediate-alu-instructions). Extended context access, protected
controls, privilege metadata, and MOVU user-access transfers are assigned below.
The embedded payload covers every current assignment. Current runtime evidence is recorded in [Appendix C](ISA.md#implementation-status-and-remaining-work).

| Area | Revised draft requirement | Allocation status |
|---|---|---|
| Existing instructions | Assigned bytes, CSR bits, B0/B1 banks and context indices 0x00–0x14 | Assigned layouts; protected semantics defined in ISA.md |
| Mode/entry/return | U I=1; protected provenance; eight ordinary frames; failed entry/overflow is terminal | SWI imm8/IRET bytes retained; no executable emergency interface |
| CPU control access | Readable committed descriptors, published-map state, user-access controls and one-way bootstrap action | GETC `$1F $A0–$A7 selector8`, SETC `$1F $B0–$B7 selector8`; control IDs/commands assigned in ISA.md; layouts included in the payload |
| Extended context access | Saved protected state and new fault metadata | GETX `$1F $80–$87 index8`, SETX `$1F $90–$97 index8`; 21 new indices assigned; base GETX/SETX included; layouts included in the payload |
| Faults/vectors | Origin/phase/access/map/start/relevant virtual and physical address/reason record | New word IDs, numeric enums, privilege cause/vector 7, and operation classes assigned in ISA.md; architectural contract assigned |
| Memory | Published/invalidate map control; whole-descriptor publication; U-rights user-access transfer | Controls and MOVU `$27` descriptor family assigned; layouts included in the payload; software generations are not hardware map bits |
| PIC/timer/MMIO | 16 sources, timer source 0, atomic snapshot/configuration/ACK | `minimal-v1` physical `$FF0000–$FF00FF` assigned in Chapter 7; ordinary MOV, no CPU-control mirror; architectural contract assigned |
| Immediate ALU | ADDI, SUBI, CMPI, ANDI, ORI, XORI; B0 only, byte/word width | $80–$AF assigned; layouts included in the payload |
| Compact B1 displacement | Signed -32..31 in existing a=0 descriptor; disp16 fallback | Assigned , no new primary or secondary tags |
| Short relative branches | All fifteen rel8 predicates, `.S`/`.W` and iterative relaxation | Assigned working `$60–$6E`, `$6F` reserved; layouts in payload |
| Compact SP memory | Unsigned 0..255 plus off16 fallback, both widths/directions | Assigned working `$28`, 32 secondary tags; layouts in payload |
| Remaining extensions | Defer post-increment / register-to-pointer arithmetic | No additional assignments |

Unassigned primary bytes and reserved fields are illegal as soon as they are
known. Do not fetch operands after an unassigned primary opcode. Assigned
immediates require the selected byte/word operand to be fetched completely before
execution; missing operand bytes are fetch faults.

### Assigned compact B1 displacements

Working MOV B1 short forms use `$15 1ds0zrrr ddddddpp`: pp selects r4–r7,
dddddd is a signed six-bit displacement (-32..31). Long forms retain
`$15 1ds1zrrr 000000pp disp_lo disp_hi` and signed disp16.  Long descriptor checks
remain unconditional and occur before fetching displacement. MOVU is unchanged.

Assemblers select short for working offsets -32..31 inclusive and long otherwise;
 Raw long forms are legal even for zero and
other short values; disassembly canonicalizes to the shortest available assembly.
For example, `MOV.W [r4+2],r0` changes from `15 90 00 02 00` (five bytes) to
`15 80 08` (three) with the same successful load and flags. Base/value aliases
sample the original registers; widened addresses, protection, atomic stores and
MMIO transactions follow [ISA.md](ISA.md#b1-indirect-forms-bit-7--1).

### Assigned short branches

Working rel8 uses `$60+condition disp8`, two bytes. Conditions 0–14 have the
same order and flags as existing `$40–$4E` rel16; `$6F` is reserved and illegal
before operand fetch.  Fifteen
new primary values are assigned and one unassigned value is held for the block;
no secondary tag is consumed. Schema 5 describes signed one-byte operands without version gates.

The target is in the input CS and the displacement is signed -128..127 from the
selected end, computed in a wide type. Complete fetch precedes predicate selection.
Taken targets require representability, descriptor execute interval/rights and
RAM backing; untaken branches do not probe discarded targets. Intermediate
next=$10000 is allowed only for a taken branch whose selected target is valid.
No instruction byte wraps. Flags, registers and precise fault/retirement rules
are identical to rel16. See [normative branch semantics](ISA.md#bcc-bra--conditional-or-unconditional-relative-branch).

`.S` forces rel8 and `.W` forces rel16; working unsuffixed forms start long.
Initially account for forced-short instructions at length two. Recompute all
positions and labels, then shorten every eligible automatic long branch from
its candidate two-byte end, including its own effect on forward labels. Repeat
to a fixed point; only then validate forced-short reach and final image bounds.
A long-first trial layout may cross the segment if the final image fits. Relocated
CALL/JS/CALLF labels use that same final layout. Forced-long never shrinks.

Numeric operands are absolute target offsets and do not relocate. A preceding
shrink can therefore move a previously eligible numeric branch out of range.
After convergence, pin any such automatic numeric branches long and restart
from the long trial layout; pins accumulate, guaranteeing termination. This may
conservatively retain a numeric long branch. No emitted displacement is truncated
or allowed to remain out of range. A forced-short numeric target is checked after
the final layout and rejects if it does not fit. Use labels for code that moves.

Disassembly emits `.S` or `.W`  to retain exact branch width on
reassembly. For example `BRA.S $100` at
$100 is `6E FE`; deliberate `BRA.W $100` is `4E FD FF`. Ordinary `BRA label`
selects the short form when the final displacement fits.

### Assigned compact SP offsets

Working uses `$28 0d11zrrr off8`, three bytes: d selects load/store, z selects
word/byte, and rrr selects r0–r7 exactly as in existing SP MOV. The second byte
is $30–$3F or $70–$7F; the other 224 values reject before offset fetch.  The offset is unsigned 0..255 regardless of
transfer width. Offset $FF means +255; 256 uses the existing four-byte
`$15 0d11zrrr lo hi` form and cannot truncate to zero.

Both forms sample old inputs, form a widened SP+unsigned(offset), check the same
SB≤SP≤ST invariant and whole allocated slot, then apply SS rights/kind/backing
and selected following-PC validation. Byte loads zero-extend, stores use original
source bits, SP/bounds/flags are unchanged, failed transfers are atomic, and no
SP access may perform MMIO. [Normative semantics](ISA.md#28-compact-sp-memory-transfers)
include fault order, exclusive ST and partial words.

Select off8 for working 0..255 and off16 for 256..65535; reject negative/out-of-range
assembly. Raw long 0..255 forms stay legal and
noncanonical. Disassembly reports their four-byte length, while working reassembly
selects three; retain raw images when exact noncanonical bytes are needed. MOV
`.B`/`.W` selects data width and does not force long addressing.

One primary and a sixth escape table are assigned: ext28 has 32 matching secondary
values and 224 reserved values. Existing ext15 tags and all assigned bytes remain
unchanged. Primary totals are 111 assigned / 145 unassigned, with 144 outside the
held $6F branch slot. Metadata schema 5 already supports this dispatch, unsigned
one-byte operand; seven total decode tables are generated.
The [current report](ISA_EXAMPLES.md#current-workload-measurements) measures actual
code sizes, instruction counts and addressing distributions.

### Assigned immediate ALU layouts

| Operation | Byte bases/ranges (2 bytes) | Word bases/ranges (3 bytes) |
|---|---|---|
| ADDI | $80–$83 | $84–$87 |
| SUBI | $88–$8B | $8C–$8F |
| CMPI | $90–$93 | $94–$97 |
| ANDI | $98–$9B | $9C–$9F |
| ORI | $A0–$A3 | $A4–$A7 |
| XORI | $A8–$AB | $AC–$AF |

Each range's low two opcode bits select r0–r3. The next byte is imm8 for `.B`;
`.W` carries imm16 low byte then high byte. There is no descriptor byte. All 48
primary bytes are assigned in the working ISA and represented in the payload. Unsuffixed assembly defaults to `.W`. Negative literals use the
selected width's two's complement, with assembly ranges -128..255 and
-32768..65535. Reject B1/internal destinations, invalid widths and truncated or
out-of-range assembly operands.

`.B` computes an eight-bit result with N from bit 7 and zero-extends destination
writes; CMPI preserves the whole register. `.W` computes sixteen-bit results
with N from bit 15. Arithmetic sets NZCV with no-borrow C for subtraction;
logic sets NZ and preserves CV; every form preserves I/E. Operation width is
semantic: never select `.B` merely because a word operation's immediate fits
in eight bits. In particular, these forms do not use a sign-extended imm4.
See ISA.md for full semantics, atomic fetch/PC failure behavior and examples.

### Assigned protected control layouts

GETC/SETC use the following assigned tags:

| Instruction | Bytes | Encoding |
|---|---:|---|
| `GETC control,rn` | 3 | `$1F`, `$A0+rn`, `selector8` |
| `SETC rn,control` | 3 | `$1F`, `$B0+rn`, `selector8` |

rn ranges from r0 through r7. The third byte is the full control index. The
[normative control tables](ISA.md#control-selectors-and-reset-values) assign every
selector, permission, scalar representation, and reset value. The
[command table](ISA.md#assigned-control-commands) assigns DESC_COMMIT=1,
DESC_LOAD=2, MAP_PUBLISH=3, MAP_INVALIDATE=4, ENTRY_COMMIT=5, ENTRY_LOAD=6,
PREPARE_USER_RETURN=7, and BACKING_QUERY=8; commands are register data written to
selector `$7F`, not eight additional opcodes.

Unknown/direction-invalid selectors are illegal at byte 2; reserved secondary
tags are illegal at byte 1. Complete legal fetch/decode precedes privilege,
scalar/command state checks, operand probes, and next-PC checks. Failure commits
no ordinary effects. GETC/SETC are K-only word transfers preserving all flags.
Readback distinguishes shadow from committed state; commands publish whole
descriptors/entry tuples atomically. Examples: `1F A2 00` reads process_map into
r2; `1F B0 7F` executes the command in r0. See the
[binary reference](ISA.md#protected-control-encoding) for exact reserved/fetch cases.

The working `$1F` table now has 93 assigned / 163 unassigned second-byte values;
the payload represents that same map. GETC/SETC add `$A0–$A7` and `$B0–$B7`;
extended GETX/SETX below add `$80–$87` and `$90–$97`. All other positions in
`$80–$BF` remain illegal. These access forms consume no primary value.

### Assigned extended context layouts

| Instruction | Bytes | Encoding | Accepted extended indices |
|---|---:|---|---|
| `GETX extended_context,rn` | 3 | `$1F`, `$80+rn`, `index8` | `$20–$29`, `$30–$3A` |
| `SETX rn,extended_context` | 3 | `$1F`, `$90+rn`, `index8` | `$20–$22` only |

rn selects any r0–r7. [The extended context table](ISA.md#extended-saved-context)
assigns 21 new words: three writable saved process/user-access controls plus
eighteen read-only provenance/event/fault words. Together with base context,
42 words are readable and twenty writable. Base words have their own
encoding; old indices under an extended tag are illegal. Assembly chooses the
encoding family from the assigned context name/index, without changing existing
source or bytes. There is no widened base five-bit field or extended alias.

Unknown/read-only write indices are illegal on byte 2, before privilege and
active-context checks. Reserved secondary tags are illegal on byte 1; missing
index8 of an otherwise assigned tag is a fetch fault. Complete legal decode
precedes K-only privilege, active-top-frame validation, and next-PC checks.
Extended SETX accepts whole-word edits without early saved-image validation;
IRET checks the full image in its existing order. Read-only origin, linkage,
depth, return-mode, bootstrap, and fault fields cannot be forged through SETX.

Privilege has cause/vector 7 at kernel 0:`$001C–$001F`. Its operation-class values
and the memory-origin/phase/access/reason enums are assigned in
[Chapter 5](ISA.md#privilege-exception-record). NONE is zero in every enum;
metadata initializes per event, memory words clear on nonmemory entries,
and physical address semantics remain unchanged. Examples:
`GETX xprocess_map,r2` = `1F 82 20`, `SETX r7,xuaccess_segment` = `1F 97 22`,
`GETX xpriv_class,r0` = `1F 80 29`. These assignments are present in the payload
and assigned.

### Assigned user-access layouts

MOVU is assigned primary `$27` with mode/base bytes and optional disp16:

```text
$27 dazrrr00 000000pp [disp_lo disp_hi]
```

d=0 load/1 store; a=0 no displacement/1 signed disp16; z=0 word/1 byte;
rrr selects value r0–r7 and pp selects base r4–r7. Short forms are three bytes,
long forms five. Reserved low mode bits reject at byte 1; reserved high base
bits reject at byte 2 before any displacement fetch. All legal operand bytes
are fetched before K-only privilege and mapping checks. Unsuffixed assembly is
word; zero displacement assembles short, while raw long-zero is valid but
noncanonical. No segment override or other addressing mode is assigned.

There are 64 matching second bytes and 192 reserved values in the new ext27
table, with four legal base bytes each: 128 short and 128 displaced structural
forms. MOVU accepts both widths with all eight value registers and four B1 bases.
Examples: `MOVU.B [r4],r0` = `27 20 00`;
`MOVU.W r1,[r5-2]` = `27 C4 01 FE FF`;
`MOVU.W [r6+2],r7` = `27 5C 02 02 00`.

The [binary reference](ISA.md#user-access-encoding) and
[execution contract](ISA.md#explicit-user-access-transfers) specify exact
sampled-input/alias, U-permission, zero-extension, flag-preservation, MMIO-denial,
and fault-priority behavior. A legal U MOVU gets Privilege/USER_ACCESS=10. K
operand failures use Memory Access/UACCESS/OPERAND; K code/next-PC failures use
KERNEL/FETCH or RESULT_PC instead. No partial transfer commits on failure.

The payload retains ext27 as the sixth decode table and adds ext28 as the seventh. Generated table counts
are derived from metadata.

### Assigned test layouts

[The TST/TSTI review](ISA_EXAMPLES.md#current-workload-measurements) accepts the following B0-only forms:

| Form | Bytes | Layout |
|---|---:|---|
| TST.B rd | 2 | `$1A`, `$40+rd` |
| TSTI.B #imm8,rd | 3 | `$1A`, `$44+rd`, imm8 |
| TSTI.W #imm16,rd | 4 | `$1A`, `$48+rd`, imm_lo, imm_hi |
| TST.W rd / TST rd | 2 | Existing `OR rd,rd`: `$10`, `$20+5*rd` |

Tests preserve all register bits and C/V/I/E, setting width-specific N/Z only.
Immediate ranges match ANDI; default width is word. The twelve test tags and
forty shift/rotate tags below give `ext1a` 80 assigned / 176 free. No primary encoding changes.

Schema 5 covers all assigned encodings without runtime-version gates. Register,
selector and reserved-bit validation apply to every decode. The
[status record](ISA.md#implementation-status-and-remaining-work) identifies executed evidence.
TST.W aliases OR; canonical disassembly remains OR. Consumers must validate schema 5 before decoding. Its
`encoding_checks.allowed_values` lists restrict selectors as soon as fetched;
ignoring them would violate decode-before-privilege ordering.

### Assigned immediate-count shifts and rotates

All forms use `$1A tag count8`, three bytes, r0–r3 only:

| Operation | Byte tags | Word tags |
|---|---|---|
| SHLI | `$60–$63` | `$64–$67` |
| SHRI | `$68–$6B` | `$6C–$6F` |
| SARI | `$70–$73` | `$74–$77` |
| ROLI | `$78–$7B` | `$7C–$7F` |
| RORI | `$80–$83` | `$84–$87` |

Count is an unsigned number of positions, valid exactly 0–16 for either width.
Unsuffixed source is `.W`. Negative/out-of-range literals, register counts,
other banks/suffixes and wrong arities are rejected. The third byte is ordinary
operand data: raw counts 17–255 raise Invalid State after full fetch and before
following-PC validation. Missing count is a fetch fault; reserved neighboring
`$5F/$88` tags are illegal without count fetch.

Count zero writes neither the destination nor any flag (including the byte form's
upper register bits), but still requires a representable following PC. Positive
counts use selected-width repeated single-bit semantics, writing byte results
zero-extended, setting N/Z/C and preserving V/I/E. Rotates use no incoming C;
full-width positive rotations produce fresh N/Z/C. All steps are one atomic
instruction, with no intermediate IRQ or retirement. The
[normative rules](ISA.md#immediate-count-shifts-and-rotates) and
[conformance vectors](ISA.md#conformance-immediate-count-shifts-and-rotates)
fix carry, overflow preservation and fault ordering exactly.

Forty tags × seventeen counts give 680 legal streams; 9,560 more select known
instructions with invalid count state. The assignment consumes zero primaries,
needs no longer instruction buffer, and leaves 176 unassigned `$1A` tags. This
accepts the escaped layout: forty dedicated primaries would save one byte per
instruction but reduce projected primary headroom from 144 to 104, without
improving functionality. Eight existing one-bit word shifts cost sixteen bytes
and eight instructions versus three bytes/one instruction for a count of eight.
No `$80–$AF` immediate ALU or existing one-bit-shift encoding is changed.

### Assigned far control layouts

| Form | Layout | Bytes | Runtime |
|---|---|---:|---|
| `CALLF abs32` | `$54 pc_lo pc_hi cs 00` | 5 | working |
| `RETF` | `$55` | 1 | working |

CALLF uses JL's canonical packed target: source accepts 0..$FFFFFF without
`#`/suffix, while encoded bits 31–24 must be zero. A nonzero high byte is
Invalid State after full fetch, not an early decode rejection. RETF accepts
no operand/suffix. These are distinct from near CALL `$1F $10 lo hi` and
RET `$1F $11`, whose bytes and two-byte offset frames are unchanged.

The [normative far-call contract](ISA.md#far-call-and-return-contract) fixes the
four-byte frame at new SS:SP as `[next_lo,next_hi,old_cs,0]`. Both words are
preflighted before one atomic RAM commit/pop, with precise fault ordering and
no mode/map authority change. Existing word fault metadata is retained: each
frame word is an operand probe of width 2. CALLF saves a representable next;
RETF validates the saved target instead of unused sequential next.

No new decode table or instruction
longer than five bytes is needed; four immediate bytes fit the existing operand
extractor. The staged RAM store can now hold the complete four-byte return frame.
Flat labels resolve to numeric offsets in segment zero; reject a CALLF label
at offset $10000 instead of silently treating it as segment 1. Use packed
literals for other segments. The assembler provides no object-file relocation or linker service.

### Assigned PIC/timer machine interface

The [minimum machine profile](ISA.md#minimum-machine-mmio-profile) assigns
physical `$FF0000–$FF00FF` as MMIO, with the remainder of frame `$FF` holes.
Registers accept aligned word MOV transactions only. Unsupported widths,
directions, reserved offsets/values, and invalid configuration fail pure DEVICE
preflight; all ordinary CPU checks also precede a single infallible commit.

| Physical frame offset | Assigned interface |
|---|---|
| `$0000–$000A`, even | PIC pending, enable, in-service, write-one ACK, level, source mask |
| `$0020–$0026`, even | Timer command, status, shadow mode, active mode |
| `$0030–$003E`, even | Four deadline and four period shadow words |
| `$0040–$0046`, even | Four snapshot time words |
| `$0048–$0056`, even | Four active deadline and four active period words |

All other starts are reserved; the normative table defines each direction and
reset value. Timer commands are SNAPSHOT_TIME=1, COMMIT_TIMER=2,
DISABLE_TIMER=3, CLEAR_DEADLINE_OVERFLOW=4. Source 0 is internal timer, 1 external
edge, 2 external level; 3–15 are unconnected. IRQ entry uses vector 5 and the
assigned xirq_source context word. ACK and IRET are independent.

[The host contract](ISA.md#deterministic-host-events-and-replay) assigns ordered
boundary events, `vcpu-replay` JSONL version 1, exact error codes, and recorded
idle advancement. These interfaces are the host event contract. No primary or secondary
opcode is consumed, and the embedded instruction metadata is unchanged.

## Opcode allocation budget

**Assignments and remaining planning capacity.** The [assigned allocation ledger](ISA.md#opcode-allocation-ledger)
accounts for all 256 primary values and each existing escape table. The working
ISA has 111 assigned primary values and 145 available values (including held `$6F`). The embedded tool
payload has the same 111 assigned primary values (all for the working ISA). The immediate ALU allocation remains
`$80–$AF`: 48 values, 18.75% of primary space, with two-byte byte operations and
three-byte word operations.

The access forms above are assigned by the normative manual; other choices below
provide a fit and cost estimate, not an allocation, approval of candidate semantics,
or production tool support. GETC/SETC and extended GETX/SETX already use 32
positions in the budgeted control/context block; its remaining 32 positions are
reserved-bit patterns, not executable alternate forms.

### Candidate layouts and costs

| Family | Assigned or budgeted layout | Primary capacity cost | Secondary or operand-field capacity | Instruction bytes |
|---|---|---:|---|---|
| Protected control and extended context access | GETC/SETC and extended GETX/SETX assigned under `$1F` in `$80–$BF` | 0 | 32 assigned tags plus 32 reserved-bit patterns held for the family; selector validity is separate | 3 |
| Control commands, map/descriptor publication, bootstrap | Assigned writes to command selector `$7F` through SETC | 0 beyond control access | Eight assigned command values; not another primary family per command | 3 per control transfer; complete sequences may need several transfers |
| User-access byte/word loads/stores | MOVU `$27`, `dazrrr00`, `000000pp`, optional disp16; now assigned | 1, already assigned | 64 valid second-byte patterns; 192 remain reserved; third byte selects four B1 bases and has six reserved bits | 3 without displacement; 5 with disp16 |
| Minimum PIC/timer | Assigned `minimal-v1` MMIO `$FF0000–$FF00FF` via existing MOV forms | 0 | MMIO addresses/register fields, not instruction opcode space | Existing MOV lengths |
| rel8 branches | Assigned `$60–$6E`, `$6F` held reserved; same predicate order as rel16 | 15 assigned plus one reserved block position | One signed displacement byte | 2 |
| Compact B1 simm6 | Assigned working: existing `$15` a=0 forms; descriptor `ddddddpp` | 0 | Reuse upper six bits of the third byte for the existing 64 short-form secondary tags; no new secondary tags | 3; retain 5-byte disp16 fallback |
| Compact SP uimm8 | Assigned working `$28`, `0d11zrrr`, off8 | 1, already assigned | 32 valid second-byte patterns; 224 remain reserved | 3; retain 4-byte off16 fallback |
| Far call and far return | Assigned CALLF `$54` packed target and RETF `$55` | 2, already assigned | Four-byte PC/CS return frame; same mode/map | 5 for call; 1 for return |
| TST.B / TST.W | Assigned `$1A $40–$43` byte tags; word OR-self alias | 0 | Four assigned tags; no new word tags | 2 |
| TSTI.B / TSTI.W | Assigned `$1A $44–$4B` plus immediate | 0 | Eight assigned tags, four per width | 3 for byte; 4 for word |
| Immediate-count shifts/rotates | Assigned `$1A $60–$87`, count8 | 0 | Forty assigned secondary tags: five operations × two widths × four destinations; count 0–16 | 3 |

The assigned user-access descriptor uses configured uaccess_segment and therefore
needs no ordinary CS/DS selector bit. B0-only test support is accepted;
expansion to other banks requires separate evidence. TST.W aliases `OR rd,rd`,
consuming no separate word-test tags.

For B1 simm6, 64 a=0 second-byte tags accept 256 descriptors each, giving
16,384 three-byte forms. The a=1 disp16 forms require zero reserved high descriptor
bits. Both forms share the existing primary and secondary table.

### Cumulative capacity projection

This projection charges whole blocks, including intentionally reserved holes.
“Remaining” means capacity uncommitted by this planning scenario. Actual assigned
primary counts are 111/145; MOVU, CALLF/RETF, compact SP and fifteen rel8 values are already charged. Control/context
accesses have consumed 32 existing-secondary positions but no primary values.

| Step | Primary charge at this step | Primary capacity remaining | Secondary capacity remaining outside budgeted blocks |
|---|---:|---:|---|
| Current working map, including all three compact families | — | 145 | `$1F`: 163; `$1A`: 176; `$27`: 192; `$28`: 224 |
| Hold reserved rel8 condition `$6F` | 1 held, not assigned | 144 | Unchanged |
| Hold remaining reserved control/context block positions | 0 | **144** | `$1F`: **131**; `$1A`: **176**; `$27`: **192**; `$28`: **224** |
| Existing commands, MOVU, devices, B1/SP compact, branches, far calls, tests and shifts | 0, already charged | 144 | Unchanged |

All three reviewed compact families are assigned. No additional primary for SP
remains to be charged: it brings the actual map to 111 assigned / 145 unassigned,
with 144 unassigned values outside held $6F. Twelve test tags and forty immediate-
shift tags are assigned under `$1A` (52 total). Any future register-to-pointer
proposal requires a separate measured adoption and allocation review.

Likewise, the control block's 32 reserved-bit patterns are unavailable to other
families in this scenario even though they remain illegal, so its 64-position
budget exceeds its 32 matching tags. Neither reserved-hole count is a new grant.

Block shape fits as well as total counts: the primary map contains whole free
16-value blocks and disjoint single-byte slots; `$1F` had two completely free
aligned 64-value blocks before control assignment. `$80–$BF` now contains its
32 assigned access tags and 32 reserved held positions, while `$C0–$FF` remains a
completely free 64-value block. `$1A` has ample disjoint free eight-value groups.
Choose the remaining actual nonoverlapping tags only at assignment review. All
four existing
escape primaries and their assigned tags are preserved; MOVU adds the fifth
and compact SP the sixth escape. The 192 reserved MOVU descriptor values remain available only to a
reviewed future extension of that family. The small `$10` and
`$15` spare regions need not be consumed to make the planned families fit.

### Encoding tradeoffs and allocation policy

| Design choice | Primary / secondary cost | Length effect and implementation tradeoff |
|---|---|---|
| Retain assigned immediate ALU primaries | Existing 48 primary values | `.B` 2 / `.W` 3 bytes, direct opcode register selection; no migration or compatibility change |
| Hypothetical immediate ALU under one new descriptor family | One primary plus descriptor space, instead of the existing 48 | `.B` 3 / `.W` 4 bytes; another descriptor decode and incompatible replacement of assigned bytes. Not proposed for adoption |
| Protected access under existing escape | Zero new primaries; 64-position secondary budget | Three bytes and a new selector namespace; preserves base GETX/SETX. A new primary escape could also be three bytes but would spend one primary unnecessarily at present |
| User access under a new primary | One primary and explicit descriptors | Three/five bytes; keeps common transfers compact and avoids consuming 64 existing secondary tags. Moving the same full descriptor behind another escape adds a byte unless the layout is redesigned |
| Compact branch/SP addressing | Sixteen primary positions for branches, one for SP; B1 reuses fields | Two-byte branches and three-byte short memory forms. Escaping a rel8 branch would make it three bytes, equal to current rel16 size; an extra SP escape byte would erase its size saving |
| Far call/return under dedicated primaries | Two primaries | Five-byte packed-target call and one-byte return. Existing-escape tags would save both primaries but make these forms six/two bytes and trigger the longer-instruction audit for the call. A three-byte target alternative must be reviewed separately |
| Assigned TST and TSTI | Twelve secondary tags total, zero new primaries | TST.B is two bytes; TSTI is three/four; TST.W aliases OR. Dedicated primaries are unnecessary for the measured sequences |
| TST.W as an assembler alias | Zero new space for the word form | Two-byte `OR rd,rd` already preserves the register and C/V/I/E; byte testing still needs distinct semantics |
| Immediate-count shifts/rotates under an existing escape | Forty secondary tags, zero primary values | Three-byte byte/word forms with a count byte; forty dedicated primary values would save one byte per form but reduce projected primary capacity from 144 to 104. Counts through 16 cannot use an unqualified four-bit count field |

The alternative of dedicated primaries for both test families plus a new control
escape would raise the additional primary charge from 17 to 34, leaving 127
positions outside budgeted blocks. Even that option fits without reclaiming
`$80–$AF`. The preferred costing scenario conserves primaries for rare controls
and tests while retaining density for immediate ALU, compact addressing, and
far control flow. TST/TSTI now use the reviewed twelve-tag assignment; the
remaining candidate allocations remain design recommendations.

Update this budget after each reviewed assignment. Reconsider immediate ALU
allocation only if a concrete remaining family cannot fit after considering
escape reuse, descriptor reuse, and the documented length tradeoffs. Audit
all matching values and reserved fields before publishing any selected layout;
selectors, exception numbers, and MMIO addresses have their own namespaces and
must not be counted as primary opcode consumption.

## Unassigned encoding proposals

The B1, rel8 and SP compact proposals have moved to the assigned layouts and
embedded payload. The six byte/word immediate ALU families are assigned.
No further compact opcode family is granted by this section. Post-increment and
checked register-to-pointer arithmetic remain unassigned. The latter was
[measured and deferred](ISA_EXAMPLES.md#checked-register-to-pointer-arithmetic-decision)
as described in Appendix B of ISA.md.
The unsigned B0-to-B1 study notation `ADDP.R` has no numeric encoding. Its two-byte
cost model needs sixteen tags in an existing escape (zero new primaries/tables);
a three-byte alternative needs one tag plus a descriptor with sixteen legal
source/destination combinations and reserved-bit validation. Neither alternative
selects or reserves a slot, adds DP, changes schema 5, or changes any assigned
encoding. The primary budget remains 111 assigned / 145 unassigned and `$1A`
remains 80 assigned / 176 unassigned tags.

Extended context/control access has moved to the assigned tables above. The
eight-bit third-byte index is a separate encoding, separate from the base
five-bit field. All four access classes preserve flags, reject illegal selectors
at decode, and keep their assigned numeric layouts separate from the proposals.

Immediate-count shifts/rotates have moved to the assigned tables above. All
forty numeric tags, count bounds, Invalid State classification, metadata and
operand validation are specified. Register counts and through-carry
rotates remain outside the assigned extension.

### Compact-form selection and exact semantics

Short branches have moved to the [assigned layouts](#assigned-short-branches)
and metadata; their numeric block and relaxation rules are no longer proposals.

B1 simm6 has moved to the [assigned layouts](#assigned-compact-b1-displacements)
and embedded metadata; it is no longer an unassigned proposal.

Compact SP has moved to the [assigned layouts](#assigned-compact-sp-offsets);
its 0..255 selection, fallback, canonicalization and precise faults are normative.

For assigned immediates, use the byte/word rules above and the normative
[conformance requirements](ISA.md#conformance-immediate-alu).
No signed-imm4 arithmetic family is assigned. The assembler must not remove a
constant setup unless register liveness and visible flag effects are proven;
no implicit constant/scratch register is consumed.

Assigned branch/B1 conformance covers every predicate, signed endpoint,
cascade, segment end, base/value alias and early long-form rejection. Compact SP
conformance includes 0/255/256, whole-slot allocation/protection and atomicity.
Unassigned forms remain illegal.

### Kernel context and control-access cost

A twenty-word spill/reload uses forty GETX/SETX transfers, 120 instruction bytes
before memory operations. This is a static size calculation, not a benchmark.
Use the [current guest/kernel measurements](ISA_EXAMPLES.md#current-workload-measurements)
for executed code and schedules. Canonical user-access selectors may be rematerialized
under the documented ABI. CPU controls use GETC/SETC; timer/PIC use ordinary MOV
to MMIO. No bulk context-transfer or alternate device-control opcode is assigned.

## Instruction-length audit

Assigned instructions currently occupy 1–5 bytes; that is not a permanent
architectural limit. No current assignment needs a longer form. Consumers reject unsupported
lengths. Before assigning or enabling a longer
form, update and validate every consumer below rather than only enlarging fetch.

| Consumer | Current implementation | Required before a longer form is supported |
|---|---|---|
| Embedded JSON | `instruction_bytes` is 1–5 for all represented assignments | Declare actual new lengths and audit all consumers |
| [generate_decode.py](../tools/generate_decode.py) | Five-element per-byte mask/expected/selector-validation arrays; uint8_t length; table count derived from metadata | Derive or enlarge validation capacity and validate length representability; the seven decode tables are independent of instruction-byte capacity |
| [cpu.cpp](../src/cpu.cpp) fetch | `std::array<uint8_t,5>` indexed by metadata length | Expand or derive fetch capacity, enforce agreement with validated metadata, and retain early rejection and checked segment ends |
| CPU operand extraction | Opcode-dependent bytes packed into uint32_t | Use layout-aware extraction or checked wider operands before exceeding four packed operand bytes; a larger fetch buffer alone is insufficient |
| [isa.py](../tools/isa.py) | Metadata-sized Python buffers; iterative branch relaxation and fixed CALL/CALLF/JS/JL sizing | Add syntax, formatting and accurate sizing/relaxation for each form; retain truncation and image-boundary checks |
| [encoding.py](../tools/encoding.py) | Validates schema/ISA identity, overlap, five-byte capacity, field/operand extents and early-byte checks | Expand supported limits and validate every new field/check before generating or consuming tables |
| Conformance | Existing lengths, truncations, segment ends and atomic faults are covered | Add exact new lengths, last-valid placement, each truncation position, early reserved-field priority and atomic fetch/next-PC failure |

A longer assignment must pass this audit together with metadata/tool/runtime
integration. No speculative buffer expansion or implicit allocation is authorized
by this audit. The [architectural conformance requirements](ISA.md#architectural-conformance-requirements)
continue to apply.

## Metadata format

All numeric values in JSON are decimal integers. Byte positions are zero-based
from the instruction's first byte; multibyte operands are little-endian.
`isa_revision="working"` identifies the sole ISA target and `schema_version=5`
identifies the metadata format. Consumers must validate both identifiers.
Every represented entry is available; there are no per-version availability fields.

Current implementation/profile and executed-coverage status belongs to
[Appendix C](ISA.md#implementation-status-and-remaining-work).

The payload includes SWI at `$26`, GETX xtrap at `$1F $34`, and read-only
context word xtrap at index `$14`, plus the TRAP alias. SWI has an unsigned
service byte and saves the following PC on exception entry. The
[worked caller/handler](ISA_EXAMPLES.md#software-trap-and-system-call-example)
illustrates the assigned instruction bytes.

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
| `bit_operands` | Packed numeric fields with name, byte, lsb, width in bits and signed interpretation; absent means none |
| `encoding_checks` | Reserved-bit mask/equality or `allowed_values` checks performed as soon as that byte is fetched; failure is Illegal Instruction. Checks apply unconditionally |
| `state_constraints` | Constraints on decoded operand values; apply after full instruction fetch with the stated fault kind |
| `destination` | A fixed register selected by the primary opcode, as with ADDP/ADJSP |
| `address` | Memory base class, segment, and whether stack-allocation checks apply |
| `transfer_bits` | Value width, independently of address/displacement width |
| `operation_bits` | ALU/test/shift/rotate operation width (8 or 16), independent of immediate/count storage |
| `flags_read`, `flags_written` | Flag inputs and updates; absent `flags_read` means none |
| `special_effects` | Conditional CSR effects or context restoration not expressible as ordinary calculated flags |
| `semantic` | Operation identifier for a consumer's execution or formatting implementation |

A field is extracted as `(bytes[field.byte] >> field.lsb) & ((1 << field.width) - 1)`. Its `bank` selects a named register array in `register_banks`. For example, the two-bit B1 selector zero means r4, not r0. GETX/SETX extract the low five bits of byte 1 as a context index; their match-value sets restrict the valid indices.

Operands are assembled from `width_bytes` starting at `byte`, then interpreted according to `signed`. This describes bits, not all assembly literal rules: for example, a MOV immediate can be written using a negative two's-complement literal even though the loaded value zero-extends. ADDP/ADJSP accept strictly signed literals, SWI accepts only unsigned 0–255, and SP-relative operands are unsigned.

For B1 memory forms, byte 2 is validated before optional disp16. Long forms
require zero upper six bits; short forms use those bits as a signed six-bit
displacement. MOVU independently requires its reserved upper base bits to be zero.
Packed operands use mask/shift extraction and sign extension and must not overlap
register fields. For example byte `$FA` gives pp=2 and displacement -2.
GETX/SETX register bytes require zero upper five bits. JL's reserved high operand
byte is instead an Invalid State check after full fetch.

Separate entries describe three-byte B1 simm6 and five-byte disp16 forms.
Assemblers select simm6 for -32..31. Raw long forms remain legal for those values;
disassembly selects canonical compact assembly. Entry IDs containing `direct`
identify the packed short form, including nonzero displacement.

The payload's `instruction_bytes.min` and `instruction_bytes.max` describe the
forms represented by that payload; the current maximum of five is not a
permanent architectural limit. Before supporting a longer assignment, update
the metadata range and audit consumer buffers, reserved-byte checks, operand
extraction, assembly sizing, and boundary/fault tests. The
[instruction-length audit](#instruction-length-audit)
records the current implementation assumptions.

## Register state and aliases

The register-bank arrays include B0–B3, all eight general registers, and the saved exception context. `context_writable_indices` excludes read-only cause/address/service metadata. `csr_masks` contains the defined and software-writable masks, with E derived from active context rather than an ordinary software-writable flag.

Aliases expand to real instructions before encoding. INCP/DECP accept r4–r7 or DP; they do not target SP. ADJSP is a separate checked stack operation with no transfer-size suffix. PULL, PUSH.W, POP.W, and PULL.W expand to the corresponding word PUSH/POP instruction as listed in JSON. General-register self-MOV expands to NOP. TRAP #imm8 expands to SWI #imm8 without changing its operand range or length. TST/TST.W use `same_b0_register_operands` to expand one B0 operand into both operands of OR. No alias may silently consume an accumulator temporary. Existing MOV suffix and operand-class rules remain defined by ISA.md.

`flags_written` lists ordinary updates; conditional CSR destinations use `special_effects`. For immediate shifts/rotates the count-zero special effect overrides N/Z/C and destination writes. Their state constraint `min`/`max` gives inclusive count bounds, checked after full fetch. IRET restores the saved image, pops one protected frame and derives E from remaining depth. SETX edits saved state and does not immediately update live flags. SWI likewise lists no ordinary calculated flags; its special effects perform exception entry, which preserves NZCV, clears I, and activates E. Its xpc is the following instruction, unlike the instruction-start PC saved for a fault.

SETX writes the complete saved xcsr word without the live CSR mask. The encoding is legal even when its stored value is temporarily invalid. A valid IRET restore requires `(xcsr & ~$002F) == 0`, including E=0; invalid values raise a nested fault when protected entry is valid, leaving the edited frame intact. This is semantic validation at return, not an illegal SETX encoding or a new table match condition.

The payload represents all 42 readable context words and twenty editable words.
Base GETX selects indices `$00–$14`; base SETX selects `$00–$10`.
Extended GETX selects `$20–$29` and `$30–$3A`; extended SETX selects `$20–$22`. xtrap is zero-extended
from the SWI service byte and reset to zero on every non-SWI entry. SWI #0 is
distinguished by cause 6. `$1F $54` remains illegal before reading a third byte.

## Validation and implementation use

Consumers must check all 256 values of each decode table for overlap, validate
selector/operand bounds and capacity, and reject reserved encodings. The
[allocation ledger](ISA.md#opcode-allocation-ledger) defines expected counts;
reserved fields reject as soon as their bytes are known.

The authoritative [implementation and evidence record](ISA.md#implementation-status-and-remaining-work)
identifies runtime/profile support, exact executed suites and software-scenario
limits. This document defines layouts and consumer requirements, not a competing
runtime-status inventory.

## Reading the embedded table

Read the JSON code block between the two marker lines; validate schema version 5
before decoding. For example, from the repository root:

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
  "schema_version": 5,
  "architecture": "virtualcpu",
  "isa_revision": "working",
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
    16,
    32,
    33,
    34
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
        },
        {
          "id": "callf",
          "mnemonic": "CALLF",
          "match": {
            "value": 84,
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
          "semantic": "call_far_absolute",
          "state_constraints": [
            {
              "operand": "target",
              "mask": 4278190080,
              "equals": 0,
              "fault": "invalid_state"
            }
          ]
        },
        {
          "id": "retf",
          "mnemonic": "RETF",
          "match": {
            "value": 85,
            "mask": 255
          },
          "length": 1,
          "fields": [],
          "operands": [],
          "flags_written": [],
          "semantic": "return_far"
        },
        {
          "id": "addi_8",
          "mnemonic": "ADDI",
          "match": {
            "value": 128,
            "mask": 252
          },
          "length": 2,
          "fields": [
            {
              "name": "destination",
              "byte": 0,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [
            {
              "name": "immediate",
              "byte": 1,
              "width_bytes": 1,
              "signed": false
            }
          ],
          "flags_written": [
            "N",
            "Z",
            "C",
            "V"
          ],
          "semantic": "alu_immediate",
          "operation_bits": 8
        },
        {
          "id": "addi_16",
          "mnemonic": "ADDI",
          "match": {
            "value": 132,
            "mask": 252
          },
          "length": 3,
          "fields": [
            {
              "name": "destination",
              "byte": 0,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [
            {
              "name": "immediate",
              "byte": 1,
              "width_bytes": 2,
              "signed": false
            }
          ],
          "flags_written": [
            "N",
            "Z",
            "C",
            "V"
          ],
          "semantic": "alu_immediate",
          "operation_bits": 16
        },
        {
          "id": "subi_8",
          "mnemonic": "SUBI",
          "match": {
            "value": 136,
            "mask": 252
          },
          "length": 2,
          "fields": [
            {
              "name": "destination",
              "byte": 0,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [
            {
              "name": "immediate",
              "byte": 1,
              "width_bytes": 1,
              "signed": false
            }
          ],
          "flags_written": [
            "N",
            "Z",
            "C",
            "V"
          ],
          "semantic": "alu_immediate",
          "operation_bits": 8
        },
        {
          "id": "subi_16",
          "mnemonic": "SUBI",
          "match": {
            "value": 140,
            "mask": 252
          },
          "length": 3,
          "fields": [
            {
              "name": "destination",
              "byte": 0,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [
            {
              "name": "immediate",
              "byte": 1,
              "width_bytes": 2,
              "signed": false
            }
          ],
          "flags_written": [
            "N",
            "Z",
            "C",
            "V"
          ],
          "semantic": "alu_immediate",
          "operation_bits": 16
        },
        {
          "id": "cmpi_8",
          "mnemonic": "CMPI",
          "match": {
            "value": 144,
            "mask": 252
          },
          "length": 2,
          "fields": [
            {
              "name": "destination",
              "byte": 0,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [
            {
              "name": "immediate",
              "byte": 1,
              "width_bytes": 1,
              "signed": false
            }
          ],
          "flags_written": [
            "N",
            "Z",
            "C",
            "V"
          ],
          "semantic": "alu_immediate",
          "operation_bits": 8
        },
        {
          "id": "cmpi_16",
          "mnemonic": "CMPI",
          "match": {
            "value": 148,
            "mask": 252
          },
          "length": 3,
          "fields": [
            {
              "name": "destination",
              "byte": 0,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [
            {
              "name": "immediate",
              "byte": 1,
              "width_bytes": 2,
              "signed": false
            }
          ],
          "flags_written": [
            "N",
            "Z",
            "C",
            "V"
          ],
          "semantic": "alu_immediate",
          "operation_bits": 16
        },
        {
          "id": "andi_8",
          "mnemonic": "ANDI",
          "match": {
            "value": 152,
            "mask": 252
          },
          "length": 2,
          "fields": [
            {
              "name": "destination",
              "byte": 0,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [
            {
              "name": "immediate",
              "byte": 1,
              "width_bytes": 1,
              "signed": false
            }
          ],
          "flags_written": [
            "N",
            "Z"
          ],
          "semantic": "alu_immediate",
          "operation_bits": 8
        },
        {
          "id": "andi_16",
          "mnemonic": "ANDI",
          "match": {
            "value": 156,
            "mask": 252
          },
          "length": 3,
          "fields": [
            {
              "name": "destination",
              "byte": 0,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [
            {
              "name": "immediate",
              "byte": 1,
              "width_bytes": 2,
              "signed": false
            }
          ],
          "flags_written": [
            "N",
            "Z"
          ],
          "semantic": "alu_immediate",
          "operation_bits": 16
        },
        {
          "id": "ori_8",
          "mnemonic": "ORI",
          "match": {
            "value": 160,
            "mask": 252
          },
          "length": 2,
          "fields": [
            {
              "name": "destination",
              "byte": 0,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [
            {
              "name": "immediate",
              "byte": 1,
              "width_bytes": 1,
              "signed": false
            }
          ],
          "flags_written": [
            "N",
            "Z"
          ],
          "semantic": "alu_immediate",
          "operation_bits": 8
        },
        {
          "id": "ori_16",
          "mnemonic": "ORI",
          "match": {
            "value": 164,
            "mask": 252
          },
          "length": 3,
          "fields": [
            {
              "name": "destination",
              "byte": 0,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [
            {
              "name": "immediate",
              "byte": 1,
              "width_bytes": 2,
              "signed": false
            }
          ],
          "flags_written": [
            "N",
            "Z"
          ],
          "semantic": "alu_immediate",
          "operation_bits": 16
        },
        {
          "id": "xori_8",
          "mnemonic": "XORI",
          "match": {
            "value": 168,
            "mask": 252
          },
          "length": 2,
          "fields": [
            {
              "name": "destination",
              "byte": 0,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [
            {
              "name": "immediate",
              "byte": 1,
              "width_bytes": 1,
              "signed": false
            }
          ],
          "flags_written": [
            "N",
            "Z"
          ],
          "semantic": "alu_immediate",
          "operation_bits": 8
        },
        {
          "id": "xori_16",
          "mnemonic": "XORI",
          "match": {
            "value": 172,
            "mask": 252
          },
          "length": 3,
          "fields": [
            {
              "name": "destination",
              "byte": 0,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [
            {
              "name": "immediate",
              "byte": 1,
              "width_bytes": 2,
              "signed": false
            }
          ],
          "flags_written": [
            "N",
            "Z"
          ],
          "semantic": "alu_immediate",
          "operation_bits": 16
        },
        {
          "id": "movu_escape",
          "match": {
            "value": 39,
            "mask": 255
          },
          "dispatch": "ext27"
        },
        {
          "id": "beq_short",
          "mnemonic": "BEQ",
          "match": {
            "value": 96,
            "mask": 255
          },
          "length": 2,
          "fields": [],
          "operands": [
            {
              "name": "displacement",
              "byte": 1,
              "width_bytes": 1,
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
          "id": "bne_short",
          "mnemonic": "BNE",
          "match": {
            "value": 97,
            "mask": 255
          },
          "length": 2,
          "fields": [],
          "operands": [
            {
              "name": "displacement",
              "byte": 1,
              "width_bytes": 1,
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
          "id": "bcs_short",
          "mnemonic": "BCS",
          "match": {
            "value": 98,
            "mask": 255
          },
          "length": 2,
          "fields": [],
          "operands": [
            {
              "name": "displacement",
              "byte": 1,
              "width_bytes": 1,
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
          "id": "bcc_short",
          "mnemonic": "BCC",
          "match": {
            "value": 99,
            "mask": 255
          },
          "length": 2,
          "fields": [],
          "operands": [
            {
              "name": "displacement",
              "byte": 1,
              "width_bytes": 1,
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
          "id": "bmi_short",
          "mnemonic": "BMI",
          "match": {
            "value": 100,
            "mask": 255
          },
          "length": 2,
          "fields": [],
          "operands": [
            {
              "name": "displacement",
              "byte": 1,
              "width_bytes": 1,
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
          "id": "bpl_short",
          "mnemonic": "BPL",
          "match": {
            "value": 101,
            "mask": 255
          },
          "length": 2,
          "fields": [],
          "operands": [
            {
              "name": "displacement",
              "byte": 1,
              "width_bytes": 1,
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
          "id": "bvs_short",
          "mnemonic": "BVS",
          "match": {
            "value": 102,
            "mask": 255
          },
          "length": 2,
          "fields": [],
          "operands": [
            {
              "name": "displacement",
              "byte": 1,
              "width_bytes": 1,
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
          "id": "bvc_short",
          "mnemonic": "BVC",
          "match": {
            "value": 103,
            "mask": 255
          },
          "length": 2,
          "fields": [],
          "operands": [
            {
              "name": "displacement",
              "byte": 1,
              "width_bytes": 1,
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
          "id": "bhi_short",
          "mnemonic": "BHI",
          "match": {
            "value": 104,
            "mask": 255
          },
          "length": 2,
          "fields": [],
          "operands": [
            {
              "name": "displacement",
              "byte": 1,
              "width_bytes": 1,
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
          "id": "bls_short",
          "mnemonic": "BLS",
          "match": {
            "value": 105,
            "mask": 255
          },
          "length": 2,
          "fields": [],
          "operands": [
            {
              "name": "displacement",
              "byte": 1,
              "width_bytes": 1,
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
          "id": "bge_short",
          "mnemonic": "BGE",
          "match": {
            "value": 106,
            "mask": 255
          },
          "length": 2,
          "fields": [],
          "operands": [
            {
              "name": "displacement",
              "byte": 1,
              "width_bytes": 1,
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
          "id": "blt_short",
          "mnemonic": "BLT",
          "match": {
            "value": 107,
            "mask": 255
          },
          "length": 2,
          "fields": [],
          "operands": [
            {
              "name": "displacement",
              "byte": 1,
              "width_bytes": 1,
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
          "id": "bgt_short",
          "mnemonic": "BGT",
          "match": {
            "value": 108,
            "mask": 255
          },
          "length": 2,
          "fields": [],
          "operands": [
            {
              "name": "displacement",
              "byte": 1,
              "width_bytes": 1,
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
          "id": "ble_short",
          "mnemonic": "BLE",
          "match": {
            "value": 109,
            "mask": 255
          },
          "length": 2,
          "fields": [],
          "operands": [
            {
              "name": "displacement",
              "byte": 1,
              "width_bytes": 1,
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
          "id": "bra_short",
          "mnemonic": "BRA",
          "match": {
            "value": 110,
            "mask": 255
          },
          "length": 2,
          "fields": [],
          "operands": [
            {
              "name": "displacement",
              "byte": 1,
              "width_bytes": 1,
              "signed": true,
              "byte_order": "little"
            }
          ],
          "flags_written": [],
          "flags_read": [],
          "semantic": "branch_ra"
        },
        {
          "id": "compact_sp_escape",
          "match": {
            "value": 40,
            "mask": 255
          },
          "dispatch": "ext28"
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
          "semantic": "load_memory_zero_extend",
          "bit_operands": [
            {
              "name": "displacement",
              "byte": 2,
              "lsb": 2,
              "width": 6,
              "signed": true
            }
          ]
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
          "semantic": "load_memory_zero_extend",
          "bit_operands": [
            {
              "name": "displacement",
              "byte": 2,
              "lsb": 2,
              "width": 6,
              "signed": true
            }
          ]
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
          "semantic": "store_memory",
          "bit_operands": [
            {
              "name": "displacement",
              "byte": 2,
              "lsb": 2,
              "width": 6,
              "signed": true
            }
          ]
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
          "semantic": "store_memory",
          "bit_operands": [
            {
              "name": "displacement",
              "byte": 2,
              "lsb": 2,
              "width": 6,
              "signed": true
            }
          ]
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
          "semantic": "load_memory_zero_extend",
          "bit_operands": [
            {
              "name": "displacement",
              "byte": 2,
              "lsb": 2,
              "width": 6,
              "signed": true
            }
          ]
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
          "semantic": "load_memory_zero_extend",
          "bit_operands": [
            {
              "name": "displacement",
              "byte": 2,
              "lsb": 2,
              "width": 6,
              "signed": true
            }
          ]
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
          "semantic": "store_memory",
          "bit_operands": [
            {
              "name": "displacement",
              "byte": 2,
              "lsb": 2,
              "width": 6,
              "signed": true
            }
          ]
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
          "semantic": "store_memory",
          "bit_operands": [
            {
              "name": "displacement",
              "byte": 2,
              "lsb": 2,
              "width": 6,
              "signed": true
            }
          ]
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
        },
        {
          "id": "tst_8",
          "mnemonic": "TST",
          "match": {
            "value": 64,
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
            "Z"
          ],
          "semantic": "test_register",
          "operation_bits": 8
        },
        {
          "id": "tsti_8",
          "mnemonic": "TSTI",
          "match": {
            "value": 68,
            "mask": 252
          },
          "length": 3,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [
            {
              "name": "immediate",
              "byte": 2,
              "width_bytes": 1,
              "signed": false
            }
          ],
          "flags_written": [
            "N",
            "Z"
          ],
          "semantic": "test_immediate",
          "operation_bits": 8
        },
        {
          "id": "tsti_16",
          "mnemonic": "TSTI",
          "match": {
            "value": 72,
            "mask": 252
          },
          "length": 4,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [
            {
              "name": "immediate",
              "byte": 2,
              "width_bytes": 2,
              "signed": false
            }
          ],
          "flags_written": [
            "N",
            "Z"
          ],
          "semantic": "test_immediate",
          "operation_bits": 16
        },
        {
          "id": "shli_8",
          "mnemonic": "SHLI",
          "match": {
            "value": 96,
            "mask": 252
          },
          "length": 3,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [
            {
              "name": "count",
              "byte": 2,
              "width_bytes": 1,
              "signed": false
            }
          ],
          "flags_written": [
            "N",
            "Z",
            "C"
          ],
          "special_effects": [
            "count_zero_preserves_entire_register_and_all_flags"
          ],
          "semantic": "shift_rotate_immediate",
          "operation_bits": 8,
          "state_constraints": [
            {
              "operand": "count",
              "min": 0,
              "max": 16,
              "fault": "invalid_state"
            }
          ]
        },
        {
          "id": "shli_16",
          "mnemonic": "SHLI",
          "match": {
            "value": 100,
            "mask": 252
          },
          "length": 3,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [
            {
              "name": "count",
              "byte": 2,
              "width_bytes": 1,
              "signed": false
            }
          ],
          "flags_written": [
            "N",
            "Z",
            "C"
          ],
          "special_effects": [
            "count_zero_preserves_entire_register_and_all_flags"
          ],
          "semantic": "shift_rotate_immediate",
          "operation_bits": 16,
          "state_constraints": [
            {
              "operand": "count",
              "min": 0,
              "max": 16,
              "fault": "invalid_state"
            }
          ]
        },
        {
          "id": "shri_8",
          "mnemonic": "SHRI",
          "match": {
            "value": 104,
            "mask": 252
          },
          "length": 3,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [
            {
              "name": "count",
              "byte": 2,
              "width_bytes": 1,
              "signed": false
            }
          ],
          "flags_written": [
            "N",
            "Z",
            "C"
          ],
          "special_effects": [
            "count_zero_preserves_entire_register_and_all_flags"
          ],
          "semantic": "shift_rotate_immediate",
          "operation_bits": 8,
          "state_constraints": [
            {
              "operand": "count",
              "min": 0,
              "max": 16,
              "fault": "invalid_state"
            }
          ]
        },
        {
          "id": "shri_16",
          "mnemonic": "SHRI",
          "match": {
            "value": 108,
            "mask": 252
          },
          "length": 3,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [
            {
              "name": "count",
              "byte": 2,
              "width_bytes": 1,
              "signed": false
            }
          ],
          "flags_written": [
            "N",
            "Z",
            "C"
          ],
          "special_effects": [
            "count_zero_preserves_entire_register_and_all_flags"
          ],
          "semantic": "shift_rotate_immediate",
          "operation_bits": 16,
          "state_constraints": [
            {
              "operand": "count",
              "min": 0,
              "max": 16,
              "fault": "invalid_state"
            }
          ]
        },
        {
          "id": "sari_8",
          "mnemonic": "SARI",
          "match": {
            "value": 112,
            "mask": 252
          },
          "length": 3,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [
            {
              "name": "count",
              "byte": 2,
              "width_bytes": 1,
              "signed": false
            }
          ],
          "flags_written": [
            "N",
            "Z",
            "C"
          ],
          "special_effects": [
            "count_zero_preserves_entire_register_and_all_flags"
          ],
          "semantic": "shift_rotate_immediate",
          "operation_bits": 8,
          "state_constraints": [
            {
              "operand": "count",
              "min": 0,
              "max": 16,
              "fault": "invalid_state"
            }
          ]
        },
        {
          "id": "sari_16",
          "mnemonic": "SARI",
          "match": {
            "value": 116,
            "mask": 252
          },
          "length": 3,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [
            {
              "name": "count",
              "byte": 2,
              "width_bytes": 1,
              "signed": false
            }
          ],
          "flags_written": [
            "N",
            "Z",
            "C"
          ],
          "special_effects": [
            "count_zero_preserves_entire_register_and_all_flags"
          ],
          "semantic": "shift_rotate_immediate",
          "operation_bits": 16,
          "state_constraints": [
            {
              "operand": "count",
              "min": 0,
              "max": 16,
              "fault": "invalid_state"
            }
          ]
        },
        {
          "id": "roli_8",
          "mnemonic": "ROLI",
          "match": {
            "value": 120,
            "mask": 252
          },
          "length": 3,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [
            {
              "name": "count",
              "byte": 2,
              "width_bytes": 1,
              "signed": false
            }
          ],
          "flags_written": [
            "N",
            "Z",
            "C"
          ],
          "special_effects": [
            "count_zero_preserves_entire_register_and_all_flags"
          ],
          "semantic": "shift_rotate_immediate",
          "operation_bits": 8,
          "state_constraints": [
            {
              "operand": "count",
              "min": 0,
              "max": 16,
              "fault": "invalid_state"
            }
          ]
        },
        {
          "id": "roli_16",
          "mnemonic": "ROLI",
          "match": {
            "value": 124,
            "mask": 252
          },
          "length": 3,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [
            {
              "name": "count",
              "byte": 2,
              "width_bytes": 1,
              "signed": false
            }
          ],
          "flags_written": [
            "N",
            "Z",
            "C"
          ],
          "special_effects": [
            "count_zero_preserves_entire_register_and_all_flags"
          ],
          "semantic": "shift_rotate_immediate",
          "operation_bits": 16,
          "state_constraints": [
            {
              "operand": "count",
              "min": 0,
              "max": 16,
              "fault": "invalid_state"
            }
          ]
        },
        {
          "id": "rori_8",
          "mnemonic": "RORI",
          "match": {
            "value": 128,
            "mask": 252
          },
          "length": 3,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [
            {
              "name": "count",
              "byte": 2,
              "width_bytes": 1,
              "signed": false
            }
          ],
          "flags_written": [
            "N",
            "Z",
            "C"
          ],
          "special_effects": [
            "count_zero_preserves_entire_register_and_all_flags"
          ],
          "semantic": "shift_rotate_immediate",
          "operation_bits": 8,
          "state_constraints": [
            {
              "operand": "count",
              "min": 0,
              "max": 16,
              "fault": "invalid_state"
            }
          ]
        },
        {
          "id": "rori_16",
          "mnemonic": "RORI",
          "match": {
            "value": 132,
            "mask": 252
          },
          "length": 3,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 0,
              "width": 2,
              "bank": "B0"
            }
          ],
          "operands": [
            {
              "name": "count",
              "byte": 2,
              "width_bytes": 1,
              "signed": false
            }
          ],
          "flags_written": [
            "N",
            "Z",
            "C"
          ],
          "special_effects": [
            "count_zero_preserves_entire_register_and_all_flags"
          ],
          "semantic": "shift_rotate_immediate",
          "operation_bits": 16,
          "state_constraints": [
            {
              "operand": "count",
              "min": 0,
              "max": 16,
              "fault": "invalid_state"
            }
          ]
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
        },
        {
          "id": "getx_extended",
          "mnemonic": "GETX",
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
            }
          ],
          "operands": [
            {
              "name": "selector",
              "byte": 2,
              "width_bytes": 1,
              "signed": false
            }
          ],
          "encoding_checks": [
            {
              "byte": 2,
              "allowed_values": [
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
                48,
                49,
                50,
                51,
                52,
                53,
                54,
                55,
                56,
                57,
                58
              ]
            }
          ],
          "flags_written": [],
          "semantic": "getx_extended"
        },
        {
          "id": "setx_extended",
          "mnemonic": "SETX",
          "match": {
            "value": 144,
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
            }
          ],
          "operands": [
            {
              "name": "selector",
              "byte": 2,
              "width_bytes": 1,
              "signed": false
            }
          ],
          "encoding_checks": [
            {
              "byte": 2,
              "allowed_values": [
                32,
                33,
                34
              ]
            }
          ],
          "flags_written": [],
          "semantic": "setx_extended"
        },
        {
          "id": "getc",
          "mnemonic": "GETC",
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
            }
          ],
          "operands": [
            {
              "name": "selector",
              "byte": 2,
              "width_bytes": 1,
              "signed": false
            }
          ],
          "encoding_checks": [
            {
              "byte": 2,
              "allowed_values": [
                0,
                1,
                2,
                3,
                4,
                5,
                16,
                17,
                18,
                19,
                20,
                24,
                25,
                26,
                27,
                28,
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
                48,
                49,
                50,
                51,
                52,
                53,
                54,
                64,
                65,
                66,
                67,
                68,
                69
              ]
            }
          ],
          "flags_written": [],
          "semantic": "getc"
        },
        {
          "id": "setc",
          "mnemonic": "SETC",
          "match": {
            "value": 176,
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
            }
          ],
          "operands": [
            {
              "name": "selector",
              "byte": 2,
              "width_bytes": 1,
              "signed": false
            }
          ],
          "encoding_checks": [
            {
              "byte": 2,
              "allowed_values": [
                0,
                1,
                2,
                16,
                17,
                18,
                19,
                20,
                32,
                33,
                34,
                35,
                36,
                37,
                38,
                39,
                40,
                64,
                65,
                127
              ]
            }
          ],
          "flags_written": [],
          "semantic": "setc"
        }
      ]
    },
    "ext27": {
      "inspect_byte": 1,
      "entries": [
        {
          "id": "movu_direct_load_8",
          "mnemonic": "MOVU",
          "match": {
            "value": 32,
            "mask": 227
          },
          "length": 3,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 2,
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
          "encoding_checks": [
            {
              "byte": 2,
              "mask": 252,
              "equals": 0
            }
          ],
          "flags_written": [],
          "transfer_bits": 8,
          "semantic": "load_user",
          "address": {
            "base": "B1",
            "segment": "uaccess"
          }
        },
        {
          "id": "movu_direct_load_16",
          "mnemonic": "MOVU",
          "match": {
            "value": 0,
            "mask": 227
          },
          "length": 3,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 2,
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
          "encoding_checks": [
            {
              "byte": 2,
              "mask": 252,
              "equals": 0
            }
          ],
          "flags_written": [],
          "transfer_bits": 16,
          "semantic": "load_user",
          "address": {
            "base": "B1",
            "segment": "uaccess"
          }
        },
        {
          "id": "movu_disp16_load_8",
          "mnemonic": "MOVU",
          "match": {
            "value": 96,
            "mask": 227
          },
          "length": 5,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 2,
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
              "signed": true
            }
          ],
          "encoding_checks": [
            {
              "byte": 2,
              "mask": 252,
              "equals": 0
            }
          ],
          "flags_written": [],
          "transfer_bits": 8,
          "semantic": "load_user",
          "address": {
            "base": "B1",
            "segment": "uaccess"
          }
        },
        {
          "id": "movu_disp16_load_16",
          "mnemonic": "MOVU",
          "match": {
            "value": 64,
            "mask": 227
          },
          "length": 5,
          "fields": [
            {
              "name": "destination",
              "byte": 1,
              "lsb": 2,
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
              "signed": true
            }
          ],
          "encoding_checks": [
            {
              "byte": 2,
              "mask": 252,
              "equals": 0
            }
          ],
          "flags_written": [],
          "transfer_bits": 16,
          "semantic": "load_user",
          "address": {
            "base": "B1",
            "segment": "uaccess"
          }
        },
        {
          "id": "movu_direct_store_8",
          "mnemonic": "MOVU",
          "match": {
            "value": 160,
            "mask": 227
          },
          "length": 3,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 2,
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
          "encoding_checks": [
            {
              "byte": 2,
              "mask": 252,
              "equals": 0
            }
          ],
          "flags_written": [],
          "transfer_bits": 8,
          "semantic": "store_user",
          "address": {
            "base": "B1",
            "segment": "uaccess"
          }
        },
        {
          "id": "movu_direct_store_16",
          "mnemonic": "MOVU",
          "match": {
            "value": 128,
            "mask": 227
          },
          "length": 3,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 2,
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
          "encoding_checks": [
            {
              "byte": 2,
              "mask": 252,
              "equals": 0
            }
          ],
          "flags_written": [],
          "transfer_bits": 16,
          "semantic": "store_user",
          "address": {
            "base": "B1",
            "segment": "uaccess"
          }
        },
        {
          "id": "movu_disp16_store_8",
          "mnemonic": "MOVU",
          "match": {
            "value": 224,
            "mask": 227
          },
          "length": 5,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 2,
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
              "signed": true
            }
          ],
          "encoding_checks": [
            {
              "byte": 2,
              "mask": 252,
              "equals": 0
            }
          ],
          "flags_written": [],
          "transfer_bits": 8,
          "semantic": "store_user",
          "address": {
            "base": "B1",
            "segment": "uaccess"
          }
        },
        {
          "id": "movu_disp16_store_16",
          "mnemonic": "MOVU",
          "match": {
            "value": 192,
            "mask": 227
          },
          "length": 5,
          "fields": [
            {
              "name": "source",
              "byte": 1,
              "lsb": 2,
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
              "signed": true
            }
          ],
          "encoding_checks": [
            {
              "byte": 2,
              "mask": 252,
              "equals": 0
            }
          ],
          "flags_written": [],
          "transfer_bits": 16,
          "semantic": "store_user",
          "address": {
            "base": "B1",
            "segment": "uaccess"
          }
        }
      ]
    },
    "ext28": {
      "inspect_byte": 1,
      "entries": [
        {
          "id": "mov_sp8_load_8",
          "mnemonic": "MOV",
          "match": {
            "value": 56,
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
              "name": "offset",
              "byte": 2,
              "width_bytes": 1,
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
          "id": "mov_sp8_store_8",
          "mnemonic": "MOV",
          "match": {
            "value": 120,
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
            }
          ],
          "operands": [
            {
              "name": "offset",
              "byte": 2,
              "width_bytes": 1,
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
          "id": "mov_sp8_load_16",
          "mnemonic": "MOV",
          "match": {
            "value": 48,
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
              "name": "offset",
              "byte": 2,
              "width_bytes": 1,
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
          "id": "mov_sp8_store_16",
          "mnemonic": "MOV",
          "match": {
            "value": 112,
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
            }
          ],
          "operands": [
            {
              "name": "offset",
              "byte": 2,
              "width_bytes": 1,
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
    },
    {
      "mnemonic": "TST",
      "expands_to": "OR",
      "same_b0_register_operands": true
    },
    {
      "mnemonic": "TST.W",
      "expands_to": "OR",
      "same_b0_register_operands": true
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
  "format_reference": "ISA_ENCODING.md",
  "control_indices": {
    "process_map": 0,
    "uaccess_map": 1,
    "uaccess_segment": 2,
    "mode": 3,
    "frame_depth": 4,
    "entry_configured": 5,
    "entry_ds": 16,
    "entry_ss": 17,
    "entry_sb": 18,
    "entry_st": 19,
    "entry_sp": 20,
    "active_entry_ds": 24,
    "active_entry_ss": 25,
    "active_entry_sb": 26,
    "active_entry_st": 27,
    "active_entry_sp": 28,
    "desc_map": 32,
    "desc_segment": 33,
    "desc_frame": 34,
    "desc_flags": 35,
    "desc_lower": 36,
    "desc_upper_lo": 37,
    "desc_upper_hi": 38,
    "desc_sb": 39,
    "desc_st": 40,
    "map_published": 41,
    "active_desc_frame": 48,
    "active_desc_flags": 49,
    "active_desc_lower": 50,
    "active_desc_upper_lo": 51,
    "active_desc_upper_hi": 52,
    "active_desc_sb": 53,
    "active_desc_st": 54,
    "backing_addr_lo": 64,
    "backing_addr_hi": 65,
    "backing_kind": 66,
    "backing_run_lo": 67,
    "backing_run_hi": 68,
    "backing_valid": 69,
    "command": 127
  },
  "control_writable_indices": [
    0,
    1,
    2,
    16,
    17,
    18,
    19,
    20,
    32,
    33,
    34,
    35,
    36,
    37,
    38,
    39,
    40,
    64,
    65,
    127
  ],
  "context_indices": {
    "xr0": 0,
    "xr1": 1,
    "xr2": 2,
    "xr3": 3,
    "xr4": 4,
    "xr5": 5,
    "xr6": 6,
    "xr7": 7,
    "xpc": 8,
    "xcs": 9,
    "xcsr": 10,
    "xsp": 11,
    "xsb": 12,
    "xst": 13,
    "xss": 14,
    "xdp": 15,
    "xds": 16,
    "xcause": 17,
    "xaddrlo": 18,
    "xaddrhi": 19,
    "xtrap": 20,
    "xprocess_map": 32,
    "xuaccess_map": 33,
    "xuaccess_segment": 34,
    "xorigin_mode": 35,
    "xreturn_mode": 36,
    "xprior_depth": 37,
    "xframe_depth": 38,
    "xbootstrap": 39,
    "xirq_source": 40,
    "xpriv_class": 41,
    "xmem_origin": 48,
    "xmem_phase": 49,
    "xaccess": 50,
    "xmapid": 51,
    "xsegment": 52,
    "xoffsetlo": 53,
    "xoffsethi": 54,
    "xvaddrlo": 55,
    "xvaddrhi": 56,
    "xwidth": 57,
    "xreason": 58
  }
}
```

<!-- ISA_ENCODING_JSON_END -->

### Console profile and instructions

The [console-v1 device/replay contract](ISA.md#console-machine-profile-and-recorded-input)
uses existing aligned word MOV, MOVU, context and control encodings. Its console
registers are machine-profile assignments; replay version 2 is a host-event format,
not a new ISA revision or metadata schema. All seven decode tables and their assigned
bytes remain unchanged. Default minimal-v1 MMIO rejection and version-1 replay
headers retain their existing semantics. The [blocking terminal](ISA_EXAMPLES.md#executable-blocking-console-terminal)
provides a software integration workload for later size measurements.

### Compiler workload measurements

[SmallC and the executed corpus](ISA_EXAMPLES.md#smallc-compiler-and-executable-workloads)
produce assigned assembly through `tools/isa.py`. Per-instruction classifications
come from the current encoding metadata after assembly; no compiler opcode table
or private encodings are introduced. The current workload report records B1/SP displacements, rel8/rel16 distances,
retirements, stack use and residual compact opportunities.
