#pragma once
#include <QWidget>
#include <QVector>
#include <QPointF>
#include "json.hpp"
#include "CarMesh.hpp"
class SceneView final:public QWidget{
public:explicit SceneView(QWidget*parent=nullptr);void setSnapshot(const nlohmann::json&snapshot);void setPresentation(const nlohmann::json&presentation);void resetCamera();QString meshName()const{return mesh_.source;}CarMesh currentMesh()const{return mesh_;}
protected:void paintEvent(QPaintEvent*)override;void mousePressEvent(QMouseEvent*)override;void mouseMoveEvent(QMouseEvent*)override;void wheelEvent(QWheelEvent*)override;
private:nlohmann::json world_=nlohmann::json::object(),traffic_=nlohmann::json::array();CarMesh defaultMesh_,mesh_;QVector<QPointF>trail_;double lastTime_=-1,orbit_=-.55,distance_=11,wheelRadius_=.32;uint64_t epoch_=0;QPoint lastMouse_;QString assetKey_;
};
