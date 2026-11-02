#pragma once
#include <QVector3D>
#include <QColor>
#include <QString>
#include <vector>
#include "json.hpp"
struct CarTriangle{QVector3D a,b,c;QColor color;QVector3D pivot;bool wheel=false,front=false;};
class CarMesh{
public:
 std::vector<CarTriangle> triangles;
 QString source;
 static CarMesh load(const QString&path,bool zUp=false);
 static CarMesh fromJson(const nlohmann::json&value);
 nlohmann::json toJson()const;
};
