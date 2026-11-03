#include "ApplicationService.hpp"

namespace vclab {

ApplicationService::ApplicationService() {
    snapshot_.epoch = epoch_;
    snapshot_.tick = tick_;
    snapshot_.sim_time_us = sim_time_us_;
    snapshot_.run_state = run_state_;
    snapshot_.project_hash = "";
    snapshot_.raw = Json::object();

    world_frame_.epoch = epoch_;
    world_frame_.tick = tick_;
    world_frame_.sim_time_us = sim_time_us_;
}

ApplicationService::~ApplicationService() = default;

CommandResult ApplicationService::submit(const CommandEnvelope &command) {
    CommandResult result;
    result.command_id = command.command_id;
    result.state = CommandState::Enqueued;
    result.effective_boundary = command.requested_boundary.empty() ? "deferred" : command.requested_boundary;
    result.error = Json::object();
    return result;
}

ApplicationSnapshot ApplicationService::snapshot() const {
    return snapshot_;
}

WorldFrame ApplicationService::worldFrame() const {
    return world_frame_;
}

const std::vector<Job> &ApplicationService::jobs() const {
    return jobs_;
}

RunRecorder &ApplicationService::recorder() {
    return recorder_;
}

const RunRecorder &ApplicationService::recorder() const {
    return recorder_;
}

} // namespace vclab
