#include <cstdint>
#include "../headers/CanTp.hpp"

#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include "../headers/Can.hpp"
#include "../headers/CanBus.hpp"
#include "../headers/Logger.hpp"



static const size_t MAX_SF_DATA = 7;
static const size_t MAX_CF_DATA = 6;
static bool situation = true;

CanTp::CanTp(uint32_t txId, uint32_t rxId, CanBus &bus)
    : txId_(txId), rxId_(rxId), busTp_(bus), ownsBus_(false)
{
    this->setbs(8);
    this->setst(10);
}

CanTp::CanTp(uint32_t txId, uint32_t rxId)
    : txId_(txId), rxId_(rxId), busTp_(*new CanBus()), ownsBus_(true)
{
    this->setbs(8);
    this->setst(10);
    busTp_.init();
}

CanTp::~CanTp() {
    if (ownsBus_) {
        delete &busTp_;
    }
}

void CanTp::sendMessageP(const std::string &message) {
    Log::tx("CAN-TP starting transmission...");
    this->sendMessage(message);
}

void CanTp::sendMessage(const std::string &message) {
    if (message.size() <= MAX_SF_DATA) {
        sendSingleFrame(message);
    } else {
        sendingMessage = message;
        sendingOffset = 0;
        sendingSequence = 1;
        sendingMultiFrame = true;
        this->sendMultiFrame(true);
        while (situation) {
            this->sendMultiFrame(false);
        }
    }
}

void CanTp::sendSingleFrame(const std::string &message) {
    std::vector<uint8_t> data;
    Log::tx("Sending single frame with ID: " + std::to_string(txId_));
    
    uint8_t pci = (0x00 << 4) | (message.size() & 0x0F);
    data.push_back(pci);
    Log::printBytes("PCI", {pci}, "tx");

    data.insert(data.end(), message.begin(), message.end());
    Log::printBytes("DATA", data, "tx");

    Can frame(txId_, data);
    frame.setFrameType(txId_ > 0x7FF);
    busTp_.send(frame);
}

void CanTp::sendMultiFrame(bool first) {
    if (first) {
        Log::tx("Sending first frame with ID: " + std::to_string(txId_));
        std::vector<uint8_t> data;
        uint16_t length = sendingMessage.size();
        uint8_t pci1 = 0x10 | ((length >> 8) & 0x0F);
        uint8_t pci2 = length & 0xFF;
        data.push_back(pci1);
        data.push_back(pci2);

        size_t dataBytes = std::min<size_t>(6, sendingMessage.size());
        data.insert(data.end(), sendingMessage.begin(), sendingMessage.begin() + dataBytes);
        sendingOffset = dataBytes;

        Can frame(txId_, data);
        frame.setFrameType(txId_ > 0x7FF);
        Log::tx("First frame:");
        Log::printBytes("DATA", data, "tx");

        busTp_.send(frame);
    }

    Log::tx("Waiting for Flow Control frame with timeout...");
    auto start = std::chrono::steady_clock::now();
    const std::chrono::seconds timeout(2);
    bool fcReceived = false;

    while (!fcReceived && std::chrono::steady_clock::now() - start < timeout) {
        Can fcFrame = busTp_.receiveFrame();

        if (fcFrame.getId() == rxId_ && !fcFrame.getData().empty()) {
            uint8_t pci = fcFrame.getData()[0];
            uint8_t frameType = (pci >> 4) & 0x0F;

            if (frameType == 0x3) {  // Flow Control frame
                fcReceived = true;
                const auto &fcData = fcFrame.getData();
                if (fcData.size() >= 3) {
                    uint8_t flowStatus = fcData[0] & 0x0F;
                    uint8_t blockSize = fcData[1];

                    if (flowStatus == 0) {
                        Log::tx("Received Flow Control (Continue)");
                        Log::printBytes("Flow Control DATA", fcData, "rx");
                        sendNextBlock(this->bs);

                    } else if (flowStatus == 1) {
                        Log::tx("Received Flow Control (Wait)");
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        break;
                    } else {
                        Log::error("Flow Control: Overflow abort!");
                        this->sendingMultiFrame = false;
                        break;
                    }
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!fcReceived) {
        Log::error("Timeout waiting for Flow Control frame");
        this->sendingMultiFrame = false;
    }
}

void CanTp::sendNextBlock(uint8_t blockSize) {
    if (blockSize == 0) blockSize = 1;

    size_t remaining = sendingMessage.size() - sendingOffset;
    for (uint8_t i = 0; i < blockSize && remaining > 0; ++i) {
        std::vector<uint8_t> data;
        uint8_t pci = 0x20 | (sendingSequence & 0x0F);
        data.push_back(pci);

        size_t bytesToSend = std::min<size_t>(MAX_CF_DATA, remaining);
        data.insert(data.end(), sendingMessage.begin() + sendingOffset,
                    sendingMessage.begin() + sendingOffset + bytesToSend);

        Can cf(txId_, data);
        cf.setFrameType(txId_ > 0x7FF);
        Log::tx("Sent Consecutive Frame #" + std::to_string(sendingSequence));
        Log::printBytes("DATA", data, "tx");

        busTp_.send(cf);

        sendingOffset += bytesToSend;
        sendingSequence = (sendingSequence + 1) & 0x0F;
        remaining -= bytesToSend;

        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(this->getst())));
    }

    if (sendingOffset >= sendingMessage.size()) {
        sendingMultiFrame = false;
        situation = false;
        Log::tx("Multi-frame transmission complete!");
    } else {
        sendingMultiFrame = true;
        situation = true;
        sendMultiFrame(false);
    }
}

std::string CanTp::receiveMessage() {
    int retryCount = 0;
    const int MAX_RETRIES = 10000;

    while (retryCount < MAX_RETRIES) {
        Can frame = busTp_.receiveFrame();
        if (frame.getId() == 0) { retryCount++; continue; }
        if (frame.getId() != rxId_) { retryCount++; continue; }

        const auto &data = frame.getData();
        if (data.empty()) { retryCount++; continue; }

        uint8_t pci = data[0];
        uint8_t frameType = (pci >> 4) & 0x0F;

        if (frameType == 0x0) {  // Single Frame
            uint8_t length = pci & 0x0F;
            if (length > 0 && data.size() > 1) {
                Log::rx("Received single frame:");
                Log::printBytes("DATA", data, "rx");
                return std::string(data.begin() + 1, data.begin() + 1 + length);
            }
        }

        // First Frame & Consecutive Frames
        if (frameType == 0x1) {
            this->receivingTotalLength = ((pci & 0x0F) << 8) | data[1];
            this->receivingBuffer.clear();
            this->receivingBuffer.insert(this->receivingBuffer.end(), data.begin() + 2, data.end());
            this->receivingOffset = data.size() - 2;
            this->receivingSequence = 1;
            this->receivingMultiFrame = true;

            Log::rx("Received First Frame, total length: " + std::to_string(receivingTotalLength));
            Log::printBytes("DATA", data, "rx");

            this->sendFlowControl(0x00, this->bs, this->st, txId_);

            while (this->receivingOffset < this->receivingTotalLength && retryCount < MAX_RETRIES) {
                Can cf = busTp_.receiveFrame();
                if (cf.getId() == 0 || cf.getId() != rxId_ || cf.getData().empty()) {
                    this->sendFlowControl(0x00, this->bs, this->st, txId_);
                    retryCount++;
                    continue;
                }

                uint8_t cfPci = cf.getData()[0];
                if (((cfPci >> 4) & 0x0F) != 0x2) { 
                    this->sendFlowControl(0x00, this->bs, this->st, txId_);
                    continue;
                }

                uint8_t sequence = cfPci & 0x0F;
                if (sequence != this->receivingSequence) {
                    Log::error("Sequence error: expected " + std::to_string(this->receivingSequence) +
                               ", got " + std::to_string(sequence));
                    retryCount++;
                    continue;
                }

                size_t bytesToCopy = std::min<size_t>(
                    cf.getData().size() - 1,
                    receivingTotalLength - receivingOffset
                );
                this->receivingBuffer.insert(receivingBuffer.end(),
                                             cf.getData().begin() + 1,
                                             cf.getData().begin() + 1 + bytesToCopy);
                this->receivingOffset += bytesToCopy;
                this->receivingSequence = (receivingSequence + 1) % 16;

                Log::rx("Received Consecutive Frame #" + std::to_string(sequence) +
                        ", progress: " + std::to_string(receivingOffset) + "/" +
                        std::to_string(receivingTotalLength));
                Log::printBytes("DATA", cf.getData(), "rx");
                retryCount = 0;
            }

            if (receivingOffset >= receivingTotalLength) {
                receivingMultiFrame = false;
                return std::string(receivingBuffer.begin(), receivingBuffer.begin() + receivingTotalLength);
            } else {
                return receiveMessage();
            }
        }

        retryCount++;
    }

    Log::error("Failed to receive message after max retries");
    return "";
}

void CanTp::sendFlowControl(uint8_t flowStatus, uint8_t blockSize, 
                            uint8_t separationTime, uint32_t targetId) {
    Log::tx("Sending Flow Control Frame");
    Log::tx("Block Size: " + std::to_string(blockSize) +
             " Separation Time: " + std::to_string(separationTime));

    std::vector<uint8_t> data = { uint8_t(0x30 | (flowStatus & 0x0F)), blockSize, separationTime };
    Log::printBytes("DATA", data, "tx");

    Can fcFrame(targetId, data);
    fcFrame.setFrameType(targetId > 0x7FF);
    busTp_.send(fcFrame);
}
