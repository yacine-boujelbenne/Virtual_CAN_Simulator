#pragma once
#include "CanBus.hpp"
#include "json.hpp"
#include <map>
#include <memory>
#include <random>
#include <string>
using Json = nlohmann::json;
class SimulationManager {
public:
    explicit SimulationManager(const std::string &scenarioPath);
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
    };
    std::string scenarioPath_, channel_;
    Json scenario_, sensors_, frames_ = Json::array(), events_ = Json::array();
    std::map<std::string,double> world_, controls_;
    std::vector<Ecu> ecus_;
    std::unique_ptr<CanBus> monitor_;
    std::mt19937 random_{42};
    unsigned stepMs_ = 10;
    uint64_t tick_ = 0, frameCount_ = 0, frameBits_ = 0;
    bool running_ = false;
    void reset();
    void tick();
    void loadEcu(const Json &definition);
    Json diagnostic(const Json &request);
};
