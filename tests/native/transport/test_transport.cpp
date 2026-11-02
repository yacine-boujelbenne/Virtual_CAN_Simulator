#include <gtest/gtest.h>
#include "transport/WorkerRegistry.hpp"
#include "transport/Barrier.hpp"

using namespace vclab;

TEST(Transport, WorkerRegistryEnforcesOnePerEcu) {
    WorkerRegistry reg;
    reg.registerWorker("ECU_A", "http://worker-a:9001");
    EXPECT_TRUE(reg.hasWorker("ECU_A"));
    EXPECT_FALSE(reg.hasWorker("MISSING"));
    EXPECT_EQ(reg.endpointFor("ECU_A"), "http://worker-a:9001");
    auto ids = reg.ecuIds();
    EXPECT_EQ(ids.size(), 1u);
}

TEST(Transport, BarrierWaitsForAllDeclaredWorkers) {
    Barrier b;
    b.prepare(1, 10);
    b.expect("ECU_A");
    b.expect("ECU_B");
    EXPECT_FALSE(b.ready());
    b.complete("ECU_A");
    EXPECT_FALSE(b.ready());
    b.complete("ECU_B");
    EXPECT_TRUE(b.ready());
    EXPECT_EQ(b.state(), "ready");
}
