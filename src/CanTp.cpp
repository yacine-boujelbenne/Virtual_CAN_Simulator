#include "CanTp.hpp"
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>
using namespace std::chrono;
CanTp::CanTp(uint32_t tx, uint32_t rx, CanBus &bus) : txId_(tx), rxId_(rx), bus_(&bus) {}
CanTp::CanTp(uint32_t tx, uint32_t rx)
    : txId_(tx), rxId_(rx), owned_(std::make_unique<CanBus>()), bus_(owned_.get()) {
    if (!bus_->init()) throw std::runtime_error("Cannot initialize CAN");
}
Can CanTp::waitFrame() {
    auto deadline = steady_clock::now() + seconds(1); Can frame;
    while (steady_clock::now() < deadline)
        if (bus_->receive(frame, milliseconds(20)) && frame.getId() == rxId_) return frame;
    throw std::runtime_error("ISO-TP receive/flow-control timeout");
}
void CanTp::setLinkLength(unsigned length,bool fd) {
    const std::vector<unsigned> legal={8,12,16,20,24,32,48,64};
    if(std::find(legal.begin(),legal.end(),length)==legal.end()||(!fd&&length!=8))throw std::invalid_argument("Invalid ISO-TP link length");
    linkLength_=length;fd_=fd;
}
void CanTp::transmit(const std::vector<uint8_t> &bytes) {
    auto payload=bytes;
    if(fd_) {
        const std::vector<unsigned> sizes={0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64};
        auto it=std::find_if(sizes.begin(),sizes.end(),[&](unsigned n){return n>=payload.size();});
        if(it==sizes.end())throw std::invalid_argument("ISO-TP frame too long");
        payload.resize(*it,0);
    }
    Can frame(txId_,payload);frame.setFrameType(txId_>0x7ff);frame.setFd(fd_,fd_);bus_->send(frame);
}
void CanTp::sendMessage(const std::string &message) {
    if (message.empty() || message.size() > 4095) throw std::invalid_argument("ISO-TP payload must be 1..4095 bytes");
    if(message.size()<=(fd_&&linkLength_>8?linkLength_-2:7)) {
        std::vector<uint8_t> bytes;
        if(fd_&&linkLength_>8&&message.size()>7)bytes={0,static_cast<uint8_t>(message.size())};
        else bytes={static_cast<uint8_t>(message.size())};
        bytes.insert(bytes.end(), message.begin(), message.end()); transmit(bytes); return;
    }
    std::vector<uint8_t> bytes{static_cast<uint8_t>(0x10 | (message.size() >> 8)), static_cast<uint8_t>(message.size())};
    bytes.insert(bytes.end(), message.begin(), message.begin() + (linkLength_-2)); transmit(bytes);
    size_t offset = linkLength_-2; uint8_t sequence = 1; unsigned waits = 0;
    while (offset < message.size()) {
        auto fc = waitFrame().getData();
        if (fc.size() < 3 || (fc[0] >> 4) != 3) throw std::runtime_error("Malformed ISO-TP flow control");
        if ((fc[0] & 15) == 1 && ++waits <= 3) continue;
        if ((fc[0] & 15) != 0) throw std::runtime_error("ISO-TP flow control aborted");
        auto delay = fc[2] <= 0x7f ? microseconds(fc[2] * 1000) :
            (fc[2] >= 0xf1 && fc[2] <= 0xf9 ? microseconds((fc[2] - 0xf0) * 100) : microseconds(-1));
        if (delay.count() < 0) throw std::runtime_error("Reserved ISO-TP STmin");
        unsigned block = fc[1] == 0 ? 4095 : fc[1];
        for (unsigned i = 0; i < block && offset < message.size(); ++i) {
            std::this_thread::sleep_for(delay);
            size_t count = std::min<size_t>(linkLength_-1, message.size() - offset);
            bytes = {static_cast<uint8_t>(0x20 | sequence)};
            bytes.insert(bytes.end(), message.begin() + offset, message.begin() + offset + count);
            transmit(bytes); offset += count; sequence = (sequence + 1) & 15;
        }
    }
}
std::string CanTp::receiveMessage() {
    auto first=waitFrame();auto bytes=first.getData();
    if (bytes.empty()) throw std::runtime_error("Empty ISO-TP frame");
    unsigned type = bytes[0] >> 4;
    if (type == 0) {
        size_t length=bytes[0]&15,header=1;
        if(length==0&&first.isFd()&&bytes.size()>8) {length=bytes[1];header=2;}
        if (!length || (header==1&&length>7) || bytes.size() < length + header) throw std::runtime_error("Malformed ISO-TP single frame");
        return std::string(bytes.begin() + header, bytes.begin() + header + length);
    }
    if (type != 1 || bytes.size() < 8) throw std::runtime_error("Malformed ISO-TP first frame");
    size_t length = ((bytes[0] & 15) << 8) | bytes[1];
    if (length <= bytes.size()-2) throw std::runtime_error("Invalid ISO-TP first-frame length");
    std::string message(bytes.begin() + 2, bytes.end());
    uint8_t sequence = 1; unsigned received = 0;
    auto sendFC = [&]{
        if (st_ > 0x7f && !(st_ >= 0xf1 && st_ <= 0xf9)) throw std::runtime_error("Reserved ISO-TP STmin");
        transmit({0x30, bs_, st_});
    };
    sendFC();
    while (message.size() < length) {
        bytes = waitFrame().getData();
        if (bytes.size() < 2 || (bytes[0] >> 4) != 2 || (bytes[0] & 15) != sequence)
            throw std::runtime_error("ISO-TP consecutive frame/sequence error");
        size_t count = std::min<size_t>(bytes.size() - 1, length - message.size());
        message.append(bytes.begin() + 1, bytes.begin() + 1 + count);
        sequence = (sequence + 1) & 15;
        if (++received == bs_ && message.size() < length) {received = 0; sendFC();}
    }
    return message;
}
