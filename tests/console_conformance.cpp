#include "console_guest.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <tuple>
using namespace vcpu;
namespace {
size_t checks=0;
void check(bool b,const std::string& s) { ++checks; if (!b) throw std::runtime_error(s); }
const ProtectedState& ps(const VirtualCPU& c) { return *c.protected_state(); }
unsigned word(const VirtualCPU& c,unsigned a) { return console_guest::word(c,a); }
std::string record(const VirtualCPU& c,const std::string& payload) {
    return "{\"run\":\""+c.run()+"\",\"boundary\":\""+c.boundary()+"\",\"sequence\":\""+c.next_sequence()+"\","+payload+"}";
}
VirtualCPU device(MachineProfile profile=MachineProfile::ConsoleV1) {
    VirtualCPU c(0x80000,profile);
    for(unsigned i=0;i<8;++i) c.load(4*i,{0,1,0,0});
    c.load(0x100,{0}); c.reset();
    for(unsigned i=0;i<6;++i) {
        c.registers().r[7]=i==5?5:i>=3?0xfffe:0;
        c.load(0x700,{0x1f,0xb7,uint8_t(i==5?0x7f:0x10+i)});c.registers().pc=0x700;c.step();
    }
    return c;
}
void instruction(VirtualCPU& c,const std::vector<uint8_t>& code,unsigned pc=0x700) {
    c.load(pc,code); c.registers().pc=pc; check(c.step(),"device instruction progress");
}
unsigned read(VirtualCPU& c,unsigned o,unsigned width=2,unsigned pc=0x700) {
    c.registers().ds=255; instruction(c,{0x15,uint8_t(width==2?0x10:0x18),uint8_t(o),0},pc); return c.registers().r[0];
}
void write(VirtualCPU& c,unsigned o,unsigned v,unsigned width=2,unsigned pc=0x700) {
    c.registers().ds=255; c.registers().r[0]=v;
    instruction(c,{0x15,uint8_t(width==2?0x50:0x58),uint8_t(o),0},pc);
}
bool faulted(const VirtualCPU& c) {return !ps(c).frames.empty();}
void devices() {
    auto minimal=device(MachineProfile::MinimalV1);
    check(minimal.apply_boundary_json(console_guest::input(minimal,{1}))==HostError::BadFormat,"version-one JSON retains unknown-kind rejection");
    BoundaryEvent unsupported{minimal.run(),minimal.boundary(),minimal.next_sequence(),BoundaryEvent::Kind::ConsoleInput};unsupported.data={1};
    check(minimal.apply_boundary_events({unsupported})==HostError::BadProfile,"typed console input requires console profile");
    for(unsigned offset:{0x80u,0x82u,0x84u,0x86u}) {
        auto c=minimal; read(c,offset); check(c.context() && (*c.context())[17]==unsigned(Cause::Memory) && ps(c).device_commits==0,"minimal reserved console offsets remain illegal");
    }
    auto c=device();
    check(read(c,0x84)==2 && read(c,0x86)==0 && read(c,0x80)==0,"empty console status/count/read");
    check(c.apply_boundary_json(console_guest::input(c,{0,255,0x80}))==HostError::None,"binary console input");
    check(ps(c).levels==4 && ps(c).pending==4 && read(c,0x84)==3 && read(c,0x86)==3,"RX level and count");
    check(read(c,0x80)==0 && read(c,0x80)==255 && read(c,0x80)==128 && ps(c).console_rx.empty() && ps(c).levels==0 && ps(c).pending==4,"zero-extended destructive RX, sticky pending");
    write(c,0x82,0); write(c,0x82,255); check(ps(c).console_tx==std::vector<uint8_t>({0,255}),"TX captures binary bytes");
    for(unsigned o=0x80;o<0x90;++o) for(unsigned width:{1u,2u}) for(bool store:{false,true}) {
        auto d=device(); check(d.apply_boundary_json(console_guest::input(d,{0x55}))==HostError::None,"fixture input");
        bool permitted=width==2 && (store?o==0x82:o==0x80 || o==0x84 || o==0x86);
        if(store) write(d,o,0x41,width); else read(d,o,width);
        check(ps(d).device_commits==(permitted?1u:0u),"console direction/width/alignment commit count");
        check(faulted(d)!=permitted,"console reserved/direction/width/alignment fault");
        if(!permitted) check(ps(d).frames.back().words[17]==2 && ps(d).frames.back().memory[10]==7,"console transaction faults report Memory/DEVICE");
        if(!permitted) check(ps(d).console_rx==std::vector<uint8_t>({0x55}) && ps(d).console_tx.empty(),"rejected transactions have no console effect");
    }
    auto d=device(); write(d,0x82,256); check(ps(d).device_commits==0 && ps(d).console_tx.empty() && faulted(d),"TX upper-byte rejection");
    for(bool store:{false,true}) {
        d=device(); check(d.apply_boundary_json(console_guest::input(d,{7}))==HostError::None,"precise read fixture");
        if(store) write(d,0x82,65,2,0xfffc); else read(d,0x80,2,0xfffc);
        check(faulted(d) && ps(d).console_rx==std::vector<uint8_t>({7}) && ps(d).console_tx.empty() && ps(d).device_commits==0,"result PC failure rolls back console transaction");
    }
    d=device(); for(unsigned i=0;i<65536;++i) write(d,0x82,i&255);
    check(read(d,0x84)==0 && ps(d).console_tx.size()==65536,"bounded TX capture reports full");
    auto commits=ps(d).device_commits; write(d,0x82,1);
    check(faulted(d) && ps(d).console_tx.size()==65536 && ps(d).device_commits==commits,"full TX rejects atomically");
    for(const std::string& data:std::vector<std::string>{"", "0", "0G", "AA", std::string(130,'0')}) {
        auto line=record(c,"\"kind\":\"console_rx\",\"data\":\""+data+"\"");
        auto seq=c.next_sequence(); check(c.apply_boundary_json(line)==HostError::BadFormat && c.next_sequence()==seq,"strict lowercase bounded hex payload");
    }
    check(c.apply_boundary_json(record(c,"\"kind\":\"console_rx\",\"data\":[1]"))==HostError::BadFormat,"console payload requires string");
    check(c.apply_boundary_json(record(c,"\"kind\":\"console_rx\",\"data\":\"01\",\"source\":2"))==HostError::BadFormat,"console shape rejects extras");
    check(c.apply_boundary_json(record(c,"\"kind\":\"irq_level\",\"source\":2,\"asserted\":true"))==HostError::BadSource,"device owns source two");
    auto first=console_guest::input(c,{7}); auto seq=c.next_sequence();
    check(c.apply_boundary_json(first+"\n{bad")==HostError::BadFormat && ps(c).console_rx.empty() && c.next_sequence()==seq,"malformed later record rolls back console batch");
    BoundaryEvent input{c.run(),c.boundary(),c.next_sequence(),BoundaryEvent::Kind::ConsoleInput}; input.data.resize(64,9);
    auto extra=input; extra.sequence=std::to_string(std::stoull(input.sequence)+1); extra.data={1};
    check(c.apply_boundary_events({input,extra})==HostError::BadState && ps(c).console_rx.empty() && c.next_sequence()==seq,"capacity overflow rejects whole batch");
    check(c.apply_boundary_events({input})==HostError::None && ps(c).console_rx.size()==64,"exact RX bound");
    check(c.apply_boundary_json(console_guest::input(c,{1}))==HostError::BadState && ps(c).console_rx.size()==64,"full RX neither overwrites nor silently drops");
    extra.run="999"; extra.sequence="999";
    check(c.apply_boundary_events({extra})==HostError::WrongBoundary,"boundary beats sequence/capacity errors");
    extra.run=c.run(); extra.sequence="999";
    check(c.apply_boundary_events({extra})==HostError::BadSequence,"sequence beats capacity errors");
    auto old=console_guest::input(c,{1});
    check(c.apply_boundary_json(record(c,"\"kind\":\"reset\""))==HostError::None && ps(c).console_rx.empty() && ps(c).console_tx.empty() && ps(c).levels==0,"recorded reset discards queues/output/level");
    check(c.apply_boundary_json(old)==HostError::WrongBoundary,"old run input cannot enter new run");
    d=device(); d.load(0x100,{1}); d.reset(); d.step();
    check(d.state()==ExecutionState::Halted && d.apply_boundary_json(console_guest::input(d,{1}))==HostError::None,"halted console input accepted");
    VirtualCPU terminal(1,MachineProfile::ConsoleV1); terminal.reset();
    check(terminal.apply_boundary_json(console_guest::input(terminal,{1}))==HostError::BadState,"terminal console input rejected");
    // Same backing artifacts as minimal-v1: only explicit version/profile differ.
    VirtualCPU artifact(0x20000,MachineProfile::ConsoleV1);
    artifact.load(0,{0,1,0,0}); artifact.load(0x100,{1}); artifact.reset();
    std::vector<uint8_t> ram(1u<<24),backing(1u<<24); ram[1]=1; ram[0x100]=1;
    std::fill(backing.begin(),backing.begin()+0x20000,1); std::fill(backing.begin()+0xff0000,backing.begin()+0xff0100,2);
    std::string header="{\"format\":\"vcpu-replay\",\"version\":2,\"profile\":\"console-v1\",\"run\":\"0\","
        "\"ram_image_sha256\":\"4bf9f807b3a30d1c0f9406c25ffeca0d19700c1194d0ac0fe67f83c9bb8cb03c\","
        "\"backing_map_sha256\":\"5a1c4e879245e3c89516c2e251fe9372e99ed48cc675c10349361bd613bda940\"}";
    check(artifact.verify_replay_header(header,ram,backing)==HostError::None && artifact.replay_header()==header,"console replay version/profile artifact binding and header generation");
    auto bad=header; bad[bad.find("version\":2")+9]='1';
    check(artifact.verify_replay_header(bad,ram,backing)==HostError::BadProfile,"console rejects version-one header");
    ram[0x200]=1; check(artifact.verify_replay_header(header,ram,backing)==HostError::BadProfile,"console replay rejects changed artifact");
}
// Compare every retained CPU/device field, including normally invisible staging.
bool same_state(const VirtualCPU& c,const VirtualCPU& d) {
    auto regs=[](const Registers& r){return std::tie(r.r,r.pc,r.cs,r.ds,r.ss,r.sp,r.sb,r.st,r.dp,r.flags);};
    auto fault=[](const Fault& f){return std::tie(f.cause,f.address,f.memory,f.privilege);};
    if(std::make_tuple(c.state(),c.profile(),c.run(),c.boundary(),c.next_sequence(),c.csr(),c.context())!=
       std::make_tuple(d.state(),d.profile(),d.run(),d.boundary(),d.next_sequence(),d.csr(),d.context()) ||
       regs(c.registers())!=regs(d.registers()) || bool(c.diagnostic())!=bool(d.diagnostic()) ||
       bool(c.terminal_diagnostic())!=bool(d.terminal_diagnostic())) return false;
    if(c.diagnostic() && fault(*c.diagnostic())!=fault(*d.diagnostic())) return false;
    auto key=[](const ProtectedState& p){return std::tie(p.mode,p.controls,p.descriptors,p.published,p.desc_map,p.desc_segment,
        p.shadow,p.entry_shadow,p.entry,p.entry_configured,p.query_address,p.query_run,p.query_kind,p.query_valid,
        p.pending,p.enable,p.in_service,p.levels,p.time,p.deadline,p.period,p.shadow_deadline,p.shadow_period,p.snapshot,
        p.timer_mode,p.shadow_mode,p.snapshot_valid,p.deadline_overflow,p.device_commits,p.console_rx,p.console_tx);};
    auto& p=ps(c);auto& q=ps(d);
    if(key(p)!=key(q) || p.frames.size()!=q.frames.size()) return false;
    auto frame=[](const ProtectedFrame& f){return std::tie(f.words,f.controls,f.origin,f.bootstrap,f.prior_depth,f.source,f.privilege,f.memory);};
    for(size_t i=0;i<p.frames.size();++i) if(frame(p.frames[i])!=frame(q.frames[i])) return false;
    if(c.terminal_diagnostic()) {
        auto& a=*c.terminal_diagnostic();auto& b=*d.terminal_diagnostic();
        auto terminal=[](const TerminalDiagnostic& t){return std::tie(t.reason,t.stage,t.event,t.pc,t.service,t.source,t.depth,t.captured,t.mode,t.controls,t.pending,t.in_service);};
        if(terminal(a)!=terminal(b) || fault(a.failure)!=fault(b.failure) || regs(a.registers)!=regs(b.registers)) return false;
    }
    return true;
}
enum class When { Before, Blocked, Timer, Idle, IdleWindow, Cancel, CancelQueued };
struct Case {
    std::string name; unsigned count=32,pointer=0x2000,segment=3,peer=0,hole=0;
    When when=When::Blocked; std::vector<uint8_t> bytes={'h','e','l','l','o','\r','\n'};
    unsigned expected_count=7,status=0,remaining=0,faults=0;
    std::string output="HELLO\n";
};
void guest(const Case& t) {
    using namespace kernel_images;
    auto c=console_guest::machine(t.count,t.pointer,t.segment,t.peer,t.hole); auto replayed=c;
    std::vector<std::string> log; bool injected=false,blocked=false,peer_progress=false,nested=false,timer_first=false;
    const bool cancelled=t.when==When::Cancel || t.when==When::CancelQueued;
    unsigned steps=0,after_cancel=0,max_depth=0; bool halted_idle=false;
    std::optional<ProtectedFrame> interrupted;
    std::optional<ProtectedFrame> idle_outer;
    bool completed_on_peer=false,timer_selected_a=false;
    auto apply=[&](const std::string& line) {log.push_back(line);check(c.apply_boundary_json(line)==HostError::None && replayed.apply_boundary_json(line)==HostError::None,t.name+": input applied");};
    while(steps++<100000) {
        bool wait=word(c,0x402a)==2 && word(c,0x42000)>=3 && c.mode()==Mode::User && ps(c).controls[0]==8;
        bool inject=!injected && (t.when==When::Before?steps==1:t.when==When::Blocked?wait:
            t.when==When::Timer?wait && (ps(c).pending&1):t.when==When::Idle?c.state()==ExecutionState::Halted && c.registers().pc==console_idle_resume:
            t.when==When::IdleWindow?c.registers().pc==console_idle && (c.csr()&32):t.when==When::CancelQueued?c.registers().pc==console_cancel:word(c,0x3022)==4 && c.mode()==Mode::User);
        if(inject) {
            if(t.when==When::Idle) {
                halted_idle=true;
                auto target=ps(c).time+1;
                check(target<=ps(c).deadline,t.name+": idle interval before timer");
                apply(record(c,"\"kind\":\"idle_advance\",\"target_time\":\""+std::to_string(target)+"\""));
            }
            if(t.when==When::Idle || t.when==When::IdleWindow) idle_outer=ps(c).frames.front();
            apply(console_guest::input(c,t.bytes)); injected=true;
            if(t.when==When::Timer) {
                check((ps(c).pending&5)==5,t.name+": timer and RX pending together");
            }
        }
        if(console_guest::finished(c)) break;
        auto advanced=c.step(); auto again=replayed.step();
        check(advanced==again && same_state(c,replayed),t.name+": complete replay state at boundary "+c.boundary());
        check(advanced,t.name+": stalled at "+std::to_string(c.registers().pc));
        max_depth=std::max(max_depth,unsigned(ps(c).frames.size()));
        if(!ps(c).frames.empty()) {
            const auto& outer=ps(c).frames.front();
            if(outer.source==2 && outer.origin==Mode::User && outer.controls[0]==8) {
                if(!interrupted) interrupted=outer;
                check(outer.words==interrupted->words && outer.controls==interrupted->controls,t.name+": RX leaves interrupted B image intact");
                if(word(c,0x300c)==1) completed_on_peer=ps(c).controls[0]==8;
            }
            if(idle_outer && ps(c).frames.size()>1) check(outer.words==idle_outer->words && outer.controls==idle_outer->controls,t.name+": nested K handler never replaces outer B frame");
            if(completed_on_peer && outer.source==0 && outer.controls[0]==7) timer_selected_a=true;
        }
        blocked|=word(c,0x402a)==2; peer_progress|=word(c,0x42000)>=(t.peer==1?1u:3u);
        nested|=ps(c).frames.size()>=2 && ps(c).frames.back().memory[0]==3;
        if(inject && t.when==When::Timer) timer_first=ps(c).frames.size()==1 && ps(c).frames.back().source==0;
        if(cancelled && injected && c.mode()==Mode::User && ++after_cancel==50) break;
        if(c.registers().pc==0x1401 || c.terminal_diagnostic()) {
            std::ostringstream message; message<<t.name<<": fatal pc="<<std::hex<<c.registers().pc;
            if(c.context()) message<<" saved="<<(*c.context())[8]<<" cause="<<(*c.context())[17];
            throw std::runtime_error(message.str());
        }
    }
    check(steps<100000,t.name+": bounded completion");
    if(cancelled) {
        check(injected && word(c,0x402a)==3 && word(c,0x4038)==0 && word(c,0x523a)==0 && word(c,0x5238)==2 && word(c,0x3022)==4,t.name+": cancelled owner drained, generation reused, stale lease rejected");
        check(word(c,0x300c)==0 && ps(c).console_rx==t.bytes && word(c,0x72000)==0 && word(c,0x32000)==0,t.name+": late input cannot complete or write old/successor process");
    } else {
        check(console_guest::finished(c),t.name+": guest shutdown");
        check(word(c,0x32028)==1,t.name+": preserved user registers and NZCV");
        check(word(c,0x32020)==t.expected_count && word(c,0x32022)==t.status,t.name+": count/status got "+std::to_string(word(c,0x32020))+"/"+std::to_string(word(c,0x32022)));
        check(std::string(ps(c).console_tx.begin(),ps(c).console_tx.end())==t.output,t.name+": terminal output");
        check(ps(c).console_rx.size()==t.remaining && word(c,0x3008)==t.faults,t.name+": FIFO/copy faults");
        check(word(c,0x523a)==1 && word(c,0x5242)==1 && word(c,0x4038)==0 && word(c,0x4138)==0,t.name+": exactly PCB leases, all request leases drained");
        check(ps(c).enable==0 && ps(c).timer_mode==0 && ps(c).in_service==0 && ps(c).frames.size()==1,t.name+": clean shutdown retains one U-origin diagnostic frame");
        if(t.when!=When::Before) check(injected && blocked && peer_progress && word(c,0x300c)==1,t.name+": one wake/completion while peer progressed");
        if(t.faults) check(nested,t.name+": precise nested MOVU fault recovery");
        if(t.when==When::Blocked || t.when==When::Timer) check(completed_on_peer && timer_selected_a,t.name+": completes on B then selects A only through timer frame");
        if(t.when==When::Timer) check(timer_first,t.name+": lower timer source wins arbitration");
        if(t.when==When::Idle || t.when==When::IdleWindow) check(max_depth>=(t.faults?3u:2u) && word(c,0x3024)==1,t.name+": nested idle wake and peer request cancellation");
        if(t.when==When::Idle) check(halted_idle,t.name+": recorded idle wake");
    }
    check(word(c,0x5000)==0 && ps(c).controls[1]==0 && ps(c).controls[2]==0,t.name+": lock and live access controls restored");
    for(unsigned depth=0;depth<9;++depth) check(word(c,0x5a00+16*depth)==0,t.name+": no leaked scoped access lease");
    bool same=true;for(unsigned a=0;a<0x80000;++a) same&=c.peek(a)==replayed.peek(a);
    check(same,t.name+": complete final RAM replay");
    std::cout<<t.name<<": "<<c.boundary()<<" boundaries, "<<log.size()<<" recorded events\n";
}
void candidate_rollback() {
    auto c=console_guest::machine(32,0x2000,3,0,0,true); auto replayed=c;
    bool rejected=false; unsigned steps=0;
    while(steps++<30000) {
        check(c.step()==replayed.step() && same_state(c,replayed),"candidate rollback complete replay");
        rejected|=c.registers().pc==kernel_images::console_rollback;
        if(word(c,0x3026)==1 && c.mode()==Mode::User && word(c,0x42000)>=200) break;
        check(c.state()==ExecutionState::Running && c.registers().pc!=0x1401,"invalid candidate does not escape to bad U target");
    }
    check(steps<30000 && rejected && ps(c).controls[0]==8 && ps(c).frames.empty() && word(c,0x4010)==65535,"invalid candidate preserves B return image and stack baseline");
    check(word(c,0x5000)==0 && word(c,0x523a)==2 && word(c,0x5242)==1,"rollback retains request and PCB ownership");
    bool same=true;for(unsigned a=0;a<0x80000;++a) same&=c.peek(a)==replayed.peek(a);
    check(same,"candidate rollback final RAM replay");
    std::cout<<"invalid candidate rollback: "<<steps<<" boundaries\n";
}
void guests() {
    guest({"blocking terminal"});
    Case t{"input before read"};t.when=When::Before;guest(t);
    t={"one-byte partial read"};t.bytes={'a'};t.expected_count=1;t.output="A";guest(t);
    t={"maximum read with excess input"};t.bytes.assign(40,'a');t.expected_count=32;t.remaining=8;t.output.assign(32,'A');guest(t);
    t={"exact maximum read"};t.bytes.assign(32,'z');t.expected_count=32;t.output.assign(32,'Z');guest(t);
    t={"timer and console arbitration"};t.when=When::Timer;guest(t);
    t={"all blocked idle wake"};t.peer=1;t.when=When::Idle;guest(t);
    t={"EI HALT wake window"};t.peer=1;t.when=When::IdleWindow;guest(t);
    t={"zero count ignores pointer"};t.count=0;t.segment=255;t.pointer=65535;t.when=When::Before;t.expected_count=0;t.output="";t.remaining=7;guest(t);
    t={"invalid count"};t.count=33;t.when=When::Before;t.expected_count=0;t.status=1;t.output="";t.remaining=7;guest(t);
    t={"invalid selector"};t.segment=5;t.when=When::Before;t.expected_count=0;t.status=1;t.output="";t.remaining=7;guest(t);
    t={"segment end partial copy"};t.pointer=65535;t.count=2;t.bytes={'x','y'};t.expected_count=1;t.status=1;t.remaining=1;t.output="X";guest(t);
    t={"bad user buffer"};t.hole=0x32000;t.expected_count=0;t.status=1;t.remaining=6;t.faults=1;t.output="";guest(t);
    t={"partial nested user-copy fault"};t.hole=0x32001;t.expected_count=1;t.status=1;t.remaining=5;t.faults=1;t.output="H";guest(t);
    t={"idle nested copy fault"};t.bytes={'h','i'};t.hole=0x32001;t.peer=1;t.when=When::IdleWindow;t.expected_count=1;t.status=1;t.remaining=0;t.faults=1;t.output="H";guest(t);
    t={"cancel exit and map reuse"};t.peer=2;t.when=When::Cancel;guest(t);
    t={"input pending during cancellation"};t.peer=2;t.when=When::CancelQueued;guest(t);
}
}
int main() {
    try { devices(); guests(); candidate_rollback(); std::cout<<checks<<" console checks passed\n"; }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
