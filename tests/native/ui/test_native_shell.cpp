#include <gtest/gtest.h>
#include "ui/NativeShell.hpp"

using namespace vclab;

TEST(NativeShell, FixtureModeAndSnapshotBridge) {
    ApplicationService svc;
    NativeShell shell;
    shell.setService(&svc);
    EXPECT_TRUE(shell.isFixtureMode());
    EXPECT_EQ(shell.currentMode(), "fixture");
    auto snap = shell.readSnapshot();
    EXPECT_EQ(snap.tick, 0u);
    CommandEnvelope env;
    env.command_id = 42;
    env.requested_boundary = "tick";
    shell.requestCommand(env);
}

TEST(NativeShell, NoPhysicsOnUiThreadPath) {
    NativeShell shell;
    // No service attached - should not crash
    auto snap = shell.readSnapshot();
    EXPECT_EQ(snap.tick, 0u);
}
