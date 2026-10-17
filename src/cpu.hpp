#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <bitset>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace vcpu {
enum class MachineProfile { MinimalV1, ConsoleV1 };
enum class ExecutionState { Running, Halted, TerminalResetFault, TerminalDoubleFault };
enum class Cause : uint16_t { Illegal=1, Memory=2, Stack=3, Invalid=4, IRQ=5, Trap=6, Privilege=7 };
enum class Mode : uint16_t { User=0, Kernel=1 };
enum class Access : uint16_t { None=0, Fetch=1, Read=2, Write=3 };
enum class Phase : uint16_t { None=0, Fetch=1, ResultPC=3, Operand=2, ControlTarget=4, ReturnState=5, ReturnTarget=6 };
struct Fault {
    Cause cause; std::optional<uint32_t> address;
    std::array<uint16_t,11> memory{};
    uint16_t privilege=0;
};
struct Registers {
    std::array<uint16_t,8> r{};
    uint16_t pc=0, cs=0, ds=0, ss=0;
    uint16_t sp=0xfffe, sb=0, st=0xfffe, dp=0;
    uint16_t flags=0; // NZCVI only; E is synthesized from context presence.
};
using Context=std::array<uint16_t,21>;
struct ProtectedFrame {
    Context words{};
    std::array<uint16_t,3> controls{};
    Mode origin=Mode::Kernel;
    bool bootstrap=false;
    uint16_t prior_depth=0, source=0, privilege=0;
    std::array<uint16_t,11> memory{};
};
struct TerminalDiagnostic {
    std::string reason, stage;
    Fault failure;
    Cause event;
    uint16_t pc=0, service=0, source=0;
    size_t depth=0;
    bool captured=false;
    Registers registers;
    Mode mode=Mode::Kernel;
    std::array<uint16_t,3> controls{};
    uint16_t pending=0, in_service=0;
};
struct BoundaryEvent {
    enum class Kind { Edge, Level, IdleAdvance, Reset, Invalid, ConsoleInput };
    std::string run, boundary, sequence;
    Kind kind=Kind::Edge;
    unsigned source=0;
    bool asserted=false;
    std::string target_time="0";
    std::vector<uint8_t> data;
};
enum class HostError { None=0, BadFormat=1, WrongBoundary=2, BadSequence=3, BadSource=4, BadState=5, BadTime=6, BadProfile=7 };
struct ProtectedState {
    Mode mode=Mode::Kernel;
    std::array<uint16_t,3> controls{};
    std::map<uint16_t,std::array<uint16_t,7>> descriptors;
    std::bitset<256> published;
    uint16_t desc_map=0, desc_segment=0;
    std::array<uint16_t,7> shadow{};
    std::array<uint16_t,5> entry_shadow{}, entry{};
    bool entry_configured=false;
    uint32_t query_address=0, query_run=0;
    uint16_t query_kind=0;
    bool query_valid=false;
    std::vector<ProtectedFrame> frames;
    uint16_t pending=0, enable=0, in_service=0, levels=0;
    uint64_t time=0, deadline=0, period=0, shadow_deadline=0, shadow_period=0, snapshot=0;
    uint16_t timer_mode=0, shadow_mode=0;
    bool snapshot_valid=false, deadline_overflow=false;
    uint64_t device_commits=0;
    std::vector<uint8_t> console_rx, console_tx;
};
// Host loader/debugger mutations occur between steps, on the execution thread.
class VirtualCPU {
public:
    explicit VirtualCPU(size_t ram_bytes=1u<<24,
                        MachineProfile profile=MachineProfile::MinimalV1);
    Registers& registers() { return regs_; }
    const Registers& registers() const { return regs_; }
    uint16_t csr() const { return regs_.flags | (context_ ? 0x40 : 0); }
    const std::optional<Context>& context() const { return context_; }
    const std::optional<Fault>& diagnostic() const { return diagnostic_; }
    ExecutionState state() const { return state_; }
    MachineProfile profile() const { return profile_; }
    bool irq_pending() const { return protected_->pending!=0; }
    Mode mode() const { return protected_->mode; }
    const std::optional<ProtectedState>& protected_state() const { return protected_; }
    const std::optional<TerminalDiagnostic>& terminal_diagnostic() const { return terminal_; }
    const std::string& boundary() const { return boundary_; }
    const std::string& run() const { return run_; }
    const std::string& next_sequence() const { return sequence_; }
    HostError apply_boundary_events(const std::vector<BoundaryEvent>& records);
    HostError apply_boundary_json(const std::string& json_lines);
    HostError advance_idle_to(uint64_t target);
    std::string replay_header() const; // Bound to reset-boundary RAM/backing artifacts.
    HostError verify_replay_header(const std::string& json, const std::vector<uint8_t>& ram_image,
                                   const std::vector<uint8_t>& backing_map) const;
    void map(uint32_t address, size_t length, bool installed=true);
    void load(uint32_t address, const std::vector<uint8_t>& bytes);
    uint8_t peek(uint32_t address) const;
    void reset();
    // One instruction or interrupt entry. False means no progress.
    bool step();
private:
    MachineProfile profile_;
    Registers regs_;
    std::optional<Context> context_;
    std::optional<Fault> diagnostic_;
    ExecutionState state_=ExecutionState::Running;
    std::vector<uint8_t> ram_, installed_;
    std::optional<ProtectedState> protected_;
    std::optional<TerminalDiagnostic> terminal_;
    std::string boundary_="0", run_="0", sequence_="0";
    bool has_reset_=false;
    uint8_t read(uint32_t address) const;
    uint32_t access(uint16_t segment, int64_t offset, unsigned width, Access type=Access::Read,
                    Phase phase=Phase::Operand, bool mmio=false) const;
    unsigned backing(uint32_t address) const;
    std::array<uint16_t,7> descriptor(uint16_t map, uint16_t segment) const;
    std::optional<uint32_t> physical_hint(uint16_t segment, int64_t offset) const;
    Fault memory_failure(uint16_t map, Mode mode, uint16_t segment, int64_t offset, unsigned width,
                         Access access, Phase phase, unsigned origin, unsigned reason,
                         std::optional<uint32_t> physical={}, std::optional<uint16_t> relevant={}) const;
    uint32_t translate(uint16_t map, Mode mode, uint16_t segment, int64_t offset, unsigned width,
                       Access access, Phase phase, unsigned origin, bool mmio=false) const;
    void select_segment(uint16_t map, Mode mode, uint16_t segment, bool stack, Phase phase) const;
    void validate_entry(const std::array<uint16_t,5>& entry) const;
    Registers validate_return(const ProtectedFrame& frame, Mode mode) const;
    void enter_protected(const Fault& event, uint16_t pc, uint8_t service=0, uint16_t source=0);
    uint16_t read_control(uint8_t selector) const;
    std::function<void()> stage_control(uint8_t selector, uint16_t value);
    uint16_t read_context(unsigned selector) const;
    void write_context(unsigned selector, uint16_t value);
    uint16_t mmio_read(uint32_t address) const;
    void commit_mmio_read(uint32_t address);
    std::function<void()> stage_mmio(uint32_t address, uint16_t value);
    void timer_due();
    static void evaluate_timer(ProtectedState& state);
    static void increment(std::string& decimal);
    void fault(Fault failure);
    void execute();
};
}
