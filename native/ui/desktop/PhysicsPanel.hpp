#pragma once
#include <QWidget>
#include <map>
#include <functional>
#include "json.hpp"
class QDoubleSpinBox;class QCheckBox;class QComboBox;class QLabel;
class PhysicsPanel final:public QWidget{
public:
    explicit PhysicsPanel(QWidget *parent=nullptr);
    void setSnapshot(const nlohmann::json&state);
    std::function<void(nlohmann::json,std::function<void(const nlohmann::json&)>)> request;
private:
    std::map<std::string,QDoubleSpinBox*> controls_,parameters_;
    std::map<std::string,QCheckBox*> switches_;
    QComboBox *profile_;
    QLabel *metrics_,*feedback_;
    nlohmann::json state_=nlohmann::json::object();
    QString revision_;
    bool liveDirty_=false;
    uint64_t epoch_=0;
    nlohmann::json physicsConfig_=nullptr;
};
