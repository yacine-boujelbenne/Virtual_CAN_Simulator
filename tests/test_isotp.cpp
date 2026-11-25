#include <gtest/gtest.h>
#include "IsoTp.hpp"
#include <string>

// Native coverage of the event-driven ISO-TP session. The conformance vectors in
// tests/vectors/isotp replay the same scenarios against the Python oracle; this file
// keeps the protocol covered inside ctest, without the Python stack.
namespace {
std::vector<uint8_t> bytes(std::initializer_list<int> values) {
    std::vector<uint8_t> out;
    for (int value: values) out.push_back(static_cast<uint8_t>(value));
    return out;
}
IsoTpFrame frameFrom(uint32_t id, const std::vector<uint8_t> &data, bool fd = false) {
    IsoTpFrame frame;
    frame.id = id;
    frame.data = data;
    frame.fd = fd;
    frame.brs = fd;
    return frame;
}
IsoTpConfig classic() {
    IsoTpConfig config;
    config.txId = 0x7e0;
    config.rxId = 0x7e8;
    return config;
}
std::vector<uint8_t> range(size_t count, uint8_t start = 0) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i < count; ++i) out.push_back(static_cast<uint8_t>(start + i));
    return out;
}
void construct(const IsoTpConfig &config) { IsoTp session(config); }
// The other side of the wire: the identifiers and the address bytes are mirrored.
IsoTpConfig peer(const IsoTpConfig &config) {
    IsoTpConfig out=config;
    out.txId=config.rxId;
    out.rxId=config.txId;
    std::swap(out.sourceAddress,out.targetAddress);
    return out;
}
}

TEST(IsoTpSession, SingleFrameClassic) {
    IsoTp sender(classic()),receiver(peer(classic()));
    auto frames=sender.startSend(bytes({0x22,0xf1,0x90}),0);
    ASSERT_EQ(frames.size(),1U);
    EXPECT_EQ(frames[0].data,bytes({0x03,0x22,0xf1,0x90}));
    EXPECT_TRUE(receiver.onFrame(frameFrom(0x7e0,frames[0].data),0).empty());
    EXPECT_TRUE(receiver.hasMessage());
    EXPECT_EQ(receiver.message(),bytes({0x22,0xf1,0x90}));
    EXPECT_FALSE(sender.transmitting());
}

TEST(IsoTpSession, MultiFrameBlocksAndSequenceNumbers) {
    IsoTpConfig config=classic();config.blockSize=4;
    IsoTp sender(config),receiver(peer(config));
    auto payload=range(40);
    auto frames=sender.startSend(payload,0);
    ASSERT_EQ(frames.size(),1U);
    EXPECT_EQ(frames[0].data,bytes({0x10,0x28,0,1,2,3,4,5}));
    auto flow=receiver.onFrame(frameFrom(0x7e0,frames[0].data),0);
    ASSERT_EQ(flow.size(),1U);
    EXPECT_EQ(flow[0].data,bytes({0x30,0x04,0x00}));
    frames=sender.onFrame(frameFrom(0x7e8,flow[0].data),0);
    ASSERT_EQ(frames.size(),4U);
    EXPECT_EQ(frames[0].data,bytes({0x21,6,7,8,9,10,11,12}));
    EXPECT_EQ(frames[3].data,bytes({0x24,27,28,29,30,31,32,33}));
    EXPECT_TRUE(sender.transmitting());
    for (size_t index=0;index<frames.size();++index) {
        auto answer=receiver.onFrame(frameFrom(0x7e0,frames[index].data),0);
        if (index+1<frames.size()) {EXPECT_TRUE(answer.empty());continue;}
        ASSERT_EQ(answer.size(),1U);
        EXPECT_EQ(answer[0].data,bytes({0x30,0x04,0x00}));
    }
    frames=sender.onFrame(frameFrom(0x7e8,bytes({0x30,0x04,0x00})),0);
    ASSERT_EQ(frames.size(),1U);
    EXPECT_EQ(frames[0].data,bytes({0x25,34,35,36,37,38,39}));
    EXPECT_FALSE(sender.transmitting());
    EXPECT_TRUE(receiver.onFrame(frameFrom(0x7e0,frames[0].data),0).empty());
    EXPECT_TRUE(receiver.hasMessage());
    EXPECT_EQ(receiver.message(),payload);
}

TEST(IsoTpSession, TransmitterIsReleasedAfterTheLastFrame) {
    IsoTpConfig config=classic();config.blockSize=8;
    IsoTp sender(config),receiver(peer(config));
    auto payload=range(40);
    sender.startSend(payload,0);
    auto flow=receiver.onFrame(frameFrom(0x7e0,bytes({0x10,0x28,0,1,2,3,4,5})),0);
    auto frames=sender.onFrame(frameFrom(0x7e8,flow[0].data),0);
    for (auto &block: frames) receiver.onFrame(frameFrom(0x7e0,block.data),0);
    EXPECT_FALSE(sender.transmitting());
    // A second transfer on the same session is accepted right away.
    EXPECT_NO_THROW(sender.startSend(bytes({0x22,0xf1,0x90}),0));
}

TEST(IsoTpSession, ExtendedAddressingCarriesSourceAndTarget) {
    IsoTpConfig config=classic();config.extendedAddressing=true;config.sourceAddress=0xf1;config.targetAddress=0xf2;
    IsoTp sender(config),receiver(peer(config));
    auto frames=sender.startSend(bytes({0x22,0xf1,0x90}),0);
    ASSERT_EQ(frames.size(),1U);
    EXPECT_EQ(frames[0].data,bytes({0xf1,0xf2,0x03,0x22,0xf1,0x90}));
    EXPECT_TRUE(receiver.onFrame(frameFrom(0x7e0,frames[0].data),0).empty());
    EXPECT_EQ(receiver.message(),bytes({0x22,0xf1,0x90}));
    EXPECT_THROW(receiver.onFrame(frameFrom(0x7e0,bytes({0xf1,0xf1,0x03,0x22})),0),std::runtime_error);
}

TEST(IsoTpSession, CanFdEscapeSingleFrameAndWideFrames) {
    IsoTpConfig config=classic();config.fd=true;config.linkLength=12;
    IsoTp sender(config),receiver(peer(config));
    auto frames=sender.startSend(range(9),0);
    ASSERT_EQ(frames.size(),1U);
    EXPECT_EQ(frames[0].data.size(),12U);
    EXPECT_EQ(frames[0].data[0],0x00);
    EXPECT_EQ(frames[0].data[1],0x09);
    EXPECT_TRUE(receiver.onFrame(frameFrom(0x7e0,frames[0].data,true),0).empty());
    EXPECT_EQ(receiver.message(),range(9));
    auto payload=range(30,0x10);
    frames=sender.startSend(payload,0);
    ASSERT_EQ(frames.size(),1U);
    EXPECT_EQ(frames[0].data[0],0x10);
    EXPECT_EQ(frames[0].data[1],0x1e);
    auto flow=receiver.onFrame(frameFrom(0x7e0,frames[0].data,true),0);
    ASSERT_EQ(flow.size(),1U);
    frames=sender.onFrame(frameFrom(0x7e8,flow[0].data,true),0);
    ASSERT_EQ(frames.size(),2U);
    EXPECT_EQ(frames[0].data.size(),12U);
    EXPECT_EQ(frames[0].data[0],0x21);
    EXPECT_EQ(frames[1].data[0],0x22);
    for (auto &block: frames) receiver.onFrame(frameFrom(0x7e0,block.data,true),0);
    EXPECT_TRUE(receiver.hasMessage());
    EXPECT_EQ(receiver.message(),payload);
}

TEST(IsoTpSession, TimeoutNAsGivesUpWithoutFlowControl) {
    IsoTpConfig config=classic();config.nAs=100;
    IsoTp sender(config);
    sender.startSend(range(40),0);
    EXPECT_NO_THROW(sender.poll(99));
    EXPECT_THROW(sender.poll(100),std::runtime_error);
}

TEST(IsoTpSession, TimeoutNBsGivesUpWithoutNextBlock) {
    IsoTpConfig config=classic();config.nBs=100;config.blockSize=1;
    IsoTp sender(config),receiver(peer(config));
    sender.startSend(range(20),0);
    auto flow=receiver.onFrame(frameFrom(0x7e0,bytes({0x10,0x14,0,1,2,3,4,5})),0);
    sender.onFrame(frameFrom(0x7e8,flow[0].data),0);
    EXPECT_NO_THROW(sender.poll(99));
    EXPECT_THROW(sender.poll(100),std::runtime_error);
}

TEST(IsoTpSession, TimeoutNArGivesUpWithoutConsecutiveFrame) {
    IsoTpConfig config=classic();config.nAr=100;config.blockSize=4;
    IsoTp sender(config),receiver(peer(config));
    auto first=sender.startSend(range(40),0);
    ASSERT_EQ(first.size(),1U);
    auto flow=receiver.onFrame(frameFrom(0x7e0,first[0].data),0);
    auto frames=sender.onFrame(frameFrom(0x7e8,flow[0].data),0);
    for (auto &block: frames) receiver.onFrame(frameFrom(0x7e0,block.data),0);
    EXPECT_NO_THROW(receiver.poll(99));
    EXPECT_THROW(receiver.poll(100),std::runtime_error);
}

TEST(IsoTpSession, TimeoutNCrGivesUpOnAnIncompleteMessage) {
    IsoTpConfig config=classic();config.nCr=100;
    IsoTp receiver(peer(config));
    receiver.onFrame(frameFrom(0x7e0,bytes({0x10,0x14,0,1,2,3,4,5})),0);
    EXPECT_NO_THROW(receiver.poll(99));
    EXPECT_THROW(receiver.poll(100),std::runtime_error);
}

TEST(IsoTpSession, MalformedAndAbusiveFramesAreRejected) {
    IsoTp sender(classic()),receiver(peer(classic()));
    EXPECT_THROW(receiver.onFrame(frameFrom(0x7e0,bytes({0x07,0x01})),0),std::runtime_error);
    EXPECT_THROW(receiver.onFrame(frameFrom(0x7e0,bytes({0x10,0x14,0,1,2})),0),std::runtime_error);
    EXPECT_THROW(receiver.onFrame(frameFrom(0x7e0,bytes({0x50,0,0})),0),std::runtime_error);
    EXPECT_THROW(receiver.onFrame(frameFrom(0x7e0,bytes({0x30,0,0})),0),std::runtime_error);
    receiver.onFrame(frameFrom(0x7e0,bytes({0x10,0x14,0,1,2,3,4,5})),0);
    EXPECT_THROW(receiver.onFrame(frameFrom(0x7e0,bytes({0x22,0,0,0,0,0,0,0})),0),std::runtime_error);
    sender.startSend(range(40),0);
    EXPECT_THROW(sender.onFrame(frameFrom(0x7e8,bytes({0x30,0x04,0x80})),0),std::runtime_error);
    EXPECT_THROW(sender.onFrame(frameFrom(0x7e8,bytes({0x32,0x04,0x00})),0),std::runtime_error);
}

TEST(IsoTpSession, WaitFramesAreLimitedToThree) {
    IsoTp sender(classic());
    sender.startSend(range(40),0);
    EXPECT_NO_THROW(sender.onFrame(frameFrom(0x7e8,bytes({0x31,0x04,0x00})),0));
    EXPECT_NO_THROW(sender.onFrame(frameFrom(0x7e8,bytes({0x31,0x04,0x00})),0));
    EXPECT_NO_THROW(sender.onFrame(frameFrom(0x7e8,bytes({0x31,0x04,0x00})),0));
    EXPECT_THROW(sender.onFrame(frameFrom(0x7e8,bytes({0x31,0x04,0x00})),0),std::runtime_error);
}

TEST(IsoTpSession, ConfigurationAndPayloadsAreValidated) {
    IsoTpConfig config=classic();config.linkLength=9;
    EXPECT_THROW(construct(config),std::invalid_argument);
    config=classic();config.stmin=0x80;
    EXPECT_THROW(construct(config),std::invalid_argument);
    config=classic();config.nAs=0;
    EXPECT_THROW(construct(config),std::invalid_argument);
    IsoTp sender(classic());
    EXPECT_THROW(sender.startSend({},0),std::invalid_argument);
    EXPECT_THROW(sender.startSend(range(4096),0),std::invalid_argument);
}
