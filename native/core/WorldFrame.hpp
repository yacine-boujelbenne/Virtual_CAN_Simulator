#pragma once

#include "Types.hpp"
#include "json.hpp"

using Json = nlohmann::json;

namespace vclab {

struct WorldFrame {
    EpochId epoch = 0;
    TickId tick = 0;
    SimTimeUs sim_time_us = 0;
    Json ego = Json::object();
    Json world = Json::object();
    Json observations = Json::object();
    Json metadata = Json::object();
};

} // namespace vclab
