#pragma once

#include "Types.hpp"
#include <string>
#include "json.hpp"

using Json = nlohmann::json;

namespace vclab {

enum class JobState {
    Pending,
    Ready,
    Running,
    Complete,
    Failed,
    Cancelled
};

struct Job {
    JobId id = 0;
    std::string kind;
    EpochId epoch = 0;
    TickId due_tick = 0;
    SimTimeUs due_time_us = 0;
    JobState state = JobState::Pending;
    Json params = Json::object();
    Json result = Json::object();
};

} // namespace vclab
