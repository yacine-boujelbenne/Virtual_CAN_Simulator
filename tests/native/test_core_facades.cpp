#include <gtest/gtest.h>
#include "core/ApplicationService.hpp"
#include "core/Types.hpp"

using namespace vclab;

TEST(CoreFacades, CommandSubmitEnqueues) {
    ApplicationService svc;
    CommandEnvelope env;
    env.command_id = 1;
    env.expected_epoch = 0;
    env.requested_boundary = "tick";
    env.payload["cmd"] = "ping";
    auto res = svc.submit(env);
    EXPECT_EQ(res.command_id, 1u);
    EXPECT_EQ(res.state, CommandState::Enqueued);
}

TEST(CoreFacades, SnapshotAndWorldFrameExposeState) {
    ApplicationService svc;
    auto snap = svc.snapshot();
    EXPECT_EQ(snap.epoch, 0u);
    EXPECT_EQ(snap.tick, 0u);
    EXPECT_EQ(snap.sim_time_us, 0u);
    auto frame = svc.worldFrame();
    EXPECT_EQ(frame.epoch, 0u);
    EXPECT_EQ(frame.tick, 0u);
}

TEST(CoreFacades, JobsAndRecorderPresent) {
    ApplicationService svc;
    EXPECT_TRUE(svc.jobs().empty());
    auto &rec = svc.recorder();
    Json ev = Json::object();
    ev["type"] = "test";
    EXPECT_TRUE(rec.record(ev));
    auto status = rec.status();
    EXPECT_TRUE(status.contains("event_count"));
    EXPECT_TRUE(status.contains("integrity_ok"));
}
