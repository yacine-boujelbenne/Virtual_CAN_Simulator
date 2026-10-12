#include <gtest/gtest.h>
#include "EcuKernel.hpp"
#include "SimulationManager.hpp"
#include <fstream>
Json kernelDefinition(const char *name) {std::ifstream f(std::string("resources/ecu_templates/")+name+".json");Json d;f>>d;return d;}
TEST(Kernel, DuplicateTickIsIdempotentAndMissingTickRejected) {
    EcuKernel kernel(kernelDefinition("radar"));
    Json input={{"action","tick"},{"tick",0ULL},{"epoch",1ULL},{"inputs",{{"ignition",1},{"radar_distance_sensor",20},{"closing_speed_sensor",10}}},{"frames",Json::array()}};
    auto a=kernel.command(input),b=kernel.command(input);EXPECT_EQ(a,b);
    EXPECT_TRUE(a["tx_frames"][0]["fd"].get<bool>());EXPECT_EQ(a["tx_frames"][0]["data"].size(),32U);
    EXPECT_EQ(a["tx_frames"][0]["data"][10],100); // Signal above bit 64 is encoded, not silently truncated.
    input["tick"]=2ULL;EXPECT_THROW(kernel.command(input),std::invalid_argument);
}
TEST(Kernel, VirtualCpuBudgetIsEnforcedBeforeStateAdvances) {
    auto d=kernelDefinition("radar");d["hardware"]["task_budget_us"]=1;
    EcuKernel kernel(d);
    EXPECT_THROW(kernel.command({{"action","tick"},{"tick",0ULL},{"epoch",1ULL},{"inputs",Json::object()},{"frames",Json::array()}}),std::invalid_argument);
    EXPECT_EQ(kernel.snapshot()["next_tick"],0);
}
TEST(Kernel, CanFdRejectsUnsupportedPayloadLengthAndOverlappingSignals) {
    auto d=kernelDefinition("radar");EXPECT_NO_THROW(SimulationManager::validateEcu(d));
    d["messages"][0]["dlc"]=9;EXPECT_THROW(SimulationManager::validateEcu(d),std::invalid_argument);
    d=kernelDefinition("radar");d["messages"][0]["signals"][2]["start"]=8U;
    EXPECT_THROW(SimulationManager::validateEcu(d),std::invalid_argument);
}
TEST(Adas, RadarMessageLossCreatesAStaleInputFault) {
    SimulationManager simulation("resources/scenarios/ev_basic.json");
    simulation.step(30);
    simulation.command({{"action","network_policy"},{"rules",Json::array({{{"bus","adas"},{"id",1024},{"drop",true}}})}});
    simulation.step(30);
    auto state=simulation.snapshot();bool fault=false;
    for(auto &e:state["ecus"])if(e["name"]=="ADAS ECU") {
        fault=e["state"]=="FAULT";EXPECT_GT(e["values"]["radar_age_ms"].get<double>(),100);
    }
    EXPECT_TRUE(fault);EXPECT_GT(state["dropped_frames"].get<unsigned>(),0U);
}
TEST(Adas, EmergencyBrakingOnHighFrictionRoadStopsBeforeLeadVehicle) {
    SimulationManager simulation("resources/scenarios/ev_basic.json");
    simulation.command({{"action","environment"},{"values",{{"speed",25},{"lead_distance",45}}}});
    simulation.command({{"action","controls"},{"values",{{"lead_speed",0},{"road_friction",0.95},{"throttle",0}}}});
    bool braking=false;
    for(unsigned i=0;i<400;++i) {
        simulation.step();
        if(i%10==0) {auto state=simulation.snapshot();for(auto &e:state["ecus"])if(e["name"]=="ADAS ECU")braking=braking||e["values"]["aeb_request"].get<double>()==1;}
    }
    auto s=simulation.snapshot();EXPECT_TRUE(braking);EXPECT_LT(s["world"]["speed"].get<double>(),0.4);
    EXPECT_GT(s["world"]["lead_distance"].get<double>(),0);EXPECT_EQ(s["world"]["collision"],0);
}
TEST(Kernel, FullCanFdPayloadEncodesTheLastByte) {
    auto d=kernelDefinition("radar");
    d["variables"]["tail_value"]=227;
    d["messages"][0]["dlc"]=64;
    auto signal=d["messages"][0]["signals"][2];signal["name"]="tail_value";signal["start"]=504U;signal["length"]=8U;signal["max"]=255;
    d["messages"][0]["signals"].push_back(signal);
    EcuKernel kernel(d);
    auto result=kernel.command({{"action","tick"},{"tick",0ULL},{"epoch",1ULL},{"inputs",{{"ignition",1},{"radar_distance_sensor",20},{"closing_speed_sensor",10}}},{"frames",Json::array()}});
    ASSERT_EQ(result["tx_frames"][0]["data"].size(),64U);EXPECT_EQ(result["tx_frames"][0]["data"][63],227);
}
TEST(Kernel, AnalogInputQuantizationAndCanFifoAreModeled) {
    auto d=kernelDefinition("motor");d["hardware"]["adc"]={{"resolution_bits",2U},{"channels",{{"temperature_sensor",Json::array({-40,150})}}}};
    EcuKernel kernel(d);
    Json input={{"action","tick"},{"tick",0ULL},{"epoch",1ULL},{"inputs",{{"ignition",1},{"temperature_sensor",80}}},{"frames",Json::array()}};
    auto result=kernel.command(input);
    EXPECT_NEAR(result["values"]["temperature_sensor"].get<double>(),86.6666666667,1e-6);
    d=kernelDefinition("radar");d["hardware"]["can_controller"]["rx_fifo_frames"]=1;
    EcuKernel limited(d);
    input["frames"]=Json::array({{{"id",1},{"data",Json::array()}},{{"id",2},{"data",Json::array()}}});
    EXPECT_THROW(limited.command(input),std::invalid_argument);
    EXPECT_EQ(limited.snapshot()["next_tick"],0);
}
