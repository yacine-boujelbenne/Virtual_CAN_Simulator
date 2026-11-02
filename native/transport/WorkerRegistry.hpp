#pragma once

#include <map>
#include <string>
#include <vector>
#include "json.hpp"

namespace vclab {

using Json = nlohmann::json;

class WorkerRegistry {
public:
    WorkerRegistry();
    void registerWorker(const std::string &ecu_id, const std::string &endpoint);
    bool hasWorker(const std::string &ecu_id) const;
    std::string endpointFor(const std::string &ecu_id) const;
    std::vector<std::string> ecuIds() const;
    Json status() const;

private:
    std::map<std::string, std::string> workers_;
};

} // namespace vclab
