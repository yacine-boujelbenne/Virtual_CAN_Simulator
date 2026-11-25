#pragma once

#include <map>
#include <string>
#include "core/Types.hpp"

namespace vclab {

class Barrier {
public:
    Barrier();
    void prepare(vclab::EpochId epoch, vclab::TickId tick);
    bool expect(const std::string &ecu_id);
    bool complete(const std::string &ecu_id, const std::string &result_id = "");
    bool ready() const;
    std::string state() const;

private:
    vclab::EpochId epoch_ = 0;
    vclab::TickId tick_ = 0;
    std::map<std::string, bool> expected_;
    std::map<std::string, bool> completed_;
};

} // namespace vclab
