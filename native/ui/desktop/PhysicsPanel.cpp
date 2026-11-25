#include "PhysicsPanel.hpp"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
using Json=nlohmann::json;
PhysicsPanel::PhysicsPanel(QWidget*parent):QWidget(parent){
    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
    auto *live=new QGroupBox("ENVIRONMENT & ASSISTANCE");auto *form=new QFormLayout(live);
    auto number=[](QFormLayout*f,const QString&title,const std::string&name,double low,double high,double value,int decimals){auto*s=new QDoubleSpinBox;s->setObjectName(QString::fromStdString(name));s->setProperty("modelDefault",value);s->setRange(low,high);s->setDecimals(decimals);s->setValue(value);f->addRow(title,s);return s;};
    controls_["road_friction"]=number(form,"Tire grip μ","road_friction",.1,1,.8,2);
    controls_["road_grade_deg"]=number(form,"Slope ° (+ uphill)","road_grade_deg",-20,20,0,1);
    controls_["headwind_mps"]=number(form,"Wind m/s (+ headwind)","headwind_mps",-30,50,0,1);
    controls_["ambient"]=number(form,"Ambient °C","ambient",-40,60,25,1);
    controls_["lead_speed"]=number(form,"Lead vehicle m/s","lead_speed",0,50,12,1);
    controls_["target_speed"]=number(form,"Cruise target km/h","target_speed",0,160,80,1);
    for(const auto&pair:std::map<std::string,QString>{{"ignition","Ignition"},{"aeb_enabled","Automatic emergency braking"},{"assist_enabled","Adaptive cruise assistance"}}){auto*c=new QCheckBox(pair.second);c->setObjectName(QString::fromStdString(pair.first));switches_[pair.first]=c;form->addRow(c);}
    auto *apply=new QPushButton("Apply live environment");apply->setObjectName("applyEnvironment");form->addRow(apply);connect(apply,&QPushButton::clicked,this,[this]{if(!request||state_.empty())return;Json values=Json::object();for(const auto&[key,spin]:controls_)if(state_.at("controls").contains(key))values[key]=spin->value();for(const auto&[key,check]:switches_)values[key]=check->isChecked()?1:0;request({{"action","controls"},{"values",values}},[this](const Json&){liveDirty_=false;setSnapshot(state_);feedback_->setText("Environment applied to the running laboratory.");});});layout->addWidget(live);
    auto *vehicle=new QGroupBox("VEHICLE PHYSICS");auto *vf=new QFormLayout(vehicle);profile_=new QComboBox;profile_->setObjectName("physicsProfile");profile_->addItems({"Legacy bicycle","Grip-aware bicycle v2"});vf->addRow("Model",profile_);
    parameters_["mass_kg"]=number(vf,"Mass kg","mass_kg",500,5000,1500,0);
    parameters_["wheel_radius_m"]=number(vf,"Wheel radius m","wheel_radius_m",.15,.7,.32,2);
    parameters_["wheelbase_m"]=number(vf,"Wheelbase m","wheelbase_m",.5,10,2.7,2);
    parameters_["drag_area_m2"]=number(vf,"Drag area Cd·A m²","drag_area_m2",0,3,.7,2);
    parameters_["rolling_coefficient"]=number(vf,"Rolling coefficient","rolling_coefficient",0,.05,.012,3);
    parameters_["steer_rate_rad_s"]=number(vf,"Steering rate rad/s","steer_rate_rad_s",.05,3,.8,2);
    parameters_["max_steer_angle_rad"]=number(vf,"Steering limit rad","max_steer_angle_rad",.05,1.4,.6,2);
    parameters_["brake_force_n"]=number(vf,"Brake capacity N","brake_force_n",1000,30000,12000,0);
    parameters_["final_drive_ratio"]=number(vf,"Final drive ratio","final_drive_ratio",2,15,9,2);
    parameters_["motor_torque_limit_nm"]=number(vf,"Torque clamp Nm","motor_torque_limit_nm",20,320,320,0);
    auto *deploy=new QPushButton("Apply physics & restart");deploy->setObjectName("applyPhysics");vf->addRow(deploy);connect(deploy,&QPushButton::clicked,this,[this]{if(!request)return;feedback_->setText("Loading project for physics update…");request({{"action","export_project"}},[this](const Json&project){auto draft=project;if(!draft["scenario"].contains("plant")){feedback_->setText("Load the lateral scenario with EPS and transmission first.");return;}auto&plant=draft["scenario"]["plant"];plant["model"]=profile_->currentIndex()?"bicycle_grip_v2":"kinematic_bicycle_v1";for(const auto&[key,spin]:parameters_)if(profile_->currentIndex()||key=="wheelbase_m")plant[key]=spin->value();request({{"action","apply_project_draft"},{"project",draft}},[this](const Json&){feedback_->setText("Physics deployed. Experiment restarted at tick zero.");});});});layout->addWidget(vehicle);
    auto*stress=new QGroupBox("PHYSICAL STATE INJECTION");auto*sf=new QFormLayout(stress);
    auto*motor=number(sf,"Motor °C","stressMotor",-40,180,110,1);auto*battery=number(sf,"Battery °C","stressBattery",-40,100,55,1);auto*soc=number(sf,"Charge %","stressSoc",0,100,15,1);
    auto*inject=new QPushButton("Apply thermal / battery stress");inject->setObjectName("applyStateStress");sf->addRow(inject);connect(inject,&QPushButton::clicked,this,[this,motor,battery,soc]{if(request)request({{"action","environment"},{"values",{{"motor_temperature",motor->value()},{"battery_temperature",battery->value()},{"soc",soc->value()}}}},[this](const Json&){feedback_->setText("Physical state injected at a tick boundary; observe health and ECU responses.");});});layout->addWidget(stress);
    feedback_=new QLabel("Live controls apply without reset. Physics changes restart the project.");feedback_->setWordWrap(true);layout->addWidget(feedback_);metrics_=new QLabel;metrics_->setObjectName("physicsMeasurements");metrics_->setWordWrap(true);layout->addWidget(metrics_);
    for(const auto&[key,spin]:controls_)connect(spin,&QDoubleSpinBox::valueChanged,this,[this]{liveDirty_=true;feedback_->setText("Pending environment edits: press Apply live environment.");});
    for(const auto&[key,check]:switches_)connect(check,&QCheckBox::toggled,this,[this]{liveDirty_=true;feedback_->setText("Pending environment edits: press Apply live environment.");});
    connect(profile_,&QComboBox::currentIndexChanged,this,[this](int i){for(const auto&[key,spin]:parameters_)spin->setEnabled(i||key=="wheelbase_m");});
}
void PhysicsPanel::setSnapshot(const Json&s){
    state_=s;const auto epoch=s.value("epoch",uint64_t(0));const bool restarted=epoch!=epoch_;epoch_=epoch;if(restarted)liveDirty_=false;
    if(!liveDirty_){const auto&values=s.at("controls");for(const auto&[key,spin]:controls_){QSignalBlocker block(spin);spin->setEnabled(values.contains(key));if(values.contains(key))spin->setValue(values.at(key));}for(const auto&[key,check]:switches_){QSignalBlocker block(check);check->setChecked(values.value(key,0.0)!=0);}}
    if(s.contains("traffic")){controls_.at("lead_speed")->setEnabled(false);controls_.at("lead_speed")->setToolTip("Use Interactive Traffic to control individual actors.");}
    const auto cfg=s.value("physics",Json::object());if(cfg!=physicsConfig_||restarted){physicsConfig_=cfg;profile_->setCurrentIndex(cfg.value("model",std::string())=="bicycle_grip_v2"?1:0);for(const auto&[key,spin]:parameters_){spin->setValue(cfg.value(key,spin->property("modelDefault").toDouble()));spin->setEnabled(profile_->currentIndex()||key=="wheelbase_m");}}
    const auto&w=s.at("world");QString text=QString("Acceleration %1 m/s²\nYaw rate %2 rad/s\n").arg(w.value("acceleration",0.0),0,'f',2).arg(w.value("yaw_rate",0.0),0,'f',3);
    if(w.contains("lateral_accel_mps2"))text+=QString("Lateral acceleration %1 m/s²\nTire use %2 %\nDrive force %3 N\nBrake force %4 N\nAerodynamic drag %5 N\nSlope force %6 N\nKinetic energy %7 kJ\nBrake dissipation %8 kJ").arg(w.at("lateral_accel_mps2").get<double>(),0,'f',2).arg(w.at("tire_utilization").get<double>()*100,0,'f',1).arg(w.at("traction_force_n").get<double>(),0,'f',0).arg(w.at("braking_force_n").get<double>(),0,'f',0).arg(w.at("drag_force_n").get<double>(),0,'f',0).arg(w.at("grade_force_n").get<double>(),0,'f',0).arg(w.at("kinetic_energy_j").get<double>()/1000,0,'f',1).arg(w.at("brake_energy_j").get<double>()/1000,0,'f',1);
    metrics_->setText(text);
}
