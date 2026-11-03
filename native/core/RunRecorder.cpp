#include "RunRecorder.hpp"

namespace vclab {

RunRecorder::RunRecorder(const RecorderConfig &config) : config_(config) {}

bool RunRecorder::record(const Json &event) {
    if (finalized_) {
        integrity_ok_ = false;
        integrity_error_ = "recorder_finalized";
        return false;
    }
    if (events_.size() >= config_.max_events) {
        integrity_ok_ = false;
        integrity_error_ = "recorder_capacity_events";
        return false;
    }
    events_.push_back(event);
    return true;
}

bool RunRecorder::recordFrame(const WorldFrame &frame) {
    Json event = Json::object();
    event["type"] = "world_frame";
    event["epoch"] = frame.epoch;
    event["tick"] = frame.tick;
    event["sim_time_us"] = frame.sim_time_us;
    event["ego"] = frame.ego;
    event["world"] = frame.world;
    event["observations"] = frame.observations;
    event["metadata"] = frame.metadata;
    return record(event);
}

void RunRecorder::flush() {
    // No-op for in-memory recorder; seam for durable backends
}

void RunRecorder::finalize(const Json &manifest) {
    if (finalized_) {
        return;
    }
    Json finalize_event = Json::object();
    finalize_event["type"] = "recorder_finalize";
    finalize_event["manifest"] = manifest;
    finalize_event["event_count"] = events_.size();
    if (integrity_ok_) {
        events_.push_back(finalize_event);
    }
    finalized_ = true;
}

Json RunRecorder::status() const {
    Json status = Json::object();
    status["event_count"] = events_.size();
    status["finalized"] = finalized_;
    status["integrity_ok"] = integrity_ok_;
    if (!integrity_error_.empty()) {
        status["integrity_error"] = integrity_error_;
    }
    status["max_events"] = config_.max_events;
    status["max_bytes"] = config_.max_bytes;
    return status;
}

const std::vector<Json> &RunRecorder::events() const {
    return events_;
}

} // namespace vclab
