#pragma once
#include <QMainWindow>
#include <QProcess>
#include <QTimer>
#include <QElapsedTimer>
#include <QFile>
#include <deque>
#include <functional>
#include "json.hpp"
class QLabel; class QSlider; class QComboBox; class QTableWidget; class QPlainTextEdit; class QPushButton;
class SceneView;
class EcuEditor; class Workroom; class QTabWidget; class QDialog; class QDoubleSpinBox;
class PhysicsPanel;
class TrafficPanel;class HealthView;
class QTreeWidget;class QTreeWidgetItem;
using DesktopJson = nlohmann::json;

class DesktopWindow final : public QMainWindow {
public:
    DesktopWindow(const QString &runtime, const QString &scenario, const QString &root, const QString &smokeDir = {});
    ~DesktopWindow() override;
protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    friend class DesktopWorkspaceTest;
    struct Request { DesktopJson body; std::function<void(const DesktopJson&)> done; };
    QProcess process_;
    QTimer timer_, timeout_;
    QByteArray buffer_;
    std::deque<Request> requests_;
    Request current_;
    bool busy_ = false, ready_ = false, playing_ = false, controlsDirty_ = false, closing_ = false;
    int controlWrites_ = 0;
    QString lastError_, runtimePath_, scenarioPath_, resourceRoot_;
    DesktopJson recoveryProject_=DesktopJson::object();
    DesktopJson snapshot_ = DesktopJson::object();
    std::map<QString,DesktopJson> projectSlots_;
    QString activeSlot_="slot:1";
    unsigned nextSlot_=2;
    QComboBox *projectPicker_=nullptr;
    bool refreshProjectDraft_=false;
    QElapsedTimer roundTrip_;
    SceneView *scene_ = nullptr;
    EcuEditor *editor_ = nullptr;
    Workroom *workroom_ = nullptr;
    PhysicsPanel *physics_ = nullptr;
    TrafficPanel *trafficPanel_=nullptr;
    HealthView *health_=nullptr;
    QTabWidget *workspaces_ = nullptr;
    std::vector<QWidget*> workspacePages_, workspaceHolders_;
    std::vector<QDialog*> detached_;
    QLabel *status_, *speed_, *battery_, *clock_, *traffic_, *hash_, *bus_;
    QSlider *throttle_, *brake_, *steering_;
    QComboBox *gear_, *sensor_, *fault_, *diagnosticEcu_;
    QDoubleSpinBox *faultValue_;
    QPushButton *play_, *record_;
    QTableWidget *ecus_, *frames_, *performance_;
    QPlainTextEdit *events_;
    QTreeWidget *liveInspector_=nullptr;
    std::map<QString,QTreeWidgetItem*> liveItems_;
    QString inspectorRevision_;
    QFile recording_;
    QString smokeDir_;
    int smokeStage_ = 0;
    double smokePeak_ = 0, smokeTurn_ = 0;
    void enqueue(DesktopJson body, std::function<void(const DesktopJson&)> done = {});
    void pump();
    void receive();
    void updateSnapshot(const DesktopJson &state);
    void tick();
    void error(const QString &message);
    void log(const QString &message);
    void toggleRecord();
    void startRecord(const QString &path);
    void stopRecord();
    void smoke();
    void finishSmoke();
    void navigate(int index);
    void detachWorkspace(int index);
    void submitProgram(const QString &name, const DesktopJson &definition, bool deploy);
    void flushControls();
    void reconnectRuntime();
    void switchProject(const QString&key);
};
