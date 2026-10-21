#include "SimulationManager.hpp"
#include "CanTp.hpp"
#include "Sha256.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <set>
#include <sstream>
#include <stdexcept>
namespace {
Json readJson(const std::filesystem::path &path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot read " + path.string());
    Json data; input >> data; return data;
}
void check(bool ok, const std::string &message) {
    if (!ok) throw std::invalid_argument(message);
}
void requireFinite(double v) {check(std::isfinite(v), "Non-finite numeric value");}
// Nearest-rank percentile over an ascending sample vector.
double percentile(const std::vector<double> &sorted,double share) {
    if(sorted.empty())return 0;
    auto index=static_cast<size_t>(std::ceil(share*sorted.size()));
    if(index==0)index=1;
    if(index>sorted.size())index=sorted.size();
    return sorted[index-1];
}
Json statistics(const std::vector<double> &values) {
    if(values.empty())return {{"count",0},{"p50",0.0},{"p95",0.0},{"max",0.0},{"mean",0.0}};
    auto sorted=values;std::sort(sorted.begin(),sorted.end());
    double sum=0;for(auto v:sorted)sum+=v;
    return {{"count",sorted.size()},{"p50",percentile(sorted,0.50)},{"p95",percentile(sorted,0.95)},
        {"max",sorted.back()},{"mean",sum/static_cast<double>(sorted.size())}};
}
double expression(const Json &e, const std::map<std::string,double> &values, unsigned depth = 0) {
    check(depth < 24, "Expression nesting limit");
    if (e.is_number()) {double v=e.get<double>(); requireFinite(v); return v;}
    if (e.is_boolean()) return e.get<bool>() ? 1 : 0;
    if (e.is_string()) {
        auto it=values.find(e.get<std::string>()); check(it!=values.end(), "Unknown signal: " + e.get<std::string>());
        return it->second;
    }
    check(e.is_object() && e.contains("op") && e.contains("args") && e.at("args").is_array(), "Invalid expression");
    const auto &args=e.at("args"); auto op=e.at("op").get<std::string>();
    auto value=[&](size_t i){check(i<args.size(), "Missing expression argument");return expression(args[i],values,depth+1);};
    if (op=="if") {check(args.size()==3,"if takes 3 arguments"); return value(0) ? value(1) : value(2);}
    check(args.size()==2, "Binary expression takes 2 arguments");
    double a=value(0), b=value(1), result=0;
    if(op=="add") result=a+b; else if(op=="sub") result=a-b; else if(op=="mul") result=a*b;
    else if(op=="div") {check(b!=0,"Division by zero");result=a/b;}
    else if(op=="gt") result=a>b; else if(op=="lt") result=a<b;
    else if(op=="ge") result=a>=b; else if(op=="le") result=a<=b;
    else if(op=="eq") result=a==b; else if(op=="and") result=a&&b;
    else if(op=="or") result=a||b; else if(op=="min") result=std::min(a,b);
    else if(op=="max") result=std::max(a,b); else throw std::invalid_argument("Unknown operation: "+op);
    requireFinite(result); return result;
}
void checkExpression(const Json &e, const std::set<std::string> &names, unsigned depth = 0) {
    check(depth < 24, "Expression nesting limit");
    if (e.is_number()) {requireFinite(e.get<double>());return;}
    if(e.is_boolean()) return;
    if(e.is_string()) {check(names.count(e.get<std::string>()),"Unknown signal: "+e.get<std::string>());return;}
    check(e.is_object() && e.contains("op") && e.contains("args") && e.at("args").is_array(),"Invalid expression");
    std::set<std::string> ops={"add","sub","mul","div","gt","lt","ge","le","eq","and","or","min","max","if"};
    auto op=e.at("op").get<std::string>(); check(ops.count(op),"Unknown operation: "+op);
    check(e.at("args").size()==(op=="if"?3:2),"Wrong expression arity");
    for(auto &arg:e.at("args")) checkExpression(arg,names,depth+1);
    if(op=="div" && e.at("args")[1].is_number()) check(e.at("args")[1].get<double>()!=0,"Division by zero");
}
uint64_t mask(unsigned length) {return (uint64_t(1)<<length)-1;}
Can encode(const Json &message,const std::map<std::string,double> &values) {
    std::vector<uint8_t> bytes(message.value("dlc",8),0);
    for(const auto &signal:message.at("signals")) {
        double value=std::clamp(values.at(signal.at("name").get<std::string>()),signal.at("min").get<double>(),signal.at("max").get<double>());
        auto integer=static_cast<int64_t>(std::llround((value-signal.value("offset",0.0))/signal.value("factor",1.0)));
        unsigned length=signal.at("length"),start=signal.at("start");
        uint64_t raw=static_cast<uint64_t>(integer)&mask(length);
        for(unsigned bit=0;bit<length;bit++)if(raw&(uint64_t(1)<<bit))bytes[(start+bit)/8]|=1U<<((start+bit)%8);
    }
    Can frame(message.at("id"),bytes);frame.setFrameType(message.value("extended",false));
    frame.setFd(message.value("fd",false),message.value("brs",false));return frame;
}
void decode(const Json &message,const Can &frame,std::map<std::string,double> &values) {
    check(frame.getData().size()==message.value("dlc",size_t(8)),"CAN length does not match definition");
    for(const auto &s:message.at("signals")) {
        unsigned length=s.at("length"),start=s.at("start");uint64_t raw=0;
        for(unsigned bit=0;bit<length;bit++)if(frame.getData()[(start+bit)/8]&(1U<<((start+bit)%8)))raw|=uint64_t(1)<<bit;
        int64_t integer=static_cast<int64_t>(raw);
        if(s.value("signed",false)&&(raw&(uint64_t(1)<<(length-1))))integer-=int64_t(1)<<length;
        values[s.at("name").get<std::string>()]=integer*s.value("factor",1.0)+s.value("offset",0.0);
    }
}
Json wire(const Can &frame,const std::string &bus="powertrain") {
    return {{"id",frame.getId()},{"data",frame.getData()},{"extended",frame.isExtendedFrame()},{"fd",frame.isFd()},{"brs",frame.bitrateSwitch()},{"bus",bus}};
}
Can frameFrom(const Json &item) {
    check(item.at("data").is_array(),"Frame data must be an array");
    for(auto &b:item.at("data"))check(b.is_number_integer()&&b.get<int64_t>()>=0&&b.get<uint64_t>()<=255,"Invalid frame byte");
    Can frame(item.at("id"),item.at("data").get<std::vector<uint8_t>>());
    frame.setFrameType(item.value("extended",false));frame.setFd(item.value("fd",false),item.value("brs",false));return frame;
}
// ISO-TP session configuration shared by the ECU kernel and the diagnostic tester.
IsoTpConfig isotpConfig(const Json &request) {
    IsoTpConfig config;
    config.txId=request.value("tx_id",0x7e0u);
    config.rxId=request.value("rx_id",0x7e8u);
    config.fd=request.value("fd",false);
    config.linkLength=request.value("link_length",8u);
    config.blockSize=request.value("block_size",8u);
    config.stmin=request.value("stmin",0u);
    config.extendedAddressing=request.value("extended",false);
    config.sourceAddress=request.value("sa",0u);
    config.targetAddress=request.value("ta",0u);
    config.nAs=request.value("n_as",1000.0);
    config.nBs=request.value("n_bs",1000.0);
    config.nAr=request.value("n_ar",1000.0);
    config.nCr=request.value("n_cr",1000.0);
    return config;
}
Json isotpFrameJson(const IsoTpFrame &frame) {
    return {{"id",frame.id},{"data",frame.data},{"extended",frame.id>0x7ff},{"fd",frame.fd},{"brs",frame.brs},
        {"bus","diagnostics"},{"delay_us",frame.delayUs}};
}
IsoTpFrame isotpFrameFrom(const Json &item) {
    check(item.at("data").is_array(),"Frame data must be an array");
    for(auto &b:item.at("data"))check(b.is_number_integer()&&b.get<int64_t>()>=0&&b.get<uint64_t>()<=255,"Invalid frame byte");
    IsoTpFrame frame;
    frame.id=item.at("id").get<uint32_t>();
    frame.data=item.at("data").get<std::vector<uint8_t>>();
    frame.fd=item.value("fd",false);
    frame.brs=item.value("brs",false);
    return frame;
}
// Logical clock of a session: the simulation tick, or the time given by the caller.
uint64_t isotpNow(const Json &request, uint64_t fallback) {
    if (!request.contains("now_ms")) return fallback;
    auto value=request.at("now_ms");
    check(value.is_number_integer()&&value.get<int64_t>()>=0,"Invalid ISO-TP clock");
    return value.get<uint64_t>();
}
std::string hexBytes(const std::vector<uint8_t> &bytes) {
    std::ostringstream out; out<<std::hex;
    for(auto b:bytes) {if(out.tellp()>0)out<<' ';out.width(2);out.fill('0');out<<unsigned(b);}return out.str();
}
const std::set<std::string> sensorSources={"speed","rpm","motor_temperature","battery_temperature","soc","voltage","current","wheel_fl","wheel_fr","wheel_rl","wheel_rr","lead_distance","closing_speed","static_object_range"};
const std::set<std::string> allowedInputs={"throttle","brake","ignition","ambient","road_friction","speed_sensor","rpm_sensor","temperature_sensor","battery_temperature_sensor","soc_sensor","voltage_sensor","current_sensor","wheel_fl_sensor","wheel_fr_sensor","wheel_rl_sensor","wheel_rr_sensor","radar_distance_sensor","closing_speed_sensor","static_object_sensor","front_perception_sensor","assist_enabled","target_speed","aeb_enabled"};
const std::set<std::string> allowedActuators={"torque_request","brake_pressure","fan","power_allowed"};
}
void SimulationManager::validateEcu(const Json &d,unsigned stepMs) {
    check(d.is_object() && d.value("schema_version",0)==1,"ECU schema_version must be 1");
    check(d.contains("name") && d.at("name").is_string() && !d.at("name").get<std::string>().empty() && d.at("name").get<std::string>().size()<=64,"Invalid ECU name");
    check(d.contains("inputs")&&d.at("inputs").is_array()&&d.contains("variables")&&d.at("variables").is_object(),"ECU inputs/variables required");
    check(d.at("variables").size()<=128 && d.at("inputs").size()<=128,"Too many ECU signals");
    std::set<std::string> names;
    for(auto &v:d.at("inputs")) check(v.is_string() && names.insert(v.get<std::string>()).second,"Invalid/duplicate input");
    for(auto it=d.at("variables").begin();it!=d.at("variables").end();++it) {
        check(names.insert(it.key()).second,"Duplicate variable/input");check(it.value().is_number()||it.value().is_boolean(),"Numeric variables required");requireFinite(it.value().is_boolean()?(it.value().get<bool>()?1:0):it.value().get<double>());
    }
    check(d.contains("bindings") && d.at("bindings").is_object(),"ECU bindings must be an object");
    check(d.contains("tasks")&&d.at("tasks").is_array()&&d.at("tasks").size()<=64,"Invalid tasks");
    for(auto &task:d.at("tasks")) {
        check(task.contains("period_ms")&&task.at("period_ms").is_number_unsigned(),"Task period must be an unsigned integer");
        unsigned period=task.at("period_ms");check(period>=stepMs && period<=60000 && period%stepMs==0,"Task period must be a multiple of simulation step");
        check(task.contains("assign")&&task.at("assign").is_object(),"Task assignments required");
        for(auto it=task.at("assign").begin();it!=task.at("assign").end();++it) {
            check(d.at("variables").contains(it.key()),"Assignments must target variables");checkExpression(it.value(),names);
        }
    }
    for(const auto &s:d.at("bindings").items())
        check(allowedActuators.count(s.key()) && s.value().is_string() && d.at("variables").contains(s.value().get<std::string>()),"Invalid actuator binding");
    auto freshness=d.value("freshness_inputs",Json::object());
    check(freshness.is_object(),"Freshness inputs must be an object");
    for(auto &binding:freshness.items()) {
        check(std::find(d.at("inputs").begin(),d.at("inputs").end(),binding.key())!=d.at("inputs").end(),"Freshness target must be a declared input");
        bool found=false;
        for(auto &m:d.at("messages"))if(m.at("direction")=="rx"&&m.at("id")==binding.value().at("id")&&m.value("bus","powertrain")==binding.value().value("bus","powertrain"))found=true;
        check(found,"Freshness input needs a declared RX message");
    }
    auto machine=d.at("state_machine");
    check(machine.at("states").is_array()&&machine.at("transitions").is_array(),"Invalid state machine");
    std::set<std::string> states;
    for(auto &state:machine.at("states")) check(state.is_string()&&states.insert(state.get<std::string>()).second,"Invalid/duplicate state");
    check(states.count(machine.at("initial").get<std::string>()),"Invalid initial state");
    for(auto &transition:machine.at("transitions")) {
        check(states.count(transition.at("from").get<std::string>()) && states.count(transition.at("to").get<std::string>()),"Unknown transition state");
        checkExpression(transition.at("when"),names);
    }
    check(d.contains("messages")&&d.at("messages").is_array()&&d.at("messages").size()<=64,"Invalid messages");
    std::set<uint32_t> ids;
    for(auto &message:d.at("messages")) {
        check(message.is_object() && message.contains("name") && message.at("name").is_string(),"CAN message name required");
        check(message.at("id").is_number_unsigned(),"CAN ID must be an unsigned integer");
        check(message.at("id").get<uint64_t>()<=0x1fffffffU,"CAN ID exceeds 29 bits");
        uint32_t id=message.at("id"); check(id<=(message.value("extended",false)?0x1fffffffU:0x7ffU)&&ids.insert(id).second,"Invalid/duplicate CAN ID");
        if(message.contains("dlc")) check(message.at("dlc").is_number_integer() && message.at("dlc").get<uint64_t>()>=1 && message.at("dlc").get<uint64_t>()<=64,"Invalid CAN length");
        unsigned dlc=message.value("dlc",8);check(dlc>=1&&dlc<=(message.value("fd",false)?64U:8U),"CAN payload exceeds protocol limit");
        std::set<unsigned> fdLengths={1,2,3,4,5,6,7,8,12,16,20,24,32,48,64};
        check(!message.value("fd",false)||fdLengths.count(dlc),"CAN FD payload is not a legal wire length");
        check(!message.value("brs",false)||message.value("fd",false),"Bitrate switching requires CAN FD");
        auto direction=message.at("direction").get<std::string>();check(direction=="tx"||direction=="rx","Invalid CAN direction");
        if(direction=="tx") {check(message.at("period_ms").is_number_unsigned(),"Invalid message period"); unsigned p=message.at("period_ms");check(p>=stepMs&&p<=60000&&p%stepMs==0,"Message period must be a multiple of simulation step");}
        check(message.at("signals").is_array() && !message.at("signals").empty(),"Message signals required");
        std::set<unsigned> occupied; std::set<std::string> signalNames;
        for(auto &s:message.at("signals")) {
            auto name=s.at("name").get<std::string>();check(names.count(name)&&signalNames.insert(name).second,"Unknown/duplicate CAN signal");
            check(s.at("start").is_number_unsigned()&&s.at("length").is_number_unsigned(),"Unsigned signal start/length required");
            check(s.at("start").get<uint64_t>()<=511 && s.at("length").get<uint64_t>()<=32,"Signal exceeds frame");
            unsigned start=s.at("start"),length=s.at("length");check(length>=1&&length<=32&&start+length<=dlc*8,"Signal exceeds frame");
            check(s.value("byte_order","little_endian")=="little_endian","Only little_endian signals supported");
            for(unsigned bit=start;bit<start+length;bit++)check(occupied.insert(bit).second,"Overlapping CAN signals");
            double factor=s.value("factor",1.0),offset=s.value("offset",0.0),low=s.at("min"),high=s.at("max");
            requireFinite(factor);requireFinite(offset);requireFinite(low);requireFinite(high);check(factor>0&&low<=high,"Invalid scaling/range");
            double rawLow=s.value("signed",false)?-std::pow(2,length-1):0;
            double rawHigh=s.value("signed",false)?std::pow(2,length-1)-1:std::pow(2,length)-1;
            check((low-offset)/factor>=rawLow-1e-9 && (high-offset)/factor<=rawHigh+1e-9,"Signal range exceeds encoding capacity: "+name);
            if(direction=="rx")check(std::find(d.at("inputs").begin(),d.at("inputs").end(),name)!=d.at("inputs").end(),"RX signals must be ECU inputs");
        }
    }
    std::set<std::string> codes;
    for(auto &dtc:d.value("dtcs",Json::array())) {
        check(codes.insert(dtc.at("code").get<std::string>()).second,"Duplicate DTC");
        check(dtc.at("uds_code").is_number_unsigned()&&dtc.at("uds_code").get<uint32_t>()<=0xffffff,"DTC uds_code must be 24-bit");
        checkExpression(dtc.at("trigger"),names);checkExpression(dtc.at("recover"),names);
    }
}
SimulationManager::SimulationManager(const std::string &path,bool distributed):scenarioPath_(path),distributed_(distributed) {
    static std::atomic<unsigned> next{0};
    channel_="simulation-"+std::to_string(next++);
    reset();
}
void SimulationManager::loadEcu(const Json &definition) {
    validateEcu(definition,stepMs_);
    for(auto &ecu:ecus_) {
        check(ecu.definition.at("name")!=definition.at("name"),"Duplicate ECU name");
        for(auto &a:ecu.definition.at("messages"))for(auto &b:definition.at("messages"))
            if(a["direction"]=="tx" && b["direction"]=="tx") check(a["id"]!=b["id"]||a.value("bus","powertrain")!=b.value("bus","powertrain"),"CAN TX ID collision");
        for(auto &b:definition.at("bindings").items())
            check(!ecu.definition.value("bindings",Json::object()).contains(b.key()),"Actuator already has an owner");
    }
    Ecu ecu;ecu.definition=definition;ecu.state=definition.at("state_machine")["initial"];
    for(auto &input:definition.at("inputs")) ecu.values[input.get<std::string>()]=0;
    for(auto &v:definition.at("variables").items())ecu.values[v.key()]=v.value().is_boolean()?(v.value().get<bool>()?1:0):v.value().get<double>();
    ecu.kernel=std::make_unique<EcuKernel>(definition,stepMs_);
    ecu.kernel->command({{"action","reset"},{"epoch",epoch_},{"start_tick",tick_}});
    ecu.bus=std::make_unique<CanBus>(CanBus::Backend::Internal,channel_);ecu.bus->init();
    ecus_.push_back(std::move(ecu));
}
void SimulationManager::reset() { applyScenario(readJson(scenarioPath_),nullptr); }
// Applies a scenario and its ECU definitions. The definitions are optional: when they are
// missing they are read from the scenario directory, which is how a project on disk starts.
void SimulationManager::applyScenario(const Json &scenario,const Json *inlineDefinitions) {
    scenario_=scenario;if(!scenario_.contains("gateway_routes"))scenario_["gateway_routes"]=Json::array();epoch_++;prepared_=nullptr;policies_=Json::array();scheduled_=Json::array();busFreeUs_.clear();busBusyUs_.clear();dropped_=0;stepMs_=scenario_.at("step_ms");
    // A new scenario starts a new measurement window.
    metrics_.clear();busMetrics_.clear();
    check(stepMs_>=1&&stepMs_<=100,"Simulation step must be 1..100 ms");
    check(scenario_.value("bitrate",500000)>=10000 && scenario_.value("bitrate",500000)<=1000000,"Invalid CAN bitrate");
    ecus_.clear();monitor_=std::make_unique<CanBus>(CanBus::Backend::Internal,channel_);monitor_->init();
    tick_=frameCount_=frameBits_=frameSeq_=0;running_=false;random_.seed(42);
    frames_=Json::array();events_=Json::array();envelopes_=Json::array();actuations_=Json::array();scheduled_=Json::array();
    testers_.clear();
    controls_={{"throttle",0},{"brake",0},{"ignition",1},{"ambient",25},{"road_friction",0.8},{"assist_enabled",0},{"target_speed",80},{"aeb_enabled",1},{"lead_speed",12}};
    world_={{"speed",0},{"acceleration",0},{"position",0},{"rpm",0},{"motor_temperature",30},{"battery_temperature",28},
        {"soc",85},{"voltage",390},{"current",0},{"wheel_fl",0},{"wheel_fr",0},{"wheel_rl",0},{"wheel_rr",0},
        {"torque_request",0},{"brake_pressure",0},{"fan",0},{"power_allowed",1},{"lead_distance",120},{"closing_speed",0},{"lead_position",120},{"collision",0},
        {"static_object_s",150},{"static_object_range",150}};
    auto initialState=scenario_.value("initial_state",Json::object());
    for(auto &v:initialState.items()) {
        check(world_.count(v.key()),"Unknown initial state: "+v.key());double n=v.value();requireFinite(n);world_[v.key()]=n;
    }
    check(world_["speed"]>=0&&world_["speed"]<=80&&world_["soc"]>=0&&world_["soc"]<=100,"Initial state out of bounds");
    sensors_=scenario_.at("sensors");
    std::set<std::string> sensorNames;
    for(auto &sensor:sensors_) {
        auto name=sensor["name"].get<std::string>(), source=sensor["source"].get<std::string>();
        check(allowedInputs.count(name)&&sensorNames.insert(name).second&&sensorSources.count(source),"Invalid sensor name/source");
        unsigned period=sensor["period_ms"];
        check(period>=stepMs_ && period<=60000 && period%stepMs_==0,"Invalid sensor period");
        double low=sensor["min"],high=sensor["max"],noise=sensor.value("noise",0.0);requireFinite(low);requireFinite(high);requireFinite(noise);
        check(low<=high&&noise>=0,"Invalid sensor range/noise");sensor["fault"]="none";sensor["measured"]=world_.at(source);
    }
    auto directory=std::filesystem::path(scenarioPath_).parent_path();
    definitions_=Json::object();
    for(auto &file:scenario_["ecus"]) {
        auto name=file.get<std::string>();
        Json definition=inlineDefinitions&&inlineDefinitions->contains(name)?inlineDefinitions->at(name):readJson(directory/name);
        definitions_[name]=definition;
        loadEcu(definition);
    }
    // Resolve cross-ECU dependencies and require matching wire layouts.
    for(auto &ecu:ecus_)for(auto &input:ecu.definition.at("inputs")) {
        auto name=input.get<std::string>();bool found=controls_.count(name)||sensorNames.count(name)||ecu.definition.value("freshness_inputs",Json::object()).contains(name);
        for(auto &message:ecu.definition.at("messages"))if(message.at("direction")=="rx")
            for(auto &s:message.at("signals"))if(s.at("name")==name)found=true;
        check(found,"Unbound ECU input: "+name);
    }
    for(auto &ecu:ecus_)for(auto &rx:ecu.definition.at("messages"))if(rx["direction"]=="rx") {
        bool found=false;
        for(auto &sender:ecus_)for(auto &tx:sender.definition.at("messages"))if(tx["direction"]=="tx"&&tx["id"]==rx["id"]&&(tx.value("bus","powertrain")==rx.value("bus","powertrain")||std::any_of(scenario_["gateway_routes"].begin(),scenario_["gateway_routes"].end(),[&](auto &route){return route["id"]==tx["id"]&&route["from"]==tx.value("bus","powertrain")&&route["to"]==rx.value("bus","powertrain");}))) {
            check(tx.value("dlc",8)==rx.value("dlc",8)&&tx["signals"]==rx["signals"],"RX/TX layout mismatch");
            found=true;
        }
        check(found,"CAN RX has no transmitter");
    }
}
Json SimulationManager::prepareTick() {
    if(!prepared_.is_null())return prepared_;
    double nowUs=static_cast<double>(tick_*stepMs_*1000ULL);
    for(auto &sensor:sensors_) {
        unsigned period=sensor["period_ms"];if((tick_*stepMs_)%period)continue;
        double measured=world_.at(sensor["source"].get<std::string>());auto fault=sensor["fault"].get<std::string>();
        if(fault=="stuck")measured=sensor.value("fault_value",120.0);
        else if(fault=="disconnected")measured=sensor["max"].get<double>()+10;
        else {
            double sigma=sensor.value("noise",0.0)*(fault=="noisy"?25:1);
            if(sigma>0)measured+=std::normal_distribution<double>(0,sigma)(random_);
            if(fault=="bias")measured+=sensor.value("fault_value",10.0);
            measured=std::clamp(measured,sensor["min"].get<double>(),sensor["max"].get<double>());
        }
        sensor["measured"]=measured;
        sensor["sampled_at_us"]=static_cast<uint64_t>(nowUs);
        sensor["validity"]=fault=="none"?"valid":"invalid";
        // Envelope B: acquisition is a distinct event from computation and delivery.
        pushEnvelope({{"contract_version",2},{"kind","signal"},{"epoch",epoch_},{"tick",tick_},
            {"sim_time_us",static_cast<uint64_t>(nowUs)},
            {"source",{{"ecu","vehicle plant"},{"signal",sensor.at("source").get<std::string>()}}},
            {"name",sensor.at("name").get<std::string>()},{"value",measured},{"unit",sensor.value("unit","")},
            {"validity",sensor.at("validity")},{"quality_reason",fault=="none"?"none":"sensor_fault"},
            {"timestamps",{{"sampled_at_us",static_cast<uint64_t>(nowUs)}}}});
    }
    auto remaining=Json::array();
    for(auto &delivery:scheduled_) {
        if(delivery.at("arrival_us").get<double>()>nowUs) {remaining.push_back(delivery);continue;}
        double arrival=delivery.at("arrival_us").get<double>();uint64_t seq=delivery.at("seq").get<uint64_t>();
        unsigned receivers=0;
        for(auto &ecu:ecus_) {
            if(ecu.definition.at("name")==delivery.at("source"))continue;
            bool interested=false;
            for(auto &m:ecu.definition.at("messages"))
                if(m.at("direction")=="rx"&&m.at("id")==delivery.at("frame").at("id")&&m.value("bus","powertrain")==delivery.at("frame").value("bus","powertrain"))interested=true;
            if(interested) {ecu.inbox.push_back(delivery.at("frame"));receivers++;metrics_[ecu.definition.at("name").get<std::string>()].rx++;}
        }
        for(auto &record:frames_)if(record.value("seq",UINT64_MAX)==seq) {
            record["delivered_at_us"]=static_cast<uint64_t>(arrival);
            record["delivered_to"]=receivers;
            break;
        }
        // Envelope B: received_at_us is the delivered frame's bus arrival, not the sampling instant.
        pushEnvelope({{"contract_version",2},{"kind","can_frame"},{"epoch",epoch_},{"tick",tick_},
            {"sim_time_us",static_cast<uint64_t>(arrival)},
            {"source",{{"ecu",delivery.at("source").get<std::string>()},{"message",delivery.value("name","")},
                {"can_id",delivery.at("frame").at("id")},{"bus",delivery.at("frame").value("bus","powertrain")}}},
            {"name",delivery.value("name","")},{"validity","valid"},{"quality_reason","none"},
            {"timestamps",{{"received_at_us",static_cast<uint64_t>(arrival)}}}});
    }scheduled_=remaining;
    Json nodes=Json::array();
    for(auto &ecu:ecus_) {
        Json inputs=controls_;
        for(auto &sensor:sensors_)inputs[sensor.at("name").get<std::string>()]=sensor.at("measured");
        nodes.push_back({{"name",ecu.definition.at("name")},{"tick",tick_},{"epoch",epoch_},{"step_ms",stepMs_},{"inputs",inputs},{"frames",ecu.inbox}});
        ecu.inbox=Json::array();
    }
    prepared_={{"tick",tick_},{"epoch",epoch_},{"nodes",nodes}};
    return prepared_;
}
void SimulationManager::tick() {
    auto prepared=prepareTick();Json results=Json::array();
    for(size_t i=0;i<ecus_.size();++i)results.push_back(ecus_[i].kernel->tick(prepared.at("nodes")[i]));
    completeTick(results);
}
Json SimulationManager::completeTick(const Json &results) {
    check(!prepared_.is_null(),"No prepared tick");
    check(results.is_array()&&results.size()==ecus_.size(),"All ECU workers must complete the tick");
    // Validate every response before committing any plant/ECU state.
    for(size_t i=0;i<ecus_.size();++i) {
        const auto &r=results[i];auto &ecu=ecus_[i];
        check(r.at("name")==ecu.definition.at("name")&&r.at("tick")==tick_&&r.at("epoch")==epoch_,"ECU tick/epoch mismatch");
        check(r.at("values").is_object()&&r.at("tx_frames").is_array(),"Malformed ECU worker result");
        // Timing envelope (v2) is mandatory: a worker that cannot declare completion time cannot pass the barrier.
        check(r.contains("timing")&&r.at("timing").is_object(),"ECU tick result must carry a timing envelope");
        const auto &timing=r.at("timing");
        check(timing.contains("sampled_at_us")&&timing.contains("computed_at_us"),"ECU timing must declare sampled_at_us and computed_at_us");
        uint64_t tickUs=static_cast<uint64_t>(tick_)*stepMs_*1000ULL;
        uint64_t sampled=timing.at("sampled_at_us").get<uint64_t>(),computed=timing.at("computed_at_us").get<uint64_t>();
        check(sampled==tickUs,"ECU sampled_at_us must equal the tick start");
        check(computed>=tickUs&&computed<=tickUs+uint64_t(stepMs_)*1000ULL,"ECU computed_at_us outside its tick window");
        check(r.at("values").size()==ecu.values.size(),"ECU values do not match its interface");
        for(auto &v:r.at("values").items()) {check(ecu.values.count(v.key()),"Unknown ECU variable");requireFinite(v.value().get<double>());}
        for(auto &f:r.at("tx_frames")) {
            bool valid=false;
            for(auto &m:ecu.definition.at("messages"))if(m.at("direction")=="tx"&&m.at("id")==f.at("id")&&m.value("bus","powertrain")==f.value("bus","powertrain")) {
                auto expected=wire(encode(m,r.at("values").get<std::map<std::string,double>>()),m.value("bus","powertrain"));
                check(expected==f,"ECU transmitted frame does not match its validated values");valid=true;
            }check(valid,"ECU transmitted an undeclared CAN frame");
        }
    }
    struct Pending {Ecu *ecu;Json message;Can frame;};std::vector<Pending> pending;
    for(size_t i=0;i<ecus_.size();++i) {
        auto &ecu=ecus_[i];const auto &r=results[i];
        auto previous=ecu.state;ecu.state=r.at("state").get<std::string>();ecu.values=r.at("values").get<std::map<std::string,double>>();ecu.hardware=r.at("hardware");
        ecu.timing=r.at("timing");
        auto &metrics=metrics_[ecu.definition.at("name").get<std::string>()];
        // Task cost: the ECU declared when it finished computing on the simulation clock.
        uint64_t sampledAt=ecu.timing.at("sampled_at_us").get<uint64_t>(),computedAt=ecu.timing.at("computed_at_us").get<uint64_t>();
        double taskUs=static_cast<double>(computedAt-sampledAt);
        metrics.task.push_back(taskUs);metrics.taskTimeline.push_back({computedAt,taskUs});
        if(metrics.task.size()>600)metrics.task.erase(metrics.task.begin());
        if(metrics.taskTimeline.size()>300)metrics.taskTimeline.erase(metrics.taskTimeline.begin());
        ecu.dtcActive=r.at("dtc_active").get<std::map<std::string,bool>>();ecu.dtcStored=r.at("dtc_stored").get<std::map<std::string,bool>>();
        if(previous!=ecu.state) {events_.push_back({{"time",tick_*stepMs_/1000.0},{"ecu",ecu.definition.at("name")},{"from",previous},{"to",ecu.state}});metrics.states++;}
        // Plant commit is quantized to the tick boundary (addendum B): applied_at_us >= computed_at_us always.
        uint64_t tickUs=static_cast<uint64_t>(tick_)*stepMs_*1000ULL;
        uint64_t appliedUs=tickUs+uint64_t(stepMs_)*1000ULL;
        for(auto &binding:ecu.definition.at("bindings").items()) {
            double applied=ecu.values.at(binding.value().get<std::string>());
            // Control chain measured every tick: computed -> applied (plant commit) and sampled -> applied.
            double actuationUs=static_cast<double>(appliedUs-computedAt);
            double chainUs=static_cast<double>(appliedUs-tickUs);
            metrics.actuation.push_back(actuationUs);metrics.e2e.push_back(chainUs);
            metrics.chainTimeline.push_back({appliedUs,chainUs});
            if(metrics.actuation.size()>600)metrics.actuation.erase(metrics.actuation.begin());
            if(metrics.e2e.size()>600)metrics.e2e.erase(metrics.e2e.begin());
            if(metrics.chainTimeline.size()>300)metrics.chainTimeline.erase(metrics.chainTimeline.begin());
            if(world_.count(binding.key())&&world_.at(binding.key())!=applied) {
                // Envelope B: applied_at_us is the plant commit of the actuator value, distinct from its computation.
                pushActuation({{"contract_version",2},{"kind","event_chain"},{"stage","actuation"},
                    {"correlation_id",std::to_string(epoch_)+":"+std::to_string(tick_)+":"+binding.key()},
                    {"epoch",epoch_},{"tick",tick_},{"sim_time_us",appliedUs},
                    {"source",{{"ecu",ecu.definition.at("name")}}},
                    {"name",binding.key()},{"value",applied},{"validity","valid"},{"quality_reason","none"},
                    {"timestamps",{{"sampled_at_us",tickUs},{"computed_at_us",ecu.timing.at("computed_at_us")},
                        {"applied_at_us",appliedUs}}}});
            }
            world_[binding.key()]=applied;
        }
        for(auto &frame:r.at("tx_frames"))for(auto &m:ecu.definition.at("messages"))
            if(m.at("direction")=="tx"&&m.at("id")==frame.at("id")&&m.value("bus","powertrain")==frame.value("bus","powertrain"))pending.push_back({&ecu,m,frameFrom(frame)});
    }
    auto priority=[](const Pending &p) {auto id=p.frame.getId();return std::tuple(p.message.value("bus","powertrain"),p.frame.isExtendedFrame()?id>>18:id,p.frame.isExtendedFrame()?1:0,id);};
    std::stable_sort(pending.begin(),pending.end(),[&](auto &a,auto &b){return priority(a)<priority(b);});
    for(auto &p:pending) {
        auto source=p.ecu->definition.at("name").get<std::string>(),bus=p.message.value("bus","powertrain");
        transmit(source,p.message,p.frame,bus);
        for(auto &route:scenario_.value("gateway_routes",Json::array()))
            if(route.at("from")==bus&&route.at("id")==p.frame.getId())transmit(source,p.message,p.frame,route.at("to").get<std::string>(),true);
    }
    while(frames_.size()>200)frames_.erase(frames_.begin());
    while(events_.size()>80)events_.erase(events_.begin());
    double dt=stepMs_/1000.0;
    // Educational longitudinal plant: speed is m/s; wheel tangential speed is m/s.
    double speed=world_["speed"],friction=controls_["road_friction"];
    double torque=std::clamp(world_["torque_request"],0.0,320.0)*std::clamp(world_["power_allowed"],0.0,1.0)*controls_["ignition"];
    double pressure=std::clamp(world_["brake_pressure"],0.0,1.0);
    double traction=torque*9/0.32, braking=pressure*12000;
    double drag=0.42*speed*speed+(speed>0?180:0);
    world_["acceleration"]=(traction-std::min(braking,friction*1500*9.81)-drag)/1500;
    world_["speed"]=std::clamp(speed+world_["acceleration"]*dt,0.0,80.0);
    world_["position"]+=world_["speed"]*dt;
    double slip=std::clamp((pressure-friction)*0.85,0.0,0.95);
    for(auto name:{"wheel_fl","wheel_fr","wheel_rl","wheel_rr"})world_[name]=world_["speed"]*(1-slip);
    world_["rpm"]=world_["speed"]/0.32*9*60/(2*3.141592653589793);
    double power=torque*world_["rpm"]*2*3.141592653589793/60;
    world_["current"]=power/ std::max(200.0,world_["voltage"]);
    world_["soc"]=std::clamp(world_["soc"]-power*dt/(60*3600000)*100,0.0,100.0);
    world_["voltage"]=320+world_["soc"]*0.85;
    world_["motor_temperature"]+=(power*0.000025-(world_["motor_temperature"]-controls_["ambient"])*(0.015+std::clamp(world_["fan"],0.0,1.0)*0.12))*dt;
    world_["battery_temperature"]+=(world_["current"]*world_["current"]*0.000012-(world_["battery_temperature"]-controls_["ambient"])*0.012)*dt;
    world_["lead_position"]+=controls_["lead_speed"]*dt;
    world_["lead_distance"]=std::max(0.0,world_["lead_position"]-world_["position"]);
    world_["closing_speed"]=std::max(0.0,world_["speed"]-controls_["lead_speed"]);
    // Second object on the road: a static obstacle at a fixed track position, sensed independently from the lead vehicle.
    world_["static_object_range"]=std::max(0.0,world_["static_object_s"]-world_["position"]);
    world_["collision"]=world_["lead_distance"]<=0?1:0;
    tick_++;prepared_=nullptr;
    return {{"committed",true},{"tick",tick_},{"epoch",epoch_}};
}
void SimulationManager::step(unsigned count) {check(!distributed_,"Distributed mode requires the worker barrier");check(count>=1&&count<=1000,"Step count must be 1..1000");for(unsigned i=0;i<count;++i)tick();}
Json SimulationManager::performance() const {
    Json ecus=Json::array();
    double chainP95=0,taskP95=0,deliveryP95=0;
    for(auto &ecu:ecus_) {
        auto name=ecu.definition.at("name").get<std::string>();
        EcuMetrics empty;const EcuMetrics &m=metrics_.count(name)?metrics_.at(name):empty;
        Json task=statistics(m.task),e2e=statistics(m.e2e),delivery=statistics(m.delivery),actuation=statistics(m.actuation);
        chainP95=std::max(chainP95,e2e.at("p95").get<double>());
        taskP95=std::max(taskP95,task.at("p95").get<double>());
        deliveryP95=std::max(deliveryP95,delivery.at("p95").get<double>());
        Json timeline=Json::array();
        std::vector<std::tuple<uint64_t,int,double>> merged;
        for(auto &sample:m.taskTimeline)merged.emplace_back(sample.first,0,sample.second);
        for(auto &sample:m.deliveryTimeline)merged.emplace_back(sample.first,1,sample.second);
        for(auto &sample:m.chainTimeline)merged.emplace_back(sample.first,2,sample.second);
        std::sort(merged.begin(),merged.end());
        for(auto &sample:merged) {
            const char *key=std::get<1>(sample)==0?"task_us":std::get<1>(sample)==1?"delivery_us":"chain_us";
            timeline.push_back({{"t_us",std::get<0>(sample)},{key,std::get<2>(sample)}});
        }
        ecus.push_back({{"name",name},{"task",task},{"e2e",e2e},{"delivery",delivery},{"actuation",actuation},
            {"tx",m.tx},{"rx",m.rx},{"dropped",m.dropped},{"states",m.states},{"timeline",timeline}});
    }
    Json buses=Json::array();
    uint64_t frames=0,drops=0;
    for(auto &bus:scenario_.value("buses",Json::array())) {
        auto id=bus.at("id").get<std::string>();
        auto counters=busMetrics_.count(id)?busMetrics_.at(id):BusMetrics{};
        auto item=bus;auto busy=busBusyUs_.find(id);
        item["load_percent"]=tick_&&busy!=busBusyUs_.end()?100.0*busy->second/(tick_*stepMs_*1000.0):0;
        auto backlog=busFreeUs_.find(id);item["backlog_us"]=backlog==busFreeUs_.end()?0:std::max(0.0,backlog->second-tick_*stepMs_*1000.0);
        item["frames"]=counters.frames;item["dropped"]=counters.dropped;
        item["frame_rate_hz"]=tick_?static_cast<double>(counters.frames)/(tick_*stepMs_/1000.0):0;
        frames+=counters.frames;drops+=counters.dropped;
        buses.push_back(item);
    }
    return {{"ecus",ecus},{"buses",buses},
        {"totals",{{"frames",frames},{"dropped",drops},{"envelopes",envelopes_.size()},{"actuations",actuations_.size()},
            {"ticks",tick_},{"step_ms",stepMs_},{"window_s",static_cast<double>(tick_*stepMs_)/1000.0},
            {"frame_rate_hz",tick_?static_cast<double>(frames)/(tick_*stepMs_/1000.0):0},
            {"chain_p95_us",chainP95},{"task_p95_us",taskP95},{"delivery_p95_us",deliveryP95}}}};
}
Json SimulationManager::snapshot() const {
    Json data={{"name",scenario_.value("name","Vehicle lab")},{"time",tick_*stepMs_/1000.0},{"step_ms",stepMs_},{"running",running_},{"controls",controls_},{"world",world_},
        {"sensors",sensors_},{"frames",frames_},{"events",events_},{"frame_count",frameCount_},{"bitrate",scenario_.value("bitrate",500000)},
        {"bus_load_estimate",tick_?100.0*frameBits_/(tick_*stepMs_/1000.0*scenario_.value("bitrate",500000)):0},{"ecus",Json::array()}};
    for(auto &ecu:ecus_) {
        Json dtcs=Json::array();
        for(auto &d:ecu.definition.value("dtcs",Json::array())) {
            auto code=d.at("code").get<std::string>();auto it=ecu.dtcStored.find(code);
            if(it!=ecu.dtcStored.end()&&it->second) {auto item=d;item.erase("trigger");item.erase("recover");item["active"]=ecu.dtcActive.at(code);dtcs.push_back(item);}
        }
        data["ecus"].push_back({{"name",ecu.definition.at("name")},{"state",ecu.state},{"values",ecu.values},{"dtcs",dtcs},{"definition",ecu.definition},{"hardware",ecu.hardware}});
    }
    data["epoch"]=epoch_;data["tick"]=tick_;data["execution_mode"]=distributed_?"distributed":"embedded";
    // The project hash identifies the loaded scenario, definitions, CAN map and run configuration.
    data["project_hash"]="sha256:"+sha256Hex(projectContent().dump());
    // Measurement envelope: age is observed_at - sampled_at on the simulation clock only.
    data["timing"]={{"contract_version",2},{"envelopes",envelopes_},{"actuations",actuations_},{"frame_seq",frameSeq_}};
    uint64_t nowUs=static_cast<uint64_t>(tick_)*stepMs_*1000ULL;
    for(auto &sensor:data.at("sensors"))
        if(sensor.contains("sampled_at_us"))sensor["data_age_us"]=nowUs-sensor.at("sampled_at_us").get<uint64_t>();
    data["policies"]=policies_;data["dropped_frames"]=dropped_;data["queued_frames"]=scheduled_.size();data["buses"]=Json::array();
    for(auto &bus:scenario_.value("buses",Json::array())) {
        auto item=bus;auto id=bus.at("id").get<std::string>();auto it=busBusyUs_.find(id);
        item["load_percent"]=tick_&&it!=busBusyUs_.end()?100.0*it->second/(tick_*stepMs_*1000.0):0;
        auto backlog=busFreeUs_.find(id);item["backlog_us"]=backlog==busFreeUs_.end()?0:std::max(0.0,backlog->second-tick_*stepMs_*1000.0);
        data["buses"].push_back(item);
    }
    // Performance window: per-ECU latency decomposition, bus throughput and backlog.
    data["performance"]=performance();
    return data;
}
Json SimulationManager::diagnostic(const Json &request) {
    auto name=request.at("ecu").get<std::string>();Ecu *target=nullptr;
    for(auto &ecu:ecus_)if(ecu.definition.at("name")==name)target=&ecu;
    check(target!=nullptr,"Unknown diagnostic ECU");
    check(request.at("bytes").is_array(),"UDS bytes must be an array");
    for(auto &value:request.at("bytes"))check(value.is_number_unsigned() && value.get<uint64_t>()<=255,"UDS bytes must be integers 0..255");
    auto bytes=request.at("bytes").get<std::vector<uint8_t>>();check(!bytes.empty()&&bytes.size()<=4095,"Invalid UDS payload");
    // Perform an actual ISO-TP request and response over an isolated internal CAN channel.
    CanBus client(CanBus::Backend::Internal,channel_+"-diagnostics"),server(CanBus::Backend::Internal,channel_+"-diagnostics");client.init();server.init();
    CanTp tester(0x7e0,0x7e8,client),ecuTp(0x7e8,0x7e0,server);
    auto job=std::async(std::launch::async,[&]{
        auto payload=ecuTp.receiveMessage();std::vector<uint8_t> req(payload.begin(),payload.end());
        auto result=target->kernel->command({{"action","uds"},{"bytes",req}});
        auto response=result.at("bytes").get<std::vector<uint8_t>>();
        target->dtcActive=result.at("snapshot").at("dtc_active").get<std::map<std::string,bool>>();
        target->dtcStored=result.at("snapshot").at("dtc_stored").get<std::map<std::string,bool>>();
        ecuTp.sendMessage(std::string(response.begin(),response.end()));
    });
    tester.sendMessage(std::string(bytes.begin(),bytes.end()));auto payload=tester.receiveMessage();job.get();
    std::vector<uint8_t> result(payload.begin(),payload.end());
    return {{"request",hexBytes(bytes)},{"response",hexBytes(result)},{"bytes",result},{"transport","ISO-TP / internal CAN"},{"request_id",0x7e0},{"response_id",0x7e8}};
}
// Diagnostic tester endpoint: the ISO-TP session lives in the core, the controller only pumps frames.
Json SimulationManager::isotpTester(const Json &request) {
    auto action=request.value("action","");
    auto session=request.at("session").get<std::string>();
    if(action=="isotp_tester_start") {
        check(testers_.size()<64,"ISO-TP session limit");
        testers_[session]=std::make_unique<IsoTp>(isotpConfig(request));
        return {{"ready",true}};
    }
    if(action=="isotp_tester_end") {testers_.erase(session);return {{"closed",true}};}
    auto it=testers_.find(session);
    check(it!=testers_.end(),"Unknown ISO-TP session");
    auto &transport=*it->second;
    auto now=isotpNow(request,tick_*stepMs_);
    if(action=="isotp_tester_send") {
        auto payload=request.at("bytes").get<std::vector<uint8_t>>();
        check(!payload.empty()&&payload.size()<=4095,"Invalid ISO-TP payload");
        Json frames=Json::array();
        for(auto &frame:transport.startSend(payload,now))frames.push_back(isotpFrameJson(frame));
        return {{"frames",frames}};
    }
    if(action=="isotp_tester_receive") {
        auto frame=isotpFrameFrom(request.at("frame"));
        Json frames=Json::array();
        for(auto &out:transport.onFrame(frame,now))frames.push_back(isotpFrameJson(out));
        Json message=nullptr;bool done=false;
        if(transport.hasMessage()) {message=transport.takeMessage();done=true;}
        return {{"frames",frames},{"message",message},{"done",done}};
    }
    if(action=="isotp_tester_poll") {transport.poll(now);return {{"ok",true}};}
    throw std::invalid_argument("Unknown ISO-TP action");
}
// Hashed content of a project: scenario, ECU definitions, CAN map and run configuration.
// The live controls and the simulation state are runtime data and stay out of the hash.
Json projectContentOf(const Json &project) {
    return {{"scenario",project.at("scenario")},{"ecu_definitions",project.at("ecu_definitions")},
        {"can_map",project.value("can_map",Json::array())},{"run",project.value("run",Json::object())}};
}
Json SimulationManager::projectContent() const {
    Json map=Json::array();
    for(auto &ecu:ecus_)for(auto &message:ecu.definition.at("messages")) {
        Json entry={{"ecu",ecu.definition.at("name")},{"message",message.at("name")},{"id",message.at("id")},
            {"direction",message.at("direction")},{"bus",message.value("bus","powertrain")},{"dlc",message.value("dlc",8)},
            {"fd",message.value("fd",false)},{"period_ms",message.value("period_ms",0)},{"signals",Json::array()}};
        for(auto &signal:message.at("signals"))
            entry["signals"].push_back({{"name",signal.at("name")},{"start",signal.at("start")},{"length",signal.at("length")},
                {"factor",signal.value("factor",1.0)},{"offset",signal.value("offset",0.0)},{"unit",signal.value("unit","")}});
        map.push_back(entry);
    }
    return {{"scenario",scenario_},{"ecu_definitions",definitions_},{"can_map",map},
        {"run",{{"step_ms",stepMs_},{"bitrate",scenario_.value("bitrate",500000)},{"policies",policies_}}}};
}
Json SimulationManager::exportProject() const {
    auto content=projectContent();
    Json project={{"kind","vcanproj"},{"schema_version",1},{"name",scenario_.value("name","Vehicle lab")},
        {"project_hash","sha256:"+sha256Hex(content.dump())},{"scenario",content.at("scenario")},
        {"ecu_definitions",content.at("ecu_definitions")},{"can_map",content.at("can_map")},{"run",content.at("run")},
        {"runtime",{{"tick",tick_},{"controls",controls_},{"world",world_}}}};
    return project;
}
Json SimulationManager::importProject(const Json &project) {
    check(project.is_object(),"Project must be an object");
    check(project.value("kind","")=="vcanproj","Not a .vcanproj project");
    check(project.value("schema_version",0)==1,"Unsupported project schema version");
    check(project.contains("scenario")&&project.at("scenario").is_object(),"Project scenario missing");
    check(project.contains("ecu_definitions")&&project.at("ecu_definitions").is_object(),"Project ECU definitions missing");
    // The hash identifies the exact content: a modified project is refused before any state change.
    auto content=projectContentOf(project);
    auto hash=sha256Hex(content.dump());
    check(project.value("project_hash","")=="sha256:"+hash,"Project hash mismatch: declared "+project.value("project_hash","none")+", computed sha256:"+hash);
    check(project.at("scenario").contains("ecus")&&project.at("scenario").at("ecus").is_array(),"Project scenario has no ECU list");
    for(auto &file:project.at("scenario").at("ecus")) {
        auto name=file.get<std::string>();
        check(project.at("ecu_definitions").contains(name),"Project misses the definition of "+name);
    }
    auto previousScenario=scenario_,previousDefinitions=definitions_;
    try {
        applyScenario(project.at("scenario"),&project.at("ecu_definitions"));
        // The flow policies are part of the project: the laboratory restarts with them.
        auto rules=project.value("run",Json::object()).value("policies",Json::array());
        check(rules.is_array()&&rules.size()<=64,"Invalid network policies");
        for(auto &rule:rules) {
            check(rule.is_object(),"Network rule must be an object");
            double latency=rule.value("latency_ms",0.0);requireFinite(latency);check(latency>=0&&latency<=1000,"Invalid network latency");
        }
        policies_=rules;
    } catch(...) {
        // A rejected project leaves the running laboratory exactly as it was.
        applyScenario(previousScenario,&previousDefinitions);
        throw;
    }
    Json names=Json::array();
    for(auto &ecu:ecus_)names.push_back(ecu.definition.at("name"));
    return {{"imported",true},{"project_hash",exportProject().at("project_hash")},{"ecus",names}};
}
Json SimulationManager::command(const Json &r) {
    auto action=r.value("action","snapshot");
    if(action=="snapshot")return snapshot();
    if(action=="prepare_tick")return prepareTick();
    if(action=="complete_tick")return completeTick(r.at("results"));
    if(action=="refresh_ecu") {
        for(auto &ecu:ecus_)if(ecu.definition.at("name")==r.at("name")) {
            ecu.dtcActive=r.at("snapshot").at("dtc_active").get<std::map<std::string,bool>>();
            ecu.dtcStored=r.at("snapshot").at("dtc_stored").get<std::map<std::string,bool>>();
            return snapshot();
        }throw std::invalid_argument("Unknown ECU");
    }
    if(action=="environment") {
        check(prepared_.is_null(),"Complete the pending tick or reset before changing the environment");
        for(auto &v:r.at("values").items()) {
            double value=v.value();requireFinite(value);
            if(v.key()=="lead_distance") {check(value>=1&&value<=1000,"Lead distance outside 1..1000 m");world_["lead_distance"]=value;world_["lead_position"]=world_["position"]+value;}
            else if(v.key()=="speed") {check(value>=0&&value<=60,"Initial speed outside 0..60 m/s");world_["speed"]=value;for(auto name:{"wheel_fl","wheel_fr","wheel_rl","wheel_rr"})world_[name]=value;}
            else throw std::invalid_argument("Unknown environment setting");
        }return snapshot();
    }
    if(action=="network_policy") {
        check(r.at("rules").is_array()&&r.at("rules").size()<=64,"Invalid network policies");
        for(auto &rule:r.at("rules")) {
            check(rule.is_object(),"Network rule must be an object");
            double latency=rule.value("latency_ms",0.0);requireFinite(latency);check(latency>=0&&latency<=1000,"Invalid network latency");
        }policies_=r.at("rules");return snapshot();
    }
    if(action=="diagnostic_trace") {
        auto frame=frameFrom(r.at("frame"));Json message={{"name","ISO-TP diagnostics"},{"dlc",frame.getData().size()},{"signals",Json::array()}};
        transmit(r.value("source","Diagnostic tester"),message,frame,"diagnostics");
        while(frames_.size()>200)frames_.erase(frames_.begin());
        return {{"recorded",true},{"dropped",frames_.back().at("dropped")}};
    }
    if(action=="run")running_=true;
    else if(action=="pause")running_=false;
    else if(action=="reset")reset();
    else if(action=="step")step(r.value("count",1U));
    else if(action=="advance") {if(running_)step(r.value("count",10U));}
    else if(action=="controls") {
        auto updated=controls_;
        for(auto &v:r.at("values").items()) {
            check(updated.count(v.key()),"Unknown control");double n=v.value();requireFinite(n);
            double low=v.key()=="ambient"?-40:(v.key()=="road_friction"?0.1:0);
            double high=v.key()=="ambient"?60:(v.key()=="target_speed"?160:(v.key()=="lead_speed"?50:1));
            check(n>=low&&n<=high,"Control out of range");updated[v.key()]=n;
        }controls_=std::move(updated);
    } else if(action=="fault") {
        auto name=r.at("sensor").get<std::string>(),mode=r.at("mode").get<std::string>();bool found=false;
        check(mode=="none"||mode=="stuck"||mode=="bias"||mode=="noisy"||mode=="disconnected","Unknown sensor fault");
        double value=r.value("value",120.0);requireFinite(value);check(value>=-10000&&value<=10000,"Fault value out of bounds");
        for(auto &s:sensors_)if(s.at("name")==name) {s["fault"]=mode;s["fault_value"]=value;found=true;}
        check(found,"Unknown sensor");
    } else if(action=="validate") {validateEcu(r.at("definition"),stepMs_);return {{"valid",true}};}
    else if(action=="add_ecu") {
        auto d=r.at("definition");validateEcu(d,stepMs_);
        // Imported ECUs can use known measurements/control inputs or declared CAN inputs.
        for(auto &input:d.at("inputs")) {
            bool found=allowedInputs.count(input.get<std::string>())||d.value("freshness_inputs",Json::object()).contains(input.get<std::string>());
            for(auto &m:d.at("messages"))if(m["direction"]=="rx")for(auto &s:m["signals"])if(s.at("name")==input)found=true;
            check(found,"Unknown imported ECU input");
        }
        for(auto &rx:d.at("messages"))if(rx["direction"]=="rx") {
            bool found=false;
            for(auto &ecu:ecus_)for(auto &tx:ecu.definition.at("messages"))if(tx["direction"]=="tx"&&tx["id"]==rx["id"]&&(tx.value("bus","powertrain")==rx.value("bus","powertrain")||std::any_of(scenario_["gateway_routes"].begin(),scenario_["gateway_routes"].end(),[&](auto &route){return route["id"]==tx["id"]&&route["from"]==tx.value("bus","powertrain")&&route["to"]==rx.value("bus","powertrain");}))) {
                check(tx.value("dlc",8)==rx.value("dlc",8)&&tx["signals"]==rx["signals"],"RX/TX layout mismatch");found=true;
            }check(found,"CAN RX has no transmitter");
        }
        check(ecus_.size()<32,"ECU limit reached");loadEcu(d);
    } else if(action=="diagnostic")return diagnostic(r);
    else if(action=="isotp_tester_start"||action=="isotp_tester_send"||action=="isotp_tester_receive"||
            action=="isotp_tester_poll"||action=="isotp_tester_end")return isotpTester(r);
    else if(action=="export_project")return exportProject();
    else if(action=="import_project")return importProject(r.at("project"));
    else if(action=="hash_project") {
        check(r.contains("project")&&r.at("project").is_object(),"Project required");
        return {{"project_hash","sha256:"+sha256Hex(projectContentOf(r.at("project")).dump())}};
    }
    else throw std::invalid_argument("Unknown action: "+action);
    return snapshot();
}

// Functional hardware profile: budgets are modeled; Docker CPU/RAM limits are separate.
EcuKernel::EcuKernel(Json definition,unsigned stepMs):definition_(std::move(definition)),stepMs_(stepMs) {
    SimulationManager::validateEcu(definition_,stepMs_);reset(1);
}
void EcuKernel::reset(uint64_t epoch) {
    epoch_=epoch;nextTick_=lastRxTick_=0;cached_=nullptr;values_.clear();active_.clear();stored_.clear();rxTicks_.clear();
    sessions_.clear();responders_.clear();
    state_=definition_.at("state_machine").at("initial");
    for(auto &input:definition_.at("inputs"))values_[input.get<std::string>()]=0;
    for(auto &v:definition_.at("variables").items())values_[v.key()]=v.value().is_boolean()?(v.value().get<bool>()?1:0):v.value().get<double>();
    auto profile=definition_.value("hardware",Json::object());
    double mhz=profile.value("cpu_mhz",120.0);requireFinite(mhz);check(mhz>=1&&mhz<=2000,"Invalid virtual CPU clock");
    unsigned ram=profile.value("ram_kib",128U),flash=profile.value("flash_kib",512U);
    check(ram>=1&&ram<=1048576&&flash>=1&&flash<=1048576,"Invalid virtual RAM/flash capacity");
    double budget=profile.value("task_budget_us",stepMs_*800.0);requireFinite(budget);check(budget>0&&budget<=stepMs_*1000.0,"Invalid task budget");
    hardware_={{"cpu_mhz",mhz},{"ram_capacity_bytes",uint64_t(ram)*1024},{"flash_capacity_bytes",uint64_t(flash)*1024},
        {"task_budget_us",budget},{"last_cpu_us",0},{"cpu_load_percent",0},{"ram_used_bytes",values_.size()*32+4096},
        {"flash_used_bytes",definition_.dump().size()},{"rx_age_ms",0},{"watchdog","HEALTHY"},{"can_controller",profile.value("can_controller",Json({{"fd_capable",true},{"rx_fifo_frames",128}}))},
        {"adc",profile.value("adc",Json({{"resolution_bits",12},{"channels",Json::object()}}))},{"gpio_outputs",profile.value("gpio_outputs",Json::array())}};
    const auto &adc=hardware_.at("adc");unsigned bits=adc.value("resolution_bits",12U);check(bits>=1&&bits<=24,"Invalid ADC resolution");
    auto channels=adc.value("channels",Json::object());check(channels.is_object(),"ADC channels must be an object");
    for(auto &channel:channels.items()) {
        check(values_.count(channel.key()),"ADC channel needs a declared input");
        check(channel.value().is_array()&&channel.value().size()==2,"ADC range needs two bounds");
        double low=channel.value()[0],high=channel.value()[1];requireFinite(low);requireFinite(high);check(high>low,"Invalid ADC range");
    }
    for(auto &gpio:hardware_.at("gpio_outputs"))check(gpio.is_string()&&definition_.at("variables").contains(gpio.get<std::string>()),"GPIO output needs a declared variable");
    auto can=hardware_.at("can_controller");unsigned fifo=can.value("rx_fifo_frames",128U);check(fifo>=1&&fifo<=8192,"Invalid CAN FIFO capacity");
    for(auto &message:definition_.at("messages"))check(!message.value("fd",false)||can.value("fd_capable",true),"ECU CAN controller lacks FD support");
    check(hardware_.at("flash_used_bytes").get<uint64_t>()<=uint64_t(flash)*1024,"Definition exceeds virtual flash");
    check(hardware_.at("ram_used_bytes").get<uint64_t>()<=uint64_t(ram)*1024,"State exceeds virtual RAM");
}
Json EcuKernel::snapshot() const {
    Json dtcs=Json::array();
    for(auto &d:definition_.value("dtcs",Json::array())) {
        auto code=d.at("code").get<std::string>();auto it=stored_.find(code);
        if(it!=stored_.end()&&it->second) {
            auto copy=d;copy.erase("trigger");copy.erase("recover");
            auto active=active_.find(code);copy["active"]=active!=active_.end()&&active->second;dtcs.push_back(copy);
        }
    }
    return {{"name",definition_.at("name")},{"state",state_},{"values",values_},{"dtcs",dtcs},{"hardware",hardware_},
        {"dtc_active",active_},{"dtc_stored",stored_},{"next_tick",nextTick_},{"epoch",epoch_}};
}
Json EcuKernel::tick(const Json &request) {
    uint64_t tick=request.at("tick"),epoch=request.at("epoch");
    check(epoch==epoch_,"ECU epoch mismatch: reset required");
    if(tick<nextTick_) {check(!cached_.is_null()&&cached_.at("tick")==tick,"Stale ECU tick");return cached_;}
    check(tick==nextTick_,"ECU missed a tick: reset required");
    double cost=0;
    for(auto &task:definition_.at("tasks"))
        if((tick*stepMs_)%task.at("period_ms").get<unsigned>()==0) {
            double cycles=task.value("cost_us",100.0);requireFinite(cycles);check(cycles>0&&cycles<=1000000,"Invalid task cost");
            cost+=cycles*120.0/hardware_.at("cpu_mhz").get<double>();
        }
    if(cost>hardware_.at("task_budget_us").get<double>()) {hardware_["watchdog"]="TRIPPED";throw std::invalid_argument("Virtual CPU budget overrun");}
    auto controller=hardware_.at("can_controller");
    unsigned fifo=controller.value("rx_fifo_frames",128U);check(fifo>=1&&fifo<=8192,"Invalid virtual RX FIFO capacity");
    if(request.at("frames").size()>fifo) {hardware_["watchdog"]="CAN_RX_OVERRUN";throw std::invalid_argument("Virtual CAN RX FIFO overflow");}
    auto adc=hardware_.at("adc");unsigned adcBits=adc.value("resolution_bits",12U);check(adcBits>=1&&adcBits<=24,"Invalid ADC resolution");
    auto channels=adc.value("channels",Json::object());check(channels.is_object(),"ADC channels must be an object");
    auto previous=state_;
    for(auto &input:definition_.at("inputs")) {
        auto name=input.get<std::string>();if(request.at("inputs").contains(name)) {
            double value=request.at("inputs").at(name);requireFinite(value);
            if(channels.contains(name)) {
                auto range=channels.at(name);check(range.is_array()&&range.size()==2,"ADC channel range requires two bounds");
                double low=range[0],high=range[1];requireFinite(low);requireFinite(high);check(high>low,"Invalid ADC channel range");
                double levels=std::pow(2,adcBits)-1;
                value=low+std::llround((std::clamp(value,low,high)-low)/(high-low)*levels)/levels*(high-low);
            }
            values_[name]=value;
        }
    }
    for(auto &item:request.at("frames")) {
        auto frame=frameFrom(item);check(!frame.isFd()||controller.value("fd_capable",true),"Virtual CAN controller is not FD capable");
        for(auto &message:definition_.at("messages"))
            if(message.at("direction")=="rx"&&message.at("id")==frame.getId()&&message.value("bus","powertrain")==item.value("bus","powertrain")) {
                check(message.value("fd",false)==frame.isFd()&&message.value("extended",false)==frame.isExtendedFrame(),"RX protocol mismatch");
                decode(message,frame,values_);lastRxTick_=tick;rxTicks_[message.value("bus","powertrain")+":"+std::to_string(frame.getId())]=tick;
            }
    }
    auto freshness=definition_.value("freshness_inputs",Json::object());
    for(auto &binding:freshness.items()) {
        auto key=binding.value().value("bus","powertrain")+":"+std::to_string(binding.value().at("id").get<unsigned>());
        auto seen=rxTicks_.find(key);values_.at(binding.key())=(seen==rxTicks_.end()?tick+1:tick-seen->second)*stepMs_;
    }
    for(auto &task:definition_.at("tasks")) {
        unsigned period=task.at("period_ms");if((tick*stepMs_)%period)continue;
        auto updated=values_;
        for(auto &a:task.at("assign").items())updated[a.key()]=expression(a.value(),values_);
        values_=std::move(updated);
    }
    for(auto &transition:definition_.at("state_machine").at("transitions"))
        if(transition.at("from")==state_&&expression(transition.at("when"),values_)) {state_=transition.at("to");break;}
    for(auto &dtc:definition_.value("dtcs",Json::array())) {
        auto code=dtc.at("code").get<std::string>();
        if(expression(dtc.at("trigger"),values_)) {active_[code]=true;stored_[code]=true;}
        else if(expression(dtc.at("recover"),values_))active_[code]=false;
    }
    hardware_["last_cpu_us"]=cost;hardware_["cpu_load_percent"]=cost/(stepMs_*1000.0)*100;
    hardware_["rx_age_ms"]=(tick-lastRxTick_)*stepMs_;
    Json frames=Json::array();
    for(auto &m:definition_.at("messages"))
        if(m.at("direction")=="tx"&&(tick*stepMs_)%m.at("period_ms").get<unsigned>()==0)
            frames.push_back(wire(encode(m,values_),m.value("bus","powertrain")));
    auto result=snapshot();result["tick"]=tick;result["epoch"]=epoch;result["tx_frames"]=frames;result["previous_state"]=previous;
    // Timing envelope: inputs are sampled at tick start; completion adds the modeled task cost of this tick.
    result["timing"]={{"sampled_at_us",tick*static_cast<uint64_t>(stepMs_)*1000ULL},
        {"computed_at_us",tick*static_cast<uint64_t>(stepMs_)*1000ULL+static_cast<uint64_t>(std::ceil(cost))},
        {"task_cost_us",cost},{"task_budget_us",hardware_.at("task_budget_us")},
        {"deadline_miss",false}};
    nextTick_++;cached_=result;return result;
}
Json EcuKernel::isotp(const Json &request) {
    auto action=request.value("action","");
    auto session=request.at("session").get<std::string>();
    if(action=="isotp_start") {
        check(sessions_.size()<64,"ISO-TP session limit");
        sessions_[session]=std::make_unique<IsoTp>(isotpConfig(request));
        responders_[session]=request.value("respond",false);
        return {{"ready",true}};
    }
    if(action=="isotp_end") {sessions_.erase(session);responders_.erase(session);return {{"closed",true}};}
    auto it=sessions_.find(session);
    check(it!=sessions_.end(),"Unknown ISO-TP session");
    auto &transport=*it->second;
    auto now=isotpNow(request,clockMs());
    if(action=="isotp_send") {
        auto payload=request.at("bytes").get<std::vector<uint8_t>>();
        check(!payload.empty()&&payload.size()<=4095,"Invalid ISO-TP payload");
        Json frames=Json::array();
        for(auto &frame:transport.startSend(payload,now))frames.push_back(isotpFrameJson(frame));
        return {{"frames",frames}};
    }
    if(action=="isotp_frame") {
        auto frame=isotpFrameFrom(request.at("frame"));
        Json frames=Json::array();
        for(auto &out:transport.onFrame(frame,now))frames.push_back(isotpFrameJson(out));
        Json message=nullptr;bool done=false;
        if(transport.hasMessage()) {
            auto bytes=transport.takeMessage();
            message=bytes;done=true;
            if(responders_[session]) {
                auto response=uds({{"bytes",bytes}}).at("bytes").get<std::vector<uint8_t>>();
                for(auto &out:transport.startSend(response,now))frames.push_back(isotpFrameJson(out));
            }
        }
        return {{"frames",frames},{"message",message},{"done",done}};
    }
    if(action=="isotp_poll") {transport.poll(now);return {{"ok",true}};}
    throw std::invalid_argument("Unknown ISO-TP action");
}
Json EcuKernel::uds(const Json &request) {
    check(request.at("bytes").is_array()&&!request.at("bytes").empty(),"UDS bytes required");
    for(auto &v:request.at("bytes"))check(v.is_number_integer()&&v.get<int64_t>()>=0&&v.get<uint64_t>()<=255,"UDS byte outside 0..255");
    auto req=request.at("bytes").get<std::vector<uint8_t>>();std::vector<uint8_t> response;
    uint8_t sid=req[0];auto negative=[&](uint8_t nrc){response={0x7f,sid,nrc};};
    if(sid==0x10||sid==0x3e) {
        if(req.size()!=2)negative(0x13);
        else if((sid==0x10&&(req[1]<1||req[1]>3))||(sid==0x3e&&req[1]!=0))negative(0x12);
        else {response={static_cast<uint8_t>(sid+0x40),req[1]};if(sid==0x10)response.insert(response.end(),{0,50,1,244});}
    } else if(sid==0x22) {
        if(req.size()!=3)negative(0x13);
        else if(req[1]==0xf1&&req[2]==0x90) {response={0x62,0xf1,0x90};std::string vin="SIMULATOR00000001";response.insert(response.end(),vin.begin(),vin.end());}
        else if(req[1]==0xf2&&req[2]==0) {
            double speed=values_.count("vehicle_speed")?values_.at("vehicle_speed"):(values_.count("speed_sensor")?values_.at("speed_sensor")*3.6:0);
            unsigned raw=static_cast<unsigned>(std::llround(std::clamp(speed,0.0,655.35)*100));
            response={0x62,0xf2,0,static_cast<uint8_t>(raw>>8),static_cast<uint8_t>(raw)};
        } else negative(0x31);
    } else if(sid==0x19) {
        if(req.size()!=3)negative(0x13);else if(req[1]!=2)negative(0x12);
        else {response={0x59,2,9};
            for(auto &d:definition_.value("dtcs",Json::array())) {
                auto code=d.at("code").get<std::string>();uint8_t status=(stored_[code]?8:0)|(active_[code]?1:0);
                if(status&req[2]) {uint32_t v=d.at("uds_code");response.insert(response.end(),{static_cast<uint8_t>(v>>16),static_cast<uint8_t>(v>>8),static_cast<uint8_t>(v),status});}
            }
        }
    } else if(sid==0x14) {
        if(req.size()!=4)negative(0x13);
        else if(req[1]!=255||req[2]!=255||req[3]!=255)negative(0x31);
        else {stored_.clear();active_.clear();response={0x54};}
    } else negative(0x11);
    return {{"bytes",response},{"snapshot",snapshot()}};
}
Json EcuKernel::command(const Json &request) {
    auto action=request.value("action","snapshot");
    if(action=="tick")return tick(request);
    if(action=="reset") {reset(request.value("epoch",epoch_+1));nextTick_=request.value("start_tick",0ULL);return snapshot();}
    if(action=="uds")return uds(request);
    if(action=="snapshot")return snapshot();
    if(action=="isotp_start"||action=="isotp_send"||action=="isotp_frame"||action=="isotp_end"||action=="isotp_poll")
        return isotp(request);
    throw std::invalid_argument("Unknown ECU kernel command");
}
// Envelope records follow resources/schemas/event-envelope.v2.json; retention matches the frame ring (200).
// Actuations get their own ring: acquisition/delivery events would otherwise evict the rare but decisive ones.
void SimulationManager::pushEnvelope(const Json &envelope) {
    envelopes_.push_back(envelope);
    while(envelopes_.size()>200)envelopes_.erase(envelopes_.begin());
}
void SimulationManager::pushActuation(const Json &envelope) {
    actuations_.push_back(envelope);
    while(actuations_.size()>200)actuations_.erase(actuations_.begin());
}
void SimulationManager::transmit(const std::string &source,const Json &message,const Can &frame,const std::string &bus,bool gateway) {
    double latency=0;bool drop=false;
    for(auto &rule:policies_) {
        if(rule.contains("source")&&rule.at("source")!=source)continue;
        if(rule.contains("bus")&&rule.at("bus")!=bus)continue;
        if(rule.contains("id")&&rule.at("id")!=frame.getId())continue;
        drop=drop||rule.value("drop",false);latency+=rule.value("latency_ms",0.0)*1000;
    }
    double nominal=500000,data=2000000;
    bool capable=bus=="diagnostics";
    for(auto &b:scenario_.value("buses",Json::array()))if(b.at("id")==bus) {
        nominal=b.value("bitrate",500000.0);data=b.value("data_bitrate",nominal);capable=b.value("fd",false);
    }
    if(frame.isFd()&&!capable)drop=true;
    // Envelope B rule: a frame may not reach the wire before its producer finished computing it.
    Json producerTiming=nullptr;
    for(auto &ecu:ecus_)if(ecu.definition.at("name")==source&&ecu.timing.contains("computed_at_us"))producerTiming=ecu.timing.at("computed_at_us");
    // Estimated serialization with a conservative 20% stuffing allowance; not bit-exact CRC modeling.
    double arbitrationBits=frame.isExtendedFrame()?67:47;
    double payloadBits=frame.getData().size()*8+(frame.isFd()?(frame.getData().size()>16?21:17):0);
    double durationUs=1e6*1.2*(arbitrationBits/nominal+payloadBits/((frame.isFd()&&frame.bitrateSwitch())?data:nominal));
    double startUs=std::max(std::max(tick_*stepMs_*1000.0,busFreeUs_[bus]),
        producerTiming.is_number()?producerTiming.get<double>():0.0),finishUs=startUs+durationUs;
    if(!drop) {busFreeUs_[bus]=finishUs;busBusyUs_[bus]+=durationUs;}
    else dropped_++;
    // Performance counters: frames per bus and delivery latency per producer.
    auto &busMetrics=busMetrics_[bus];
    busMetrics.frames++;
    bool isEcu=false;for(auto &ecu:ecus_)if(ecu.definition.at("name")==source)isEcu=true;
    if(isEcu) {
        auto &metrics=metrics_[source];
        metrics.tx++;
        if(drop)metrics.dropped++;
        if(!drop&&producerTiming.is_number()) {
                double deliveryUs=finishUs+latency-producerTiming.get<double>();
            if(deliveryUs>=0) {
                metrics.delivery.push_back(deliveryUs);
                metrics.deliveryTimeline.push_back({static_cast<uint64_t>(finishUs+latency),deliveryUs});
                if(metrics.delivery.size()>600)metrics.delivery.erase(metrics.delivery.begin());
                if(metrics.deliveryTimeline.size()>300)metrics.deliveryTimeline.erase(metrics.deliveryTimeline.begin());
            }
        }
    }
    if(drop)busMetrics.dropped++;
    frameCount_++;frameBits_+=static_cast<uint64_t>(arbitrationBits+payloadBits);
    uint64_t seq=frameSeq_++;
    std::map<std::string,double> decoded;if(!message.at("signals").empty())decode(message,frame,decoded);
    frames_.push_back({{"time",startUs/1e6},{"seq",seq},{"id",frame.getId()},{"ecu",source},{"name",message.at("name")},
        {"dlc",frame.getData().size()},{"wire_dlc",frame.getData().size()<=8?frame.getData().size():frame.getData().size()==12?9:frame.getData().size()==16?10:frame.getData().size()==20?11:frame.getData().size()==24?12:frame.getData().size()==32?13:frame.getData().size()==48?14:15},
        {"bytes",hexBytes(frame.getData())},{"signals",decoded},{"bus",bus},{"fd",frame.isFd()},{"brs",frame.bitrateSwitch()},
        {"extended",frame.isExtendedFrame()},{"duration_us",durationUs},{"gateway",gateway},{"dropped",drop},
        {"computed_at_us",producerTiming},{"delivered_at_us",nullptr}});
    if(drop)pushEnvelope({{"contract_version",2},{"kind","can_frame"},{"epoch",epoch_},{"tick",tick_},
        {"sim_time_us",static_cast<uint64_t>(startUs)},
        {"source",{{"ecu",source},{"message",message.at("name")},{"can_id",frame.getId()},{"bus",bus}}},
        {"name",message.at("name")},{"validity","invalid"},{"quality_reason","dropped_frame"},
        {"timestamps",{{"computed_at_us",producerTiming}}}});
    if(!drop&&bus!="diagnostics")scheduled_.push_back({{"source",source},{"arrival_us",finishUs+latency},{"frame",wire(frame,bus)},
        {"seq",seq},{"name",message.at("name")}});
}
