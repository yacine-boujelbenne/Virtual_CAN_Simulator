#pragma once
#include "json.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <stdexcept>
#include <set>

namespace vclab {
// Deterministic kinematic actors, metres/radians/seconds. Ego remains external.
class TrafficWorld {
public:
    using Json=nlohmann::json;
    Json actors=Json::array();
    static Json validated(const Json&input){
        if(!input.is_array()||input.size()>32)throw std::invalid_argument("Traffic requires an array of at most 32 objects");
        Json result=Json::array();std::set<std::string> ids;
        for(const auto&a:input){
            auto id=a.at("id").get<std::string>();if(id.empty()||id.size()>64||!ids.insert(id).second)throw std::invalid_argument("Traffic IDs must be unique and nonempty");
            Json out={{"id",id}};
            for(const auto*key:{"x","y","yaw","speed","target_speed","length","width"}){
                double fallback=std::string(key)=="length"?4.5:std::string(key)=="width"?1.8:std::string(key)=="target_speed"?a.value("speed",0.0):0;
                double value=a.value(key,fallback);if(!std::isfinite(value))throw std::invalid_argument("Traffic values must be finite");
                if((std::string(key)=="speed"||std::string(key)=="target_speed")&&(value<0||value>60))throw std::invalid_argument("Traffic speed must be 0..60 m/s");
                if((std::string(key)=="length"||std::string(key)=="width")&&(value<.2||value>20))throw std::invalid_argument("Invalid actor dimensions");
                if(std::abs(value)>1e6)throw std::invalid_argument("Actor position outside world limits");
                out[key]=value;
            }
            result.push_back(out);
        }return result;
    }
    void step(double dt){for(auto&a:actors){double speed=a.at("speed"),target=a.at("target_speed");speed+=std::clamp(target-speed,-4*dt,2*dt);a["speed"]=speed;a["x"]=a.at("x").get<double>()+speed*std::cos(a.at("yaw").get<double>())*dt;a["y"]=a.at("y").get<double>()+speed*std::sin(a.at("yaw").get<double>())*dt;}}
    struct Observation { double gap=600,closing=0;bool collision=false;std::string lead; };
    Observation observe(double x,double y,double yaw,double speed)const{
        Observation out;const double c=std::cos(yaw),s=std::sin(yaw);
        for(const auto&a:actors){
            const double dx=a.at("x").get<double>()-x,dy=a.at("y").get<double>()-y,ayaw=a.at("yaw");
            const double along=dx*c+dy*s,side=-dx*s+dy*c,relative=ayaw-yaw;
            const double halfLength=a.at("length").get<double>()*.5,halfWidth=a.at("width").get<double>()*.5;
            const double projectedLength=std::abs(std::cos(relative))*halfLength+std::abs(std::sin(relative))*halfWidth;
            const double projectedWidth=std::abs(std::sin(relative))*halfLength+std::abs(std::cos(relative))*halfWidth;
            const double gap=std::max(0.0,along-2.25-projectedLength);
            if(along>=0&&std::abs(side)<=.9+projectedWidth&&gap<out.gap){out.gap=gap;out.closing=std::max(0.0,speed-a.at("speed").get<double>()*std::cos(relative));out.lead=a.at("id");}
            // Rectangle intersection SAT on both vehicle frames.
            const double actorAlong=dx*std::cos(ayaw)+dy*std::sin(ayaw),actorSide=-dx*std::sin(ayaw)+dy*std::cos(ayaw);
            const bool overlap=std::abs(along)<=2.25+projectedLength&&std::abs(side)<=.9+projectedWidth&&std::abs(actorAlong)<=halfLength+2.25*std::abs(std::cos(relative))+.9*std::abs(std::sin(relative))&&std::abs(actorSide)<=halfWidth+2.25*std::abs(std::sin(relative))+.9*std::abs(std::cos(relative));out.collision|=overlap;
        }return out;
    }
};
}
