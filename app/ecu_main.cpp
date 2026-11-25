#include "EcuKernel.hpp"
#include <fstream>
#include <iostream>
#include <memory>
int main() {
    std::unique_ptr<EcuKernel> kernel;
    std::string line;
    while(std::getline(std::cin,line)) {
        try {
            auto request=Json::parse(line);
            if(request.value("action","")=="configure") {
                auto next=std::make_unique<EcuKernel>(request.at("definition"),request.value("step_ms",10U));
                next->command({{"action","reset"},{"epoch",request.value("epoch",1ULL)},{"start_tick",request.value("start_tick",0ULL)}});
                kernel=std::move(next);
                std::cout<<kernel->snapshot().dump()<<std::endl;
            } else {
                if(!kernel)throw std::runtime_error("ECU is not configured");
                std::cout<<kernel->command(request).dump()<<std::endl;
            }
        } catch(const std::exception &error) {std::cout<<Json({{"error",error.what()}}).dump()<<std::endl;}
    }
}
