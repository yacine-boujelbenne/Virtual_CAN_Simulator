#include "NativeShell.hpp"

namespace vclab {

NativeShell::NativeShell() {}

void NativeShell::setService(ApplicationService *service) {
    service_ = service;
}

std::string NativeShell::currentMode() const {
    return mode_;
}

bool NativeShell::isFixtureMode() const {
    return mode_ == "fixture";
}

void NativeShell::requestCommand(const vclab::CommandEnvelope &command) {
    if (service_) {
        service_->submit(command);
    }
}

vclab::ApplicationSnapshot NativeShell::readSnapshot() const {
    if (service_) {
        return service_->snapshot();
    }
    return vclab::ApplicationSnapshot{};
}

} // namespace vclab
