#include "runtime/Worker.hpp"
#include "transport/HttpWire.hpp"
#include <arpa/inet.h>
#include <csignal>
#include <cstdlib>
#include <iostream>

namespace {
volatile std::sig_atomic_t stopped = 0;
void stop(int) { stopped = 1; }
void reply(int fd, int code, const Json &value, vclab::Deadline deadline) {
    auto body = value.dump();
    if (body.size() > vclab::wireLimit) throw std::runtime_error("Worker response size limit");
    vclab::sendAll(fd, "HTTP/1.1 " + std::to_string(code) + " Result\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " +
        std::to_string(body.size()) + "\r\n\r\n" + body, deadline);
}
}
int main(int argc, char **argv) {
    try {
        vclab::Worker worker;
        if (argc == 1) {
            std::string line;
            while (std::getline(std::cin, line)) {
                try {
                    if (line.size() > vclab::wireLimit) throw std::invalid_argument("Worker request size limit");
                    std::cout << worker.request(Json::parse(line)).dump() << std::endl;
                } catch (const std::exception &e) { std::cout << Json({{"error", e.what()}}).dump() << std::endl; }
            }
            return 0;
        }
        if (argc < 3 || argc > 4 || std::string(argv[1]) != "--http")
            throw std::invalid_argument("Usage: vclab_worker [--http PORT [IPv4_BIND_ADDRESS]]; default: JSON lines on stdin/stdout");
        std::string portText = argv[2]; unsigned port = 0;
        auto parsed = std::from_chars(portText.data(), portText.data() + portText.size(), port);
        if (parsed.ec != std::errc{} || parsed.ptr != portText.data() + portText.size() || port > 65535)
            throw std::invalid_argument("Invalid worker port");
        vclab::Fd server(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));
        if (server.get() < 0) throw std::runtime_error("Cannot create worker HTTP socket");
        int reuse = 1; ::setsockopt(server.get(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(port);
        if (::inet_pton(AF_INET, argc == 4 ? argv[3] : "127.0.0.1", &address.sin_addr) != 1)
            throw std::invalid_argument("Invalid worker bind address");
        if (::bind(server.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) || ::listen(server.get(), 32))
            throw std::runtime_error("Cannot bind worker HTTP listener");
        socklen_t size = sizeof(address);
        if (::getsockname(server.get(), reinterpret_cast<sockaddr*>(&address), &size)) throw std::runtime_error("Cannot read worker port");
        std::signal(SIGTERM, stop); std::signal(SIGINT, stop);
        std::cout << Json({{"port", ntohs(address.sin_port)}, {"pid", ::getpid()}}).dump() << std::endl;
        while (!stopped) {
            pollfd event{server.get(), POLLIN, 0};
            if (::poll(&event, 1, 100) <= 0) continue;
            vclab::Fd client(::accept4(server.get(), nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK));
            if (client.get() < 0) continue;
            auto deadline = vclab::deadlineAfter(5000);
            try {
                auto message = vclab::readHttp(client.get(), deadline);
                if (message.start == "GET /health HTTP/1.1" || message.start == "GET /health HTTP/1.0") {
                    reply(client.get(), 200, worker.health(), deadline); continue;
                }
                if (message.start != "POST /command HTTP/1.1" && message.start != "POST /command HTTP/1.0") {
                    reply(client.get(), 404, {{"error", "Not found"}}, deadline); continue;
                }
                const char *configured = std::getenv("CLUSTER_TOKEN");
                std::string token = configured ? configured : "local-development-token";
                if (message.headers["x-cluster-token"] != token) {
                    reply(client.get(), 403, {{"error", "Cluster token mismatch"}}, deadline); continue;
                }
                reply(client.get(), 200, worker.request(Json::parse(message.body)), deadline);
            } catch (const Json::exception &e) {
                try { reply(client.get(), 400, {{"error", e.what()}}, deadline); } catch (...) {}
            } catch (const std::invalid_argument &e) {
                try { reply(client.get(), 400, {{"error", e.what()}}, deadline); } catch (...) {}
            } catch (const std::exception &e) {
                try { reply(client.get(), 503, {{"error", e.what()}}, deadline); } catch (...) {}
            }
        }
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
