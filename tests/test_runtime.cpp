#include <gtest/gtest.h>
#include "SimulationManager.hpp"
#include <fstream>
namespace {
Json definition(const char *name) {std::ifstream file(std::string("resources/ecu_templates/")+name+".json");Json d;file>>d;return d;}
Json findEcu(const Json &s,const std::string &name) {for(auto &e:s["ecus"])if(e["name"]==name)return e;throw std::runtime_error("Missing ECU");}
}
TEST(Runtime, VehicleAcceleratesAndClusterReceivesDecodedCan) {
    SimulationManager sim("resources/scenarios/ev_basic.json");
    sim.command({{"action","controls"},{"values",{{"throttle",0.6}}}});sim.step(1000);
    auto s=sim.snapshot();EXPECT_GT(s["world"]["speed"].get<double>(),20);
    auto cluster=findEcu(s,"Instrument Cluster");
    EXPECT_NEAR(cluster["values"]["display_speed"].get<double>(),s["world"]["speed"].get<double>()*3.6,1);
    EXPECT_LT(s["world"]["soc"].get<double>(),85);
    EXPECT_GT(s["frame_count"].get<unsigned>(),1000U);
    EXPECT_LE(s["frames"].size(),200U);
    EXPECT_NEAR(s["time"].get<double>(),10,1e-12);
}
TEST(Runtime, SensorFaultCausesDtcFanClusterWarningAndRecovery) {
    SimulationManager sim("resources/scenarios/ev_basic.json");
    sim.command({{"action","fault"},{"sensor","temperature_sensor"},{"mode","stuck"},{"value",120}});
    sim.step(20);
    auto s=sim.snapshot();auto motor=findEcu(s,"Motor ECU"),cluster=findEcu(s,"Instrument Cluster");
    EXPECT_EQ(motor["state"],"FAULT");ASSERT_EQ(motor["dtcs"].size(),1U);EXPECT_TRUE(motor["dtcs"][0]["active"].get<bool>());
    EXPECT_EQ(s["world"]["fan"],1);EXPECT_EQ(cluster["values"]["warning"],1);
    sim.command({{"action","fault"},{"sensor","temperature_sensor"},{"mode","none"}});sim.step(20);
    auto recovered=findEcu(sim.snapshot(),"Motor ECU");EXPECT_EQ(recovered["state"],"RUN");EXPECT_FALSE(recovered["dtcs"][0]["active"].get<bool>());
    sim.command({{"action","diagnostic"},{"ecu","Motor ECU"},{"bytes",std::vector<uint8_t>{0x14,0xff,0xff,0xff}}});
    EXPECT_TRUE(findEcu(sim.snapshot(),"Motor ECU")["dtcs"].empty());
}
TEST(Runtime, AbsActivatesAndBrakePressureIsModulatedOnLowFriction) {
    SimulationManager sim("resources/scenarios/ev_basic.json");
    sim.command({{"action","controls"},{"values",{{"throttle",0.7}}}});sim.step(1000);
    sim.command({{"action","controls"},{"values",{{"throttle",0},{"brake",1},{"road_friction",0.2}}}});
    bool active=false,pressureReduced=false;
    for(unsigned i=0;i<100;i++){
        sim.step();auto s=sim.snapshot();
        active=active||findEcu(s,"ABS ECU")["values"]["abs_active"].get<double>()==1;
        pressureReduced=pressureReduced||s["world"]["brake_pressure"].get<double>()<0.5;
    }
    EXPECT_TRUE(active);EXPECT_TRUE(pressureReduced);
}
TEST(Runtime, SeededReplayResetAndPause) {
    SimulationManager a("resources/scenarios/ev_basic.json"),b("resources/scenarios/ev_basic.json");
    for(auto *sim:{&a,&b}) {
        sim->command({{"action","controls"},{"values",{{"throttle",0.4}}}});
        sim->command({{"action","fault"},{"sensor","temperature_sensor"},{"mode","noisy"}});
        sim->step(100);
    }
    EXPECT_EQ(a.snapshot()["world"],b.snapshot()["world"]);
    a.command({{"action","advance"},{"count",10}});EXPECT_EQ(a.snapshot()["time"],1);
    a.command({{"action","reset"}});EXPECT_EQ(a.snapshot()["time"],0);
    EXPECT_EQ(a.snapshot()["world"]["soc"],85);
}
TEST(Runtime, UdsVinIsMultiFrameAndUnsupportedServiceReturnsNrc) {
    SimulationManager sim("resources/scenarios/ev_basic.json");
    auto vin=sim.command({{"action","diagnostic"},{"ecu","Motor ECU"},{"bytes",std::vector<uint8_t>{0x22,0xf1,0x90}}});
    EXPECT_EQ(vin["bytes"].size(),20U);EXPECT_EQ(vin["bytes"][0],0x62);EXPECT_EQ(vin["transport"],"ISO-TP / internal CAN");
    auto unsupported=sim.command({{"action","diagnostic"},{"ecu","BMS ECU"},{"bytes",std::vector<uint8_t>{0x27,1}}});
    EXPECT_EQ(unsupported["bytes"],(std::vector<uint8_t>{0x7f,0x27,0x11}));
}
TEST(Validation, SignalCapacityOverlapReferencesAndPeriods) {
    auto d=definition("motor");EXPECT_NO_THROW(SimulationManager::validateEcu(d));
    auto invalid=d;invalid["messages"][0]["signals"][0]["length"]=8U;
    EXPECT_THROW(SimulationManager::validateEcu(invalid),std::invalid_argument);
    invalid=d;invalid["messages"][0]["signals"][1]["start"]=0U;
    EXPECT_THROW(SimulationManager::validateEcu(invalid),std::invalid_argument);
    invalid=d;invalid["messages"][0]["signals"][0]["start"]=uint64_t(0xffffffff);
    EXPECT_THROW(SimulationManager::validateEcu(invalid),std::invalid_argument);
    invalid=d;invalid["tasks"][0]["period_ms"]=5U;
    EXPECT_THROW(SimulationManager::validateEcu(invalid),std::invalid_argument);
    invalid=d;invalid["tasks"][0]["assign"]["motor_rpm"]="undefined_input";
    EXPECT_THROW(SimulationManager::validateEcu(invalid),std::invalid_argument);
}
TEST(Validation, ImportsRejectCollisionsAndControlsAreAtomic) {
    SimulationManager sim("resources/scenarios/ev_basic.json");
    auto d=definition("motor");d["name"]="Duplicate motor";
    EXPECT_THROW(sim.command({{"action","add_ecu"},{"definition",d}}),std::invalid_argument);
    EXPECT_THROW(sim.command({{"action","controls"},{"values",{{"throttle",0.5},{"unknown",1}}}}),std::invalid_argument);
    EXPECT_EQ(sim.snapshot()["controls"]["throttle"],0);
}

TEST(Validation, MissingFieldsRejectWithoutAbortingTheProcess) {
    auto d=definition("motor");
    for(const char *key:{"state_machine","messages","bindings"}) {
        auto invalid=d;invalid.erase(key);
        EXPECT_THROW(SimulationManager::validateEcu(invalid),std::exception);
    }
    auto invalid=d;invalid["messages"][0].erase("id");
    EXPECT_THROW(SimulationManager::validateEcu(invalid),std::exception);
    invalid=d;invalid["messages"][0]["signals"][0].erase("min");
    EXPECT_THROW(SimulationManager::validateEcu(invalid),std::exception);
}
