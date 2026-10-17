// Guest-only descriptor/context image transfers and exact-site nested LDX retry.
#include "bulk_guest.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) {
    try {
        auto profile=vcpu::MachineProfile::MinimalV1;
        if(argc==2 && std::string(argv[1])=="console-v1")profile=vcpu::MachineProfile::ConsoleV1;
        else if(argc>2 || (argc==2 && std::string(argv[1])!="minimal-v1"))
            throw std::runtime_error("usage: bulk_demo [minimal-v1|console-v1]");
        auto c=bulk_guest::machine(profile);unsigned boundaries=0,max_depth=0;
        while(c.state()==vcpu::ExecutionState::Running && boundaries<1000) {
            c.step();++boundaries;max_depth=std::max(max_depth,unsigned(c.protected_state()->frames.size()));
        }
        bool ok=bulk_guest::finished(c) && max_depth==2;
        std::cout<<(ok?"PASS":"FAIL")<<": "<<boundaries<<" boundaries, "<<c.protected_state()->time
                 <<" retirements, "<<max_depth<<" frame depth, recovered="<<bulk_guest::word(c,0x2202)<<"\n";
        return ok?0:1;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
