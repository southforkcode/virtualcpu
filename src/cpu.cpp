#include "cpu.hpp"
#include "decode_table.hpp"
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
VirtualCPU::VirtualCPU(Revision revision, size_t ram_bytes)
    : revision_(revision), ram_(ram_bytes), installed_(ram_bytes,1) {
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
    return ram_[a];
}
uint32_t VirtualCPU::access(uint16_t segment, int64_t offset, unsigned width) const {
    auto a=address(segment,offset);
    if (!a || offset+width>65536) throw Fault{Cause::Memory,a};
    for (unsigned i=0;i<width;++i) read(*a+i);
    return *a;
}
uint32_t VirtualCPU::vector(unsigned cause) const {
    uint32_t packed=0;
    for (unsigned i=0;i<4;++i) packed |= uint32_t(read(4*cause+i))<<(8*i);
    if (packed&0xff000000) throw Fault{Cause::Invalid,4*cause};
    read(packed);
    return packed;
}
void VirtualCPU::reset() {
    regs_=Registers{}; context_.reset(); diagnostic_.reset(); pending_=false;
    state_=ExecutionState::Running;
    try {
        auto t=vector(0); regs_.cs=uint16_t(t>>16); regs_.pc=uint16_t(t);
    } catch (Fault f) { diagnostic_=f; state_=ExecutionState::TerminalResetFault; }
}
void VirtualCPU::enter(Cause cause, uint16_t pc, std::optional<uint32_t> a, uint8_t service) {
    Context x{};
    std::copy(regs_.r.begin(),regs_.r.end(),x.begin());
    x[8]=pc; x[9]=regs_.cs; x[10]=regs_.flags; x[11]=regs_.sp;
    x[12]=regs_.sb; x[13]=regs_.st; x[14]=regs_.ss; x[15]=regs_.dp; x[16]=regs_.ds;
    x[17]=uint16_t(cause); x[18]=a?uint16_t(*a):0;
    x[19]=a?uint16_t(0x100|(*a>>16)):0; x[20]=service;
    context_=x; regs_.flags &= ~I; state_=ExecutionState::Running;
    try {
        auto t=vector(unsigned(cause)); regs_.cs=uint16_t(t>>16); regs_.pc=uint16_t(t);
        if (cause==Cause::IRQ) pending_=false;
    } catch (Fault f) { diagnostic_=f; state_=ExecutionState::TerminalDoubleFault; }
}
void VirtualCPU::fault(Fault f) {
    if (context_) { diagnostic_=f; state_=ExecutionState::TerminalDoubleFault; }
    else enter(f.cause,regs_.pc,f.address);
}
bool VirtualCPU::step() {
    if (state_==ExecutionState::TerminalDoubleFault || state_==ExecutionState::TerminalResetFault) return false;
    if (pending_ && (regs_.flags&I) && !context_) { enter(Cause::IRQ,regs_.pc); return true; }
    if (state_==ExecutionState::Halted) return false;
    try { execute(); } catch (Fault f) { fault(f); }
    return true;
}
void VirtualCPU::execute() {
    const auto& old=regs_;
    auto s=old; // Staged ordinary state; a fault snapshots regs_, not this copy.
    auto ia=address(old.cs,old.pc);
    auto invalid=[&] { throw Fault{Cause::Invalid,ia}; };
    auto illegal=[&] { throw Fault{Cause::Illegal,ia}; };
    std::array<uint8_t,5> b{};
    unsigned fetched=0;
    auto fetch=[&] {
        if (unsigned(old.pc)+fetched>65535) throw Fault{Cause::Memory,{}};
        b[fetched]=read((uint32_t(old.cs)<<16)|(unsigned(old.pc)+fetched));
        return b[fetched++];
    };
    // Fetch/decode never advances live PC. Reserved descriptors fail as read.
    auto op=fetch();
    auto e=encoding::tables[0][op];
    if (e.dispatch) e=encoding::tables[e.dispatch][fetch()];
    if (!e.length || (revision_==Revision::V04 && e.min_revision>4)) illegal();
    while (fetched<e.length) {
        auto i=fetched; fetch();
        if ((b[i]&e.masks[i])!=e.expected[i]) illegal();
    }
    uint32_t operand=0;
    unsigned start=(op==0x15 || op==0x1f)?2:1;
    for (unsigned j=start;j<e.length;++j) operand |= uint32_t(b[j])<<(8*(j-start));
    int64_t next=unsigned(old.pc)+e.length, target=next;
    uint16_t target_segment=old.cs;
    bool explicit_target=false, halt=false, returning=false;
    std::optional<std::pair<uint32_t,uint16_t>> write;
    unsigned write_width=0;
    std::optional<std::pair<unsigned,uint16_t>> edit;
    auto read_word=[&](uint32_t a) { return uint16_t(ram_[a]|uint16_t(ram_[a+1])<<8); };
    auto stage_store=[&](uint32_t a,uint16_t value,unsigned width) { write=std::make_pair(a,value); write_width=width; };
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
                    if (!(old.sb<=v && v<=old.st)) throw Fault{Cause::Stack,address(old.ss,v)};
                    s.sp=v;
                } else if (dst==1) s.sb=v;
                else if (dst==2) s.dp=v;
                else { if (v&~0x6f) invalid(); s.flags=v&0x2f; }
            } else if (kind==5) {
                if (dst<3 && v>255) invalid();
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
        if (!(old.sb<=old.sp && old.sp<=old.st)) throw Fault{Cause::Stack,address(old.ss,old.sp)};
        int32_t value=old.sp+signed8(b[1]);
        if (value<old.sb || value>old.st) throw Fault{Cause::Stack,address(old.ss,value)};
        s.sp=uint16_t(value);
    } else if (op==0x26) {
        if (context_) invalid();
        if (next>65535) throw Fault{Cause::Memory,{}};
        enter(Cause::Trap,uint16_t(next),{},b[1]); return;
    } else if (op==0x15) {
        unsigned x=b[1], reg=x&7, width=(x&8)?1:2, mode=(x>>4)&3;
        bool store=x&64;
        if (!(x&128) && mode==0) s.r[reg]=uint16_t(operand);
        else {
            int64_t offset; uint16_t segment=old.ds;
            if (x&128) {
                offset=old.r[4+(b[2]&3)]+((x&16)?signed16(b[3]|uint32_t(b[4])<<8):0);
                if (x&32) segment=old.cs;
            } else if (mode==1) offset=operand;
            else if (mode==2) offset=old.dp+signed16(operand);
            else { offset=int64_t(old.sp)+operand; segment=old.ss; stack(old,offset,offset+width); }
            auto a=access(segment,offset,width);
            if (store) stage_store(a,old.r[reg],width);
            else s.r[reg]=width==1?ram_[a]:read_word(a);
        }
    } else if (op==0x1a) {
        unsigned x=b[1], dst=x&3; auto a=old.r[dst];
        if (x<16) arith(8,a,old.r[(x>>2)&3]);
        else {
            unsigned kind=x>>4;
            uint16_t value=kind==1?uint16_t(a<<1):kind==2?a>>1:(a>>1)|(a&0x8000);
            s.r[dst]=value; s.flags=(old.flags&~7)|nz(value)|((kind==1?a&0x8000:a&1)?C:0);
        }
    } else if (op>=0x40 && op<=0x4e) {
        bool z=old.flags&Z,n=old.flags&N,c=old.flags&C,v=old.flags&V;
        std::array<bool,15> conditions{z,!z,c,!c,n,!n,v,!v,c&&!z,!c||z,n==v,n!=v,!z&&n==v,z||n!=v,true};
        if (conditions[op-0x40]) jump(next+signed16(operand));
    } else if (op==0x52) jump(operand);
    else if (op==0x53) {
        if (operand&0xff000000) invalid();
        target_segment=uint16_t(operand>>16); s.cs=target_segment; jump(operand&65535);
    } else if (op==0x1f) {
        unsigned x=b[1];
        if (x<16 || x==0x10 || x==0x11) {
            bool pushing=x<8 || x==0x10;
            int64_t offset=int64_t(old.sp)+(pushing?-2:0);
            stack(old,offset,pushing?old.sp:int64_t(old.sp)+2);
            auto a=access(old.ss,offset,2);
            if (pushing) { stage_store(a,x==0x10?uint16_t(next):old.r[x],2); s.sp=uint16_t(offset); }
            else { auto value=read_word(a); s.sp=old.sp+2; if (x==0x11) jump(value); else s.r[x-8]=value; }
            if (x==0x10) {
                if (next>65535) throw Fault{Cause::Memory,{}};
                jump(operand);
            }
        } else if (x==0x12) {
            if (!context_) invalid();
            const auto& a=*context_;
            if (a[9]>255 || a[14]>255 || a[16]>255 || (a[10]&~0x2f)) invalid();
            if (!(a[12]<=a[11] && a[11]<=a[13])) throw Fault{Cause::Stack,address(a[14],a[11])};
            std::copy_n(a.begin(),8,s.r.begin());
            s.pc=a[8]; s.cs=a[9]; s.flags=a[10]; s.sp=a[11]; s.sb=a[12]; s.st=a[13];
            s.ss=a[14]; s.dp=a[15]; s.ds=a[16]; target_segment=s.cs; jump(s.pc); returning=true;
        } else if (x>=0x20 && x<=0x50) {
            if (!context_) invalid();
            if (x<0x40) s.r[b[2]]=(*context_)[x-0x20]; else edit=std::make_pair(x-0x40,old.r[b[2]]);
        } else if (x>=0x60 && x<=0x63) jump(old.r[4+x-0x60]);
    } else throw std::logic_error("Encoding lacks execution semantics");
    // Target validation is last: operand/backing faults take precedence.
    if (target<0 || target>65535) throw Fault{Cause::Memory,{}};
    if (explicit_target) access(target_segment,target,1);
    s.pc=uint16_t(target);
    // All guest-visible effects below are infallible ordinary-RAM commits.
    if (write) for (unsigned i=0;i<write_width;++i) ram_[write->first+i]=uint8_t(write->second>>(8*i));
    if (edit) (*context_)[edit->first]=edit->second;
    regs_=s;
    if (returning) context_.reset();
    if (halt) state_=ExecutionState::Halted;
}
}
