#include "HttpClient.hpp"
#include "HttpWire.hpp"
#include <cstdlib>
#include <memory>
#include <netdb.h>
#include <fcntl.h>

namespace vclab {

HttpClient::HttpClient() {}

namespace {
Json request(const std::string &url, const std::string &method, const std::string &body, int timeout_ms) {
    if (!url.starts_with("http://") || url.find_first_of("\r\n") != std::string::npos)
        throw std::invalid_argument("Worker URL must use http://");
    auto split = url.find('/', 7);
    auto authority = url.substr(7, split == std::string::npos ? split : split - 7);
    auto path = split == std::string::npos ? "/" : url.substr(split);
    auto colon = authority.find(':');
    auto host = authority.substr(0, colon);
    auto port = colon == std::string::npos ? "80" : authority.substr(colon + 1);
    unsigned number = 0;
    auto parsed = std::from_chars(port.data(), port.data() + port.size(), number);
    if (host.empty() || host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") != std::string::npos ||
        parsed.ec != std::errc{} || parsed.ptr != port.data() + port.size() || number == 0 || number > 65535)
        throw std::invalid_argument("Invalid worker URL host/port (IPv4 or DNS required)");
    if (body.size() > wireLimit) throw std::invalid_argument("Worker request size limit");
    auto deadline = deadlineAfter(timeout_ms);
    addrinfo hints{}; hints.ai_socktype = SOCK_STREAM; hints.ai_family = AF_INET;
    addrinfo *addresses = nullptr;
    if (::getaddrinfo(host.c_str(), port.c_str(), &hints, &addresses)) throw std::runtime_error("Worker DNS lookup failed");
    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> owned(addresses, freeaddrinfo);
    int connected = -1;
    for (auto *a = addresses; a; a = a->ai_next) {
        int fd = ::socket(a->ai_family, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
        if (fd < 0) continue;
        try {
            if (::connect(fd, a->ai_addr, a->ai_addrlen) < 0) {
                if (errno != EINPROGRESS) throw std::runtime_error("Worker connection failed");
                waitFor(fd, POLLOUT, deadline);
                int error = 0; socklen_t size = sizeof(error);
                if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) || error) throw std::runtime_error("Worker connection failed");
            }
            connected = fd; break;
        } catch (...) { ::close(fd); }
    }
    if (connected < 0) throw std::runtime_error("Worker unavailable at " + authority);
    Fd fd(connected);
    auto token = std::getenv("CLUSTER_TOKEN");
    std::string secret = token ? token : "local-development-token";
    if (secret.find_first_of("\r\n") != std::string::npos) throw std::invalid_argument("Invalid cluster token");
    sendAll(fd.get(), method + " " + path + " HTTP/1.1\r\nHost: " + authority +
        "\r\nConnection: close\r\nContent-Type: application/json\r\nX-Cluster-Token: " + secret +
        "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body, deadline);
    auto response = readHttp(fd.get(), deadline);
    std::istringstream status(response.start);
    std::string version; int code = 0;
    status >> version >> code;
    if ((version != "HTTP/1.1" && version != "HTTP/1.0") || code < 100 || code > 599)
        throw std::runtime_error("Malformed worker HTTP status");
    auto result = Json::parse(response.body);
    if (!result.is_object()) throw std::runtime_error("Invalid worker HTTP JSON");
    if (code != 200 || result.contains("error")) throw std::runtime_error(result.value("error", "Worker HTTP error " + std::to_string(code)));
    return result;
}
}
Json HttpClient::post(const std::string &url, const Json &payload, int timeout_ms) const {
    return request(url, "POST", payload.dump(), timeout_ms);
}

Json HttpClient::get(const std::string &url, int timeout_ms) const {
    return request(url, "GET", "", timeout_ms);
}

} // namespace vclab
