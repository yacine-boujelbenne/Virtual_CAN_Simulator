#pragma once
#include "CanBus.hpp"
#include "IsoTp.hpp"
#include <memory>
#include <string>

// Blocking ISO-TP endpoint on top of the event-driven session: one transfer at a time.
class CanTp {
public:
    CanTp(uint32_t txId, uint32_t rxId, CanBus &bus);
    CanTp(uint32_t txId, uint32_t rxId);
    void setLinkLength(unsigned length, bool fd = true);
    static constexpr uint8_t staticBS = 255, staticST = 127;
    void sendMessage(const std::string &message);
    void sendMessageP(const std::string &message) {sendMessage(message);}
    std::string receiveMessage();
    void setbusManager(CanBus &bus) {owned_.reset(); bus_ = &bus;}
    CanBus &getbus() {return *bus_;}
    void setRx(int id) {rxId_ = static_cast<uint32_t>(id); rebuild();}
    void setTx(int id) {txId_ = static_cast<uint32_t>(id); rebuild();}
    void setbs(uint8_t value) {bs_ = value; rebuild();}
    uint8_t getbs() const {return bs_;}
    void setst(uint8_t value) {st_ = value; rebuild();}
    uint8_t getst() const {return st_;}
private:
    Can waitFrame(unsigned timeoutMs);
    void transmit(const std::vector<uint8_t> &bytes);
    void rebuild();
    uint32_t txId_, rxId_;
    std::unique_ptr<CanBus> owned_;
    CanBus *bus_;
    uint8_t bs_ = 8, st_ = 0;
    unsigned linkLength_ = 8;
    bool fd_ = false;
    IsoTp session_{IsoTpConfig{}};
};
