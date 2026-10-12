#include "CanBus.hpp"
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <stdexcept>
#ifdef __linux__
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#endif
struct CanBus::Endpoint {std::mutex mutex; std::condition_variable ready; std::deque<Can> frames;};
namespace {
std::mutex registryMutex;
std::map<std::string, std::vector<std::weak_ptr<void>>> registry;
}
CanBus::CanBus(Backend backend, std::string channel, bool loopback)
    : backend_(backend), channel_(std::move(channel)), loopback_(loopback) {}
CanBus::~CanBus() {closeSocket();}
bool CanBus::createVCAN() {
    if (backend_ == Backend::Internal) return true;
#ifdef __linux__
    return if_nametoindex(channel_.c_str()) != 0;
#else
    return false;
#endif
}
bool CanBus::init() {
    if (endpoint_ || socket_fd >= 0) return true;
    if (backend_ == Backend::Internal) {
        endpoint_ = std::make_shared<Endpoint>();
        std::lock_guard<std::mutex> lock(registryMutex);
        auto &peers = registry[channel_];
        peers.erase(std::remove_if(peers.begin(), peers.end(), [](auto &p){return p.expired();}), peers.end());
        peers.push_back(endpoint_); return true;
    }
#ifdef __linux__
    if (channel_.size() >= IFNAMSIZ) return false;
    socket_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (socket_fd < 0) return false;
    ifreq request{}; std::strcpy(request.ifr_name, channel_.c_str());
    sockaddr_can address{}; address.can_family = AF_CAN;
    if (ioctl(socket_fd, SIOCGIFINDEX, &request) < 0) {closeSocket(); return false;}
    address.can_ifindex = request.ifr_ifindex;
    int enableFd=1;
    if(setsockopt(socket_fd,SOL_CAN_RAW,CAN_RAW_FD_FRAMES,&enableFd,sizeof(enableFd))<0) {closeSocket();return false;}
    int own = loopback_ ? 1 : 0;
    if (setsockopt(socket_fd, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, &own, sizeof(own)) < 0 ||
        bind(socket_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {closeSocket(); return false;}
    return true;
#else
    return false;
#endif
}
void CanBus::send(const Can &frame) {
    const auto length=frame.getData().size();
    const std::vector<size_t> legal={0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64};
    if (length > (frame.isFd()?64U:8U) || (frame.isFd() && std::find(legal.begin(),legal.end(),length)==legal.end()) || frame.getId() > (frame.isExtendedFrame() ? 0x1fffffffU : 0x7ffU))
        throw std::invalid_argument("Invalid classic CAN ID or DLC");
    if (backend_ == Backend::Internal) {
        if (!endpoint_) throw std::runtime_error("CAN endpoint is not initialized");
        std::lock_guard<std::mutex> registryLock(registryMutex);
        for (auto &weak : registry[channel_]) {
            auto peer = std::static_pointer_cast<Endpoint>(weak.lock());
            if (!peer || (peer == endpoint_ && !loopback_)) continue;
            {
                std::lock_guard<std::mutex> lock(peer->mutex);
                if (peer->frames.size() >= 8192) throw std::runtime_error("Internal CAN receive queue full");
                peer->frames.push_back(frame);
            }
            peer->ready.notify_one();
        }
        return;
    }
#ifdef __linux__
    if (socket_fd < 0) throw std::runtime_error("SocketCAN endpoint is not initialized");
    canfd_frame raw{}; raw.can_id = frame.getId() | (frame.isExtendedFrame() ? CAN_EFF_FLAG : 0);
    raw.len=static_cast<__u8>(frame.getData().size());raw.flags=frame.bitrateSwitch()?CANFD_BRS:0;
    std::copy(frame.getData().begin(),frame.getData().end(),raw.data);
    const size_t mtu=frame.isFd()?CANFD_MTU:CAN_MTU;
    if(write(socket_fd,&raw,mtu)!=static_cast<ssize_t>(mtu))throw std::runtime_error("SocketCAN write failed");
#endif
}
bool CanBus::receive(Can &frame, std::chrono::milliseconds timeout) {
    if (backend_ == Backend::Internal) {
        if (!endpoint_) return false;
        std::unique_lock<std::mutex> lock(endpoint_->mutex);
        if (!endpoint_->ready.wait_for(lock, timeout, [&]{return !endpoint_->frames.empty();})) return false;
        frame = endpoint_->frames.front(); endpoint_->frames.pop_front(); return true;
    }
#ifdef __linux__
    if (socket_fd < 0) return false;
    pollfd descriptor{socket_fd, POLLIN, 0};
    if (poll(&descriptor, 1, static_cast<int>(timeout.count())) <= 0) return false;
    canfd_frame raw{};ssize_t count=read(socket_fd,&raw,sizeof(raw));
    bool fd=count==CANFD_MTU;
    if((count!=CAN_MTU&&!fd)||raw.len>(fd?64:8)||(raw.can_id&(CAN_ERR_FLAG|CAN_RTR_FLAG)))return false;
    bool extended=(raw.can_id&CAN_EFF_FLAG)!=0;
    frame=Can(raw.can_id&(extended?CAN_EFF_MASK:CAN_SFF_MASK),{raw.data,raw.data+raw.len});
    frame.setFrameType(extended);frame.setFd(fd,fd&&(raw.flags&CANFD_BRS));return true;
#else
    return false;
#endif
}
Can CanBus::receiveFrame() {Can frame; receive(frame); return frame;}
void CanBus::closeSocket() {
    endpoint_.reset();
#ifdef __linux__
    if (socket_fd >= 0) {close(socket_fd); socket_fd = -1;}
#endif
}
