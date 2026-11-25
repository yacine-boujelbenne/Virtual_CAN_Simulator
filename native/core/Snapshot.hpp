#pragma once

#include "Types.hpp"
#include "json.hpp"

using Json = nlohmann::json;

namespace vclab {

struct ApplicationSnapshot {
    EpochId epoch = 0;
    TickId tick = 0;
    SimTimeUs sim_time_us = 0;
    std::string run_state;
    std::string project_hash;
    Json profiles = Json::object();
    Json world = Json::object();
    Json ecus = Json::object();
    Json observations = Json::object();
    Json integrity = Json::object();
    Json raw = Json::object();
};

} // namespace vclab
