#include "Barrier.hpp"

namespace vclab {

Barrier::Barrier() {}

void Barrier::prepare(vclab::EpochId epoch, vclab::TickId tick) {
    epoch_ = epoch;
    tick_ = tick;
    completed_.clear();
}

bool Barrier::expect(const std::string &ecu_id) {
    expected_[ecu_id] = true;
    return true;
}

bool Barrier::complete(const std::string &ecu_id, const std::string &result_id) {
    (void)result_id;
    completed_[ecu_id] = true;
    return true;
}

bool Barrier::ready() const {
    for (const auto &p : expected_) {
        if (completed_.find(p.first) == completed_.end() || !completed_.at(p.first)) {
            return false;
        }
    }
    return true;
}

std::string Barrier::state() const {
    return ready() ? "ready" : "waiting";
}

} // namespace vclab
