#include <gtest/gtest.h>
#include "Receiver.hpp"
TEST(ReceiverTest, IdentityAndAvailability) {
    Receiver receiver("Receiver",true);receiver.setEcuName("Updated");
    EXPECT_EQ(receiver.getEcuName(),"Updated");receiver.setAvailability(false);EXPECT_FALSE(receiver.isAvailable());
}
