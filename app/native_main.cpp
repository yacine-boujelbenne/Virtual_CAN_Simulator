#include "runtime/Controller.hpp"
#include "transport/Channel.hpp"
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <unistd.h>
#include <chrono>
#include <cerrno>
#include <algorithm>

namespace {
constexpr size_t controllerInputLimit=16*1024*1024; // Project/mesh payloads; worker messages retain their smaller bound.
Json load(const std::string &path) {
    std::ifstream input(path);
    if (!input) throw std::invalid_argument("Cannot open JSON file: " + path);
    return Json::parse(input);
}
}
int main(int argc, char **argv) {
    try {
        vclab::RuntimeConfig config;
        config.worker_binary = (std::filesystem::canonical("/proc/self/exe").parent_path() / "vclab_worker").string();
        std::string project, save;
        unsigned ticks = 0; bool batch = false, realtime=false;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if(arg=="--realtime"){realtime=true;continue;}
            if (arg == "--help") {
                std::cout << "Usage: vclab [--scenario FILE] [--worker BINARY] [--endpoints FILE] [--project FILE] [--steps N] [--save-project FILE]\n"
                    "Default: one native child process per ECU; JSON-line commands on stdin/stdout.\n"
                    "--endpoints selects dedicated HTTP workers: {\"workers\":{\"ECU name\":\"http://host:port\"},\"spares\":[]}.\n"
                    "--steps runs fixed simulation ticks without wall-clock pacing, prints the final state and exits.\n"
                    "--realtime owns paced simulation ticks while accepting JSON-line commands.\n";
                return 0;
            }
            if (++i >= argc) throw std::invalid_argument("Missing value for " + arg);
            std::string value = argv[i];
            if (arg == "--scenario") config.scenario = value;
            else if (arg == "--worker") config.worker_binary = value;
            else if (arg == "--project") project = value;
            else if (arg == "--save-project") save = value;
            else if (arg == "--endpoints") {
                auto data = load(value); config.endpoints = data.at("workers").get<std::map<std::string, std::string>>();
                if (config.endpoints.empty()) throw std::invalid_argument("HTTP worker mapping must not be empty");
                config.spares = data.value("spares", std::vector<std::string>{});
            } else if (arg == "--steps") {
                auto parsed = std::from_chars(value.data(), value.data() + value.size(), ticks);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || ticks > 1000000)
                    throw std::invalid_argument("Steps must be 0..1000000");
                batch = true;
            } else throw std::invalid_argument("Unknown option: " + arg);
        }
        vclab::Controller controller(config);
        if (!project.empty()) controller.request({{"action", "import_project"}, {"project", load(project)}});
        if (batch) {
            while (ticks) {
                auto n = std::min(ticks, 1000U); controller.request({{"action", "step"}, {"count", n}}); ticks -= n;
            }
            std::cout << controller.request({{"action", "snapshot"}}).dump() << std::endl;
        } else if(realtime){
            using Clock=std::chrono::steady_clock;
            auto next=Clock::now();std::string buffer;bool eof=false;
            while(!eof){
                pollfd input{STDIN_FILENO,POLLIN,0};
                int timeout=controller.running()?std::max(0,int(std::chrono::duration_cast<std::chrono::milliseconds>(next-Clock::now()).count())):100;
                const int ready=::poll(&input,1,timeout);
                if(ready<0){if(errno==EINTR)continue;throw std::runtime_error("Controller input poll failed");}
                if(ready>0&&(input.revents&(POLLIN|POLLHUP))){
                    char bytes[8192];const auto count=::read(STDIN_FILENO,bytes,sizeof(bytes));
                    if(count<=0){eof=true;continue;}buffer.append(bytes,size_t(count));
                    size_t end;
                    while((end=buffer.find('\n'))!=std::string::npos){
                        auto line=buffer.substr(0,end);buffer.erase(0,end+1);
                        try{if(line.size()>controllerInputLimit)throw std::invalid_argument("Controller command exceeds 16 MiB");std::cout<<controller.request(Json::parse(line)).dump()<<std::endl;}
                        catch(const std::exception&e){std::cout<<Json({{"error",e.what()}}).dump()<<std::endl;}
                    }
                    if(buffer.size()>controllerInputLimit)throw std::invalid_argument("Controller command exceeds 16 MiB");
                }
                if(controller.running()&&Clock::now()>=next){
                    try{controller.pacedTick();}catch(const std::exception&e){std::cerr<<e.what()<<'\n';}
                    // Include work in the deadline. A late host starts its next tick
                    // immediately, without skipping simulation ticks or accumulating debt.
                    next=std::max(next+std::chrono::milliseconds(controller.stepMilliseconds()),Clock::now());
                }else if(!controller.running())next=Clock::now()+std::chrono::milliseconds(controller.stepMilliseconds());
            }
        } else {
            std::string line;
            while (std::getline(std::cin, line)) {
                try {
                    if (line.size() > controllerInputLimit) throw std::invalid_argument("Controller command exceeds 16 MiB");
                    std::cout << controller.request(Json::parse(line)).dump() << std::endl;
                } catch (const std::exception &e) { std::cout << Json({{"error", e.what()}}).dump() << std::endl; }
            }
        }
        if (!save.empty()) {
            std::ofstream output(save);
            if (!output) throw std::runtime_error("Cannot write project file: " + save);
            output << controller.request({{"action", "export_project"}}).dump(2) << '\n';
            output.close();
            if (!output) throw std::runtime_error("Project file write failed");
        }
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
