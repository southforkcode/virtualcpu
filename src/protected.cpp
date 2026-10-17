#include "cpu.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace vcpu {
namespace {
constexpr unsigned P=1, DEVICE=2, STACK=4;
constexpr uint64_t maximum=std::numeric_limits<uint64_t>::max();
uint32_t upper(const std::array<uint16_t,7>& d) { return d[3]+uint32_t(d[4])*65536; }
Mode destination(const ProtectedFrame& f) { return f.origin==Mode::User || f.bootstrap?Mode::User:Mode::Kernel; }
}
void VirtualCPU::increment(std::string& value) {
    for (size_t i=value.size();i>0;--i) { if (value[i-1]!='9') { ++value[i-1]; return; } value[i-1]='0'; }
    value.insert(value.begin(),'1');
}
unsigned VirtualCPU::backing(uint32_t a) const {
    if (a>=0xff0000 && a<0x1000000) return a<0xff0100?2:0;
    return a<ram_.size() && installed_[a]?1:0;
}
std::array<uint16_t,7> VirtualCPU::descriptor(uint16_t map,uint16_t segment) const {
    auto found=protected_->descriptors.find(uint16_t((map<<8)|segment));
    return found==protected_->descriptors.end()?std::array<uint16_t,7>{}:found->second;
}
std::optional<uint32_t> VirtualCPU::physical_hint(uint16_t segment,int64_t offset) const {
    if (offset<0 || offset>65535) return {};
    auto d=descriptor(mode()==Mode::Kernel?0:protected_->controls[0],segment);
    if (!(d[1]&P)) return {};
    return (uint32_t(d[0])<<16)|uint32_t(offset);
}
Fault VirtualCPU::memory_failure(uint16_t map,Mode mode,uint16_t segment,int64_t offset,unsigned width,
                                Access type,Phase phase,unsigned origin,unsigned reason,
                                std::optional<uint32_t> physical,std::optional<uint16_t> relevant) const {
    (void)mode;
    Fault f{Cause::Memory,physical};
    if (!relevant && width && offset>=0 && offset<=65535) relevant=uint16_t(offset);
    uint32_t widened=uint32_t(offset);
    f.memory={uint16_t(origin),uint16_t(phase),uint16_t(type),map,segment,uint16_t(widened),uint16_t(widened>>16),
              uint16_t(relevant?*relevant:0),uint16_t(relevant?0x100|segment:0),uint16_t(width),uint16_t(reason)};
    return f;
}
uint32_t VirtualCPU::translate(uint16_t map,Mode access_mode,uint16_t segment,int64_t offset,unsigned width,
                               Access type,Phase phase,unsigned origin,bool mmio) const {
    auto fail=[&](unsigned reason,std::optional<uint32_t> physical={},std::optional<uint16_t> relevant={}) {
        throw memory_failure(map,access_mode,segment,offset,width,type,phase,origin,reason,physical,relevant);
    };
    if (offset<0 || offset>65535 || offset+width>65536) fail(1);
    if (origin==3 && map==0) fail(2);
    if (map>255 || !protected_->published[map] || (access_mode==Mode::User && map==0)) fail(3);
    auto d=descriptor(map,segment);
    if (!(d[1]&P)) fail(4);
    uint32_t a=(uint32_t(d[0])<<16)|uint32_t(offset);
    if (offset<d[2] || offset+width>upper(d)) fail(5,a);
    unsigned right=(access_mode==Mode::Kernel?6:3)+(type==Access::Fetch?2:type==Access::Write?1:0);
    if (!(d[1]&(1u<<right))) fail(6,a);
    if ((!mmio && (d[1]&DEVICE)) || (origin==3 && (d[1]&DEVICE))) fail(7,a);
    for (unsigned i=0;i<width;++i) {
        unsigned kind=backing(a+i);
        if (kind && kind!=((d[1]&DEVICE)?2u:1u)) fail(7,a+i,uint16_t(offset+i));
    }
    for (unsigned i=0;i<width;++i) if (!backing(a+i)) fail(8,a+i,uint16_t(offset+i));
    return a;
}
void VirtualCPU::select_segment(uint16_t map,Mode access_mode,uint16_t segment,bool stack,Phase phase) const {
    auto d=descriptor(map,segment); unsigned origin=access_mode==Mode::User?1:2;
    auto fail=[&](unsigned reason) { throw memory_failure(map,access_mode,segment,0,0,Access::None,phase,origin,reason); };
    if (!(d[1]&P)) fail(4);
    unsigned rights=access_mode==Mode::User?0x18:0xc0;
    if (stack ? ((d[1]&rights)!=rights || (access_mode==Mode::User && !(d[1]&STACK))) : !(d[1]&rights)) fail(6);
    if (d[1]&DEVICE) fail(7);
}
void VirtualCPU::validate_entry(const std::array<uint16_t,5>& e) const {
    if (!(e[2]<=e[4] && e[4]<=e[3] && unsigned(e[4])>=unsigned(e[2])+2))
        throw Fault{Cause::Stack, e[4]>=2?std::optional<uint32_t>((uint32_t(descriptor(0,e[1])[0])<<16)|(e[4]-2)):std::nullopt};
    select_segment(0,Mode::Kernel,e[0],false,Phase::Operand);
    for (unsigned offset=e[4]-2;offset<e[3];++offset) {
        translate(0,Mode::Kernel,e[1],offset,1,Access::Read,Phase::Operand,2);
        translate(0,Mode::Kernel,e[1],offset,1,Access::Write,Phase::Operand,2);
    }
}
Registers VirtualCPU::validate_return(const ProtectedFrame& frame,Mode target_mode) const {
    const auto& x=frame.words; const auto& control=frame.controls;
    auto invalid=[&] { throw Fault{Cause::Invalid,physical_hint(regs_.cs,regs_.pc)}; };
    if (x[9]>255 || x[14]>255 || x[16]>255 || (x[10]&~0x2f) ||
        control[0]>255 || control[1]>255 || control[2]>255) invalid();
    if ((target_mode==Mode::User && control[0]==0) || !protected_->published[control[0]]) invalid();
    if ((control[1] && !protected_->published[control[1]]) ||
        (target_mode==Mode::User && (control[1] || control[2])) || (target_mode==Mode::User && !(x[10]&32))) invalid();
    if (!(x[12]<=x[11] && x[11]<=x[13])) {
        auto d=descriptor(target_mode==Mode::User?control[0]:0,x[14]);
        throw Fault{Cause::Stack,(d[1]&P)?std::optional<uint32_t>((uint32_t(d[0])<<16)|x[11]):std::nullopt};
    }
    uint16_t map=target_mode==Mode::User?control[0]:0;
    if (target_mode==Mode::User) {
        select_segment(map,target_mode,x[16],false,Phase::ReturnState);
        select_segment(map,target_mode,x[14],true,Phase::ReturnState);
        auto d=descriptor(map,x[14]);
        if (x[12]!=d[5] || x[13]!=d[6] || x[12]<d[2] || x[13]>upper(d))
            throw Fault{Cause::Stack,std::optional<uint32_t>((uint32_t(d[0])<<16)|x[11])};
    }
    translate(map,target_mode,x[9],x[8],1,Access::Fetch,Phase::ReturnTarget,target_mode==Mode::User?1:2);
    Registers r; std::copy_n(x.begin(),8,r.r.begin()); r.pc=x[8]; r.cs=x[9]; r.flags=x[10];
    r.sp=x[11]; r.sb=x[12]; r.st=x[13]; r.ss=x[14]; r.dp=x[15]; r.ds=x[16]; return r;
}
void VirtualCPU::enter_protected(const Fault& event,uint16_t pc,uint8_t service,uint16_t source) {
    auto& p=*protected_; auto before=regs_; auto controls=p.controls; auto before_mode=p.mode;
    std::string stage="reserve"; bool captured=false;
    auto terminal=[&](const Fault& failure,const std::string& reason) {
        if (!terminal_) terminal_=TerminalDiagnostic{reason,stage,failure,event.cause,pc,service,source,p.frames.size(),captured,before,before_mode,controls,p.pending,p.in_service};
        diagnostic_=failure; state_=ExecutionState::TerminalDoubleFault;
    };
    if (p.frames.size()==8) { terminal(Fault{Cause::Invalid,{}},"capacity"); return; }
    ProtectedFrame frame; auto& x=frame.words;
    std::copy(regs_.r.begin(),regs_.r.end(),x.begin());
    x[8]=pc; x[9]=regs_.cs; x[10]=regs_.flags; x[11]=regs_.sp; x[12]=regs_.sb; x[13]=regs_.st;
    x[14]=regs_.ss; x[15]=regs_.dp; x[16]=regs_.ds; x[17]=uint16_t(event.cause);
    x[18]=event.address?uint16_t(*event.address):0; x[19]=event.address?uint16_t(0x100|(*event.address>>16)):0;
    x[20]=event.cause==Cause::Trap?service:0; frame.origin=p.mode; frame.controls=p.controls;
    frame.prior_depth=uint16_t(p.frames.size()); frame.memory=event.memory; frame.privilege=event.privilege;
    frame.source=event.cause==Cause::IRQ?source:0;
    p.frames.push_back(frame); context_=frame.words; captured=true;
    try {
        stage="vector_read"; uint32_t target=0;
        for (unsigned i=0;i<4;++i) target|=uint32_t(read(translate(0,Mode::Kernel,0,4*unsigned(event.cause)+i,1,Access::Read,Phase::Operand,2)))<<(8*i);
        stage="vector_format"; if (target&0xff000000)
            throw Fault{Cause::Invalid,(uint32_t(descriptor(0,0)[0])<<16)|uint32_t(4*unsigned(event.cause))};
        stage="handler"; translate(0,Mode::Kernel,uint16_t(target>>16),target&65535,1,Access::Fetch,Phase::ControlTarget,2);
        stage="DS"; if (!p.entry_configured) throw Fault{Cause::Invalid,{}};
        select_segment(0,Mode::Kernel,p.entry[0],false,Phase::Operand);
        auto selected=p.entry;
        if (p.mode==Mode::Kernel) selected={p.entry[0],regs_.ss,regs_.sb,regs_.st,regs_.sp};
        stage="stack"; validate_entry(selected);
        auto next=regs_; next.cs=uint16_t(target>>16); next.pc=uint16_t(target); next.ds=selected[0];
        next.ss=selected[1]; next.sb=selected[2]; next.st=selected[3]; next.sp=selected[4]; next.flags&=~32;
        regs_=next; p.mode=Mode::Kernel; state_=ExecutionState::Running;
        if (event.cause==Cause::IRQ) { p.pending&=uint16_t(~(1u<<source)); p.in_service|=uint16_t(1u<<source); }
    } catch (const Fault& f) { terminal(f,"entry_failure"); }
}
uint16_t VirtualCPU::read_context(unsigned index) const {
    if (!context_) throw Fault{Cause::Invalid,physical_hint(regs_.cs,regs_.pc)};
    if (index<21) return (*context_)[index];
    const auto& f=protected_->frames.back();
    if (index>=0x20 && index<=0x22) return f.controls[index-0x20];
    switch (index) {
    case 0x23:return uint16_t(f.origin); case 0x24:return uint16_t(destination(f));
    case 0x25:return f.prior_depth; case 0x26:return f.prior_depth+1; case 0x27:return f.bootstrap;
    case 0x28:return f.source; case 0x29:return f.privilege;
    default:return f.memory.at(index-0x30);
    }
}
void VirtualCPU::write_context(unsigned index,uint16_t value) {
    if (index<21) { (*context_)[index]=value; protected_->frames.back().words[index]=value; }
    else protected_->frames.back().controls.at(index-0x20)=value;
}
uint16_t VirtualCPU::read_control(uint8_t id) const {
    const auto& p=*protected_;
    if (id<3) return p.controls[id];
    if (id==3) return uint16_t(p.mode);
    if (id==4) return uint16_t(p.frames.size());
    if (id==5) return p.entry_configured;
    if (id>=0x10 && id<=0x14) return p.entry_shadow[id-0x10];
    if (id>=0x18 && id<=0x1c) return p.entry[id-0x18];
    if (id==0x20) return p.desc_map; if (id==0x21) return p.desc_segment;
    if (id>=0x22 && id<=0x28) return p.shadow[id-0x22];
    if (id==0x29) return p.published[p.desc_map];
    if (id>=0x30 && id<=0x36) return descriptor(p.desc_map,p.desc_segment)[id-0x30];
    if (id==0x40) return uint16_t(p.query_address); if (id==0x41) return uint16_t(p.query_address>>16);
    if (id==0x42) return p.query_kind; if (id==0x43) return uint16_t(p.query_run);
    if (id==0x44) return uint16_t(p.query_run>>16); if (id==0x45) return p.query_valid;
    throw Fault{Cause::Illegal,physical_hint(regs_.cs,regs_.pc)};
}
std::function<void()> VirtualCPU::stage_control(uint8_t id,uint16_t value) {
    auto& p=*protected_; auto invalid=[&] { throw Fault{Cause::Invalid,physical_hint(regs_.cs,regs_.pc)}; };
    if (id<3 || id==0x20 || id==0x21 || id==0x41 || id==0x10 || id==0x11 || id==0x22) if (value>255) invalid();
    if (id==0 && value && !p.published[value]) invalid();
    if (id==0x23 && (value&~0x1ff)) invalid();
    if (id==0x26 && value>1) invalid();
    if (id!=0x7f) return [this,id,value] {
        auto& s=*protected_;
        if (id<3) { s.controls[id]=value; if (id==1 && !value) s.controls[2]=0; }
        else if (id>=0x10 && id<=0x14) s.entry_shadow[id-0x10]=value;
        else if (id==0x20) s.desc_map=value; else if (id==0x21) s.desc_segment=value;
        else if (id>=0x22 && id<=0x28) s.shadow[id-0x22]=value;
        else if (id==0x40) s.query_address=(s.query_address&0xff0000)|value;
        else if (id==0x41) s.query_address=(s.query_address&65535)|(uint32_t(value)<<16);
    };
    if (value<1 || value>8) invalid();
    if ((value==3 || value==4) && p.desc_map==0) invalid();
    auto d=p.shadow; uint16_t key=uint16_t((p.desc_map<<8)|p.desc_segment);
    if (value==1) {
        if (d[0]>255 || (d[1]&~0x1ff) || d[4]>1) invalid();
        if (!(d[1]&P)) { if (std::any_of(d.begin(),d.end(),[](auto v){return v!=0;})) invalid(); }
        else {
            if (upper(d)>65536 || d[2]>upper(d)) invalid();
            if ((p.desc_map==0 && (d[1]&0x38)) || ((d[1]&DEVICE) && (d[1]&0x13c))) invalid();
            if (!(d[1]&STACK)) { if (d[5] || d[6]) invalid(); }
            else if (!(d[2]<=d[5] && d[5]<=d[6] && d[6]<=upper(d))) invalid();
        }
    }
    if (value==5) validate_entry(p.entry_shadow);
    if (value==7) {
        if ((regs_.flags&32) || p.frames.size()!=1 || p.frames.back().origin!=Mode::Kernel || p.frames.back().bootstrap || !p.entry_configured) invalid();
        validate_return(p.frames.back(),Mode::User); validate_entry(p.entry);
    }
    uint32_t run=0; unsigned kind=0;
    if (value==8) { kind=backing(p.query_address); for (uint32_t a=p.query_address;a<0x1000000 && backing(a)==kind;++a) ++run; }
    return [this,value,d,key,run,kind] {
        auto& s=*protected_;
        if (value==1) s.descriptors[key]=d;
        if (value==2) s.shadow=descriptor(s.desc_map,s.desc_segment);
        if (value==3) s.published.set(s.desc_map);
        if (value==4) {
            s.published.reset(s.desc_map);
            auto it=s.descriptors.lower_bound(uint16_t(s.desc_map<<8));
            while (it!=s.descriptors.end() && (it->first>>8)==s.desc_map) it=s.descriptors.erase(it);
            s.shadow.fill(0);
        }
        if (value==5) { s.entry=s.entry_shadow; s.entry_configured=true; }
        if (value==6) s.entry_shadow=s.entry;
        if (value==7) s.frames.back().bootstrap=true;
        if (value==8) { s.query_run=run; s.query_kind=uint16_t(kind); s.query_valid=true; }
    };
}
void VirtualCPU::timer_due() { evaluate_timer(*protected_); }
void VirtualCPU::evaluate_timer(ProtectedState& p) {
    if (p.timer_mode && p.time>=p.deadline) {
        p.pending|=1;
        if (p.timer_mode==1) p.timer_mode=0;
        else {
            uint64_t elapsed=p.time-p.deadline, periods=elapsed/p.period;
            // Check the quotient before adding one and before multiplying.
            if (periods==maximum || periods>=(maximum-p.deadline)/p.period) {
                p.deadline_overflow=true; p.timer_mode=0;
            } else p.deadline+=(periods+1)*p.period;
        }
    }
    if (p.time==maximum) p.timer_mode=0;
}
uint16_t VirtualCPU::mmio_read(uint32_t address) const {
    const auto& p=*protected_; unsigned o=address-0xff0000;
    auto invalid=[&]() -> uint16_t { throw Fault{Cause::Memory,address}; };
    if (profile_==MachineProfile::ConsoleV1) {
        if (o==0x80) return p.console_rx.empty()?0:p.console_rx.front();
        if (o==0x84) return (!p.console_rx.empty()?1:0)|(p.console_tx.size()<65536?2:0);
        if (o==0x86) return uint16_t(p.console_rx.size());
    }
    switch (o) {
    case 0:return p.pending; case 2:return p.enable; case 4:return p.in_service; case 8:return p.levels; case 10:return 7;
    case 0x22:return (p.snapshot_valid?1:0)|(p.time==maximum?2:0)|(p.deadline_overflow?4:0);
    case 0x24:return p.shadow_mode; case 0x26:return p.timer_mode;
    }
    if (o&1) return invalid();
    if (o>=0x30 && o<=0x36) return uint16_t(p.shadow_deadline>>(8*(o-0x30)));
    if (o>=0x38 && o<=0x3e) return uint16_t(p.shadow_period>>(8*(o-0x38)));
    if (o>=0x40 && o<=0x46) return uint16_t(p.snapshot>>(8*(o-0x40)));
    if (o>=0x48 && o<=0x4e) return uint16_t(p.deadline>>(8*(o-0x48)));
    if (o>=0x50 && o<=0x56) return uint16_t(p.period>>(8*(o-0x50)));
    return invalid();
}
void VirtualCPU::commit_mmio_read(uint32_t address) {
    auto& p=*protected_;
    if (profile_==MachineProfile::ConsoleV1 && address==0xff0080 && !p.console_rx.empty()) {
        p.console_rx.erase(p.console_rx.begin());
        if (p.console_rx.empty()) p.levels&=uint16_t(~4u);
    }
}
std::function<void()> VirtualCPU::stage_mmio(uint32_t address,uint16_t value) {
    const auto& p=*protected_; unsigned o=address-0xff0000;
    auto invalid=[&] { throw Fault{Cause::Memory,address}; };
    if (o&1) invalid();
    if (profile_==MachineProfile::ConsoleV1 && o==0x82) {
        if (value>255 || p.console_tx.size()>=65536) invalid();
        return [this,value]{protected_->console_tx.push_back(uint8_t(value));};
    }
    if (o==2 || o==6) return [this,o,value] { if (o==2) protected_->enable=value; else protected_->in_service&=uint16_t(~value); };
    if (o==0x24) { if (value!=0 && value!=1 && value!=3) invalid(); return [this,value]{protected_->shadow_mode=value;}; }
    if (o>=0x30 && o<=0x3e) return [this,o,value] {
        auto& target=o<0x38?protected_->shadow_deadline:protected_->shadow_period;
        unsigned shift=8*(o-(o<0x38?0x30:0x38)); target=(target&~(uint64_t(65535)<<shift))|(uint64_t(value)<<shift);
    };
    if (o!=0x20 || value<1 || value>4) invalid();
    if (value==2 && ((!p.shadow_mode && (p.shadow_deadline || p.shadow_period)) ||
        (p.shadow_mode && p.shadow_deadline<=p.time) || (p.shadow_mode==1 && p.shadow_period) ||
        (p.shadow_mode==3 && !p.shadow_period))) invalid();
    return [this,value] {
        auto& s=*protected_;
        if (value==1) { s.snapshot=s.time; s.snapshot_valid=true; }
        if (value==2) { s.timer_mode=s.shadow_mode; s.deadline=s.shadow_deadline; s.period=s.shadow_period; }
        if (value==3) s.timer_mode=0;
        if (value==4) s.deadline_overflow=false;
    };
}
}
