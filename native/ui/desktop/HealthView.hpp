#pragma once
#include <QWidget>
#include "json.hpp"
class HealthView final:public QWidget{
public:explicit HealthView(QWidget*parent=nullptr);void setSnapshot(const nlohmann::json&s);QString summary()const;
protected:void paintEvent(QPaintEvent*)override;
private:nlohmann::json state_=nlohmann::json::object();QString summary_;
};
