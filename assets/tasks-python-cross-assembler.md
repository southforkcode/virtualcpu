# Implementation Plan: Python Cross-Assembler Toolchain

Source: user-provided Python Cross-Assembler Design, with the requirement that the implementation be well documented and commented for direct owner maintenance.
Generated: 2026-10-17
Standards: PRIN-1 User-First; PRIN-4 Maintainability; PRIN-5 Incremental Delivery. No local standards manifest found.
Status: high-level planning; implementation and detailed sub-tasks have not started.

## Scope and approach

Deliver one Python package with `vasm`, `vlink`, `vimage`, and `vdis`, supporting the complete assigned working ISA, separate assembly modules, deterministic linking and images, and the OS facilities in the supplied design. Keep dependencies minimal: standard library at runtime and pytest for new tests. Dynamic linking and link-time optimization remain deferred; optional privilege and macro-contract analysis are lint facilities.

Evolve the existing tools within this repository rather than introduce a disconnected toolchain. All milestones must leave existing compiler, fixture-generation, emulator, and replay workflows usable. Runtime execution semantics remain authoritative in `doc/ISA.md`; instruction layouts come only from the embedded JSON in `doc/ISA_ENCODING.md`.

Documentation and comments are deliverables in every milestone, not end-of-project cleanup. Explain why architectural restrictions exist, what each stage consumes and produces, and how an owner changes the implementation safely. Favor straightforward Python, explicit types, small modules, and named invariants over clever abstractions.

## Repository findings

| Existing component | Planning consequence |
| --- | --- |
| `doc/ISA_ENCODING.md` contains the complete schema-5 JSON between documented markers. | The design's missing-payload dependency is already satisfied. Reuse it; do not create a second encoding database. |
| `tools/encoding.py` validates metadata and generates lookup tables; `tools/generate_decode.py` generates C++ decoder tables. | Extract or adapt this shared foundation with compatibility imports and installed-package resource handling. |
| `tools/isa.py` already assembles/disassembles the working ISA and jointly relaxes branches/CALLR with numeric-target pinning. | Preserve published bytes and reuse these algorithms and regression cases as the migration baseline. General fragment layout requires additional reasoning. |
| The current parser lowercases labels and operands. | Case-sensitive symbols are an intentional language change; specify migration behavior and update affected tests explicitly. |
| SmallC, fixture generators, and measurement tools import `assemble`, `parse`, or `decode` from `tools/isa.py`. | Keep documented adapters until consumers migrate; row shapes and symbol extraction are compatibility concerns. |
| `CMakeLists.txt` runs Python unittest scripts and C++ conformance tests through CTest. | Add new pytest coverage without dropping existing execution checks or silently adding a dependency to the old build path. |
| README currently requires Python 3.10+. | Scope Python 3.12+ to the new package initially and document interpreter selection; raising the whole project's baseline needs an explicit migration decision. |
| Kernel, console, bulk-transfer, and arithmetic guest examples already run in the emulator. | Reuse these as behavioral baselines and migrate a small kernel to separately linked modules for OS acceptance. |
| The working tree already contains extensive tracked and untracked work. | Plan against the current files and preserve all existing changes; branch creation belongs to implementation kickoff. |

## Specifications to settle first

The first milestone produces normative specifications before formats become compatibility commitments. Examples must be labeled illustrative and tested when presented as executable.

- **Language:** choose `.` as canonical current location; define `$$` and whether legacy standalone `$` exists. Specify signed immediate/data ranges, literal ambiguities, string encodings, multiline statements, symbol scope, macro token comparison, division/modulo, and stable conditional evaluation. Clarify `.offset`, structure packing, and absolute-section syntax rather than infer behavior from examples.
- **Address domains:** define absolute integers, section-relative values, virtual addresses including address-space/map identity, and physical addresses. Specify legal arithmetic, near-target compatibility, `ADDR`/`SEG`/`OFF`, image-stage `PHYS`, and unresolved expressions.
- **Objects and executables:** specify VOF/1, the linked executable container, binary bounds, explicit addends, expression serialization, stable fragment anchors, relaxation candidates, source provenance, version/hash compatibility, and malformed-input rejection. Resolve how `.quad`, `.long`, symbol differences, and extraction functions are represented when link-dependent; the proposed relocation list alone does not cover every expression.
- **Link layout:** define the supported script grammar, separate VMA/LMA mappings, memory ownership, orphan sections, archive ordering, weak symbols, GC roots/KEEP, link assertions, and all CLI defaults. Specify that `--no-relax` chooses valid long automatic forms while honoring forced widths.
- **Relaxation:** extend numeric-target pinning to multiple sections, alignment, forward `.org`, placement, and link-dependent memory displacements. Shrinking fragments does not guarantee all distances decrease. Require a documented termination argument, deterministic fallback, and final validation.
- **OS/image ABIs:** fix FIXUP/1, vector ownership/merging, missing-vector policy, descriptor/context defaults, executable loading, and manifest/replay artifact schemas. `.addr24`/FAR32 emits four bytes with a zero high byte; `.u24`/ABS24 emits three. DESC/1 absence zeros the seven descriptor payload words while retaining valid map/segment selectors; CTX/1 emits the twenty writable fields in normative order.
- **Reproducibility:** define encoding-hash canonicalization, file/path remapping, dependency hashes, ordering rules, archive serialization, and deterministic diagnostics/debug data. Preserve compatibility or version any change to existing raw-payload hash consumers.

## Relevant files and planned destinations

These are expected ownership boundaries, not a requirement to create every proposed module before it has behavior.

| Files | Responsibility |
| --- | --- |
| `pyproject.toml`, `vcpu/isa/`, `tools/encoding.py`, `tools/isa.py`, `tools/generate_decode.py` | Package, shared ISA model/resources, and compatibility interfaces. |
| `vcpu/asm/`, `vcpu/obj/`, `vcpu/link/`, `vcpu/image/`, `vcpu/disasm/`, `vcpu/cli/` | Staged assembler, formats, linker, image builder, disassembler, and entry points. |
| `include/*.inc`, `examples/linker/`, new multi-module examples | Architectural definitions, explicit macros, and runnable demonstrations. |
| `doc/ASM_LANGUAGE.md`, `doc/ASM_OBJECT_FORMAT.md`, `doc/ASM_LINKER.md`, `doc/ASM_IMAGE_FORMAT.md`, `doc/ASM_CONFORMANCE.md` | Versioned normative language and format contracts. |
| `doc/ASM_ARCHITECTURE.md`, `doc/ASM_MAINTENANCE.md`, `README.md` | Pipeline/ownership guide, owner change recipes, setup, and commands. |
| New `tests/assembler/` coverage; existing `tests/test_toolchain.py`, `tests/test_branches.py`, `tests/test_sp_offsets.py`, compiler and C++ tests | New features plus compatibility and execution regressions. |
| `CMakeLists.txt`, `tools/smallc.py`, `tools/generate_*`, measurement tools | Build dependencies and incremental consumer migration. |

## Documentation and commenting acceptance

Every milestone is incomplete until its documentation matches its behavior. Public APIs need docstrings covering inputs, outputs, invariants, failure behavior, and address units/domains. Modules need short purpose and ownership descriptions. Comments should explain non-obvious decisions, architectural constraints, and algorithm invariants; avoid comments that merely repeat Python statements.

The architecture guide must follow one program from source tokens through macro expansion, fragments, objects, symbol resolution, relaxation, relocation, executable output, and physical image placement. Include concrete before/after representations and explain which module owns each transformation. Explain source locations and fragment anchors so changes do not corrupt listings or exception fixups.

The maintenance guide must provide worked recipes for extending an assigned instruction form through authoritative metadata, adding a directive, changing a macro facility, adding a relocation, and extending a linker-script rule or machine profile. Each recipe identifies exact files, invariants, relevant tests, expected outputs, and compatibility/versioning consequences. Document how to debug a wrong byte, bad relocation, unexpected relaxation result, and image-placement failure.

Documentation examples should run as part of appropriate conformance checks. An owner should be able to follow an extension recipe and locate the relevant code and focused validation command without reconstructing implementation history from commits or chat.

## Tasks (High-Level)

- [ ] 0.0 Create feature branch.

  Begin implementation on `feature/python-cross-assembler`, preserving the existing working tree. Planning alone does not create or switch branches.

- [ ] 1.0 Establish versioned specifications, compatibility boundaries, and the maintenance documentation framework.

  Deliver the five normative specifications, architecture/maintenance guide structure, and explicit resolutions for the format-affecting decisions above. Acceptance: the language, address domains, serialized IR, VOF/1, executable/image contracts, and relaxation invariants are precise enough to implement without guessing; existing ISA authorities remain unchanged by the planning work.

- [ ] 2.0 Package the shared ISA model and deliver a complete typed core assembler.

  Deliver Python packaging, lexer/parser, arbitrary-precision typed expressions, source provenance, symbols, operand validation, fragment IR, and every assigned instruction/alias. Preserve the old flat-image APIs through adapters, and introduce the new language's case-sensitive symbols deliberately. Acceptance: exact existing byte fixtures, register/suffix/selector/range rejection, and compiler-facing compatibility pass; public APIs and the encoding extension path are documented.

- [ ] 3.0 Deliver sections, directives, and hygienic macro assembly.

  Cover the specified section/data/symbol/include/conditional facilities and token-based macros, including defaults, named/variadic arguments, repetition, local/numeric labels, introspection, resource limits, and expansion diagnostics. Acceptance: representative multi-file programs assemble deterministically; NOBITS, forward origins, alignment, include dependencies, and unstable-condition failures are correct; language examples and macro/directive maintenance guidance are executable and current.

- [ ] 4.0 Deliver VOF/1 objects and a fixed-layout separate-module linking baseline.

  Deliver validated JSON development serialization followed by the production binary format, expression/relocation records, fragment anchors, encoding hashes, source metadata, and strong/weak symbol resolution. Keep automatic instructions conservatively long for this baseline so linking can be tested independently of optimization. Acceptance: independently assembled modules resolve code/data/far references, reject incompatible domains/metadata and malformed inputs, and preserve all semantics needed for later re-encoding; object inspection and relocation debugging are documented.

- [ ] 5.0 Deliver segment-aware linker scripts, placement, and deterministic final relaxation.

  Deliver the script language, VMA/LMA layout, linker expressions/assertions/symbols, executable emission, and unified branch/CALLR/memory-displacement relaxation. Acceptance: cross-module boundaries, forced widths, numeric pinning, alignment/origin interactions, segment overflows, instruction crossing, and nonidentity placement pass final validation; termination, fallback behavior, and extension points have explanatory comments and worked documentation.

- [ ] 6.0 Deliver OS data facilities and validated boot/replay images.

  Deliver general structures, typed DESC/1 and CTX/1, merged vectors, final-layout FIXUP/1 records, generated architectural includes, and explicit RAM/MMIO/hole placement with manifests. Acceptance: a separately linked kernel boots, enters user execution, handles syscall/nested faults, and uses exact fixup addresses after relaxation; image and backing-map artifacts each match the required 16,777,216-byte replay contract. Document runtime responsibilities and the image/profile maintenance workflow.

- [ ] 7.0 Complete development tools, static libraries, integration, and release conformance.

  Deliver the shared-model disassembler, listings/maps/symbols, four CLIs and binary convenience mode, deterministic static archive support/extraction, section GC/KEEP, and the remaining consumer migration. Acceptance: existing and new emulator tests agree on bytes/state/fault PCs, canonical and preservation-mode disassembly contracts hold, package installation works outside the checkout, and outputs reproduce across hash seeds and checkout paths. The owner guides must be complete, tested, and sufficient to make a focused change directly.

## Dependencies and release gates

The main dependency order is 0 → 1 → 2 → 3 → 4 → 5 → 6 → 7. Documentation, focused tests, and compatibility checks accompany each milestone rather than wait for milestone 7. Fixed-layout multi-module linking is the first relocatable deliverable; milestone 6 is the first OS-capable release candidate. Milestone 7 closes the complete initial-release scope.

Reuse existing unittest and CTest cases as independent regressions; add pytest coverage for the new package. Encoding acceptance must include independent golden bytes and emulator behavior, because assembler/disassembler agreement alone can reproduce the same mistake. Exercise signed/unsigned endpoints, reserved encodings, semantic width preservation, cross-segment failures, malformed containers, and exact fault/fixup sites at the appropriate milestone.

For integration validation, use the repository's documented CMake configure/build/CTest workflow with an explicitly selected interpreter when needed. New package tests should have documented focused pytest commands. Reproducibility checks compare complete artifacts, including debug/dependency metadata, from different checkout roots and Python hash seeds with identical inputs/options. Do not update pinned workload measurements merely to hide an unexpected change.

## Execution instructions

This file currently contains parent tasks only. After approval to expand it, add actionable sub-tasks with exact files, dependencies, focused validation, documentation updates, and completion criteria. During implementation, update each completed checkbox immediately. Planning does not authorize treating the implementation tasks as already executed.
