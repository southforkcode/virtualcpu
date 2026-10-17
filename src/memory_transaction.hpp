#pragma once
#include "cpu.hpp"
#include <array>
#include <stdexcept>

namespace vcpu::detail {
inline constexpr size_t descriptor_image_words=9;
inline constexpr size_t context_image_words=20;
inline constexpr size_t max_image_words=context_image_words;
inline constexpr size_t max_staged_ram_bytes=2*max_image_words;

// Internal commit buffer, not an MMIO path. Every target must already have been
// preflighted. Construction/staging touch no guest RAM. Commit cannot fault or
// allocate; repeated physical targets commit in staging order (last byte wins).
class RamWriteBatch {
    struct Byte { uint32_t address; uint8_t value; };
    std::array<Byte,max_staged_ram_bytes> bytes_;
    size_t count_=0;
public:
    size_t size() const noexcept { return count_; }
    void stage(uint32_t address,uint32_t value,unsigned width) {
        if (!width || width>4 || width>max_staged_ram_bytes-count_ ||
            uint64_t(address)+width>(1u<<24))
            throw std::logic_error("Invalid bounded RAM write batch");
        for (unsigned i=0;i<width;++i) bytes_[count_++]={address+i,uint8_t(value>>(8*i))};
    }
    void commit(std::vector<uint8_t>& ram) const noexcept {
        for (size_t i=0;i<count_;++i) ram[bytes_[i].address]=bytes_[i].value;
    }
};

// Frozen physical word targets, resolved before any control/frame publication.
// Helpers are internal building blocks; legal decode, privilege and active-frame
// checks belong to the instruction caller, before invoking these helpers.
struct ImageSpan {
    std::array<uint32_t,max_image_words> physical{};
    size_t count=0;
    Access access=Access::None;
};
struct WordImage {
    std::array<uint16_t,max_image_words> words{};
    size_t count=0;
};
class KernelImageTransfer {
public:
    static void check_range(const VirtualCPU& cpu,uint16_t segment,uint16_t base,size_t count,Access type) {
        if (cpu.mode()!=Mode::Kernel || !count || count>max_image_words ||
            (type!=Access::Read && type!=Access::Write))
            throw std::logic_error("Invalid internal kernel image operation");
        if (uint32_t(base)+2*count>65536) {
            // Whole-span rejection precedes descriptor lookup. Diagnose the
            // first two-byte subtransfer that cannot fit, retaining xwidth=2.
            uint32_t first_bad=uint32_t(base)+2*((65536u-base)/2);
            throw cpu.memory_failure(0,Mode::Kernel,segment,first_bad,2,type,Phase::Operand,2,1);
        }
    }
    static ImageSpan preflight(const VirtualCPU& cpu,uint16_t segment,uint16_t base,size_t count,Access type) {
        check_range(cpu,segment,base,count,type);
        ImageSpan span;span.count=count;span.access=type;
        for (size_t i=0;i<count;++i)
            span.physical[i]=cpu.translate(0,Mode::Kernel,segment,uint32_t(base)+2*i,2,type,Phase::Operand,2);
        return span;
    }
    static WordImage snapshot(const VirtualCPU& cpu,const ImageSpan& source) {
        if (source.access!=Access::Read || !source.count || source.count>max_image_words)
            throw std::logic_error("Image source was not preflighted for read");
        WordImage image;image.count=source.count;
        for (size_t i=0;i<source.count;++i) {
            auto a=source.physical[i];
            auto low=cpu.read(a);
            auto high=cpu.read(a+1);
            image.words[i]=uint16_t(low|uint16_t(high)<<8);
        }
        return image;
    }
    static void stage(RamWriteBatch& batch,const ImageSpan& destination,const WordImage& image) {
        if (destination.access!=Access::Write || !destination.count || destination.count>max_image_words ||
            destination.count!=image.count || 2*image.count>max_staged_ram_bytes-batch.size())
            throw std::logic_error("Image destination/batch mismatch");
        for (size_t i=0;i<image.count;++i) batch.stage(destination.physical[i],image.words[i],2);
    }
};
}
