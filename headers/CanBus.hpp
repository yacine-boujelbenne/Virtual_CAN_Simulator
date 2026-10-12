#pragma once
#include "Can.hpp"
#include <chrono>
#include <memory>
#include <string>
class CanBus {
public:
    enum class Backend { Internal, SocketCAN };
    explicit CanBus(Backend backend = Backend::Internal, std::string channel = "vehicle", bool loopback = false);
    ~CanBus();
    CanBus(const CanBus&) = delete;
    CanBus& operator=(const CanBus&) = delete;
    bool createVCAN(); // Compatibility: only checks; never provisions a host interface.
    bool init();
    void closeSocket();
    void send(const Can &frame);
    Can receiveFrame();
    bool receive(Can &frame, std::chrono::milliseconds timeout = std::chrono::milliseconds(100));
private:
    struct Endpoint;
    std::shared_ptr<Endpoint> endpoint_;
    Backend backend_;
    std::string channel_;
    bool loopback_;
    int socket_fd = -1;
};
