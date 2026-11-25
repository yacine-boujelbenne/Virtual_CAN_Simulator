#include "ProcessClient.hpp"
#include "Channel.hpp"
#include <csignal>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
extern char **environ;

namespace vclab {
ProcessClient::ProcessClient(const std::string &binary) {
    int pair[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) != 0)
        throw std::runtime_error("Cannot create native worker channel");
    Fd child(pair[1]);
    fd_ = pair[0];
    posix_spawn_file_actions_t actions;
    ::posix_spawn_file_actions_init(&actions);
    ::posix_spawn_file_actions_adddup2(&actions, child.get(), STDIN_FILENO);
    ::posix_spawn_file_actions_adddup2(&actions, child.get(), STDOUT_FILENO);
    ::posix_spawn_file_actions_addclose(&actions, child.get());
    ::posix_spawn_file_actions_addclose(&actions, fd_);
    char *argv[] = {const_cast<char*>(binary.c_str()), nullptr};
    int error = ::posix_spawnp(&pid_, binary.c_str(), &actions, nullptr, argv, environ);
    ::posix_spawn_file_actions_destroy(&actions);
    if (error) {
        ::close(fd_); fd_ = -1; pid_ = -1;
        throw std::runtime_error("Cannot launch native ECU worker: " + binary);
    }
}
ProcessClient::~ProcessClient() { stop(); }
void ProcessClient::stop() {
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    if (pid_ <= 0) return;
    ::kill(pid_, SIGTERM);
    for (int i = 0; i < 20; ++i) {
        pid_t result = ::waitpid(pid_, nullptr, WNOHANG);
        if (result == pid_ || (result < 0 && errno == ECHILD)) { pid_ = -1; return; }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ::kill(pid_, SIGKILL);
    while (::waitpid(pid_, nullptr, 0) < 0 && errno == EINTR) {}
    pid_ = -1;
}
nlohmann::json ProcessClient::request(const nlohmann::json &command, int timeout_ms) {
    auto deadline = deadlineAfter(timeout_ms);
    auto payload = command.dump();
    if (payload.size() > wireLimit) throw std::invalid_argument("Worker request size limit");
    if (fd_ < 0) throw std::runtime_error("Native worker is unavailable; reset required");
    try {
        sendAll(fd_, payload + "\n", deadline);
        size_t end;
        while ((end = buffer_.find('\n')) == std::string::npos)
            receiveMore(fd_, buffer_, deadline, wireLimit + 1);
        auto result = nlohmann::json::parse(buffer_.substr(0, end));
        buffer_.erase(0, end + 1);
        if (!result.is_object()) throw std::runtime_error("Invalid native worker response");
        if (result.contains("error")) throw std::invalid_argument(result.at("error").get<std::string>());
        return result;
    } catch (const std::invalid_argument&) { throw; }
    catch (...) { stop(); throw; }
}
}
