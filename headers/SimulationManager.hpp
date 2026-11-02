#pragma once
#include "CanBus.hpp"
#include "EcuKernel.hpp"
#include "json.hpp"
#include "../native/world/TrafficWorld.hpp"
#include <map>
#include <memory>
#include <random>
#include <string>
using Json = nlohmann::json;
class SimulationManager {
public:
    explicit SimulationManager(const std::string &scenarioPath, bool distributed = false);
    Json command(const Json &request);
    Json snapshot() const;
    void step(unsigned count = 1);
    bool running() const { return running_; }
    unsigned stepMilliseconds() const { return stepMs_; }
    static void validateEcu(const Json &definition, unsigned stepMs = 10);
private:
    struct Ecu {
        Json definition;
        std::map<std::string,double> values;
        std::string state;
        std::unique_ptr<CanBus> bus;
        std::map<std::string,bool> dtcActive, dtcStored;
        std::unique_ptr<EcuKernel> kernel;
        Json inbox=Json::array(), hardware=Json::object(), timing=Json::object();
        uint64_t lastJobId=0;
    };
    // Rolling performance window: task cost, end-to-end latency and frame counters per ECU.
    struct EcuMetrics {
        std::vector<double> task, e2e, delivery, actuation;
        std::vector<std::pair<uint64_t,double>> taskTimeline, deliveryTimeline, chainTimeline;
        uint64_t tx=0, rx=0, dropped=0, states=0;
    };
    struct BusMetrics {uint64_t frames=0, dropped=0;};
    std::string scenarioPath_, channel_;
    Json scenario_, sensors_, frames_ = Json::array(), events_ = Json::array();
    std::map<std::string,double> world_, controls_;
    std::vector<Ecu> ecus_;
    std::unique_ptr<CanBus> monitor_;
    std::mt19937 random_{42};
    unsigned stepMs_ = 10;
    uint64_t tick_ = 0, frameCount_ = 0, frameBits_ = 0;
    bool running_ = false, distributed_ = false, lateralPlant_ = false;
    double wheelbaseM_ = 2.7, maxSteerAngleRad_ = 0.6, parkSpeedMps_ = 0.5, maxYawRateRadS_ = 2.0;
    double parkBrakeMps2_ = 2.0;
    vclab::TrafficWorld trafficWorld_;
    bool trafficEnabled_=false;
    bool trafficContact_=false;
    std::string leadActor_;
    void observeTraffic();
    bool gripPhysics_=false;
    double massKg_=1500,wheelRadiusM_=0.32,dragAreaM2_=0.7,rollingCoefficient_=0.012,steerRate_=0.8,previousSteer_=0;
    double brakeForceN_=12000,driveRatio_=9,torqueLimitNm_=320;
    bool parkInterlockRamping_ = false;
    uint64_t epoch_=0;
    Json prepared_=nullptr, policies_=Json::array(), scheduled_=Json::array();
    Json envelopes_=Json::array(), actuations_=Json::array();
    std::map<std::string,double> busFreeUs_, busBusyUs_;
    std::map<std::string,std::unique_ptr<IsoTp>> testers_;
    Json definitions_=Json::object();
    std::map<std::string,EcuMetrics> metrics_;
    std::map<std::string,BusMetrics> busMetrics_;
    uint64_t dropped_=0, frameSeq_=0;
    void pushEnvelope(const Json &envelope);
    void pushActuation(const Json &envelope);
    Json prepareTick();
    Json completeTick(const Json &results);
    void transmit(const std::string &source, const Json &message, const Can &frame, const std::string &bus, bool gateway = false, const Json &timing = Json());
    void reset();
    void applyScenario(const Json &scenario, const Json *inlineDefinitions);
    void tick();
    void loadEcu(const Json &definition);
    Json diagnostic(const Json &request);
    Json isotpTester(const Json &request);
    Json projectContent() const;
    Json exportProject() const;
    Json importProject(const Json &project);
    Json performance() const;
};
