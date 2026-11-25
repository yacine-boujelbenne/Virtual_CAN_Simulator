#include "Controller.hpp"
#include <deque>
#include <iomanip>
#include <set>
#include <sstream>
#include <thread>
#include <cmath>
#include <filesystem>

namespace vclab {
namespace {
void validatePresentation(const Json&p){
    if(!p.is_object()||p.dump().size()>8*1024*1024)throw std::invalid_argument("Invalid presentation metadata (8 MiB limit)");
    if(p.contains("car_mesh")){
        const auto&mesh=p.at("car_mesh");const auto&triangles=mesh.at("triangles");
        if(!triangles.is_array()||triangles.empty()||triangles.size()>20000)throw std::invalid_argument("Car mesh must contain 1..20000 triangles");
        if(mesh.at("name").get<std::string>().size()>256)throw std::invalid_argument("Mesh name too long");
        for(const auto&triangle:triangles){if(!triangle.is_array()||triangle.size()!=17)throw std::invalid_argument("Invalid mesh triangle");for(size_t i=0;i<17;++i){const auto&v=triangle.at(i);if(!v.is_number()||!std::isfinite(v.get<double>())||std::abs(v.get<double>())>1000)throw std::invalid_argument("Invalid mesh number");if(i>=9&&i<12&&(v.get<double>()<0||v.get<double>()>255))throw std::invalid_argument("Invalid mesh colour");if(i>=15&&v!=0&&v!=1)throw std::invalid_argument("Invalid wheel flag");}}
    }
    const auto positions=p.value("node_offsets",Json::object());
    if(!positions.is_object()||positions.size()>256)throw std::invalid_argument("Invalid node layout");
    for(const auto&point:positions){if(!point.is_array()||point.size()!=2)throw std::invalid_argument("Node offset must be [x,y]");for(const auto&v:point)if(!v.is_number()||!std::isfinite(v.get<double>())||std::abs(v.get<double>())>100000)throw std::invalid_argument("Invalid node offset");}
}
unsigned countOf(const Json &command, unsigned fallback) {
    const auto value = command.value("count", Json(fallback));
    if (!value.is_number_integer() || value.get<int64_t>() < 1 || value.get<int64_t>() > 1000)
        throw std::invalid_argument("Step count must be 1..1000");
    return value.get<unsigned>();
}
std::string hex(const Json &bytes) {
    std::ostringstream out; out << std::hex << std::setfill('0');
    for (size_t i = 0; i < bytes.size(); ++i) out << (i ? " " : "") << std::setw(2) << bytes[i].get<unsigned>();
    return out.str();
}
}
Json Controller::Node::call(const Json &command) {
    return process ? process->request(command) : HttpClient().post(endpoint + "/command", command);
}
Controller::Controller(RuntimeConfig config)
    : config_(std::move(config)), core_(config_.scenario, true), spares_(config_.spares) {
    if (config_.endpoints.empty()) {
        if (config_.worker_binary.empty()) throw std::invalid_argument("Native worker binary required");
        spares_ = {"native-process"};
    } else {
        std::set<std::string> unique;
        auto checkEndpoint = [&](std::string endpoint) {
            while (endpoint.ends_with('/')) endpoint.pop_back();
            if (endpoint.empty() || !unique.insert(endpoint).second)
                throw std::invalid_argument("Each ECU/reserve requires a distinct dedicated endpoint");
            return endpoint;
        };
        for (auto &[name, endpoint] : config_.endpoints) endpoint = checkEndpoint(endpoint);
        for (auto &endpoint : spares_) endpoint = checkEndpoint(endpoint);
        config_.spares = spares_;
    }
    nodes_ = configuredNodes(core_.snapshot());
}
Controller::Nodes Controller::configuredNodes(const Json &state) {
    Nodes result;
    auto endpoints = config_.endpoints;
    for (const auto &[name, node] : nodes_) if (!node.endpoint.empty()) endpoints[name] = node.endpoint;
    // Validate the entire mapping before touching any remote ECU.
    if (!config_.endpoints.empty()) {
        std::set<std::string> used;
        for (const auto &ecu : state.at("ecus")) {
            auto name = ecu.at("name").get<std::string>();
            if (!endpoints.contains(name)) throw std::invalid_argument("No dedicated ECU endpoint for " + name);
            if (!used.insert(endpoints.at(name)).second) throw std::invalid_argument("ECUs cannot share a worker endpoint");
        }
    }
    for (const auto &ecu : state.at("ecus")) {
        auto name = ecu.at("name").get<std::string>();
        Node node;
        if (config_.endpoints.empty()) node.process = std::make_unique<ProcessClient>(config_.worker_binary);
        else node.endpoint = endpoints.at(name);
        auto reply = node.call({{"action", "configure"}, {"definition", ecu.at("definition")},
            {"step_ms", state.at("step_ms")}, {"epoch", state.at("epoch")}, {"start_tick", state.at("tick")}});
        node.runtime = reply.value("runtime", Json::object()); node.status = "connected";
        result.emplace(name, std::move(node));
    }
    return result;
}
Json Controller::snapshot() const {
    auto state = core_.snapshot();
    state["presentation"]=Json::object();for(const auto&item:presentation_.items())if(item.key()!="car_mesh")state["presentation"][item.key()]=item.value();
    if(presentation_.contains("car_mesh"))state["presentation"]["car_mesh"]={{"name",presentation_.at("car_mesh").at("name")},{"faces",presentation_.at("car_mesh").at("triangles").size()}};
    size_t connected = 0;
    for (auto &ecu : state["ecus"]) {
        auto found = nodes_.find(ecu.at("name").get<std::string>());
        Json container = {{"status", "unavailable"}};
        if (found != nodes_.end()) {
            container = found->second.runtime;
            container["status"] = found->second.status;
            if (found->second.status == "connected") ++connected;
        }
        ecu["container"] = container;
    }
    state["cluster"] = {{"mode", "distributed"}, {"controller", "native-cpp"},
        {"worker_transport", config_.endpoints.empty() ? "native-process" : "http"},
        {"connected", connected}, {"required", state.at("ecus").size()}, {"reserve_slots", spares_.size()},
        {"last_error", failure_}, {"requires_reset", needs_reset_}};
    return state;
}
Json Controller::step(unsigned count,bool includeSnapshot) {
    if (needs_reset_) throw std::runtime_error("Worker configuration failed; reset required");
    for (unsigned i = 0; i < count; ++i) {
        try {
            auto prepared = core_.command({{"action", "prepare_tick"}});
            pending_ = true;
            Json results = Json::array();
            for (const auto &input : prepared.at("nodes")) {
                auto name = input.at("name").get<std::string>();
                auto &node = nodes_.at(name);
                try {
                    auto command = input; command["action"] = "tick";
                    results.push_back(node.call(command)); node.status = "connected";
                } catch (...) { node.status = "fault";if(node.process&&node.process->pid()<=0)needs_reset_=true;throw; }
            }
            core_.command({{"action", "complete_tick"}, {"results", results}});
            pending_ = false; failure_ = nullptr;
        } catch (const std::exception &e) {
            failure_ = e.what(); core_.command({{"action", "pause"}}); throw;
        }
    }
    return includeSnapshot?snapshot():Json::object();
}
Json Controller::diagnostic(const Json &command) {
    auto name = command.at("ecu").get<std::string>();
    auto found = nodes_.find(name);
    if (found == nodes_.end()) throw std::invalid_argument("Unknown diagnostic ECU");
    const auto &payload = command.at("bytes");
    if (!payload.is_array() || payload.empty() || payload.size() > 4095)
        throw std::invalid_argument("Diagnostic PDU must contain 1..4095 bytes");
    for (const auto &byte : payload)
        if (!byte.is_number_integer() || byte.get<int64_t>() < 0 || byte.get<int64_t>() > 255)
            throw std::invalid_argument("Diagnostic bytes must be integers 0..255");
    bool fd = command.value("fd", false);
    auto session = std::string("native-") + std::to_string(++session_);
    auto &worker = found->second;
    auto close = [&] {
        try { worker.call({{"action", "isotp_end"}, {"session", session}}); } catch (...) {}
        try { core_.command({{"action", "isotp_tester_end"}, {"session", session}}); } catch (...) {}
    };
    try {
        Json settings = {{"session", session}, {"fd", fd}, {"link_length", command.value("link_length", fd ? 64 : 8)},
            {"block_size", command.value("block_size", 8)}, {"stmin", command.value("stmin", 0)}};
        auto start = settings; start["action"] = "isotp_start";
        start["response_id"] = 0x7e8; start["request_id"] = 0x7e0; worker.call(start);
        start = settings; start["action"] = "isotp_tester_start"; start["tx_id"] = 0x7e0; start["rx_id"] = 0x7e8;
        core_.command(start);
        auto sent = core_.command({{"action", "isotp_tester_send"}, {"session", session}, {"bytes", payload}});
        std::deque<std::pair<bool, Json>> queue;
        for (auto &frame : sent.at("frames")) queue.emplace_back(true, frame);
        Json trace = Json::array(), message = nullptr;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!queue.empty()) {
            if (std::chrono::steady_clock::now() >= deadline || trace.size() >= 2048)
                throw std::runtime_error("ISO-TP diagnostic transfer limit");
            auto [toWorker, frame] = std::move(queue.front()); queue.pop_front();
            auto delay = frame.value("delay_us", 0ULL);
            if (delay > 1000000) throw std::runtime_error("Invalid ISO-TP frame delay");
            std::this_thread::sleep_for(std::chrono::microseconds(delay));
            auto record = core_.command({{"action", "diagnostic_trace"}, {"source", toWorker ? "Diagnostic tester" : name}, {"frame", frame}});
            if (record.at("dropped").get<bool>()) throw std::runtime_error("Diagnostic frame blocked by bus policy");
            auto copy = frame; copy["source"] = toWorker ? "Diagnostic tester" : name; trace.push_back(copy);
            Json reply;
            if (toWorker) reply = worker.call({{"action", "isotp_frame"}, {"session", session}, {"frame", frame}});
            else reply = core_.command({{"action", "isotp_tester_receive"}, {"session", session}, {"frame", frame}});
            for (const auto &next : reply.at("frames")) queue.emplace_back(!toWorker, next);
            if (!toWorker && reply.value("done", false)) message = reply.at("message");
        }
        if (message.is_null()) throw std::runtime_error("ISO-TP response incomplete");
        auto state = worker.call({{"action", "snapshot"}});
        core_.command({{"action", "refresh_ecu"}, {"name", name}, {"snapshot", state}});
        close();
        return {{"request", hex(payload)}, {"response", hex(message)}, {"bytes", message},
            {"transport", std::string("ISO-TP / distributed ") + (fd ? "CAN FD" : "CAN")},
            {"request_id", 0x7e0}, {"response_id", 0x7e8}, {"frames", trace}, {"frame_count", trace.size()}, {"ecu_container", name}};
    } catch (...) { close(); throw; }
}
Json Controller::importProject(const Json &project) {
    auto presentation=project.value("presentation",Json::object());
    validatePresentation(presentation);
    // Validate hash, map, definitions and scenario in an isolated core before any worker changes.
    SimulationManager validator(config_.scenario, true);
    validator.command({{"action", "import_project"}, {"project", project}});
    auto stagedState = validator.snapshot();
    stagedState["epoch"] = core_.snapshot().at("epoch").get<uint64_t>() + 1;
    // Reject missing/aliased mappings without changing active state or requiring reset.
    if (!config_.endpoints.empty()) {
        std::set<std::string> used;
        for (const auto &ecu : stagedState.at("ecus")) {
            auto name = ecu.at("name").get<std::string>();
            auto active = nodes_.find(name);
            auto declared = config_.endpoints.find(name);
            if (active == nodes_.end() && declared == config_.endpoints.end())
                throw std::invalid_argument("No dedicated ECU endpoint for " + name);
            auto endpoint = active == nodes_.end() ? declared->second : active->second.endpoint;
            if (!used.insert(endpoint).second) throw std::invalid_argument("ECUs cannot share a worker endpoint");
        }
    }
    Nodes staged;
    try { staged = configuredNodes(stagedState); }
    catch (const std::exception &e) {
        // Remote configure is not a distributed transaction. Keep the active project/time,
        // pause and require reset if some remote workers may have changed epoch/state.
        if (!config_.endpoints.empty()) {
            needs_reset_ = true; failure_ = e.what(); core_.command({{"action", "pause"}});
        }
        throw;
    }
    auto result = core_.command({{"action", "import_project"}, {"project", project}});
    if (!config_.endpoints.empty()) {
        std::set<std::string> available(config_.spares.begin(), config_.spares.end());
        for (const auto &[name, node] : nodes_) available.insert(node.endpoint);
        for (const auto &[name, node] : staged) available.erase(node.endpoint);
        spares_.assign(available.begin(), available.end());
    }
    nodes_ = std::move(staged); pending_ = false; needs_reset_ = false; failure_ = nullptr;presentation_=std::move(presentation);
    if (config_.endpoints.empty()) spares_ = {"native-process"};
    return result;
}
Json Controller::request(const Json &command) {
    if (!command.is_object()) throw std::invalid_argument("Command must be an object");
    auto action = command.value("action", "snapshot");
    if (action == "snapshot" || action == "cluster_ready") return snapshot();
    if(action=="export_project") {auto p=core_.command(command);if(!presentation_.empty())p["presentation"]=presentation_;return p;}
    if (action == "hash_project" || action == "validate") return core_.command(command);
    if(action=="set_car_mesh"){
        auto staged=presentation_;if(command.at("mesh").is_null())staged.erase("car_mesh");else staged["car_mesh"]=command.at("mesh");validatePresentation(staged);presentation_=std::move(staged);return snapshot();
    }
    if(action=="set_ui_layout"){
        const auto &positions=command.at("positions");
        if(!positions.is_object()||positions.size()>256)throw std::invalid_argument("Invalid layout");
        for(const auto &entry:positions.items()){
            if(!entry.value().is_array()||entry.value().size()!=2)throw std::invalid_argument("Position must be [x,y]");
            for(const auto &v:entry.value())if(!v.is_number()||!std::isfinite(v.get<double>())||std::abs(v.get<double>())>100000)throw std::invalid_argument("Invalid node coordinate");
        }
        presentation_["node_offsets"]=positions;return snapshot();
    }
    if (action == "node_fault") {
        auto name = command.at("name").get<std::string>();
        if (!nodes_.contains(name)) throw std::invalid_argument("Unknown ECU");
        auto &node = nodes_.at(name);
        auto reply = node.call({{"action", "fault"}, {"mode", command.at("mode")}});
        node.status = reply.at("halted").get<bool>() ? "fault" : "connected";
        if (reply.at("halted").get<bool>()) core_.command({{"action", "pause"}});
        return snapshot();
    }
    if (action == "reset") {
        auto state = core_.command(command); pending_ = false;
        try {
            nodes_ = configuredNodes(state); needs_reset_ = false; failure_ = nullptr;
            spares_ = config_.endpoints.empty() ? std::vector<std::string>{"native-process"} : config_.spares;
        } catch (const std::exception &e) { needs_reset_ = true; failure_ = e.what(); throw; }
        return snapshot();
    }
    // Restart the loaded project, including deployed ECU programs and network policies.
    // The legacy reset command intentionally keeps its startup-scenario behavior.
    if (action == "restart_project") {
        importProject(request({{"action", "export_project"}}));
        return snapshot();
    }
    if (action == "step") return step(countOf(command, 1));
    if (action == "advance") {
        auto count = countOf(command, 10);
        return core_.snapshot().at("running").get<bool>() ? step(count) : snapshot();
    }
    if (action == "pause") { core_.command(command); return snapshot(); }
    if (action == "run") {
        if (needs_reset_) throw std::runtime_error("Restart the current project to replace unavailable workers");
        for (const auto &[name,node] : nodes_)
            if(node.status!="connected") throw std::runtime_error("Restore worker before running: "+name);
        if (pending_) step(1); // Retry the frozen barrier once; never skip or apply it twice.
        core_.command(command);return snapshot();
    }
    if (pending_ || needs_reset_) throw std::runtime_error("Complete the pending tick or reset before changing the laboratory");
    if(action=="new_project"){
        auto name=command.at("scenario").get<std::string>();
        if(name!="ev_basic.json"&&name!="ev_lateral.json"&&name!="ev_physics.json"&&name!="ev_interactive.json")throw std::invalid_argument("Unknown built-in project");
        SimulationManager source((std::filesystem::path(config_.scenario).parent_path()/name).string(),true);importProject(source.command({{"action","export_project"}}));return snapshot();
    }
    if(action=="validate_project_draft"||action=="apply_project_draft"){
        auto project=command.at("project");
        validatePresentation(project.value("presentation",Json::object()));
        std::map<std::string,bool> buses;
        for(const auto &bus:project.at("scenario").at("buses")){
            const auto id=bus.at("id").get<std::string>();
            if(id.empty()||buses.contains(id))throw std::invalid_argument("Duplicate or empty bus identifier");
            auto rate=bus.at("bitrate").get<int64_t>();if(rate<10000||rate>1000000)throw std::invalid_argument("Invalid bus bitrate");
            buses[id]=bus.value("fd",false);
        }
        for(const auto &definition:project.at("ecu_definitions"))for(const auto &message:definition.at("messages")){
            auto bus=message.value("bus","powertrain");
            if(!buses.contains(bus))throw std::invalid_argument("Message references unknown bus: "+bus);
            if(message.value("fd",false)&&!buses.at(bus))throw std::invalid_argument("CAN FD message needs an FD-capable bus");
        }
        for(const auto &route:project.at("scenario").value("gateway_routes",Json::array()))
            if(!buses.contains(route.at("from"))||!buses.contains(route.at("to")))throw std::invalid_argument("Gateway references unknown bus");
        project["project_hash"]=core_.command({{"action","hash_project"},{"project",project}}).at("project_hash");
        SimulationManager validator(config_.scenario,true);
        validator.command({{"action","import_project"},{"project",project}});
        auto canonical=validator.command({{"action","export_project"}});
        canonical["presentation"]=project.value("presentation",Json::object());
        if(action=="validate_project_draft")return {{"valid",true},{"project_hash",canonical.at("project_hash")}};
        if(core_.snapshot().at("running").get<bool>())throw std::invalid_argument("Pause before applying topology changes");
        importProject(canonical);return snapshot();
    }
    if (action == "validate_ecu_update" || action == "update_ecu") {
        const auto name = command.at("name").get<std::string>();
        const auto &definition = command.at("definition");
        if (!nodes_.contains(name)) throw std::invalid_argument("Unknown ECU: " + name);
        if (definition.at("name") != name) throw std::invalid_argument("Keep the ECU name when updating its program");
        if (action == "update_ecu" && core_.snapshot().at("running").get<bool>())
            throw std::invalid_argument("Pause the experiment before deploying an ECU program");
        auto project = request({{"action", "export_project"}});
        bool replaced = false;
        for (auto &entry : project.at("ecu_definitions").items()) {
            if (entry.value().at("name") == name) { entry.value() = definition; replaced = true; }
        }
        if (!replaced) throw std::invalid_argument("ECU has no project definition");
        project["project_hash"] = core_.command({{"action", "hash_project"}, {"project", project}}).at("project_hash");
        // Validate the complete candidate in isolation, including CAN collisions and
        // exclusive actuator ownership, then derive its canonical CAN map in C++.
        SimulationManager validator(config_.scenario, true);
        validator.command({{"action", "import_project"}, {"project", project}});
        auto metadata=project.value("presentation",Json::object());
        project = validator.command({{"action", "export_project"}});project["presentation"]=metadata;
        if (action == "validate_ecu_update")
            return {{"valid", true}, {"name", name}, {"project_hash", project.at("project_hash")}};
        importProject(project);
        return snapshot();
    }
    if (action == "import_project") return importProject(command.at("project"));
    if (action == "diagnostic") return diagnostic(command);
    if (action == "add_ecu") {
        if (spares_.empty()) throw std::invalid_argument("No reserve worker available");
        auto definition = command.at("definition");
        auto name = definition.at("name").get<std::string>();
        if (nodes_.contains(name)) throw std::invalid_argument("ECU name already registered");
        core_.command({{"action", "validate"}, {"definition", definition}});
        auto state = core_.snapshot(); Node node;
        if (config_.endpoints.empty()) node.process = std::make_unique<ProcessClient>(config_.worker_binary);
        else node.endpoint = spares_.front();
        auto reply = node.call({{"action", "configure"}, {"definition", definition}, {"step_ms", state.at("step_ms")},
            {"epoch", state.at("epoch")}, {"start_tick", state.at("tick")}});
        core_.command(command);
        node.runtime = reply.value("runtime", Json::object()); node.status = "connected";
        nodes_.emplace(name, std::move(node)); spares_.erase(spares_.begin());
        // Remote reserve assignments remain ephemeral and reset releases them.
        return snapshot();
    }
    if (action == "prepare_tick" || action == "complete_tick" || action == "refresh_ecu" ||
        action.starts_with("isotp_tester_") || action == "diagnostic_trace")
        throw std::invalid_argument("This command is owned by the native controller");
    core_.command(command); return snapshot();
}
}
