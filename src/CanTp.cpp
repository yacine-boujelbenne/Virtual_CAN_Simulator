#include "CanTp.hpp"
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>
using namespace std::chrono;
namespace {
uint64_t nowMs() {
    return static_cast<uint64_t>(steady_clock::now().time_since_epoch().count() / 1000000);
}
}
CanTp::CanTp(uint32_t tx, uint32_t rx, CanBus &bus) : txId_(tx), rxId_(rx), bus_(&bus) {rebuild();}
CanTp::CanTp(uint32_t tx, uint32_t rx)
    : txId_(tx), rxId_(rx), owned_(std::make_unique<CanBus>()), bus_(owned_.get()) {
    if (!bus_->init()) throw std::runtime_error("Cannot initialize CAN");
    rebuild();
}
void CanTp::rebuild() {
    IsoTpConfig config;
    config.txId = txId_;
    config.rxId = rxId_;
    config.fd = fd_;
    config.linkLength = linkLength_;
    config.blockSize = bs_;
    config.stmin = st_;
    session_ = IsoTp(config);
}
Can CanTp::waitFrame(unsigned timeoutMs) {
    auto deadline = steady_clock::now() + milliseconds(timeoutMs); Can frame;
    while (steady_clock::now() < deadline)
        if (bus_->receive(frame, milliseconds(20)) && frame.getId() == rxId_) return frame;
    throw std::runtime_error("ISO-TP receive/flow-control timeout");
}
void CanTp::setLinkLength(unsigned length, bool fd) {
    const std::vector<unsigned> legal={8,12,16,20,24,32,48,64};
    if(std::find(legal.begin(),legal.end(),length)==legal.end()||(!fd&&length!=8))throw std::invalid_argument("Invalid ISO-TP link length");
    linkLength_=length;fd_=fd;rebuild();
}
void CanTp::transmit(const std::vector<uint8_t> &bytes) {
    Can frame(txId_,bytes);frame.setFrameType(txId_>0x7ff);frame.setFd(fd_,fd_);bus_->send(frame);
}
void CanTp::sendMessage(const std::string &message) {
    std::vector<uint8_t> payload(message.begin(), message.end());
    for (auto &frame : session_.startSend(payload, nowMs())) transmit(frame.data);
    // The receiver answers the first frame with flow control; every block waits for the next one.
    while (session_.transmitting()) {
        auto frame = waitFrame(static_cast<unsigned>(session_.config().nAs));
        IsoTpFrame incoming;
        incoming.id = frame.getId();
        incoming.data = frame.getData();
        incoming.fd = frame.isFd();
        incoming.brs = frame.bitrateSwitch();
        for (auto &out : session_.onFrame(incoming, nowMs())) {
            if (out.delayUs) std::this_thread::sleep_for(microseconds(out.delayUs));
            transmit(out.data);
        }
    }
}
std::string CanTp::receiveMessage() {
    while (true) {
        auto frame = waitFrame(static_cast<unsigned>(session_.config().nCr));
        IsoTpFrame incoming;
        incoming.id = frame.getId();
        incoming.data = frame.getData();
        incoming.fd = frame.isFd();
        incoming.brs = frame.bitrateSwitch();
        for (auto &out : session_.onFrame(incoming, nowMs())) transmit(out.data);
        if (session_.hasMessage()) {
            auto payload = session_.takeMessage();
            return std::string(payload.begin(), payload.end());
        }
    }
}
