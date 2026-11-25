#pragma once
#include "Can.hpp"
#include "IsoTp.hpp"
#include "EcuJobScheduler.hpp"
#include "json.hpp"
#include <map>
#include <memory>
#include <string>
using Json = nlohmann::json;
// One behavior kernel per process/container. No vehicle model or other ECU instances.
class EcuKernel {
public:
    EcuKernel(Json definition, unsigned stepMs = 10);
    Json command(const Json &request);
    Json tick(const Json &request);
    Json snapshot() const;
private:
    Json definition_, hardware_, cached_, cachedRequest_, provenance_;
    std::map<std::string,double> values_;
    std::map<std::string,bool> active_, stored_;
    std::map<std::string,uint64_t> rxTicks_;
    std::map<std::string,Json> rxAcquisition_;
    std::map<std::string,std::unique_ptr<IsoTp>> sessions_;
    std::map<std::string,bool> responders_;
    std::string state_;
    unsigned stepMs_;
    uint64_t nextTick_=0, epoch_=0, lastRxTick_=0;
    uint64_t baseRamBytes_=0;
    bool resourceMode_=false;
    bool identityInitialized_=false;
    EcuJobScheduler scheduler_;
    Json evaluate(const Json &request, uint64_t observedUs, uint64_t computedUs, double cost);
    Json evaluateAtomic(const Json &request, uint64_t observedUs, uint64_t computedUs, double cost);
    double taskCost(uint64_t tick) const;
    void validateTick(const Json &request, uint64_t nowUs) const;
    Json uds(const Json &request);
    Json isotp(const Json &request);
    void reset(uint64_t epoch);
    uint64_t clockMs() const {return nextTick_*stepMs_;}
};
