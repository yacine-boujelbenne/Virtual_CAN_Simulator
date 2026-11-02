#include "HealthView.hpp"
#include <QPainter>
#include <algorithm>
#include <cmath>
HealthView::HealthView(QWidget*p):QWidget(p){setObjectName("vehicleHealth");setMinimumSize(290,230);setMaximumWidth(380);}
void HealthView::setSnapshot(const nlohmann::json&s){state_=s;int faults=0,dtcs=0;for(const auto&a:s.at("sensors"))faults+=a.value("fault",std::string("none"))!="none";for(const auto&e:s.at("ecus"))for(const auto&d:e.at("dtcs"))dtcs+=d.value("active",false);const auto&w=s.at("world");QStringList conditions;if(w.value("collision",0.0)>0)conditions<<"COLLISION";else if(w.value("collision_events",0.0)>0)conditions<<"CONTACT RECORDED";if(w.value("soc",100.0)<15)conditions<<"LOW BATTERY";if(w.value("motor_temperature",0.0)>=100)conditions<<"MOTOR HOT";if(w.value("battery_temperature",0.0)>=50)conditions<<"BATTERY HOT";if(w.value("tire_utilization",0.0)>.9)conditions<<"GRIP LIMIT";if(faults)conditions<<QString("%1 sensor faults").arg(faults);if(dtcs)conditions<<QString("%1 active DTCs").arg(dtcs);if(s.at("cluster").at("connected")!=s.at("cluster").at("required"))conditions<<"WORKER FAULT";summary_=conditions.empty()?"HEALTHY · live measurements":conditions.join(" · ");setToolTip(summary_);setAccessibleDescription(summary_);update();}
QString HealthView::summary()const{return summary_;}
void HealthView::paintEvent(QPaintEvent*){
 QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),QColor("#122333"));p.setPen(QColor("#cce4ed"));p.drawText(12,22,"LIVE VEHICLE HEALTH / STRESS");if(state_.empty())return;auto&w=state_.at("world");
 auto color=[](double v,double warning,double severe){return QColor(v>=severe?"#f36a71":v>=warning?"#efbd5b":"#4dd8b8");};
 const double x=width()*.5;const double bodyY=40;
 p.setPen(QPen(w.value("collision",0.0)>0?QColor("#ff5263"):QColor("#5d899a"),2));p.setBrush(QColor("#203e51"));p.drawRoundedRect(QRectF(x-46,bodyY,92,138),19,19);
 for(int sign:{-1,1})for(int axle:{0,1}){p.setBrush(color(w.value("tire_utilization",0.0),.75,.95));p.drawRoundedRect(QRectF(x+sign*51-7,bodyY+15+axle*83,14,31),4,4);}
 p.setPen(Qt::NoPen);p.setBrush(color(w.value("motor_temperature",30.0),80,100));p.drawRoundedRect(QRectF(x-32,bodyY+9,64,25),4,4);
 p.setPen(QColor("#07222a"));p.drawText(QRectF(x-32,bodyY+9,64,25),Qt::AlignCenter,QString("%1 °C").arg(w.value("motor_temperature",0.0),0,'f',0));
 p.setBrush(color(w.value("battery_temperature",28.0),42,50));p.drawRoundedRect(QRectF(x-32,bodyY+52,64,51),5,5);p.drawText(QRectF(x-32,bodyY+52,64,51),Qt::AlignCenter,QString("SOC %1%\n%2 °C").arg(w.value("soc",0.0),0,'f',0).arg(w.value("battery_temperature",0.0),0,'f',0));
 p.setPen(QColor("#9dbcca"));p.drawText(8,76,"Motor");p.drawText(8,112,"Battery");p.drawText(8,156,"Tires");p.drawText(QRectF(width()-85,45,80,120),Qt::AlignLeft|Qt::TextWordWrap,QString("Grip\n%1%\n\nBrake\n%2%").arg(w.value("tire_utilization",0.0)*100,0,'f',0).arg(w.value("brake_pressure",0.0)*100,0,'f',0));
 p.setPen(summary_.startsWith("HEALTHY")?QColor("#4dd8b8"):QColor("#ffbb66"));p.drawText(QRectF(10,185,width()-20,height()-185),Qt::TextWordWrap,summary_);
}
