#pragma once
#include <QWidget>
#include <functional>
#include "json.hpp"
class QComboBox;class QDoubleSpinBox;class QTableWidget;class QLabel;
class TrafficPanel final:public QWidget{
public:explicit TrafficPanel(QWidget*parent=nullptr);void setSnapshot(const nlohmann::json&s);std::function<void(nlohmann::json,std::function<void(const nlohmann::json&)>)> request;
private:QComboBox *selected_;QDoubleSpinBox *gap_,*side_,*speed_,*limit_,*time_,*standstill_,*gain_;QTableWidget*table_;QLabel*feedback_,*following_;nlohmann::json state_=nlohmann::json::object();unsigned nextId_=1;bool followingDirty_=false;void command(const std::string&operation,bool reposition);
};
