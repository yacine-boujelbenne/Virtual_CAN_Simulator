#pragma once

#include "Types.hpp"
#include "WorldFrame.hpp"
#include "json.hpp"
#include <string>
#include <vector>

using Json = nlohmann::json;

namespace vclab {

struct RecorderConfig {
    size_t max_events = 100000;
    size_t max_bytes = 256 * 1024 * 1024;
};

class RunRecorder {
public:
    explicit RunRecorder(const RecorderConfig &config = RecorderConfig{});
    bool record(const Json &event);
    bool recordFrame(const WorldFrame &frame);
    void flush();
    void finalize(const Json &manifest = Json::object());
    Json status() const;
    const std::vector<Json> &events() const;

private:
    RecorderConfig config_;
    std::vector<Json> events_;
    bool finalized_ = false;
    bool integrity_ok_ = true;
    std::string integrity_error_;
};

} // namespace vclab
