#pragma once

#include "Types.hpp"
#include "json.hpp"
#include <optional>
#include <string>

using Json = nlohmann::json;

namespace vclab {

struct CommandEnvelope {
    CommandId command_id = 0;
    EpochId expected_epoch = 0;
    std::string requested_boundary;
    Json payload = Json::object();
};

enum class CommandState {
    Enqueued,
    Accepted,
    Rejected,
    Applied
};

struct CommandResult {
    CommandId command_id = 0;
    CommandState state = CommandState::Rejected;
    std::string effective_boundary;
    Json error = Json::object();
};

} // namespace vclab
