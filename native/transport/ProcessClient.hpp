#pragma once
#include "json.hpp"
#include <string>
#include <sys/types.h>

namespace vclab {
// A persistent, isolated native worker. Never restarts a failed ECU implicitly.
class ProcessClient {
public:
    explicit ProcessClient(const std::string &binary);
    ~ProcessClient();
    ProcessClient(const ProcessClient&) = delete;
    ProcessClient& operator=(const ProcessClient&) = delete;
    nlohmann::json request(const nlohmann::json &command, int timeout_ms = 5000);
    pid_t pid() const { return pid_; }
private:
    void stop();
    int fd_ = -1;
    pid_t pid_ = -1;
    std::string buffer_;
};
}
