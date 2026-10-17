#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace vcpu {
enum class Revision { V04, V05 };
enum class ExecutionState { Running, Halted, TerminalResetFault, TerminalDoubleFault };
enum class Cause : uint16_t { Illegal=1, Memory=2, Stack=3, Invalid=4, IRQ=5, Trap=6 };
struct Fault { Cause cause; std::optional<uint32_t> address; };
struct Registers {
    std::array<uint16_t,8> r{};
    uint16_t pc=0, cs=0, ds=0, ss=0;
    uint16_t sp=0xfffe, sb=0, st=0xfffe, dp=0;
    uint16_t flags=0; // NZCVI only; E is synthesized from context presence.
};
using Context=std::array<uint16_t,21>;
// Host loader/debugger mutations occur between steps, on the execution thread.
class VirtualCPU {
public:
    explicit VirtualCPU(Revision revision=Revision::V05, size_t ram_bytes=1u<<24);
    Registers& registers() { return regs_; }
    const Registers& registers() const { return regs_; }
    uint16_t csr() const { return regs_.flags | (context_ ? 0x40 : 0); }
    const std::optional<Context>& context() const { return context_; }
    const std::optional<Fault>& diagnostic() const { return diagnostic_; }
    ExecutionState state() const { return state_; }
    Revision revision() const { return revision_; }
    bool irq_pending() const { return pending_; }
    void request_irq() {
        if (state_==ExecutionState::Running || state_==ExecutionState::Halted) pending_=true;
    }
    void map(uint32_t address, size_t length, bool installed=true);
    void load(uint32_t address, const std::vector<uint8_t>& bytes);
    uint8_t peek(uint32_t address) const;
    void reset();
    // One instruction or interrupt entry. False means no progress.
    bool step();
private:
    Revision revision_;
    Registers regs_;
    std::optional<Context> context_;
    std::optional<Fault> diagnostic_;
    ExecutionState state_=ExecutionState::Running;
    bool pending_=false;
    std::vector<uint8_t> ram_, installed_;
    uint8_t read(uint32_t address) const;
    uint32_t access(uint16_t segment, int64_t offset, unsigned width) const;
    uint32_t vector(unsigned cause) const;
    void enter(Cause cause, uint16_t pc, std::optional<uint32_t> address={}, uint8_t service=0);
    void fault(Fault failure);
    void execute();
};
}
