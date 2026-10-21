#include "IsoTp.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace {
void check(bool ok, const std::string &message) {
    if (!ok) throw std::invalid_argument(message);
}
// Protocol errors detected on the wire are runtime errors; configuration errors are not.
void fail(bool ok, const std::string &message) {
    if (!ok) throw std::runtime_error(message);
}
}

IsoTp::IsoTp(IsoTpConfig config) : config_(std::move(config)) {
    const std::vector<unsigned> legal{8, 12, 16, 20, 24, 32, 48, 64};
    check(std::find(legal.begin(), legal.end(), config_.linkLength) != legal.end() &&
          (config_.fd || config_.linkLength == 8), "Invalid ISO-TP link length");
    stminToUs(config_.stmin);
    check(config_.nAs > 0 && config_.nBs > 0 && config_.nAr > 0 && config_.nCr > 0, "Invalid ISO-TP timeout");
}

const std::vector<unsigned> &IsoTp::fdLengths() {
    static const std::vector<unsigned> lengths{0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
    return lengths;
}

uint32_t IsoTp::stminToUs(uint8_t value) {
    if (value <= 0x7f) return static_cast<uint32_t>(value) * 1000;
    if (value >= 0xf1 && value <= 0xf9) return static_cast<uint32_t>(value - 0xf0) * 100;
    throw std::invalid_argument("Reserved ISO-TP STmin");
}

std::vector<IsoTpFrame> IsoTp::buildFrame(std::vector<uint8_t> data, uint32_t delayUs) const {
    if (config_.extendedAddressing) data.insert(data.begin(), {config_.sourceAddress, config_.targetAddress});
    if (config_.fd) {
        check(data.size() <= 64, "ISO-TP frame too long");
        size_t target = 64;
        for (auto length: fdLengths()) if (length >= data.size()) { target = length; break; }
        data.resize(target, 0);
    } else check(data.size() <= 8, "ISO-TP frame too long");
    IsoTpFrame frame;
    frame.id = config_.txId;
    frame.data = std::move(data);
    frame.fd = config_.fd;
    frame.brs = config_.fd;
    frame.delayUs = delayUs;
    return {frame};
}

std::vector<IsoTpFrame> IsoTp::flowControl() const {
    return buildFrame({0x30, config_.blockSize, config_.stmin});
}

void IsoTp::checkAddresses(const std::vector<uint8_t> &data) const {
    if (!config_.extendedAddressing) return;
    fail(data.size() >= 2 && data[0] == config_.targetAddress && data[1] == config_.sourceAddress,
         "ISO-TP addressing mismatch");
}

std::vector<IsoTpFrame> IsoTp::startSend(const std::vector<uint8_t> &payload, uint64_t nowMs) {
    check(!payload.empty() && payload.size() <= maxPayload, "ISO-TP PDU must be 1..4095 bytes");
    fail(!tx_.active, "ISO-TP transmitter busy");
    size_t limit = (config_.fd && config_.linkLength > 8) ? config_.linkLength - 2 : 7;
    if (payload.size() <= limit) {
        std::vector<uint8_t> data;
        if (config_.fd && config_.linkLength > 8 && payload.size() > 7) data = {0, static_cast<uint8_t>(payload.size())};
        else data = {static_cast<uint8_t>(payload.size())};
        data.insert(data.end(), payload.begin(), payload.end());
        return buildFrame(std::move(data));
    }
    std::vector<uint8_t> data{static_cast<uint8_t>(0x10 | (payload.size() >> 8)), static_cast<uint8_t>(payload.size() & 0xff)};
    data.insert(data.end(), payload.begin(), payload.begin() + (config_.linkLength - 2));
    tx_ = Tx{true, payload, config_.linkLength - 2, 1, 0};
    fcDeadline_ = nowMs + config_.nAs;
    blockDeadline_ = 0;
    return buildFrame(std::move(data));
}

std::vector<IsoTpFrame> IsoTp::onFrame(const IsoTpFrame &frame, uint64_t nowMs) {
    const auto &data = frame.data;
    fail(data.size() <= (frame.fd ? 64u : 8u), "Invalid CAN/CAN FD wire length");
    if (frame.fd) fail(std::find(fdLengths().begin(), fdLengths().end(), data.size()) != fdLengths().end(),
                       "Invalid CAN/CAN FD wire length");
    if (frame.id != config_.rxId) return {};
    fail(frame.fd == config_.fd, "ISO-TP link protocol mismatch");
    fail(!data.empty(), "Empty ISO-TP frame");
    checkAddresses(data);
    const size_t offset = addressOffset();
    const unsigned kind = data[offset] >> 4;
    if (kind == 0) {
        size_t length = data[offset] & 15, header = offset + 1;
        if (length == 0 && config_.fd && data.size() > offset + 8) { length = data[offset + 1]; header = offset + 2; }
        fail(length && !(header == offset + 1 && length > 7) && data.size() >= header + length,
             "Malformed ISO-TP single frame");
        message_.assign(data.begin() + header, data.begin() + header + length);
        hasMessage_ = true;
        rx_.active = false;
        completeDeadline_ = 0;
        rxDeadline_ = 0;
        return {};
    }
    if (kind == 1) {
        fail(data.size() >= offset + 8, "Malformed ISO-TP first frame");
        size_t length = static_cast<size_t>((data[offset] & 15) << 8) | data[offset + 1];
        fail(length > data.size() - offset - 2, "Invalid first-frame length");
        rx_ = Rx{true, length, std::vector<uint8_t>(data.begin() + offset + 2, data.end()), 1, 0};
        completeDeadline_ = nowMs + config_.nCr;
        return flowControl();
    }
    if (kind == 2) {
        fail(rx_.active && data.size() >= offset + 2 && (data[offset] & 15) == rx_.sequence, "ISO-TP sequence error");
        size_t count = std::min(data.size() - offset - 1, rx_.length - rx_.payload.size());
        rx_.payload.insert(rx_.payload.end(), data.begin() + offset + 1, data.begin() + offset + 1 + count);
        rx_.sequence = static_cast<uint8_t>((rx_.sequence + 1) & 15);
        rx_.count++;
        if (rx_.payload.size() == rx_.length) {
            message_ = rx_.payload;
            hasMessage_ = true;
            rx_.active = false;
            completeDeadline_ = 0;
            return {};
        }
        if (config_.blockSize && rx_.count == config_.blockSize) {
            rx_.count = 0;
            rxDeadline_ = nowMs + config_.nAr;
            return flowControl();
        }
        rxDeadline_ = 0;
        return {};
    }
    if (kind == 3) {
        fail(tx_.active && data.size() >= offset + 3, "Unexpected/malformed flow control");
        unsigned status = data[offset] & 15;
        if (status == 1) {
            tx_.waits++;
            fail(tx_.waits <= 3, "ISO-TP wait-frame limit exceeded");
            fcDeadline_ = nowMs + config_.nAs;
            return {};
        }
        if (status != 0) {
            tx_.active = false;
            fcDeadline_ = 0;
            blockDeadline_ = 0;
            throw std::runtime_error("ISO-TP flow control aborted");
        }
        uint32_t delay = 0;
        try { delay = stminToUs(data[offset + 2]); }
        catch (const std::invalid_argument &) { throw std::runtime_error("Reserved ISO-TP STmin"); }
        size_t block = data[offset + 1] ? data[offset + 1] : 4095;
        std::vector<IsoTpFrame> frames;
        for (size_t i = 0; i < block && tx_.offset < tx_.payload.size(); ++i) {
            size_t count = std::min<size_t>(config_.linkLength - 1, tx_.payload.size() - tx_.offset);
            std::vector<uint8_t> data{static_cast<uint8_t>(0x20 | tx_.sequence)};
            data.insert(data.end(), tx_.payload.begin() + tx_.offset, tx_.payload.begin() + tx_.offset + count);
            frames.push_back(buildFrame(std::move(data), delay)[0]);
            tx_.offset += count;
            tx_.sequence = static_cast<uint8_t>((tx_.sequence + 1) & 15);
        }
        fcDeadline_ = 0;
        // The transmission ends with the last consecutive frame of the payload: the transmitter is free again.
        if (tx_.offset >= tx_.payload.size()) tx_.active = false;
        blockDeadline_ = tx_.active ? nowMs + config_.nBs : 0;
        return frames;
    }
    throw std::runtime_error("Unsupported ISO-TP frame type");
}

void IsoTp::poll(uint64_t nowMs) {
    if (tx_.active) {
        if (fcDeadline_ > 0 && nowMs >= fcDeadline_) throw std::runtime_error("ISO-TP N_As timeout");
        if (blockDeadline_ > 0 && nowMs >= blockDeadline_) throw std::runtime_error("ISO-TP N_Bs timeout");
    }
    if (rx_.active) {
        if (rxDeadline_ > 0 && nowMs >= rxDeadline_) throw std::runtime_error("ISO-TP N_Ar timeout");
        if (completeDeadline_ > 0 && nowMs >= completeDeadline_) throw std::runtime_error("ISO-TP N_Cr timeout");
    }
}
