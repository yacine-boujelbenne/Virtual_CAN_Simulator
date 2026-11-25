#pragma once

#include <string>
#include "json.hpp"

namespace vclab {

using Json = nlohmann::json;

class HttpClient {
public:
    HttpClient();
    Json post(const std::string &url, const Json &payload, int timeout_ms = 5000) const;
    Json get(const std::string &url, int timeout_ms = 5000) const;
};

} // namespace vclab
