#include "SceneView.hpp"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QCryptographicHash>
#include <algorithm>
#include <cmath>
#include <vector>
SceneView::SceneView(QWidget*p):QWidget(p){setMinimumSize(500,220);defaultMesh_=CarMesh::load(QString(VCLAB_RESOURCE_ROOT)+"/resources/models/default-car/sedan.obj");mesh_=defaultMesh_;setToolTip("Drag to orbit camera; wheel to zoom. Imported shape is visual; the vehicle plant owns physics.");}
void SceneView::setPresentation(const nlohmann::json&p){auto model=p.value("car_mesh",nlohmann::json());const auto key=QString::fromLatin1(QCryptographicHash::hash(QByteArray::fromStdString(model.dump()),QCryptographicHash::Sha256).toHex());if(key==assetKey_)return;auto staged=model.is_null()?defaultMesh_:CarMesh::fromJson(model);mesh_=std::move(staged);assetKey_=key;update();}
void SceneView::setSnapshot(const nlohmann::json&s){world_=s.at("world");wheelRadius_=s.value("physics",nlohmann::json::object()).value("wheel_radius_m",.32);traffic_=s.value("traffic",nlohmann::json::object()).value("actors",nlohmann::json::array());double time=s.at("time");auto epoch=s.value("epoch",uint64_t(0));if(epoch!=epoch_||time<lastTime_)trail_.clear();if(time!=lastTime_||epoch!=epoch_){trail_.append({world_.value("x",world_.value("position",0.0)),world_.value("y",0.0)});if(trail_.size()>1800)trail_.removeFirst();}lastTime_=time;epoch_=epoch;update();}
void SceneView::resetCamera(){orbit_=-.55;distance_=11;update();}
void SceneView::mousePressEvent(QMouseEvent*e){lastMouse_=e->pos();}
void SceneView::mouseMoveEvent(QMouseEvent*e){if(e->buttons()&Qt::LeftButton){orbit_+=(e->pos().x()-lastMouse_.x())*.008;lastMouse_=e->pos();update();}}
void SceneView::wheelEvent(QWheelEvent*e){distance_=std::clamp(distance_*(e->angleDelta().y()>0?.88:1/.88),5.0,45.0);update();e->accept();}
void SceneView::paintEvent(QPaintEvent*){
 QPainter p(this);p.setRenderHint(QPainter::Antialiasing);QLinearGradient sky(0,0,0,height());sky.setColorAt(0,QColor("#8caec2"));sky.setColorAt(.48,QColor("#c3d7d5"));sky.setColorAt(1,QColor("#284c45"));p.fillRect(rect(),sky);
 double x=world_.value("x",world_.value("position",0.0)),y=world_.value("y",0.0),heading=world_.value("heading",0.0),cameraYaw=heading+orbit_;
 QVector3D eye(x-distance_*std::cos(cameraYaw),y-distance_*std::sin(cameraYaw),distance_*.45+2),target(x+3*std::cos(heading),y+3*std::sin(heading),1);
 QVector3D forward=(target-eye).normalized(),right=QVector3D::crossProduct(forward,{0,0,1}).normalized(),up=QVector3D::crossProduct(right,forward).normalized();double focal=std::min(width(),height())*1.25;
 auto depth=[&](QVector3D v){return double(QVector3D::dotProduct(v-eye,forward));};
 auto project=[&](QVector3D v){auto d=v-eye;double z=std::max(.1,double(QVector3D::dotProduct(d,forward)));return QPointF(width()*.5+focal*QVector3D::dotProduct(d,right)/z,height()*.52-focal*QVector3D::dotProduct(d,up)/z);};
 struct Face{QPolygonF points;QColor color;double z;};std::vector<Face>faces;faces.reserve(mesh_.triangles.size()+traffic_.size()*defaultMesh_.triangles.size()+1000);
 auto polygon=[&](const std::vector<QVector3D>&vertices,QColor color){QPolygonF points;double z=0;for(const auto&v:vertices){if(depth(v)<.2)return;points<<project(v);z+=depth(v);}faces.push_back({points,color,z/vertices.size()});};
 auto flush=[&]{std::stable_sort(faces.begin(),faces.end(),[](const auto&a,const auto&b){return a.z>b.z;});p.setPen(Qt::NoPen);for(const auto&face:faces){p.setBrush(face.color);p.drawPolygon(face.points);}faces.clear();};
 // Metric ground and world-aligned road. The camera has no physics authority.
 for(int i=int(x/10)*10-100;i<int(x)+160;i+=10)for(int j=int(y/10)*10-80;j<int(y)+80;j+=10){bool alternate=((i/10+j/10)&1);polygon({{float(i),float(j),-.03f},{float(i+10),float(j),-.03f},{float(i+10),float(j+10),-.03f},{float(i),float(j+10),-.03f}},alternate?QColor("#627c57"):QColor("#68835c"));}
 flush();
 for(int i=int(x/5)*5-100;i<int(x)+180;i+=5){polygon({{float(i),-5.4f,0},{float(i+5),-5.4f,0},{float(i+5),5.4f,0},{float(i),5.4f,0}},QColor("#495761"));for(float lane:{-5.2f,5.2f})polygon({{float(i),lane,.01f},{float(i+5),lane,.01f},{float(i+5),lane+.07f,.01f},{float(i),lane+.07f,.01f}},QColor("#e0e6dc"));if((i/5)%2==0)polygon({{float(i),-.035f,.015f},{float(i+3),-.035f,.015f},{float(i+3),.035f,.015f},{float(i),.035f,.015f}},QColor("#dde4d5"));}
 flush();
 auto car=[&](const CarMesh&model,double px,double py,double yaw,bool ego,double travel){
  const double roll=ego?std::clamp(-world_.value("lateral_accel_mps2",0.0)*.012,-.09,.09):0,pitch=ego?std::clamp(world_.value("acceleration",0.0)*.012,-.07,.07):0;
  const double steer=ego?world_.value("steering_angle",0.0):0,spin=travel/(ego?wheelRadius_:.32);
  for(const auto&t:model.triangles){auto transform=[&](QVector3D v){if(t.wheel){auto d=v-t.pivot;double wx=std::cos(spin)*d.x()+std::sin(spin)*d.z(),wz=-std::sin(spin)*d.x()+std::cos(spin)*d.z();d.setX(wx);d.setZ(wz);if(t.front){double sx=std::cos(steer)*d.x()-std::sin(steer)*d.y();d.setY(std::sin(steer)*d.x()+std::cos(steer)*d.y());d.setX(sx);}v=t.pivot+d;}double vz=std::sin(roll)*v.y()+std::cos(roll)*v.z(),vy=std::cos(roll)*v.y()-std::sin(roll)*v.z();double vx=std::cos(pitch)*v.x()+std::sin(pitch)*vz;vz=-std::sin(pitch)*v.x()+std::cos(pitch)*vz;return QVector3D(px+std::cos(yaw)*vx-std::sin(yaw)*vy,py+std::sin(yaw)*vx+std::cos(yaw)*vy,vz);};auto a=transform(t.a),b=transform(t.b),c=transform(t.c);auto normal=QVector3D::crossProduct(b-a,c-a).normalized();double light=.45+.55*std::abs(QVector3D::dotProduct(normal,QVector3D(-.4f,-.3f,1).normalized()));auto color=t.color;color.setRedF(std::clamp(color.redF()*light,0.,1.));color.setGreenF(std::clamp(color.greenF()*light,0.,1.));color.setBlueF(std::clamp(color.blueF()*light,0.,1.));if(ego&&world_.value("collision",0.0)>0)color=QColor("#eb5565");polygon({a,b,c},color);}
 };
 car(mesh_,x,y,heading,true,world_.value("position",0.0));for(const auto&a:traffic_)car(defaultMesh_,a.at("x"),a.at("y"),a.at("yaw"),false,a.at("x").get<double>()+a.at("y").get<double>());
 flush();
 p.setPen(QColor("#e8f3f4"));p.setFont(QFont("Sans",10,QFont::DemiBold));p.drawText(15,23,"3D VEHICLE / LIVE PLANT · drag to orbit · wheel to zoom");p.setFont(QFont("Sans",9));p.drawText(15,height()-16,QString("%1 · X %2 m · Y %3 m · wheel %4° · %5 actors").arg(mesh_.source).arg(x,0,'f',1).arg(y,0,'f',1).arg(world_.value("steering_angle",0.0)*180/M_PI,0,'f',1).arg(traffic_.size()));
 for(const auto&actor:traffic_){QVector3D pos(actor.at("x"),actor.at("y"),2.8);if(depth(pos)>.2){p.setPen(QColor("#ffe2a0"));p.drawText(project(pos),QString::fromStdString(actor.at("id"))+QString(" · %1 km/h").arg(actor.at("speed").get<double>()*3.6,0,'f',0));}}
 // Top-down actual trace and actor positions.
 QRectF inset(width()-170,40,155,130);p.fillRect(inset,QColor(8,20,29,210));p.setClipRect(inset.adjusted(1,1,-1,-1));QPointF origin=inset.center();p.setPen(QPen(QColor("#56e0ce"),2));QPainterPath trace;for(qsizetype i=0;i<trail_.size();++i){auto point=origin+QPointF((trail_[i].x()-x)*2,-(trail_[i].y()-y)*2);if(i==0)trace.moveTo(point);else trace.lineTo(point);}p.drawPath(trace);p.setBrush(Qt::white);p.drawEllipse(origin,3,3);for(const auto&a:traffic_){auto point=origin+QPointF((a.at("x").get<double>()-x)*2,-(a.at("y").get<double>()-y)*2);p.setBrush(QColor("#ffbc5c"));p.drawRect(QRectF(point-QPointF(4,2),QSizeF(8,4)));}p.setClipping(false);
}
