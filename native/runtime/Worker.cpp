#include "Worker.hpp"
#include <cstdlib>
#include <unistd.h>

namespace vclab {
Json Worker::health() const {
    return {{"status", "ready"}, {"configured", bool(kernel_)}, {"halted", halted_}, {"pid", ::getpid()}};
}
Json Worker::request(Json command) {
    if (!command.is_object()) throw std::invalid_argument("Worker command must be an object");
    auto action = command.value("action", "snapshot");
    Json result;
    if (action == "fault") {
        auto mode = command.value("mode", "");
        if (mode != "halt" && mode != "restore") throw std::invalid_argument("Unknown ECU fault");
        halted_ = mode == "halt";
        result = {{"halted", halted_}};
    } else if (action == "configure") {
        auto next = std::make_unique<EcuKernel>(command.at("definition"), command.value("step_ms", 10U));
        result = next->command({{"action", "reset"}, {"epoch", command.value("epoch", 1ULL)}, {"start_tick", command.value("start_tick", 0ULL)}});
        kernel_ = std::move(next); halted_ = false; sessions_.clear();
    } else {
        if (!kernel_) throw std::invalid_argument("ECU is not configured");
        if (halted_) throw std::runtime_error("ECU watchdog: kernel halted");
        if (action == "isotp_start") {
            auto session = command.at("session").get<std::string>();
            if (session.size() > 128 || (sessions_.size() >= 32 && !sessions_.contains(session)))
                throw std::invalid_argument("ISO-TP session limit");
            command["tx_id"] = command.value("response_id", 0x7e8);
            command["rx_id"] = command.value("request_id", 0x7e0);
            command["respond"] = true;
            kernel_->command(command);
            sessions_[session] = std::chrono::steady_clock::now();
            result = {{"ready", true}};
        } else {
            if (action == "isotp_frame") {
                auto session = command.at("session").get<std::string>();
                auto found = sessions_.find(session);
                if (found == sessions_.end()) throw std::invalid_argument("Unknown ISO-TP session");
                if (std::chrono::steady_clock::now() - found->second > std::chrono::seconds(10)) {
                    kernel_->command({{"action", "isotp_end"}, {"session", session}});
                    sessions_.erase(found); throw std::invalid_argument("ISO-TP session timeout");
                }
            }
            if (action == "reset") sessions_.clear();
            if (action == "isotp_end") sessions_.erase(command.at("session").get<std::string>());
            result = kernel_->command(command);
        }
    }
    const char *service = std::getenv("ECU_SERVICE");
    result["runtime"] = {{"kernel_pid", ::getpid()}, {"service", service ? service : "native-ecu"}};
    return result;
}
}
