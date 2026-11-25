#pragma once
#include <QWidget>
#include <functional>
#include <map>
#include "json.hpp"
class QGraphicsView;class QGraphicsScene;class QGraphicsRectItem;class QGraphicsTextItem;
class QComboBox;class QPlainTextEdit;class QDoubleSpinBox;class QCheckBox;class QLabel;
class QDialog;
class Workroom final:public QWidget {
public:
    explicit Workroom(QWidget *parent=nullptr);
    void setSnapshot(const nlohmann::json &snapshot);
    std::function<void(const QString &)> program;
    std::function<void(nlohmann::json)> command;
    std::function<void(nlohmann::json,std::function<void(const nlohmann::json&)>)> request;
    void showError(const QString &message);
    bool hasDrafts() const;
    void loadProjectDraft(const nlohmann::json&p);
private:
    QGraphicsView *view_;QGraphicsScene *graph_;
    QComboBox *selected_;
    QPlainTextEdit *details_;
    QDoubleSpinBox *latency_;
    QCheckBox *drop_;
    QLabel *policyStatus_;
    nlohmann::json state_=nlohmann::json::object();
    QString graphKey_;
    QString projectBase_;
    QDialog *projectWindow_=nullptr;
    QPlainTextEdit *projectCode_=nullptr;
    QLabel *projectStatus_=nullptr;
    std::map<QString,QGraphicsTextItem*> labels_;
    std::map<QString,QGraphicsRectItem*> nodes_;
    void rebuild();
    void inspect();
    void policy(bool clear);
    void editTopology();
    void saveLayout();
    void replaceDraft(const nlohmann::json &project);
};
