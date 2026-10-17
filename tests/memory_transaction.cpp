#include "memory_transaction.hpp"
#include <iostream>
#include <stdexcept>
#include <type_traits>
using namespace vcpu;
using namespace vcpu::detail;
namespace {
unsigned checks=0;
void check(bool ok,const char* text){++checks;if(!ok)throw std::runtime_error(text);}
template<class F> Fault failure(F action) {
    try {action();} catch(const Fault& f){return f;}
    throw std::runtime_error("Expected memory preflight fault");
}
VirtualCPU machine() {VirtualCPU c(0x80000);c.load(0,{0,1,0,0});c.reset();return c;}
void setc(VirtualCPU& c,uint8_t selector,uint16_t value) {
    c.registers().r[7]=value;c.registers().cs=0;c.registers().pc=0x700;c.load(0x700,{0x1f,0xb7,selector});
    check(c.step() && !c.context(),"valid scalar setup must not fault");
}
void descriptor(VirtualCPU& c,unsigned map,unsigned segment,std::array<uint16_t,7> words) {
    setc(c,0x20,map);setc(c,0x21,segment);for(unsigned i=0;i<7;++i)setc(c,0x22+i,words[i]);setc(c,0x7f,1);
}
std::vector<uint8_t> bytes(const VirtualCPU& c,unsigned address,unsigned size) {
    std::vector<uint8_t> result;for(unsigned i=0;i<size;++i)result.push_back(c.peek(address+i));return result;
}
void batches() {
    static_assert(max_staged_ram_bytes==40 && descriptor_image_words==9 && context_image_words==20);
    static_assert(noexcept(std::declval<RamWriteBatch>().commit(std::declval<std::vector<uint8_t>&>())));
    std::vector<uint8_t> ram(128,0xa5);RamWriteBatch batch;
    batch.stage(3,0xab,1);batch.stage(5,0x1234,2);batch.stage(9,0x89abcdef,4);
    check(ram[3]==0xa5 && batch.size()==7,"staging cannot write RAM");
    batch.commit(ram);
    check(ram[3]==0xab && ram[5]==0x34 && ram[6]==0x12 && ram[9]==0xef && ram[12]==0x89 && ram[4]==0xa5,"mixed-size little-endian writes preserve neighbors");
    RamWriteBatch full;for(unsigned i=0;i<20;++i)full.stage(2*i,0x1200+i,2);
    auto old=ram;
    try {full.stage(0,0,1);check(false,"overflow accepted");}catch(const std::logic_error&){}
    check(full.size()==40 && ram==old,"capacity rejection changes neither batch nor RAM");
    full.commit(ram);for(unsigned i=0;i<20;++i)check(ram[2*i]==i && ram[2*i+1]==0x12,"complete forty-byte commit");
    RamWriteBatch aliases;aliases.stage(60,0x1234,2);aliases.stage(61,0xab,1);aliases.commit(ram);
    check(ram[60]==0x34 && ram[61]==0xab,"ordered overlapping physical writes");
    old=ram;{RamWriteBatch discarded;discarded.stage(0,0,4);}check(ram==old,"discarded transaction has no effects");
    for(unsigned width:{0u,5u}) {RamWriteBatch bad;try{bad.stage(0,0,width);check(false,"bad width accepted");}catch(const std::logic_error&){}check(!bad.size(),"bad width leaves empty batch");}
}
void spans_and_snapshot() {
    auto c=machine();descriptor(c,0,3,{3,0xc1,0,0,1,0,0});
    descriptor(c,7,3,{5,0x19,0,0,1,0,0});setc(c,0x7f,3);setc(c,0,7);
    descriptor(c,8,3,{6,0x19,0,0,1,0,0});setc(c,0x7f,3);setc(c,1,8);setc(c,2,3);
    c.registers().ds=3;c.registers().ss=4;c.registers().r[4]=0x2001;
    std::vector<uint8_t> source(40);for(unsigned i=0;i<40;++i)source[i]=i+1;c.load(0x32001,source);
    auto old=c.registers();c.measure_traffic(true);
    auto span=KernelImageTransfer::preflight(c,old.ds,old.r[4],20,Access::Read);
    check(span.physical[0]==0x32001 && span.physical[19]==0x32027,"old K DS/map0, unaligned word addresses; ignore SS/process/uaccess maps");
    check(!c.step_traffic().ram_reads && !c.step_traffic().ram_writes,"preflight reads no payload bytes");
    auto image=KernelImageTransfer::snapshot(c,span);
    for(unsigned i=0;i<20;++i)check(image.words[i]==unsigned(source[2*i])+(unsigned(source[2*i+1])<<8),"snapshot little-endian payload");
    check(c.step_traffic().ram_reads==40 && c.protected_state()->device_commits==0,"snapshot counts only forty RAM reads");
}
void every_memory_failure() {
    for(unsigned count:{9u,20u})for(auto access:{Access::Read,Access::Write})for(unsigned bad=0;bad<2*count;++bad) {
        auto c=machine();std::vector<uint8_t> payload(2*count,0xa5);c.load(0x3001,payload);c.map(0x3001+bad,1,false);
        auto before=c.registers();auto descriptors=c.protected_state()->descriptors;c.measure_traffic(true);
        auto f=failure([&]{KernelImageTransfer::preflight(c,0,0x3001,count,access);});
        check(f.cause==Cause::Memory && f.address==0x3001+bad,"first absent backing byte physical address");
        check(f.memory==std::array<uint16_t,11>{2,2,uint16_t(access),0,0,uint16_t(0x3001+(bad/2)*2),0,uint16_t(0x3001+bad),0x100,2,8},"exact subword metadata including last/odd byte");
        check(bytes(c,0x3001,2*count)==payload && c.registers().r==before.r && c.registers().pc==before.pc && c.protected_state()->descriptors==descriptors && !c.context(),"failed preflight changes no RAM/register/control/frame");
        check(!c.step_traffic().ram_reads && !c.step_traffic().ram_writes && !c.protected_state()->device_commits,"late preflight failure causes no payload read/write/device callback");
    }
}
void ranges_and_permissions() {
    for(unsigned count:{9u,20u}) {
        auto c=machine();KernelImageTransfer::preflight(c,0,uint16_t(65536-2*count),count,Access::Read);
        KernelImageTransfer::preflight(c,0,uint16_t(65535-2*count),count,Access::Write); // odd alignment
        for(unsigned extra:{1u,2u}) {
            uint16_t base=uint16_t(65536-2*count+extra);descriptor(c,0,3,{});c.measure_traffic(true);
            auto f=failure([&]{KernelImageTransfer::preflight(c,3,base,count,Access::Read);});
            unsigned attempted=extra==1?65535:65536;
            check(f.memory==std::array<uint16_t,11>{2,2,2,0,3,uint16_t(attempted),uint16_t(attempted>>16),uint16_t(extra==1?65535:0),uint16_t(extra==1?0x103:0),2,1} && !f.address,"whole-span range failure uses first impossible word and no physical address");
            check(!c.step_traffic().descriptor_lookups,"range fault precedes absent descriptor lookup");
        }
    }
    for(auto d:std::vector<std::array<uint16_t,7>>{{3,0x81,0,0,1,0,0},{3,0x41,0,0,1,0,0},{255,0xc3,0,0,1,0,0},{255,0xc1,0,0,1,0,0}}) {
        auto c=machine();descriptor(c,0,3,d);c.measure_traffic(true);
        if(d[1]==0x81) {
            KernelImageTransfer::preflight(c,3,0,20,Access::Write);
            auto f=failure([&]{KernelImageTransfer::preflight(c,3,0,2,Access::Read);});check(f.memory[10]==6,"STX destination can be write-only; STC header needs read");
        } else {
            auto f=failure([&]{KernelImageTransfer::preflight(c,3,0,9,Access::Write);});
            check(f.memory[10]==(d[1]==0x41?6:7),"rights precede device; reject descriptor DEVICE or actual MMIO");
        }
        check(!c.protected_state()->device_commits && !c.step_traffic().mmio_samples && !c.step_traffic().ram_reads,"forbidden images never sample devices or RAM");
    }
    auto c=machine();descriptor(c,0,3,{3,0xc1,0,0x3025,0,0,0});c.measure_traffic(true);
    auto f=failure([&]{KernelImageTransfer::preflight(c,3,0x3000,20,Access::Write);});
    check(f.memory[5]==0x3024 && f.memory[9]==2 && f.memory[10]==5 && f.address==0x33024,"partial last descriptor word faults BOUNDS at its word start");
}
void frozen_targets() {
    auto c=machine();descriptor(c,0,3,{3,0xc1,0,0,1,0,0});
    c.load(0x32000,{0x34,0x12,0x78,0x56});auto input=KernelImageTransfer::preflight(c,3,0x2000,2,Access::Read);
    auto image=KernelImageTransfer::snapshot(c,input);auto output=KernelImageTransfer::preflight(c,3,0x2001,2,Access::Write);
    RamWriteBatch batch;KernelImageTransfer::stage(batch,output,image);
    descriptor(c,0,3,{4,0xc1,0,0,1,0,0}); // Model a publication after all targets/source were captured.
    std::vector<uint8_t> ram(0x80000,0xa5);batch.commit(ram);
    check(ram[0x32001]==0x34 && ram[0x32002]==0x12 && ram[0x32003]==0x78 && ram[0x32004]==0x56 && ram[0x42001]==0xa5,"source alias is snapshotted; destination uses frozen old physical targets");
    WordImage too_big;too_big.count=3;RamWriteBatch mismatch;
    try {KernelImageTransfer::stage(mismatch,output,too_big);check(false,"mismatched image accepted");}catch(const std::logic_error&){}
    check(!mismatch.size(),"invalid image length cannot stage partial batch");
}
}
int main() {
    try {batches();spans_and_snapshot();every_memory_failure();ranges_and_permissions();frozen_targets();std::cout<<checks<<" bounded memory transaction checks passed\n";}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
