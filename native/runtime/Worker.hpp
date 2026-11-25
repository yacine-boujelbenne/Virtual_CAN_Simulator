#pragma once
#include "EcuKernel.hpp"
#include <chrono>
#include <map>
#include <memory>

namespace vclab {
// Exactly one configurable kernel in this process, shared by stdio and HTTP.
class Worker {
public:
    Json request(Json command);
    Json health() const;
private:
    std::unique_ptr<EcuKernel> kernel_;
    bool halted_ = false;
    std::map<std::string, std::chrono::steady_clock::time_point> sessions_;
};
}
