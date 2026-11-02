#pragma once
#include <QWidget>
#include <map>
#include <functional>
#include "json.hpp"
class QComboBox; class QPlainTextEdit; class QLabel; class QSpinBox; class QTreeWidget;

class EcuEditor final : public QWidget {
public:
    explicit EcuEditor(QWidget *parent = nullptr);
    void setSnapshot(const nlohmann::json &snapshot);
    void selectEcu(const QString &name);
    void result(const QString &message, bool success, bool deployed = false, const QString &name = {});
    bool hasDrafts() const;
    void discardDrafts(){drafts_.clear();load(selected_);}
    std::function<void(const QString &, const nlohmann::json &, bool)> submit;
private:
    QComboBox *selector_;
    QPlainTextEdit *code_;
    QLabel *feedback_, *draftState_;
    QSpinBox *cpu_, *ram_, *flash_;
    QTreeWidget *structure_;
    QString selected_;
    std::map<QString, QString> live_, drafts_;
    void load(const QString &name);
    void send(bool deploy);
    void inspect();
};
