#ifndef _CAN_
#define _CAN_

#include <cstdint>
#include <vector>
#include <string>

class Can {
public:
    Can();
    Can(uint32_t id, const std::vector<uint8_t> &data);
    void print();
    void setFd(bool fd, bool brs = false) { fd_ = fd; brs_ = fd && brs; }
    bool isFd() const { return fd_; }
    bool bitrateSwitch() const { return brs_; }
    void setFrameType(bool extended) { extendedFrame_ = extended; }
    bool isExtendedFrame() const { return extendedFrame_; }
    void setId(uint32_t id) { id_ = id; }
    void setData(std::vector<uint8_t> data) { data_ = data; }
    uint32_t getId() const { return id_; }
    const std::vector<uint8_t>& getData() const { return data_; }
    
    static std::vector<uint8_t> encoder(const std::string& data) {
        return std::vector<uint8_t>(data.begin(), data.end());
    }
    
    static std::string decoder(const std::vector<uint8_t>& data) {
        return std::string(data.begin(), data.end());
    }

private:
    bool extendedFrame_ = false, fd_ = false, brs_ = false;
    uint32_t id_ = 0;
    std::vector<uint8_t> data_;
};

#endif
