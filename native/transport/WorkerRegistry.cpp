#include "WorkerRegistry.hpp"

namespace vclab {

WorkerRegistry::WorkerRegistry() {}

void WorkerRegistry::registerWorker(const std::string &ecu_id, const std::string &endpoint) {
    workers_[ecu_id] = endpoint;
}

bool WorkerRegistry::hasWorker(const std::string &ecu_id) const {
    return workers_.find(ecu_id) != workers_.end();
}

std::string WorkerRegistry::endpointFor(const std::string &ecu_id) const {
    auto it = workers_.find(ecu_id);
    if (it == workers_.end()) return "";
    return it->second;
}

std::vector<std::string> WorkerRegistry::ecuIds() const {
    std::vector<std::string> ids;
    for (const auto &p : workers_) ids.push_back(p.first);
    return ids;
}

Json WorkerRegistry::status() const {
    Json s = Json::object();
    for (const auto &p : workers_) s[p.first] = p.second;
    return s;
}

} // namespace vclab
