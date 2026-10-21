#pragma once
#include "CanBus.hpp"
#include "EcuKernel.hpp"
#include "json.hpp"
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
    };
    std::string scenarioPath_, channel_;
    Json scenario_, sensors_, frames_ = Json::array(), events_ = Json::array();
    std::map<std::string,double> world_, controls_;
    std::vector<Ecu> ecus_;
    std::unique_ptr<CanBus> monitor_;
    std::mt19937 random_{42};
    unsigned stepMs_ = 10;
    uint64_t tick_ = 0, frameCount_ = 0, frameBits_ = 0;
    bool running_ = false, distributed_ = false;
    uint64_t epoch_=0;
    Json prepared_=nullptr, policies_=Json::array(), scheduled_=Json::array();
    Json envelopes_=Json::array(), actuations_=Json::array();
    std::map<std::string,double> busFreeUs_, busBusyUs_;
    std::map<std::string,std::unique_ptr<IsoTp>> testers_;
    Json definitions_=Json::object();
    uint64_t dropped_=0, frameSeq_=0;
    void pushEnvelope(const Json &envelope);
    void pushActuation(const Json &envelope);
    Json prepareTick();
    Json completeTick(const Json &results);
    void transmit(const std::string &source, const Json &message, const Can &frame, const std::string &bus, bool gateway = false);
    void reset();
    void applyScenario(const Json &scenario, const Json *inlineDefinitions);
    void tick();
    void loadEcu(const Json &definition);
    Json diagnostic(const Json &request);
    Json isotpTester(const Json &request);
    Json projectContent() const;
    Json exportProject() const;
    Json importProject(const Json &project);
};
