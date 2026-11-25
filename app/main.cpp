#include "SimulationManager.hpp"
#include <iostream>
#include <cstdlib>
int main(int argc,char **argv) {
    try {
        SimulationManager simulation(argc>1?argv[1]:"resources/scenarios/ev_basic.json",std::getenv("SIM_DISTRIBUTED")!=nullptr);
        std::string line;
        while(std::getline(std::cin,line)) {
            try {std::cout<<simulation.command(Json::parse(line)).dump()<<std::endl;}
            catch(const std::exception &error) {std::cout<<Json({{"error",error.what()}}).dump()<<std::endl;}
        }
    } catch(const std::exception &error) {std::cerr<<error.what()<<std::endl;return 1;}
}
