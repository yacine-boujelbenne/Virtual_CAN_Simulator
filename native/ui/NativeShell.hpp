#pragma once

#include <string>
#include "core/ApplicationService.hpp"

namespace vclab {

class NativeShell {
public:
    NativeShell();
    void setService(ApplicationService *service);
    std::string currentMode() const;
    bool isFixtureMode() const;
    void requestCommand(const vclab::CommandEnvelope &command);
    vclab::ApplicationSnapshot readSnapshot() const;

private:
    ApplicationService *service_ = nullptr;
    std::string mode_ = "fixture";
};

} // namespace vclab
