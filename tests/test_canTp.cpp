#include <gtest/gtest.h>
#include "CanTp.hpp"
#include <future>
TEST(CanTpTest, RepeatedTransfersWithBlockSizesAndSequenceWrap) {
    CanBus a(CanBus::Backend::Internal,"transport"),b(CanBus::Backend::Internal,"transport");a.init();b.init();
    CanTp tx(0x700,0x708,a),rx(0x708,0x700,b);
    for(unsigned block:{0,1,8}) {
        rx.setbs(block);rx.setst(0);
        for(unsigned size:{1,7,8,50,140,4095}) {
            std::string message;for(unsigned i=0;i<size;++i)message.push_back(static_cast<char>(i%256));
            auto incoming=std::async(std::launch::async,[&]{return rx.receiveMessage();});
            tx.sendMessage(message);EXPECT_EQ(incoming.get(),message);
        }
    }
}
TEST(CanTpTest, MalformedSingleAndConsecutiveFramesRejected) {
    CanBus a(CanBus::Backend::Internal,"malformed"),b(CanBus::Backend::Internal,"malformed");a.init();b.init();
    CanTp rx(0x708,0x700,b);
    a.send(Can(0x700,{0x07,1}));EXPECT_THROW(rx.receiveMessage(),std::runtime_error);
    a.send(Can(0x700,{0x10,20,1,2,3,4,5,6}));a.send(Can(0x700,{0x22,1,2,3}));
    EXPECT_THROW(rx.receiveMessage(),std::runtime_error);
}
TEST(CanTpTest, BoundsAndTimeout) {
    CanBus a(CanBus::Backend::Internal,"timeout");a.init();CanTp tx(0x700,0x708,a);
    EXPECT_THROW(tx.sendMessage(""),std::invalid_argument);
    EXPECT_THROW(tx.sendMessage(std::string(4096,'x')),std::invalid_argument);
    EXPECT_THROW(tx.sendMessage(std::string(50,'x')),std::runtime_error);
}
TEST(CanTpTest, CanFdSingleEscapeAndMultiFrameTransfers) {
    CanBus a(CanBus::Backend::Internal,"fd-transport"),b(CanBus::Backend::Internal,"fd-transport");a.init();b.init();
    CanTp tx(0x700,0x708,a),rx(0x708,0x700,b);
    for(unsigned link:{12,32,64}) {
        tx.setLinkLength(link);rx.setLinkLength(link);rx.setbs(1);
        for(unsigned size:{1,7,8,10,31,62,63,140,4095}) {
            std::string message(size,'F');
            auto incoming=std::async(std::launch::async,[&]{return rx.receiveMessage();});
            tx.sendMessage(message);EXPECT_EQ(incoming.get(),message);
        }
    }
}
