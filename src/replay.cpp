#include "cpu.hpp"
#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace vcpu {
namespace {
bool decimal(const std::string& s) {
    return !s.empty() && (s.size()==1 || s[0]!='0') && std::all_of(s.begin(),s.end(),[](char c){return c>='0' && c<='9';});
}
bool uint64(const std::string& s,uint64_t& result) {
    result=0;
    for (char c:s) {
        unsigned digit=unsigned(c-'0');
        if (result>(UINT64_MAX-digit)/10) return false;
        result=result*10+digit;
    }
    return true;
}
struct BadJSON {};
struct Value { char type; std::string text; };
using Object=std::map<std::string,Value>;
void utf8(const std::string& s) {
    for (size_t i=0;i<s.size();) {
        unsigned c=uint8_t(s[i++]),count=0,cp=0,min=0;
        if (c<128) continue;
        if (c>=0xc2 && c<=0xdf) { count=1; cp=c&31; min=128; }
        else if (c>=0xe0 && c<=0xef) { count=2; cp=c&15; min=2048; }
        else if (c>=0xf0 && c<=0xf4) { count=3; cp=c&7; min=65536; }
        else throw BadJSON{};
        while (count--) { if (i==s.size() || (uint8_t(s[i])&0xc0)!=0x80) throw BadJSON{}; cp=(cp<<6)|(uint8_t(s[i++])&63); }
        if (cp<min || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) throw BadJSON{};
    }
}
class Parser {
    const std::string& s; size_t pos=0;
    char take() { if (pos==s.size()) throw BadJSON{}; return s[pos++]; }
    void ws() { while (pos<s.size() && (s[pos]==' ' || s[pos]=='\t' || s[pos]=='\r' || s[pos]=='\n')) ++pos; }
    unsigned hex4() {
        unsigned value=0;
        for (unsigned i=0;i<4;++i) { char c=take(); unsigned digit=c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:c>='A' && c<='F'?c-'A'+10:16; if (digit==16) throw BadJSON{}; value=(value<<4)|digit; }
        return value;
    }
    std::string string() {
        if (take()!='"') throw BadJSON{};
        std::string result;
        for (;;) {
            unsigned char c=uint8_t(take());
            if (c=='"') return result;
            if (c<32) throw BadJSON{};
            if (c!='\\') { result+=char(c); continue; }
            char escape=take();
            if (escape=='"' || escape=='/' || escape=='\\') result+=escape;
            else if (escape=='b') result+='\b'; else if (escape=='f') result+='\f';
            else if (escape=='n') result+='\n'; else if (escape=='r') result+='\r'; else if (escape=='t') result+='\t';
            else if (escape=='u') {
                unsigned cp=hex4();
                if (cp>=0xd800 && cp<=0xdbff) {
                    if (take()!='\\' || take()!='u') throw BadJSON{};
                    unsigned low=hex4(); if (low<0xdc00 || low>0xdfff) throw BadJSON{};
                    cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);
                } else if (cp>=0xdc00 && cp<=0xdfff) throw BadJSON{};
                if (cp<128) result+=char(cp);
                else if (cp<2048) { result+=char(0xc0|(cp>>6)); result+=char(0x80|(cp&63)); }
                else if (cp<65536) { result+=char(0xe0|(cp>>12)); result+=char(0x80|((cp>>6)&63)); result+=char(0x80|(cp&63)); }
                else { result+=char(0xf0|(cp>>18)); result+=char(0x80|((cp>>12)&63)); result+=char(0x80|((cp>>6)&63)); result+=char(0x80|(cp&63)); }
            } else throw BadJSON{};
        }
    }
    Value value() {
        ws(); if (pos==s.size()) throw BadJSON{};
        if (s[pos]=='"') return {'s',string()};
        for (auto literal: {"true","false","null"}) {
            std::string text=literal;
            if (s.compare(pos,text.size(),text)==0) { pos+=text.size(); return {text=="null"?'z':'b',text}; }
        }
        size_t start=pos;
        if (s[pos]=='-') ++pos;
        if (pos==s.size()) throw BadJSON{};
        if (s[pos]=='0') ++pos;
        else { if (s[pos]<'1' || s[pos]>'9') throw BadJSON{}; while (pos<s.size() && s[pos]>='0' && s[pos]<='9') ++pos; }
        if (pos<s.size() && s[pos]=='.') { ++pos; size_t digits=pos; while (pos<s.size() && s[pos]>='0' && s[pos]<='9') ++pos; if (digits==pos) throw BadJSON{}; }
        if (pos<s.size() && (s[pos]=='e' || s[pos]=='E')) { ++pos; if (pos<s.size() && (s[pos]=='+' || s[pos]=='-')) ++pos; size_t digits=pos; while (pos<s.size() && s[pos]>='0' && s[pos]<='9') ++pos; if (digits==pos) throw BadJSON{}; }
        return {'n',s.substr(start,pos-start)};
    }
public:
    explicit Parser(const std::string& text):s(text) { utf8(s); }
    Object object() {
        Object result; ws(); if (take()!='{') throw BadJSON{}; ws();
        if (pos<s.size() && s[pos]=='}') ++pos;
        else for (;;) {
            ws(); auto key=string(); ws(); if (take()!=':') throw BadJSON{};
            auto v=value(); if (!result.emplace(key,v).second) throw BadJSON{};
            ws(); char next=take(); if (next=='}') break; if (next!=',') throw BadJSON{};
        }
        ws(); if (pos!=s.size()) throw BadJSON{}; return result;
    }
};
std::string field(const Object& o,const char* key,char type='s') {
    auto f=o.find(key); if (f==o.end() || f->second.type!=type) throw BadJSON{}; return f->second.text;
}
void keys(const Object& o,std::initializer_list<const char*> names) {
    if (o.size()!=names.size()) throw BadJSON{};
    for (auto name:names) if (!o.count(name)) throw BadJSON{};
}
uint32_t rotate(uint32_t x,unsigned n) { return (x>>n)|(x<<(32-n)); }
std::string sha256(const std::vector<uint8_t>& input) {
    static constexpr uint32_t k[]={
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    std::vector<uint8_t> message=input; uint64_t bits=uint64_t(input.size())*8; message.push_back(128);
    while (message.size()%64!=56) message.push_back(0);
    for (int shift=56;shift>=0;shift-=8) message.push_back(uint8_t(bits>>shift));
    std::array<uint32_t,8> hash{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    for (size_t offset=0;offset<message.size();offset+=64) {
        uint32_t w[64]{};
        for (unsigned i=0;i<16;++i) for (unsigned j=0;j<4;++j) w[i]=(w[i]<<8)|message[offset+4*i+j];
        for (unsigned i=16;i<64;++i) w[i]=w[i-16]+(rotate(w[i-15],7)^rotate(w[i-15],18)^(w[i-15]>>3))+w[i-7]+(rotate(w[i-2],17)^rotate(w[i-2],19)^(w[i-2]>>10));
        auto v=hash;
        for (unsigned i=0;i<64;++i) {
            uint32_t t1=v[7]+(rotate(v[4],6)^rotate(v[4],11)^rotate(v[4],25))+((v[4]&v[5])^(~v[4]&v[6]))+k[i]+w[i];
            uint32_t t2=(rotate(v[0],2)^rotate(v[0],13)^rotate(v[0],22))+((v[0]&v[1])^(v[0]&v[2])^(v[1]&v[2]));
            v={t1+t2,v[0],v[1],v[2],v[3]+t1,v[4],v[5],v[6]};
        }
        for (unsigned i=0;i<8;++i) hash[i]+=v[i];
    }
    std::ostringstream out; out<<std::hex<<std::setfill('0'); for (auto word:hash) out<<std::setw(8)<<word; return out.str();
}
}
HostError VirtualCPU::apply_boundary_events(const std::vector<BoundaryEvent>& records) {
    if (records.empty()) return HostError::None;
    auto staged=*protected_; auto sequence=sequence_;
    bool terminal=state_==ExecutionState::TerminalDoubleFault || state_==ExecutionState::TerminalResetFault;
    bool reset_event=false;
    for (const auto& event:records) {
        using Kind=BoundaryEvent::Kind;
        if (!decimal(event.run) || !decimal(event.boundary) || !decimal(event.sequence) ||
            (event.kind==Kind::IdleAdvance && !decimal(event.target_time)) ||
            (event.kind==Kind::ConsoleInput && (event.data.empty() || event.data.size()>64)) ||
            (event.kind!=Kind::ConsoleInput && event.kind!=Kind::Edge && event.kind!=Kind::Level && event.kind!=Kind::IdleAdvance && event.kind!=Kind::Reset)) return HostError::BadFormat;
        if (event.run!=run_ || event.boundary!=boundary_) return HostError::WrongBoundary;
        if (event.sequence!=sequence) return HostError::BadSequence;
        if ((event.kind==Kind::Edge && event.source!=1) || (event.kind==Kind::Level && (event.source!=2 || profile_==MachineProfile::ConsoleV1))) return HostError::BadSource;
        if (event.kind==Kind::ConsoleInput && profile_!=MachineProfile::ConsoleV1) return HostError::BadProfile;
        if ((terminal && event.kind!=Kind::Reset) || (event.kind==Kind::Reset && records.size()!=1)) return HostError::BadState;
        if (event.kind==Kind::ConsoleInput) {
            if (staged.console_rx.size()+event.data.size()>64) return HostError::BadState;
            staged.console_rx.insert(staged.console_rx.end(),event.data.begin(),event.data.end());
            staged.levels|=4; staged.pending|=4;
        }
        if (event.kind==Kind::Edge) staged.pending|=2;
        if (event.kind==Kind::Level) { staged.levels=event.asserted?4:0; if (event.asserted) staged.pending|=4; }
        if (event.kind==Kind::IdleAdvance) {
            if (state_!=ExecutionState::Halted || ((regs_.flags&32) && ((staged.pending|staged.levels)&staged.enable&~staged.in_service))) return HostError::BadState;
            uint64_t target=0;
            if (!uint64(event.target_time,target) || target<=staged.time ||
                (staged.timer_mode && (regs_.flags&32) && (staged.enable&1) && !(staged.in_service&1) && target>staged.deadline)) return HostError::BadTime;
            staged.time=target; evaluate_timer(staged);
        }
        reset_event=event.kind==Kind::Reset; increment(sequence);
    }
    if (reset_event) reset(); else { protected_=std::move(staged); sequence_=sequence; }
    return HostError::None;
}
HostError VirtualCPU::advance_idle_to(uint64_t target) {
    BoundaryEvent event{run_,boundary_,sequence_,BoundaryEvent::Kind::IdleAdvance,0,false,"0",{}}; event.target_time=std::to_string(target);
    return apply_boundary_events({event});
}
HostError VirtualCPU::apply_boundary_json(const std::string& lines) {
    std::vector<BoundaryEvent> records; std::istringstream input(lines); std::string line;
    while (std::getline(input,line)) {
        BoundaryEvent event; event.kind=BoundaryEvent::Kind::Invalid;
        try {
            auto o=Parser(line).object(); auto kind=field(o,"kind");
            event.run=field(o,"run"); event.boundary=field(o,"boundary"); event.sequence=field(o,"sequence");
            if (kind=="irq_edge" || kind=="irq_level") {
                if (kind=="irq_edge") keys(o,{"run","boundary","sequence","kind","source"});
                else keys(o,{"run","boundary","sequence","kind","source","asserted"});
                auto source=field(o,"source",'n');
                if (!decimal(source) && !(source.size()>1 && source[0]=='-' && decimal(source.substr(1)))) throw BadJSON{};
                event.source=source=="1"?1:source=="2"?2:UINT32_MAX;
                if (kind=="irq_level") event.asserted=field(o,"asserted",'b')=="true";
                event.kind=kind=="irq_edge"?BoundaryEvent::Kind::Edge:BoundaryEvent::Kind::Level;
            } else if (kind=="idle_advance") {
                keys(o,{"run","boundary","sequence","kind","target_time"}); event.target_time=field(o,"target_time"); event.kind=BoundaryEvent::Kind::IdleAdvance;
            } else if (kind=="console_rx" && profile_==MachineProfile::ConsoleV1) {
                keys(o,{"run","boundary","sequence","kind","data"});
                auto data=field(o,"data");
                if (data.empty() || data.size()>128 || data.size()%2) throw BadJSON{};
                auto nibble=[](char c)->unsigned { if (c>='0' && c<='9') return c-'0'; if (c>='a' && c<='f') return c-'a'+10; throw BadJSON{}; };
                for (size_t i=0;i<data.size();i+=2) event.data.push_back(uint8_t(16*nibble(data[i])+nibble(data[i+1])));
                event.kind=BoundaryEvent::Kind::ConsoleInput;
            } else if (kind=="reset") { keys(o,{"run","boundary","sequence","kind"}); event.kind=BoundaryEvent::Kind::Reset; }
        } catch (const BadJSON&) { event.kind=BoundaryEvent::Kind::Invalid; }
        records.push_back(event);
    }
    return apply_boundary_events(records);
}
std::string VirtualCPU::replay_header() const {
    if (boundary_!="0") throw std::logic_error("Replay header requires Working reset boundary");
    std::vector<uint8_t> ram(1u<<24), tags(1u<<24);
    for (uint32_t a=0;a<0x1000000;++a) { tags[a]=uint8_t(backing(a)); if (tags[a]==1) ram[a]=ram_[a]; }
    return "{\"format\":\"vcpu-replay\",\"version\":"+std::string(profile_==MachineProfile::ConsoleV1?"2":"1")+
        ",\"profile\":\""+(profile_==MachineProfile::ConsoleV1?"console-v1":"minimal-v1")+"\",\"run\":\""+run_+
        "\",\"ram_image_sha256\":\""+sha256(ram)+"\",\"backing_map_sha256\":\""+sha256(tags)+"\"}";
}
HostError VirtualCPU::verify_replay_header(const std::string& json,const std::vector<uint8_t>& ram_image,const std::vector<uint8_t>& backing_map) const {
    try {
        auto o=Parser(json).object(); keys(o,{"format","version","profile","run","ram_image_sha256","backing_map_sha256"});
        auto version=field(o,"version",'n'), profile=field(o,"profile"), run=field(o,"run");
        auto ram_hash=field(o,"ram_image_sha256"), backing_hash=field(o,"backing_map_sha256");
        auto digest=[](const std::string& s){return s.size()==64 && std::all_of(s.begin(),s.end(),[](char c){return (c>='0' && c<='9') || (c>='a' && c<='f');});};
        if (field(o,"format")!="vcpu-replay" || !decimal(run) || !decimal(version) || !digest(ram_hash) || !digest(backing_hash)) return HostError::BadFormat;
        if (version!=(profile_==MachineProfile::ConsoleV1?"2":"1") || profile!=(profile_==MachineProfile::ConsoleV1?"console-v1":"minimal-v1") || ram_image.size()!=0x1000000 || backing_map.size()!=0x1000000) return HostError::BadProfile;
        for (uint32_t a=0;a<0x1000000;++a) if (backing_map[a]!=backing(a) || ram_image[a]!=(backing(a)==1?ram_[a]:0)) return HostError::BadProfile;
        if (sha256(ram_image)!=ram_hash || sha256(backing_map)!=backing_hash) return HostError::BadProfile;
        if (run!=run_ || boundary_!="0") return HostError::WrongBoundary;
        return HostError::None;
    } catch (const BadJSON&) { return HostError::BadFormat; }
}
}
