#pragma once

#include "Command.hpp"
#include "Job.hpp"
#include "Snapshot.hpp"
#include "Types.hpp"
#include "WorldFrame.hpp"
#include "RunRecorder.hpp"
#include <memory>
#include <string>
#include <vector>

namespace vclab {

class ApplicationService {
public:
    ApplicationService();
    ~ApplicationService();

    CommandResult submit(const CommandEnvelope &command);
    ApplicationSnapshot snapshot() const;
    WorldFrame worldFrame() const;
    const std::vector<Job> &jobs() const;
    RunRecorder &recorder();
    const RunRecorder &recorder() const;

    EpochId epoch() const { return epoch_; }
    TickId tick() const { return tick_; }
    SimTimeUs simTimeUs() const { return sim_time_us_; }
    std::string runState() const { return run_state_; }

private:
    EpochId epoch_ = 0;
    TickId tick_ = 0;
    SimTimeUs sim_time_us_ = 0;
    std::string run_state_ = "idle";
    ApplicationSnapshot snapshot_;
    WorldFrame world_frame_;
    std::vector<Job> jobs_;
    RunRecorder recorder_;
};

} // namespace vclab
