#include "cpu.hpp"
#include "decode_table.hpp"
#include "memory_transaction.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace vcpu {
namespace {
constexpr uint16_t Z=1, N=2, C=4, V=8, I=32;
int32_t signed16(uint32_t x) { return x<0x8000 ? int32_t(x) : int32_t(x)-65536; }
int32_t signed8(uint8_t x) { return x<128 ? x : int32_t(x)-256; }
std::optional<uint32_t> address(uint16_t segment, int64_t offset) {
    if (offset<0 || offset>65535) return {};
    return (uint32_t(segment)<<16) | uint32_t(offset);
}
void stack(const Registers& s, int64_t start, int64_t end) {
    if (!(s.sb<=s.sp && s.sp<=s.st && start>=s.sb && start>=0 && end<=s.st))
        throw Fault{Cause::Stack,address(s.ss,start)};
}
uint16_t nz(uint16_t x) { return (x==0?Z:0) | (x&0x8000?N:0); }
}
VirtualCPU::VirtualCPU(size_t ram_bytes, MachineProfile profile)
    : profile_(profile), ram_(ram_bytes), installed_(ram_bytes,1), protected_(ProtectedState{}) {
    if (profile!=MachineProfile::MinimalV1 && profile!=MachineProfile::ConsoleV1)
        throw std::invalid_argument("Unsupported machine profile");
    if (ram_bytes>(1u<<24)) throw std::invalid_argument("RAM exceeds physical address space");
}
void VirtualCPU::map(uint32_t start, size_t length, bool installed) {
    if (start>ram_.size() || length>ram_.size()-start) throw std::out_of_range("RAM map");
    std::fill(installed_.begin()+start,installed_.begin()+start+length,installed);
}
void VirtualCPU::load(uint32_t start, const std::vector<uint8_t>& bytes) {
    if (start>ram_.size() || bytes.size()>ram_.size()-start) throw std::out_of_range("RAM load");
    std::copy(bytes.begin(),bytes.end(),ram_.begin()+start);
}
uint8_t VirtualCPU::peek(uint32_t a) const { return ram_.at(a); }
uint8_t VirtualCPU::read(uint32_t a) const {
    if (a>=ram_.size() || !installed_[a]) throw Fault{Cause::Memory,a};
    if (measure_traffic_) ++traffic_.ram_reads;
    return ram_[a];
}
uint32_t VirtualCPU::access(uint16_t segment, int64_t offset, unsigned width, Access type, Phase phase, bool mmio) const {
    return translate(mode()==Mode::Kernel?0:protected_->controls[0],mode(),segment,offset,width,type,phase,mode()==Mode::Kernel?2:1,mmio);
}
void VirtualCPU::reset() {
    traffic_={};
    regs_=Registers{}; context_.reset(); diagnostic_.reset();
    state_=ExecutionState::Running;
    if (has_reset_) increment(run_);
    has_reset_=true; boundary_="0"; sequence_="0"; terminal_.reset();
    protected_=ProtectedState{}; protected_->published.set(0);
    for (unsigned s=0;s<256;++s) protected_->descriptors[uint16_t(s)]={uint16_t(s),uint16_t(s==255?0xc3:0x1c1),0,0,1,0,0};
    try {
        uint32_t packed=0;
        for (unsigned i=0;i<4;++i) packed|=uint32_t(read(translate(0,Mode::Kernel,0,i,1,Access::Read,Phase::Operand,2)))<<(8*i);
        if (packed&0xff000000) throw Fault{Cause::Invalid,0};
        translate(0,Mode::Kernel,uint16_t(packed>>16),packed&65535,1,Access::Fetch,Phase::ControlTarget,2);
        regs_.cs=uint16_t(packed>>16); regs_.pc=uint16_t(packed);
    } catch (const Fault& f) { diagnostic_=f; state_=ExecutionState::TerminalResetFault; }
}
void VirtualCPU::fault(Fault f) {
    enter_protected(f,regs_.pc);
}
bool VirtualCPU::step() {
    traffic_={};
    if (state_==ExecutionState::TerminalDoubleFault || state_==ExecutionState::TerminalResetFault) return false;
    auto& p=*protected_; p.pending|=p.levels;
    uint16_t eligible=p.pending&p.enable&uint16_t(~p.in_service);
    if (eligible && (regs_.flags&32)) {
        unsigned source=0; while (!(eligible&(1u<<source))) ++source;
        enter_protected(Fault{Cause::IRQ,{}},regs_.pc,0,uint16_t(source)); increment(boundary_); return true;
    }
    if (state_==ExecutionState::Halted) return false;
    try {
        execute();
        if (state_!=ExecutionState::TerminalDoubleFault) { if (p.time!=UINT64_MAX) ++p.time; timer_due(); }
    } catch (const Fault& f) { fault(f); }
    increment(boundary_); return true;
}
void VirtualCPU::execute() {
    const auto& old=regs_;
    auto s=old; // Staged ordinary state; a fault snapshots regs_, not this copy.
    auto ia=physical_hint(old.cs,old.pc);
    auto invalid=[&] { throw Fault{Cause::Invalid,ia}; };
    auto illegal=[&] { throw Fault{Cause::Illegal,ia}; };
    auto check_stack=[&](int64_t start,int64_t end) {
        try { stack(old,start,end); }
        catch (Fault f) { f.address=physical_hint(old.ss,start); throw f; }
    };
    std::array<uint8_t,5> b{};
    unsigned fetched=0;
    auto fetch=[&] {
        auto a=access(old.cs,int64_t(old.pc)+fetched,1,Access::Fetch,Phase::Fetch);
        b[fetched]=read(a);
        if (measure_traffic_) ++traffic_.fetch_reads;
        return b[fetched++];
    };
    // Fetch/decode never advances live PC. Reserved descriptors fail as read.
    auto op=fetch();
    auto e=encoding::tables[0][op];
    if (!e.length && !e.dispatch) illegal();
    if (e.dispatch) e=encoding::tables[e.dispatch][fetch()];
    if (!e.length) illegal();
    while (fetched<e.length) {
        auto i=fetched; fetch();
        if ((b[i]&e.masks[i])!=e.expected[i]) illegal();
        if (!(encoding::valid_bytes[e.validation[i]][b[i]/8]&(1u<<(b[i]%8)))) illegal();
    }
    if (mode()==Mode::User) {
        unsigned privilege=0, x=b[1];
        if (op==1) privilege=3; if (op==2 || op==3) privilege=2; if (op==0x27) privilege=10;
        if (op==0x10 && (x&0xf0)==0x90) { if ((x&3)==3) privilege=1; if ((x&3)==1) privilege=4; }
        if (op==0x10 && (x&0xf0)==0xd0 && (x&3)==3) privilege=4;
        if (op==0x1f) {
            if (x==0x12) privilege=7;
            if ((x>=0x20 && x<=0x34) || (x>=0x80 && x<=0x87)) privilege=5;
            if ((x>=0x40 && x<=0x50) || (x>=0x90 && x<=0x97)) privilege=6;
            if (x>=0xa0 && x<=0xa7) privilege=8; if (x>=0xb0 && x<=0xb7) privilege=9;
            if (x>=0xc0 && x<=0xc3) privilege=9;
            if (x>=0xc4 && x<=0xc7) privilege=8;
            if (x>=0xc8 && x<=0xcb) privilege=6;
            if (x>=0xcc && x<=0xcf) privilege=5;
        }
        if (privilege) { Fault f{Cause::Privilege,ia}; f.privilege=uint16_t(privilege); throw f; }
    }
    uint32_t operand=0;
    unsigned start=(op==0x15 || op==0x28 || op==0x1f)?2:1;
    for (unsigned j=start;j<e.length;++j) operand |= uint32_t(b[j])<<(8*(j-start));
    int64_t next=unsigned(old.pc)+e.length, target=next;
    uint16_t target_segment=old.cs;
    bool explicit_target=false, halt=false, returning=false;
    detail::RamWriteBatch writes;
    std::optional<std::pair<unsigned,uint16_t>> edit;
    std::function<void()> action;
    // Prepare sparse-map insertion storage before any architectural commit.
    std::map<uint16_t,std::array<uint16_t,7>> descriptor_node;
    auto descriptor_target=protected_->descriptors.end();
    std::optional<std::array<uint16_t,7>> descriptor_value;
    std::optional<ProtectedFrame> frame_candidate;
    auto read_word=[&](uint32_t a) { if (measure_traffic_) traffic_.ram_reads+=2; return uint16_t(ram_[a]|uint16_t(ram_[a+1])<<8); };
    auto read_byte=[&](uint32_t a) { if (measure_traffic_) ++traffic_.ram_reads; return ram_[a]; };
    auto stage_store=[&](uint32_t a,uint32_t value,unsigned width) { writes.stage(a,value,width); };
    auto jump=[&](int64_t value) { target=value; explicit_target=true; };
    auto arith=[&](unsigned kind,uint16_t a,uint16_t v) {
        bool sub=kind==5 || kind==7 || kind==8;
        int extra=kind==5 ? !(old.flags&C) : kind==4 ? bool(old.flags&C) : 0;
        int32_t wide=sub ? int32_t(a)-v-extra : int32_t(a)+v+extra;
        int32_t sign=sub ? signed16(a)-signed16(v)-extra : signed16(a)+signed16(v)+extra;
        uint16_t result=uint16_t(wide);
        bool carry=sub ? int32_t(a)>=int32_t(v)+extra : wide>65535;
        s.flags=(old.flags&~15)|nz(result)|(carry?C:0)|((sign < -32768 || sign>32767)?V:0);
        return result;
    };
    if (op<=3) {
        halt=op==1;
        if (op==2) s.flags &= ~I;
        if (op==3) s.flags |= I;
    } else if (op>=0x10 && op<=0x1f && op!=0x10 && op!=0x15 && op!=0x1a && op!=0x1f) {
        s.r[op&3]=old.r[(op>>2)&3];
    } else if (op==0x10) {
        unsigned x=b[1], src=(x>>2)&3, dst=x&3, kind=(x>>4)&7;
        if (x<128) {
            auto a=old.r[dst], v=old.r[src];
            if (kind>=4) s.r[dst]=arith(kind,a,v);
            else {
                uint16_t result=kind==0?a^v:kind==1?a&v:kind==2?a|v:uint16_t(~v);
                s.r[dst]=result; s.flags=(old.flags&~3)|nz(result);
            }
        } else if (kind==0) {
            std::array<uint16_t,4> bank{old.sp,old.sb,old.dp,csr()}; s.r[dst]=bank[src];
        } else if (kind==2) s.r[dst]=old.r[4+src];
        else if (kind==3) s.r[4+dst]=old.r[src];
        else if (kind==4) {
            std::array<uint16_t,4> bank{old.ss,old.cs,old.ds,old.st}; s.r[dst]=bank[src];
        } else {
            auto v=old.r[src];
            if (kind==1) {
                if (dst==0) {
                    if (!(old.sb<=v && v<=old.st)) throw Fault{Cause::Stack,physical_hint(old.ss,v)};
                    s.sp=v;
                } else if (dst==1) s.sb=v;
                else if (dst==2) s.dp=v;
                else { if (v&~0x6f) invalid(); s.flags=v&0x2f; }
            } else if (kind==5) {
                if (dst<3 && v>255) invalid();
                if (mode()==Mode::User && (dst==0 || dst==2)) {
                    select_segment(protected_->controls[0],Mode::User,v,dst==0,Phase::Operand);
                    if (dst==0) { auto d=descriptor(protected_->controls[0],v); s.sb=d[5]; s.st=d[6]; s.sp=d[6]; }
                }
                if (dst==0) s.ss=v;
                else if (dst==1) { s.cs=v; target_segment=v; explicit_target=true; }
                else if (dst==2) s.ds=v;
                else s.st=v;
            }
        }
    } else if (op>=0x20 && op<=0x24) {
        int32_t value=(op==0x24?old.dp:old.r[4+op-0x20])+signed8(b[1]);
        if (value<0 || value>65535) invalid();
        if (op==0x24) s.dp=uint16_t(value); else s.r[4+op-0x20]=uint16_t(value);
    } else if (op==0x25) {
        if (!(old.sb<=old.sp && old.sp<=old.st)) throw Fault{Cause::Stack,physical_hint(old.ss,old.sp)};
        int32_t value=old.sp+signed8(b[1]);
        if (value<old.sb || value>old.st) throw Fault{Cause::Stack,physical_hint(old.ss,value)};
        s.sp=uint16_t(value);
    } else if (op==0x26) {
        if (next>65535) {
            throw memory_failure(mode()==Mode::Kernel?0:protected_->controls[0],mode(),old.cs,next,1,Access::Fetch,Phase::ResultPC,mode()==Mode::Kernel?2:1,1);
        }
        enter_protected(Fault{Cause::Trap,{}},uint16_t(next),b[1]); return;
    } else if (op>=0x80 && op<=0xaf) {
        unsigned kind=(op-0x80)>>3,bits=(op&4)?16:8,mask=(1u<<bits)-1;
        uint32_t a=old.r[op&3]&mask,v=bits==8?b[1]:uint32_t(b[1])|(uint32_t(b[2])<<8),result=0;
        if (kind<3) {
            bool sub=kind!=0; int32_t wide=sub?int32_t(a)-int32_t(v):int32_t(a)+int32_t(v);
            auto sign=[&](uint32_t x) { return x&(1u<<(bits-1))?int32_t(x)-int32_t(1u<<bits):int32_t(x); };
            int32_t signed_result=sub?sign(a)-sign(v):sign(a)+sign(v); result=uint32_t(wide)&mask;
            bool carry=sub?a>=v:uint32_t(wide)>mask;
            bool overflow=signed_result<-(1<<(bits-1)) || signed_result>((1<<(bits-1))-1);
            s.flags=(old.flags&~15)|(carry?C:0)|(overflow?V:0);
        } else { result=kind==3?a&v:kind==4?a|v:a^v; s.flags=old.flags&~3; }
        s.flags|=(result==0?Z:0)|(result&(1u<<(bits-1))?N:0);
        if (kind!=2) s.r[op&3]=uint16_t(result);
    } else if (op==0x27) {
        unsigned x=b[1],reg=(x>>2)&7,width=(x&32)?1:2; bool store=x&128;
        int64_t offset=old.r[4+(b[2]&3)]+((x&64)?signed16(b[3]|uint32_t(b[4])<<8):0);
        auto a=translate(protected_->controls[1],Mode::User,protected_->controls[2],offset,width,store?Access::Write:Access::Read,Phase::Operand,3);
        if (store) stage_store(a,old.r[reg],width); else s.r[reg]=width==1?read_byte(a):read_word(a);
    } else if (op==0x15 && b[1]>=0x40 && b[1]<=0x4f) {
        unsigned x=b[1],reg=x&3,base=4+(b[2]&3),width=(x&4)?1:2;
        bool store=x&8; auto offset=old.r[base];
        unsigned advanced=unsigned(offset)+width;
        if (advanced>65535) invalid();
        // RAM-only preflight occurs before any read/store or pointer update.
        auto a=access(old.ds,offset,width,store?Access::Write:Access::Read,Phase::Operand,false);
        s.r[base]=uint16_t(advanced);
        if (store) stage_store(a,old.r[reg],width);
        else action=[&,a,width,reg]{s.r[reg]=width==1?read_byte(a):read_word(a);};
    } else if (op==0x15 || op==0x28) {
        unsigned x=b[1], reg=x&7, width=(x&8)?1:2, mode=(x>>4)&3;
        bool store=x&64;
        if (!(x&128) && mode==0) s.r[reg]=uint16_t(operand);
        else {
            int64_t offset; uint16_t segment=old.ds;
            if (x&128) {
                int displacement=(x&16)?signed16(b[3]|uint32_t(b[4])<<8):int(b[2]>>2)-(b[2]&128?64:0);
                offset=int64_t(old.r[4+(b[2]&3)])+displacement;
                if (x&32) segment=old.cs;
            } else if (mode==1) offset=operand;
            else if (mode==2) offset=old.dp+signed16(operand);
            else { offset=int64_t(old.sp)+operand; segment=old.ss; check_stack(offset,offset+width); }
            auto a=access(segment,offset,width,store?Access::Write:Access::Read,Phase::Operand,!(!(x&128) && mode==3));
            if (backing(a)==2) {
                try {
                    if (width!=2 || (a&1)) throw Fault{Cause::Memory,a};
                    if (store) {
                        auto commit=stage_mmio(a,old.r[reg]); action=[this,commit]{commit();++protected_->device_commits;};
                    } else { s.r[reg]=mmio_read(a); if (measure_traffic_) traffic_.mmio_samples+=2; action=[this,a]{commit_mmio_read(a);++protected_->device_commits;}; }
                } catch (const Fault& f) {
                    throw memory_failure(this->mode()==Mode::Kernel?0:protected_->controls[0],this->mode(),segment,offset,width,
                        store?Access::Write:Access::Read,Phase::Operand,this->mode()==Mode::Kernel?2:1,7,f.address);
                }
            } else if (store) stage_store(a,old.r[reg],width);
            else s.r[reg]=width==1?read_byte(a):read_word(a);
        }
    } else if (op==0x1a && b[1]>=0xa0 && b[1]<=0xef) {
        unsigned kind=(b[1]-0xa0)>>4,src=(b[1]>>2)&3,dst=b[1]&3;
        uint16_t a=old.r[dst],v=old.r[src],result;
        if (kind==0) result=uint16_t(uint32_t(a)*uint32_t(v));
        else {
            // Check the guest quotient domain before host division/modulo.
            // MODS shares DIVS's MIN/-1 fault even though its remainder fits.
            bool is_signed=kind==2 || kind==4;
            if (!v || (is_signed && a==0x8000 && v==0xffff)) throw Fault{Cause::Arithmetic,ia};
            if (is_signed) {
                auto dividend=signed16(a),divisor=signed16(v);
                result=uint16_t(kind==2?dividend/divisor:dividend%divisor);
            } else result=uint16_t(kind==1?a/v:a%v);
        }
        s.r[dst]=result;s.flags=(old.flags&~3)|nz(result);
    } else if (op==0x1a) {
        unsigned x=b[1], dst=x&3; auto a=old.r[dst];
        if (x>=0xf0 && x<=0xf7) {
            s.r[dst]=arith(x<0xf4?6:7,a,1);
        } else if (x>=0xf8 && x<=0xfb) {
            auto value=uint16_t(signed8(uint8_t(a)));
            s.r[dst]=value;s.flags=(old.flags&~3)|nz(value);
        } else if (x>=0x60 && x<=0x87) {
            unsigned count=b[2], kind=(x-0x60)>>3, bits=(x&4)?16:8;
            if (count>16) invalid();
            if (count) {
                unsigned mask=(1u<<bits)-1, sign=1u<<(bits-1), value=a&mask;
                bool carry=false;
                for (unsigned i=0;i<count;++i) {
                    if (kind==0 || kind==3) {
                        carry=value&sign;
                        value=((value<<1)&mask)|((kind==3 && carry)?1:0);
                    } else {
                        carry=value&1;
                        unsigned incoming=kind==2?(value&sign):(kind==4 && carry)?sign:0;
                        value=(value>>1)|incoming;
                    }
                }
                s.r[dst]=uint16_t(value);
                s.flags=(old.flags&~7)|(value==0?Z:0)|(value&sign?N:0)|(carry?C:0);
            }
        } else if (x>=0x40 && x<=0x4b) {
            unsigned bits=x<0x48?8:16;
            uint16_t mask=x<0x44?0xff:uint16_t(b[2]|(bits==16?uint16_t(b[3])<<8:0));
            uint16_t result=a&mask;
            s.flags=(old.flags&~3)|(result==0?Z:0)|(result&(1u<<(bits-1))?N:0);
        } else if (x<16) arith(8,a,old.r[(x>>2)&3]);
        else {
            unsigned kind=x>>4;
            uint16_t value=kind==1?uint16_t(a<<1):kind==2?a>>1:(a>>1)|(a&0x8000);
            s.r[dst]=value; s.flags=(old.flags&~7)|nz(value)|((kind==1?a&0x8000:a&1)?C:0);
        }
    } else if ((op>=0x40 && op<=0x4e) || (op>=0x60 && op<=0x6e)) {
        bool z=old.flags&Z,n=old.flags&N,c=old.flags&C,v=old.flags&V;
        std::array<bool,15> conditions{z,!z,c,!c,n,!n,v,!v,c&&!z,!c||z,n==v,n!=v,!z&&n==v,z||n!=v,true};
        unsigned condition=op-(op>=0x60?0x60:0x40);
        if (conditions[condition]) jump(next+(op>=0x60?signed8(b[1]):signed16(operand)));
    } else if (op==0x56 || op==0x57) {
        int64_t offset=int64_t(old.sp)-2;
        check_stack(offset,old.sp);
        auto a=access(old.ss,offset,2,Access::Write);
        stage_store(a,uint16_t(next),2);s.sp=uint16_t(offset);
        if (next>65535) {
            throw memory_failure(mode()==Mode::Kernel?0:protected_->controls[0],mode(),old.cs,next,1,Access::Fetch,Phase::ResultPC,mode()==Mode::Kernel?2:1,1);
        }
        jump(next+(op==0x56?signed8(b[1]):signed16(operand)));
    } else if (op==0x52) jump(operand);
    else if (op==0x53) {
        if (operand&0xff000000) invalid();
        target_segment=uint16_t(operand>>16); s.cs=target_segment; jump(operand&65535);
    } else if (op==0x54 || op==0x55) {
        bool calling=op==0x54;
        if (calling && (operand&0xff000000)) invalid();
        int64_t offset=int64_t(old.sp)+(calling?-4:0);
        check_stack(offset,calling?old.sp:int64_t(old.sp)+4);
        auto a=access(old.ss,offset,2,calling?Access::Write:Access::Read);
        access(old.ss,offset+2,2,calling?Access::Write:Access::Read); // Both words before interpreting/writing.
        if (calling) {
            if (next>65535) {
                throw memory_failure(mode()==Mode::Kernel?0:protected_->controls[0],mode(),old.cs,next,1,Access::Fetch,Phase::ResultPC,mode()==Mode::Kernel?2:1,1);
            }
            stage_store(a,uint32_t(next)|(uint32_t(old.cs)<<16),4);
            s.sp=uint16_t(offset);
            target_segment=uint16_t(operand>>16); jump(operand&65535);
        } else {
            auto saved_cs=read_word(a+2);
            if (saved_cs>255) invalid();
            target_segment=saved_cs; jump(read_word(a)); s.sp=old.sp+4;
        }
        s.cs=target_segment;
    } else if (op==0x1f) {
        unsigned x=b[1];
        if (x>=0xc0 && x<=0xc7) {
            using detail::KernelImageTransfer;
            using detail::descriptor_image_words;
            const auto base=old.r[4+(x&3)];
            if (x<0xc4) {
                auto source=KernelImageTransfer::preflight(*this,old.ds,base,descriptor_image_words,Access::Read);
                auto image=KernelImageTransfer::snapshot(*this,source);
                std::array<uint16_t,7> d;std::copy_n(image.words.begin()+2,7,d.begin());
                validate_descriptor(image.words[0],image.words[1],d);
                uint16_t key=uint16_t((image.words[0]<<8)|image.words[1]);
                descriptor_target=protected_->descriptors.find(key);
                if (descriptor_target==protected_->descriptors.end()) descriptor_node.emplace(key,d);
                else descriptor_value=d;
            } else {
                KernelImageTransfer::check_range(*this,old.ds,base,descriptor_image_words,Access::Write);
                auto header=KernelImageTransfer::snapshot(*this,KernelImageTransfer::preflight(*this,old.ds,base,2,Access::Read));
                auto destination=KernelImageTransfer::preflight(*this,old.ds,base,descriptor_image_words,Access::Write);
                if (header.words[0]>255 || header.words[1]>255) invalid();
                auto d=descriptor(header.words[0],header.words[1]);
                detail::WordImage image;image.count=descriptor_image_words;
                image.words[0]=header.words[0];image.words[1]=header.words[1];
                std::copy(d.begin(),d.end(),image.words.begin()+2);
                KernelImageTransfer::stage(writes,destination,image);
            }
        } else if (x>=0xc8 && x<=0xcf) {
            if (protected_->frames.empty()) invalid();
            using detail::KernelImageTransfer;
            const auto base=old.r[4+(x&3)];
            if (x<0xcc) {
                auto image=KernelImageTransfer::snapshot(*this,KernelImageTransfer::preflight(*this,old.ds,base,detail::context_image_words,Access::Read));
                auto candidate=protected_->frames.back();
                std::copy_n(image.words.begin(),17,candidate.words.begin());
                std::copy_n(image.words.begin()+17,3,candidate.controls.begin());
                validate_return(candidate,candidate.origin==Mode::User || candidate.bootstrap?Mode::User:Mode::Kernel);
                frame_candidate=candidate;
            } else {
                auto destination=KernelImageTransfer::preflight(*this,old.ds,base,detail::context_image_words,Access::Write);
                detail::WordImage image;image.count=detail::context_image_words;
                const auto& frame=protected_->frames.back();
                std::copy_n(frame.words.begin(),17,image.words.begin());
                std::copy(frame.controls.begin(),frame.controls.end(),image.words.begin()+17);
                KernelImageTransfer::stage(writes,destination,image);
            }
        } else if (x>=0x80 && x<=0xb7) {
            if (x>=0xb0) action=stage_control(b[2],old.r[x&7]);
            else if (x>=0xa0) s.r[x&7]=read_control(b[2]);
            else if (x>=0x90) { if (!context_) invalid(); edit=std::make_pair(unsigned(b[2]),old.r[x&7]); }
            else s.r[x&7]=read_context(b[2]);
        } else if (x<16 || x==0x10 || x==0x11) {
            bool pushing=x<8 || x==0x10;
            int64_t offset=int64_t(old.sp)+(pushing?-2:0);
            check_stack(offset,pushing?old.sp:int64_t(old.sp)+2);
            auto a=access(old.ss,offset,2,pushing?Access::Write:Access::Read);
            if (pushing) { stage_store(a,x==0x10?uint16_t(next):old.r[x],2); s.sp=uint16_t(offset); }
            else { auto value=read_word(a); s.sp=old.sp+2; if (x==0x11) jump(value); else s.r[x-8]=value; }
            if (x==0x10) {
                if (next>65535) {
                    throw memory_failure(mode()==Mode::Kernel?0:protected_->controls[0],mode(),old.cs,next,1,Access::Fetch,Phase::ResultPC,mode()==Mode::Kernel?2:1,1);
                }
                jump(operand);
            }
        } else if (x==0x12) {
            if (!context_) invalid();
            const auto& f=protected_->frames.back();
            s=validate_return(f,(f.origin==Mode::User || f.bootstrap)?Mode::User:Mode::Kernel);
            target=s.pc; target_segment=s.cs; returning=true;
        } else if (x>=0x20 && x<=0x50) {
            if (!context_) invalid();
            if (x<0x40) s.r[b[2]]=(*context_)[x-0x20]; else edit=std::make_pair(x-0x40,old.r[b[2]]);
        } else if (x>=0x60 && x<=0x63) jump(old.r[4+x-0x60]);
    } else throw std::logic_error("Encoding lacks execution semantics");
    // Target validation is last: operand/backing faults take precedence.
    if (target<0 || target>65535) {
        throw memory_failure(mode()==Mode::Kernel?0:protected_->controls[0],mode(),target_segment,target,1,Access::Fetch,Phase::ResultPC,mode()==Mode::Kernel?2:1,1);
    }
    if (explicit_target) access(target_segment,target,1,Access::Fetch,Phase::ControlTarget);
    s.pc=uint16_t(target);
    // All guest-visible effects below are infallible ordinary-RAM commits.
    writes.commit(ram_);
    if (measure_traffic_) traffic_.ram_writes+=writes.size();
    if (descriptor_value) descriptor_target->second=*descriptor_value;
    if (!descriptor_node.empty()) protected_->descriptors.insert(descriptor_node.extract(descriptor_node.begin()));
    if (frame_candidate) { protected_->frames.back()=*frame_candidate; context_=frame_candidate->words; }
    if (edit) write_context(edit->first,edit->second);
    if (action) action();
    regs_=s;
    if (returning) {
        const auto& f=protected_->frames.back();
        protected_->mode=f.origin==Mode::User || f.bootstrap?Mode::User:Mode::Kernel;
        protected_->controls=f.controls; protected_->frames.pop_back();
        context_=protected_->frames.empty()?std::optional<Context>{}:protected_->frames.back().words;
    }
    if (halt) state_=ExecutionState::Halted;
}
}
