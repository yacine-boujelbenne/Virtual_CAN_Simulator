#pragma once
#include <algorithm>
#include <cstdint>
#include <chrono>
#include <cerrno>
#include <climits>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace vclab {
inline constexpr size_t wireLimit = 2000000;
using Deadline = std::chrono::steady_clock::time_point;
inline Deadline deadlineAfter(int ms) {
    if (ms <= 0) throw std::invalid_argument("Timeout must be positive");
    return std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
}
class Fd {
public:
    explicit Fd(int fd = -1) : fd_(fd) {}
    ~Fd() { if (fd_ >= 0) ::close(fd_); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    int get() const { return fd_; }
private:
    int fd_;
};
inline void waitFor(int fd, short events, Deadline deadline) {
    for (;;) {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) throw std::runtime_error("Worker transport timeout");
        pollfd p{fd, events, 0};
        int n = ::poll(&p, 1, static_cast<int>(std::min<int64_t>(left + 1, INT_MAX)));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw std::runtime_error(n == 0 ? "Worker transport timeout" : "Worker poll failed");
        if (p.revents & events) return;
        throw std::runtime_error("Worker connection closed");
    }
}
inline void sendAll(int fd, const std::string &data, Deadline deadline) {
    size_t offset = 0;
    while (offset < data.size()) {
        waitFor(fd, POLLOUT, deadline);
        auto n = ::send(fd, data.data() + offset, data.size() - offset, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (n <= 0) throw std::runtime_error("Worker send failed");
        offset += static_cast<size_t>(n);
    }
}
inline void receiveMore(int fd, std::string &buffer, Deadline deadline, size_t limit) {
    waitFor(fd, POLLIN, deadline);
    char bytes[8192];
    auto n = ::recv(fd, bytes, sizeof(bytes), MSG_DONTWAIT);
    if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) return;
    if (n <= 0) throw std::runtime_error("Worker closed an incomplete response");
    buffer.append(bytes, static_cast<size_t>(n));
    if (buffer.size() > limit) throw std::runtime_error("Worker response size limit");
}
} // namespace vclab
