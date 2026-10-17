// A self-contained single-run recorder/replayer; no post-reset debugger writes.
#include "console_guest.hpp"
#include <fstream>
#include <iostream>
#include <regex>
#include <stdexcept>

int main(int argc,char** argv) {
    try {
        bool replay=argc==3 && std::string(argv[1])=="--replay";
        bool record=argc==3 && std::string(argv[1])=="--record";
        if (argc!=1 && !replay && !record) throw std::runtime_error("usage: console_demo [--record FILE | --replay FILE]");
        auto cpu=console_guest::machine();
        std::ifstream source; std::ofstream destination;
        if (record) { destination.open(argv[2]); if(!destination) throw std::runtime_error("cannot create replay file"); }
        std::string next;
        auto read_next=[&] {
            if(std::getline(source,next)) {if(next.empty()) throw std::runtime_error("blank replay record");}
            else {if(!source.eof()) throw std::runtime_error("cannot read replay file");next.clear();}
        };
        const std::regex boundary_expression("\"boundary\":\"(0|[1-9][0-9]*)\"");
        if (replay) {
            source.open(argv[2]); std::string header;
            if(!std::getline(source,header)) throw std::runtime_error("missing replay header");
            // Artifacts describe this exact loader image, before its first boundary.
            std::vector<uint8_t> ram(1u<<24),backing(1u<<24);
            for(unsigned a=0;a<0x80000;++a) {ram[a]=cpu.peek(a);backing[a]=1;}
            std::fill(backing.begin()+0xff0000,backing.begin()+0xff0100,2);
            if(cpu.verify_replay_header(header,ram,backing)!=vcpu::HostError::None) throw std::runtime_error("replay artifact/profile/header mismatch");
            read_next();
        } else {
            auto header=cpu.replay_header();
            if(record) destination<<header<<'\n'; else std::cout<<header<<'\n';
        }
        bool injected=false; unsigned steps=0;
        while (!console_guest::finished(cpu) && steps++<100000) {
            if(replay && !next.empty()) {
                // Scheduling accepts the canonical lines written by this recorder.
                // CPU parsing/validation remains authoritative when a record is due.
                std::smatch match;
                if(!std::regex_search(next,match,boundary_expression)) throw std::runtime_error("invalid queued boundary");
                auto boundary=match[1].str();
                if(boundary.size()<cpu.boundary().size() || (boundary.size()==cpu.boundary().size() && boundary<cpu.boundary())) throw std::runtime_error("stale queued event");
                while(boundary==cpu.boundary()) {
                    if(cpu.apply_boundary_json(next)!=vcpu::HostError::None) throw std::runtime_error("rejected recorded event");
                    if(cpu.run()!="0") throw std::runtime_error("single-run demo requires a new loader/header after reset");
                    injected=true;
                    read_next();
                    if(next.empty()) break;
                    if(!std::regex_search(next,match,boundary_expression)) throw std::runtime_error("invalid queued boundary");
                    boundary=match[1].str();
                }
            }
            if(!replay && !injected && console_guest::word(cpu,0x402a)==2 && console_guest::word(cpu,0x42000)>=3 && cpu.mode()==vcpu::Mode::User) {
                auto line=console_guest::input(cpu,{'h','e','l','l','o','\r','\n'});
                if(record) destination<<line<<'\n'; else std::cout<<line<<'\n';
                if(cpu.apply_boundary_json(line)!=vcpu::HostError::None) throw std::runtime_error("rejected input");
                injected=true;
            }
            if(!cpu.step()) throw std::runtime_error("guest stalled");
        }
        auto& p=*cpu.protected_state();
        bool ok=injected && console_guest::finished(cpu) && next.empty() &&
            p.console_tx==std::vector<uint8_t>({'H','E','L','L','O','\n'}) && console_guest::word(cpu,0x32020)==7 &&
            console_guest::word(cpu,0x32022)==0 && console_guest::word(cpu,0x300c)==1;
        std::cout<<(ok?"PASS":"FAIL")<<": "<<steps<<" boundaries, peer progress="<<console_guest::word(cpu,0x42000)
                 <<", read="<<console_guest::word(cpu,0x32020)<<", output="<<std::string(p.console_tx.begin(),p.console_tx.end());
        if (!ok) std::cerr<<"pc="<<std::hex<<cpu.registers().pc<<", depth="<<p.frames.size()<<'\n';
        if(record) {destination.flush();if(!destination) throw std::runtime_error("cannot write replay file");}
        return ok?0:1;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
