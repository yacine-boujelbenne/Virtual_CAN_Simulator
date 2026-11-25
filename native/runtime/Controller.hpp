#pragma once
#include "SimulationManager.hpp"
#include "transport/HttpClient.hpp"
#include "transport/ProcessClient.hpp"
#include <map>
#include <memory>

namespace vclab {
struct RuntimeConfig {
    std::string scenario = "resources/scenarios/ev_basic.json";
    std::string worker_binary;
    // Empty endpoints selects explicitly labelled local native processes.
    std::map<std::string, std::string> endpoints;
    std::vector<std::string> spares;
};
class Controller {
public:
    explicit Controller(RuntimeConfig config);
    Json request(const Json &command);
    bool running() const { return core_.running(); }
    unsigned stepMilliseconds() const { return core_.stepMilliseconds(); }
    void pacedTick() { if(running()) step(1,false); }
private:
    struct Node {
        std::unique_ptr<ProcessClient> process;
        std::string endpoint;
        std::string status = "starting";
        Json runtime = Json::object();
        Json call(const Json &command);
    };
    using Nodes = std::map<std::string, Node>;
    RuntimeConfig config_;
    SimulationManager core_;
    Nodes nodes_;
    std::vector<std::string> spares_;
    Json failure_ = nullptr;
    Json presentation_ = Json::object();
    bool pending_ = false, needs_reset_ = false;
    uint64_t session_ = 0;
    Nodes configuredNodes(const Json &state);
    Json snapshot() const;
    Json step(unsigned count,bool includeSnapshot=true);
    Json diagnostic(const Json &command);
    Json importProject(const Json &project);
};
}
