#pragma once
#include "Channel.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <map>
#include <sstream>

namespace vclab {
struct HttpMessage {
    std::string start, body;
    std::map<std::string, std::string> headers;
};
inline HttpMessage readHttp(int fd, Deadline deadline) {
    std::string bytes;
    size_t end;
    while ((end = bytes.find("\r\n\r\n")) == std::string::npos) {
        receiveMore(fd, bytes, deadline, wireLimit + 16384);
        if (bytes.find("\r\n\r\n") == std::string::npos && bytes.size() > 16384)
            throw std::invalid_argument("HTTP header size limit");
    }
    if (end > 16384) throw std::invalid_argument("HTTP header size limit");
    std::istringstream lines(bytes.substr(0, end));
    HttpMessage message;
    std::getline(lines, message.start);
    if (!message.start.empty() && message.start.back() == '\r') message.start.pop_back();
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto colon = line.find(':');
        if (colon == std::string::npos) throw std::invalid_argument("Malformed HTTP header");
        auto key = line.substr(0, colon), value = line.substr(colon + 1);
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });
        auto first = value.find_first_not_of(" \t"), last = value.find_last_not_of(" \t");
        value = first == std::string::npos ? "" : value.substr(first, last - first + 1);
        if (!message.headers.emplace(key, value).second) throw std::invalid_argument("Duplicate HTTP header");
    }
    if (message.headers.contains("transfer-encoding")) throw std::invalid_argument("Chunked worker HTTP is unsupported");
    size_t size = 0;
    if (auto it = message.headers.find("content-length"); it != message.headers.end()) {
        auto &value = it->second;
        auto parsed = std::from_chars(value.data(), value.data() + value.size(), size);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || size > wireLimit)
            throw std::invalid_argument("Invalid HTTP content length");
    }
    auto start = end + 4;
    while (bytes.size() < start + size) receiveMore(fd, bytes, deadline, wireLimit + 16384);
    if (bytes.size() != start + size) throw std::invalid_argument("Unexpected trailing HTTP data");
    message.body = bytes.substr(start, size);
    return message;
}
}
