#include <gtest/gtest.h>
#include "Sender.hpp"
#include "CanTp.hpp"
TEST(SenderTest, AvailableSenderUsesTransport) {
    Sender sender("TestSender",true);
    CanBus a(CanBus::Backend::Internal,"sender"),b(CanBus::Backend::Internal,"sender");a.init();b.init();
    CanTp tx(0x700,0x708,a),rx(0x708,0x700,b);
    std::string message="Hello";sender.sendEcuData(message,tx);
    EXPECT_EQ(rx.receiveMessage(),message);
    sender.setAvailability(false);sender.sendEcuData(message,tx);
    Can frame;EXPECT_FALSE(b.receive(frame,std::chrono::milliseconds(0)));
}
