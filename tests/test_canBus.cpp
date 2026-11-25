#include <gtest/gtest.h>
#include "CanBus.hpp"
TEST(CanBusTest, InternalBroadcastAndNoSelfReception) {
    CanBus tx(CanBus::Backend::Internal,"bus-test"),a(CanBus::Backend::Internal,"bus-test"),b(CanBus::Backend::Internal,"bus-test");
    ASSERT_TRUE(tx.init());ASSERT_TRUE(a.init());ASSERT_TRUE(b.init());
    tx.send(Can(0,{0xaa,0xbb}));
    Can frame;
    ASSERT_TRUE(a.receive(frame,std::chrono::milliseconds(0)));EXPECT_EQ(frame.getId(),0U);EXPECT_EQ(frame.getData()[1],0xbb);
    ASSERT_TRUE(b.receive(frame,std::chrono::milliseconds(0)));
    EXPECT_FALSE(tx.receive(frame,std::chrono::milliseconds(0)));
}
TEST(CanBusTest, ChannelIsolationAndBounds) {
    CanBus a(CanBus::Backend::Internal,"channel-a"),b(CanBus::Backend::Internal,"channel-b");a.init();b.init();
    a.send(Can(0x123,{1}));Can f;EXPECT_FALSE(b.receive(f,std::chrono::milliseconds(0)));
    EXPECT_THROW(a.send(Can(0x123,std::vector<uint8_t>(9,0))),std::invalid_argument);
    EXPECT_THROW(a.send(Can(0x800,{1})),std::invalid_argument);
    Can extended(0x12345,{1});extended.setFrameType(true);EXPECT_NO_THROW(a.send(extended));
}
TEST(CanBusTest, MissingSocketCanInterfaceFailsCleanly) {
    CanBus bus(CanBus::Backend::SocketCAN,"missing999");EXPECT_FALSE(bus.init());EXPECT_FALSE(bus.init());
}
TEST(CanBusTest, CanFdPayloadFlagsAndWireLengthValidation) {
    CanBus a(CanBus::Backend::Internal,"fd-bus"),b(CanBus::Backend::Internal,"fd-bus");a.init();b.init();
    Can frame(0x123,std::vector<uint8_t>(64,0xab));frame.setFd(true,true);a.send(frame);
    Can received;ASSERT_TRUE(b.receive(received,std::chrono::milliseconds(0)));
    EXPECT_TRUE(received.isFd());EXPECT_TRUE(received.bitrateSwitch());EXPECT_EQ(received.getData().size(),64U);
    Can malformed(0x123,std::vector<uint8_t>(9,0));malformed.setFd(true,true);
    EXPECT_THROW(a.send(malformed),std::invalid_argument);
}
