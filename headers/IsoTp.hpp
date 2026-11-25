#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Event-driven ISO-TP (ISO 15765-2) session for one pair of CAN identifiers.
// Normal addressing, or 11-bit extended addressing with source/target address bytes.
// Classic CAN and CAN FD links, payloads of 1..4095 bytes, ISO-TP 2013 timeouts.
struct IsoTpConfig {
    uint32_t txId = 0x7e0, rxId = 0x7e8;
    bool fd = false;
    unsigned linkLength = 8;
    uint8_t blockSize = 8, stmin = 0;
    bool extendedAddressing = false;
    uint8_t sourceAddress = 0, targetAddress = 0;
    // N_As, N_Bs, N_Ar and N_Cr in milliseconds.
    double nAs = 1000, nBs = 1000, nAr = 1000, nCr = 1000;
};

struct IsoTpFrame {
    uint32_t id = 0;
    std::vector<uint8_t> data;
    bool fd = false, brs = false;
    uint32_t delayUs = 0;
};

// The session never touches a bus: the owner feeds frames in and puts the returned frames on the wire.
class IsoTp {
public:
    explicit IsoTp(IsoTpConfig config);
    static constexpr size_t maxPayload = 4095;
    static const std::vector<unsigned> &fdLengths();
    static uint32_t stminToUs(uint8_t value);
    // Starts a transmission and returns the frames to send (single frame or first frame).
    std::vector<IsoTpFrame> startSend(const std::vector<uint8_t> &payload, uint64_t nowMs = 0);
    // Feeds a received frame and returns the frames to answer with (flow control, consecutive frames).
    std::vector<IsoTpFrame> onFrame(const IsoTpFrame &frame, uint64_t nowMs = 0);
    // Timeout supervision; throws std::runtime_error when an N_* timer expires.
    void poll(uint64_t nowMs);
    bool hasMessage() const { return hasMessage_;}
    const std::vector<uint8_t> &message() const { return message_;}
    std::vector<uint8_t> takeMessage() {
        std::vector<uint8_t> out;
        out.swap(message_);
        hasMessage_ = false;
        return out;
    }
    bool transmitting() const { return tx_.active;}
    const IsoTpConfig &config() const { return config_;}
private:
    struct Tx {
        bool active = false;
        std::vector<uint8_t> payload;
        size_t offset = 0;
        uint8_t sequence = 1;
        unsigned waits = 0;
    };
    struct Rx {
        bool active = false;
        size_t length = 0;
        std::vector<uint8_t> payload;
        uint8_t sequence = 1;
        unsigned count = 0;
    };
    IsoTpConfig config_;
    Tx tx_;
    Rx rx_;
    bool hasMessage_ = false;
    std::vector<uint8_t> message_;
    double fcDeadline_ = 0, blockDeadline_ = 0, rxDeadline_ = 0, completeDeadline_ = 0;
    size_t addressOffset() const { return config_.extendedAddressing ? 2 : 0;}
    std::vector<IsoTpFrame> buildFrame(std::vector<uint8_t> data, uint32_t delayUs = 0) const;
    std::vector<IsoTpFrame> flowControl() const;
    void checkAddresses(const std::vector<uint8_t> &data) const;
};
