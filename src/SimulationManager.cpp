#include "SimulationManager.hpp"
#include "CanTp.hpp"
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
    uint64_t payload=0;
    for(const auto &signal:message.at("signals")) {
        double value=values.at(signal.at("name").get<std::string>());
        double low=signal.at("min"), high=signal.at("max");
        // Output saturation is explicit in the engineering model.
        double raw=(std::clamp(value,low,high)-signal.value("offset",0.0))/signal.value("factor",1.0);
        auto integer=static_cast<int64_t>(std::llround(raw)); unsigned length=signal.at("length"),start=signal.at("start");
        payload|=(static_cast<uint64_t>(integer)&mask(length))<<start;
    }
    std::vector<uint8_t> bytes(message.value("dlc",8),0);
    for(size_t i=0;i<bytes.size();++i) bytes[i]=static_cast<uint8_t>(payload>>(i*8));
    Can frame(message.at("id"),bytes);frame.setFrameType(message.value("extended",false));return frame;
}
void decode(const Json &message,const Can &frame,std::map<std::string,double> &values) {
    check(frame.getData().size()==message.value("dlc",size_t(8)),"CAN DLC does not match definition");
    uint64_t payload=0;
    for(size_t i=0;i<frame.getData().size();++i) payload|=uint64_t(frame.getData()[i])<<(i*8);
    for(const auto &s:message.at("signals")) {
        unsigned length=s.at("length"),start=s.at("start"); uint64_t raw=(payload>>start)&mask(length);
        int64_t integer=static_cast<int64_t>(raw);
        if(s.value("signed",false) && (raw&(uint64_t(1)<<(length-1)))) integer-=int64_t(1)<<length;
        values[s.at("name").get<std::string>()]=integer*s.value("factor",1.0)+s.value("offset",0.0);
    }
}
std::string hexBytes(const std::vector<uint8_t> &bytes) {
    std::ostringstream out; out<<std::hex;
    for(auto b:bytes) {if(out.tellp()>0)out<<' ';out.width(2);out.fill('0');out<<unsigned(b);}return out.str();
}
const std::set<std::string> sensorSources={"speed","rpm","motor_temperature","battery_temperature","soc","voltage","current","wheel_fl","wheel_fr","wheel_rl","wheel_rr"};
const std::set<std::string> allowedInputs={"throttle","brake","ignition","ambient","road_friction","speed_sensor","rpm_sensor","temperature_sensor","battery_temperature_sensor","soc_sensor","voltage_sensor","current_sensor","wheel_fl_sensor","wheel_fr_sensor","wheel_rl_sensor","wheel_rr_sensor"};
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
        if(message.contains("dlc")) check(message.at("dlc").is_number_integer() && message.at("dlc").get<uint64_t>()>=1 && message.at("dlc").get<uint64_t>()<=8,"Invalid CAN DLC");
        unsigned dlc=message.value("dlc",8);check(dlc>=1&&dlc<=8,"Classic CAN DLC must be 1..8");
        auto direction=message.at("direction").get<std::string>();check(direction=="tx"||direction=="rx","Invalid CAN direction");
        if(direction=="tx") {check(message.at("period_ms").is_number_unsigned(),"Invalid message period"); unsigned p=message.at("period_ms");check(p>=stepMs&&p<=60000&&p%stepMs==0,"Message period must be a multiple of simulation step");}
        check(message.at("signals").is_array() && !message.at("signals").empty(),"Message signals required");
        uint64_t occupied=0; std::set<std::string> signalNames;
        for(auto &s:message.at("signals")) {
            auto name=s.at("name").get<std::string>();check(names.count(name)&&signalNames.insert(name).second,"Unknown/duplicate CAN signal");
            check(s.at("start").is_number_unsigned()&&s.at("length").is_number_unsigned(),"Unsigned signal start/length required");
            check(s.at("start").get<uint64_t>()<=63 && s.at("length").get<uint64_t>()<=32,"Signal exceeds frame");
            unsigned start=s.at("start"),length=s.at("length");check(length>=1&&length<=32&&start+length<=dlc*8,"Signal exceeds frame");
            check(s.value("byte_order","little_endian")=="little_endian","Only little_endian signals supported");
            uint64_t bits=mask(length)<<start; check(!(occupied&bits),"Overlapping CAN signals");occupied|=bits;
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
SimulationManager::SimulationManager(const std::string &path):scenarioPath_(path) {
    static std::atomic<unsigned> next{0};
    channel_="simulation-"+std::to_string(next++);
    reset();
}
void SimulationManager::loadEcu(const Json &definition) {
    validateEcu(definition,stepMs_);
    for(auto &ecu:ecus_) {
        check(ecu.definition.at("name")!=definition.at("name"),"Duplicate ECU name");
        for(auto &a:ecu.definition.at("messages"))for(auto &b:definition.at("messages"))
            if(a["direction"]=="tx" && b["direction"]=="tx") check(a["id"]!=b["id"],"CAN TX ID collision");
        for(auto &b:definition.at("bindings").items())
            check(!ecu.definition.value("bindings",Json::object()).contains(b.key()),"Actuator already has an owner");
    }
    Ecu ecu;ecu.definition=definition;ecu.state=definition.at("state_machine")["initial"];
    for(auto &input:definition.at("inputs")) ecu.values[input.get<std::string>()]=0;
    for(auto &v:definition.at("variables").items())ecu.values[v.key()]=v.value().is_boolean()?(v.value().get<bool>()?1:0):v.value().get<double>();
    ecu.bus=std::make_unique<CanBus>(CanBus::Backend::Internal,channel_);ecu.bus->init();
    ecus_.push_back(std::move(ecu));
}
void SimulationManager::reset() {
    scenario_=readJson(scenarioPath_);stepMs_=scenario_.at("step_ms");
    check(stepMs_>=1&&stepMs_<=100,"Simulation step must be 1..100 ms");
    check(scenario_.value("bitrate",500000)>=10000 && scenario_.value("bitrate",500000)<=1000000,"Invalid CAN bitrate");
    ecus_.clear();monitor_=std::make_unique<CanBus>(CanBus::Backend::Internal,channel_);monitor_->init();
    tick_=frameCount_=frameBits_=0;running_=false;random_.seed(42);
    frames_=Json::array();events_=Json::array();
    controls_={{"throttle",0},{"brake",0},{"ignition",1},{"ambient",25},{"road_friction",0.8}};
    world_={{"speed",0},{"acceleration",0},{"position",0},{"rpm",0},{"motor_temperature",30},{"battery_temperature",28},
        {"soc",85},{"voltage",390},{"current",0},{"wheel_fl",0},{"wheel_fr",0},{"wheel_rl",0},{"wheel_rr",0},
        {"torque_request",0},{"brake_pressure",0},{"fan",0},{"power_allowed",1}};
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
    for(auto &file:scenario_["ecus"]) loadEcu(readJson(directory/file.get<std::string>()));
    // Resolve cross-ECU dependencies and require matching wire layouts.
    for(auto &ecu:ecus_)for(auto &input:ecu.definition.at("inputs")) {
        auto name=input.get<std::string>();bool found=controls_.count(name)||sensorNames.count(name);
        for(auto &message:ecu.definition.at("messages"))if(message.at("direction")=="rx")
            for(auto &s:message.at("signals"))if(s.at("name")==name)found=true;
        check(found,"Unbound ECU input: "+name);
    }
    for(auto &ecu:ecus_)for(auto &rx:ecu.definition.at("messages"))if(rx["direction"]=="rx") {
        bool found=false;
        for(auto &sender:ecus_)for(auto &tx:sender.definition.at("messages"))if(tx["direction"]=="tx"&&tx["id"]==rx["id"]) {
            check(tx.value("dlc",8)==rx.value("dlc",8)&&tx["signals"]==rx["signals"],"RX/TX layout mismatch");
            found=true;
        }
        check(found,"CAN RX has no transmitter");
    }
}
void SimulationManager::tick() {
    double dt=stepMs_/1000.0;
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
    }
    for(auto &ecu:ecus_) {
        for(auto &input:ecu.definition.at("inputs")) {
            auto name=input.get<std::string>();
            if(controls_.count(name))ecu.values[name]=controls_.at(name);
            for(auto &s:sensors_)if(s.at("name")==name)ecu.values[name]=s["measured"].get<double>();
        }
        Can frame;while(ecu.bus->receive(frame,std::chrono::milliseconds(0)))
            for(auto &message:ecu.definition.at("messages"))
                if(message.at("direction")=="rx"&&message.at("id")==frame.getId())decode(message,frame,ecu.values);
        for(auto &task:ecu.definition.at("tasks")) {
            unsigned period=task.at("period_ms");if((tick_*stepMs_)%period)continue;
            // Assignments are simultaneous within each task; task order is explicit.
            auto updated=ecu.values;
            for(auto &a:task.at("assign").items())updated[a.key()]=expression(a.value(),ecu.values);
            ecu.values=std::move(updated);
        }
        for(auto &transition:ecu.definition.at("state_machine")["transitions"])
            if(transition.at("from")==ecu.state && expression(transition.at("when"),ecu.values)) {
                auto previous=ecu.state;ecu.state=transition.at("to").get<std::string>();
                events_.push_back({{"time",tick_*dt},{"ecu",ecu.definition.at("name")},{"from",previous},{"to",ecu.state}});break;
            }
        for(auto &dtc:ecu.definition.value("dtcs",Json::array())) {
            auto code=dtc.at("code").get<std::string>();
            if(expression(dtc.at("trigger"),ecu.values)) {ecu.dtcActive[code]=true;ecu.dtcStored[code]=true;}
            else if(expression(dtc.at("recover"),ecu.values))ecu.dtcActive[code]=false;
        }
        for(auto &binding:ecu.definition.at("bindings").items())world_[binding.key()]=ecu.values.at(binding.value().get<std::string>());
    }
    // CAN requests sharing this tick are delivered in numeric-ID priority order.
    struct Pending {Ecu *ecu;Json message;Can frame;};
    std::vector<Pending> pending;
    for(auto &ecu:ecus_)for(auto &message:ecu.definition.at("messages"))
        if(message.at("direction")=="tx" && (tick_*stepMs_)%message.at("period_ms").get<unsigned>()==0)
            pending.push_back({&ecu,message,encode(message,ecu.values)});
    std::stable_sort(pending.begin(),pending.end(),[](const auto &a,const auto &b){return a.frame.getId()<b.frame.getId();});
    for(auto &p:pending) {
        p.ecu->bus->send(p.frame);frameCount_++;frameBits_+=47+p.frame.getData().size()*8;
        std::map<std::string,double> decoded;decode(p.message,p.frame,decoded);
        frames_.push_back({{"time",tick_*dt},{"id",p.frame.getId()},{"ecu",p.ecu->definition.at("name")},{"name",p.message.at("name")},
            {"dlc",p.frame.getData().size()},{"bytes",hexBytes(p.frame.getData())},{"signals",decoded}});
    }
    Can observed;while(monitor_->receive(observed,std::chrono::milliseconds(0))) {}
    while(frames_.size()>200)frames_.erase(frames_.begin());
    while(events_.size()>80)events_.erase(events_.begin());
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
    tick_++;
}
void SimulationManager::step(unsigned count) {check(count>=1&&count<=1000,"Step count must be 1..1000");for(unsigned i=0;i<count;++i)tick();}
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
        data["ecus"].push_back({{"name",ecu.definition.at("name")},{"state",ecu.state},{"values",ecu.values},{"dtcs",dtcs},{"definition",ecu.definition}});
    }
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
        auto payload=ecuTp.receiveMessage();std::vector<uint8_t> req(payload.begin(),payload.end()),response;
        uint8_t sid=req[0];auto negative=[&](uint8_t nrc){response={0x7f,sid,nrc};};
        if(sid==0x10||sid==0x3e) {
            if(req.size()!=2)negative(0x13);
            else if((sid==0x10 && (req[1]<1||req[1]>3)) || (sid==0x3e&&req[1]!=0))negative(0x12);
            else {response={static_cast<uint8_t>(sid+0x40),req[1]};if(sid==0x10)response.insert(response.end(),{0,50,1,244});}
        } else if(sid==0x22) {
            if(req.size()!=3)negative(0x13);
            else if(req[1]==0xf1&&req[2]==0x90) {response={0x62,0xf1,0x90};std::string vin="SIMULATOR00000001";response.insert(response.end(),vin.begin(),vin.end());}
            else if(req[1]==0xf2&&req[2]==0x00) {auto speed=static_cast<unsigned>(std::llround(world_["speed"]*3.6*100));response={0x62,0xf2,0,static_cast<uint8_t>(speed>>8),static_cast<uint8_t>(speed)};}
            else negative(0x31);
        } else if(sid==0x19) {
            if(req.size()!=3)negative(0x13);else if(req[1]!=2)negative(0x12);
            else {
                response={0x59,2,0x09};
                for(auto &d:target->definition.value("dtcs",Json::array())) {
                    auto code=d.at("code").get<std::string>();uint8_t status=(target->dtcStored[code]?8:0)|(target->dtcActive[code]?1:0);
                    if(status&req[2]) {uint32_t v=d.at("uds_code");response.insert(response.end(),{static_cast<uint8_t>(v>>16),static_cast<uint8_t>(v>>8),static_cast<uint8_t>(v),status});}
                }
            }
        } else if(sid==0x14) {
            if(req.size()!=4)negative(0x13);
            else if(req[1]!=0xff||req[2]!=0xff||req[3]!=0xff)negative(0x31);
            else {target->dtcStored.clear();target->dtcActive.clear();response={0x54};}
        } else negative(0x11);
        ecuTp.sendMessage(std::string(response.begin(),response.end()));
    });
    tester.sendMessage(std::string(bytes.begin(),bytes.end()));auto payload=tester.receiveMessage();job.get();
    std::vector<uint8_t> result(payload.begin(),payload.end());
    return {{"request",hexBytes(bytes)},{"response",hexBytes(result)},{"bytes",result},{"transport","ISO-TP / internal CAN"},{"request_id",0x7e0},{"response_id",0x7e8}};
}
Json SimulationManager::command(const Json &r) {
    auto action=r.value("action","snapshot");
    if(action=="snapshot")return snapshot();
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
            double high=v.key()=="ambient"?60:1;
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
            bool found=allowedInputs.count(input.get<std::string>());
            for(auto &m:d.at("messages"))if(m["direction"]=="rx")for(auto &s:m["signals"])if(s.at("name")==input)found=true;
            check(found,"Unknown imported ECU input");
        }
        for(auto &rx:d.at("messages"))if(rx["direction"]=="rx") {
            bool found=false;
            for(auto &ecu:ecus_)for(auto &tx:ecu.definition.at("messages"))if(tx["direction"]=="tx"&&tx["id"]==rx["id"]) {
                check(tx.value("dlc",8)==rx.value("dlc",8)&&tx["signals"]==rx["signals"],"RX/TX layout mismatch");found=true;
            }check(found,"CAN RX has no transmitter");
        }
        check(ecus_.size()<32,"ECU limit reached");loadEcu(d);
    } else if(action=="diagnostic")return diagnostic(r);
    else throw std::invalid_argument("Unknown action: "+action);
    return snapshot();
}
