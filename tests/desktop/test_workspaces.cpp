#include "ui/desktop/DesktopWindow.hpp"
#include "ui/desktop/EcuEditor.hpp"
#include "ui/desktop/Workroom.hpp"
#include "ui/desktop/HealthView.hpp"
#include "ui/desktop/SceneView.hpp"
#include "ui/desktop/CarMesh.hpp"
#include <QtTest>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QTabWidget>
#include <QTabBar>
#include <QWheelEvent>
#include <QInputDialog>
#include <QGraphicsView>
#include <QGraphicsRectItem>
#include <QFileDialog>
#include <QTemporaryDir>
#include <QFile>
#include <QTreeWidget>
#include <QLineEdit>
#include <QDialogButtonBox>

using Json=nlohmann::json;
class DesktopWorkspaceTest:public QObject {
    Q_OBJECT
private slots:
    void liveHealthTrafficMeshesAndProjectSwitching(){
        DesktopWindow w(VCLAB_TEST_RUNTIME,"resources/scenarios/ev_interactive.json",VCLAB_TEST_ROOT);w.resize(1320,700);w.show();w.navigate(2);
        QTimer modalWatch;modalWatch.setSingleShot(true);connect(&modalWatch,&QTimer::timeout,&w,[&]{if(auto *d=qobject_cast<QDialog*>(QApplication::activeModalWidget())){qWarning()<<"Unexpected stuck modal:"<<d->windowTitle()<<"editor drafts"<<w.editor_->hasDrafts()<<"topology drafts"<<w.workroom_->hasDrafts();d->reject();}});modalWatch.start(15000);
        auto idle=[&]{return !w.snapshot_.empty()&&!w.busy_&&w.requests_.empty();};QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);
        QVERIFY(w.health_->summary().startsWith("HEALTHY"));QVERIFY(w.scene_->meshName().contains("sedan"));
        auto*viewport=w.findChild<QScrollArea*>("workspaceScroll2")->viewport();
        QTest::qWait(100);
        if(const auto directory=qEnvironmentVariable("VCLAB_WORKSPACE_EVIDENCE");!directory.isEmpty()){QDir().mkpath(directory);w.grab().save(directory+"/compact-layout-check.png");}
        QTRY_VERIFY_WITH_TIMEOUT(viewport->rect().contains(w.scene_->mapTo(viewport,w.scene_->rect().bottomLeft())),2000);
        auto defaultMesh=CarMesh::load(QString(VCLAB_TEST_ROOT)+"/resources/models/default-car/sedan.obj");size_t wheels=0;for(const auto&t:defaultMesh.triangles)wheels+=t.wheel;
        QVERIFY(defaultMesh.triangles.size()>100);QVERIFY(wheels>0);QVERIFY(wheels<defaultMesh.triangles.size());
        for(const auto&t:defaultMesh.triangles)if(t.front&&t.wheel)QVERIFY(t.pivot.x()>0);
        auto glb=CarMesh::load(QString(VCLAB_TEST_ROOT)+"/resources/models/default-car/sedan.glb");QVERIFY(glb.triangles.size()>100);
        w.findChild<QDoubleSpinBox*>("actorSpeed")->setValue(36);w.findChild<QPushButton*>("addTrafficCar")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);
        QCOMPARE(w.snapshot_["traffic"]["actors"].size(),size_t(1));QCOMPARE(w.snapshot_["traffic"]["lead_id"].get<std::string>(),std::string("car-1"));
        w.findChild<QDoubleSpinBox*>("followingLimit")->setValue(90);w.findChild<QDoubleSpinBox*>("followingTime")->setValue(2.3);
        w.findChild<QPushButton*>("enableFollowing")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QCOMPARE(w.snapshot_["controls"]["assist_enabled"].get<double>(),1.0);QCOMPARE(w.findChild<QDoubleSpinBox*>("target_speed")->value(),90.0);QCOMPARE(w.snapshot_["controls"]["following_time_s"].get<double>(),2.3);
        const auto trafficHash=w.snapshot_.at("project_hash");
        auto*picker=w.findChild<QComboBox*>("projectPicker");picker->setCurrentIndex(picker->findData("builtin:ev_lateral.json"));w.findChild<QPushButton*>("switchProject")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QVERIFY(!w.snapshot_.contains("traffic"));
        picker->setCurrentIndex(picker->findData("slot:1"));w.findChild<QPushButton*>("switchProject")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QVERIFY(w.snapshot_.at("project_hash")==trafficHash);QCOMPARE(w.snapshot_["traffic"]["actors"].size(),size_t(1));
        QCOMPARE(w.snapshot_["controls"]["assist_enabled"].get<double>(),1.0);QCOMPARE(w.findChild<QDoubleSpinBox*>("followingLimit")->value(),90.0);QCOMPARE(w.findChild<QDoubleSpinBox*>("followingTime")->value(),2.3);
        w.findChild<QComboBox*>("faultSensor")->setCurrentText("temperature_sensor");w.findChild<QComboBox*>("faultMode")->setCurrentText("stuck");w.findChild<QPushButton*>("applySensorFault")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);w.enqueue({{"action","step"},{"count",20}});QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QVERIFY(w.health_->summary().contains("sensor faults"));
        w.findChild<QComboBox*>("faultMode")->setCurrentText("none");w.findChild<QPushButton*>("applySensorFault")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);
        w.findChild<QPushButton*>("applyStateStress")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QVERIFY(w.health_->summary().contains("MOTOR HOT"));QVERIFY(w.liveInspector_->topLevelItemCount()>5);
        QTemporaryDir temp;QFile f(temp.path()+"/custom.stl");QVERIFY(f.open(QIODevice::WriteOnly));f.write("solid custom\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 4 0 0\nvertex 0 2 1\nendloop\nendfacet\nendsolid custom\n");f.close();auto mesh=CarMesh::load(f.fileName(),true);QCOMPARE(mesh.triangles.size(),size_t(1));
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,true);int stage=0;QTimer dialogs;
        connect(&dialogs,&QTimer::timeout,&w,[&]{if(auto*d=qobject_cast<QInputDialog*>(QApplication::activeModalWidget())){stage=2;d->accept();}else if(auto*d=qobject_cast<QFileDialog*>(QApplication::activeModalWidget())){if(auto*name=d->findChild<QLineEdit*>("fileNameEdit"))name->setText(f.fileName());if(auto*box=d->findChild<QDialogButtonBox*>())if(auto*open=box->button(QDialogButtonBox::Open);open&&open->isEnabled()){open->click();if(!d->isVisible())stage=1;}}});dialogs.start(50);
        w.findChild<QPushButton*>("importCarShape")->click();dialogs.stop();QCOMPARE(stage,2);QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QCOMPARE(w.scene_->meshName(),QString("custom.stl"));QVERIFY(w.recoveryProject_["presentation"]["car_mesh"].contains("triangles"));
        auto assetProject=w.recoveryProject_;w.enqueue({{"action","restart_project"}});QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QCOMPARE(w.scene_->meshName(),QString("custom.stl"));
        w.findChild<QPushButton*>("defaultCarShape")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QVERIFY(w.scene_->meshName().contains("sedan"));w.enqueue({{"action","import_project"},{"project",assetProject}});QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QCOMPARE(w.scene_->meshName(),QString("custom.stl"));
        picker->setCurrentIndex(picker->findData("slot:2"));w.findChild<QPushButton*>("switchProject")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QVERIFY(w.scene_->meshName().contains("sedan"));picker->setCurrentIndex(picker->findData("slot:1"));w.findChild<QPushButton*>("switchProject")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QCOMPARE(w.scene_->meshName(),QString("custom.stl"));
        if(const auto directory=qEnvironmentVariable("VCLAB_WORKSPACE_EVIDENCE");!directory.isEmpty()){QDir().mkpath(directory);w.findChild<QPushButton*>("defaultCarShape")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QVERIFY(w.grab().save(directory+"/interactive-health-traffic.png"));}
    }
    void physicsControlsChangeNativeModel(){
        DesktopWindow w(VCLAB_TEST_RUNTIME,"resources/scenarios/ev_lateral.json",VCLAB_TEST_ROOT);w.show();
        auto idle=[&]{return !w.snapshot_.empty()&&!w.busy_&&w.requests_.empty();};QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);
        w.navigate(2);w.findChild<QComboBox*>("physicsProfile")->setCurrentIndex(1);w.findChild<QDoubleSpinBox*>("mass_kg")->setValue(1800);
        w.findChild<QPushButton*>("applyPhysics")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);
        QCOMPARE(w.snapshot_.at("physics").at("model").get<std::string>(),std::string("bicycle_grip_v2"));QCOMPARE(w.snapshot_["physics"]["mass_kg"].get<double>(),1800.0);
        w.findChild<QDoubleSpinBox*>("road_grade_deg")->setValue(6);w.findChild<QDoubleSpinBox*>("headwind_mps")->setValue(15);w.findChild<QCheckBox*>("aeb_enabled")->setChecked(false);
        w.findChild<QPushButton*>("applyEnvironment")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);
        QCOMPARE(w.snapshot_["controls"]["road_grade_deg"].get<double>(),6.0);QCOMPARE(w.snapshot_["controls"]["headwind_mps"].get<double>(),15.0);QCOMPARE(w.snapshot_["controls"]["aeb_enabled"].get<double>(),0.0);
        w.enqueue({{"action","step"}});QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QVERIFY(w.snapshot_["world"]["grade_force_n"].get<double>()>1800);
        QVERIFY(w.findChild<QLabel*>("physicsMeasurements")->text().contains("Kinetic energy"));
    }
    void repairedRunAndEditableTopology(){
        DesktopWindow w(VCLAB_TEST_RUNTIME,"resources/scenarios/ev_lateral.json",VCLAB_TEST_ROOT);w.show();
        auto idle=[&]{return !w.snapshot_.empty()&&!w.busy_&&w.requests_.empty();};QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);
        for(int i=0;i<3;++i)w.detachWorkspace(i);
        auto button=[&](const char*n){return w.findChild<QPushButton*>(n);};
        w.findChild<QComboBox*>("driveGear")->setCurrentIndex(2);w.throttle_->setValue(55);
        QTest::mouseClick(button("simulationRun"),Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(w.snapshot_.value("tick",0)>10,10000);
        const auto beforeBlock=w.snapshot_.at("tick").get<int>();QThread::msleep(200);
        QTRY_VERIFY_WITH_TIMEOUT(w.snapshot_.at("tick").get<int>()>=beforeBlock+10,10000);
        auto invalid=w.snapshot_.at("ecus")[0].at("definition");invalid["name"]="wrong";
        w.enqueue({{"action","validate_ecu_update"},{"name","Motor ECU"},{"definition",invalid}});
        auto oldTick=w.snapshot_.at("tick").get<int>();QTRY_VERIFY_WITH_TIMEOUT(w.snapshot_.at("tick").get<int>()>oldTick+10,10000);
        QTest::mouseClick(button("simulationRun"),Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(idle()&&!w.playing_,10000);
        w.findChild<QComboBox*>("schemaEcu")->setCurrentText("Radar ECU");button("haltWorker")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);
        button("stepSimulation")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);const auto frozen=w.snapshot_.at("tick").get<int>();
        button("restoreWorker")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QTest::mouseClick(button("simulationRun"),Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(w.snapshot_.at("tick").get<int>()>frozen+5,10000);
        QTest::mouseClick(button("simulationRun"),Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(idle()&&!w.playing_,10000);
        button("editTopology")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);auto *code=w.findChild<QPlainTextEdit*>("topologyCode");QVERIFY(code);QVERIFY(!code->toPlainText().isEmpty());
        QTimer::singleShot(20,[]{auto *dialog=qobject_cast<QInputDialog*>(QApplication::activeModalWidget());if(dialog){dialog->setTextValue("Desktop Test ECU");dialog->accept();}});
        button("addTopologyEcu")->click();button("validateTopology")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);
        button("applyTopology")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QCOMPARE(w.snapshot_.at("ecus").size(),size_t(10));
        w.findChild<QComboBox*>("schemaEcu")->setCurrentText("Desktop Test ECU");
        QTimer::singleShot(20,[]{if(auto*d=qobject_cast<QInputDialog*>(QApplication::activeModalWidget())){d->setTextValue("extra_test_bus");d->accept();}QTimer::singleShot(20,[]{if(auto*d=qobject_cast<QInputDialog*>(QApplication::activeModalWidget()))d->accept();});});
        button("Add CAN bus")->click();auto beforeUndo=code->toPlainText();button("Undo draft")->click();QVERIFY(code->toPlainText()!=beforeUndo);button("Redo draft")->click();QCOMPARE(code->toPlainText(),beforeUndo);
        QTimer::singleShot(20,[]{if(auto*d=qobject_cast<QInputDialog*>(QApplication::activeModalWidget()))d->accept();QTimer::singleShot(20,[]{if(auto*d=qobject_cast<QInputDialog*>(QApplication::activeModalWidget())){d->setTextValue("extra_test_bus");d->accept();}});});
        button("Connect selected ECU message")->click();button("applyTopology")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);
        w.enqueue({{"action","step"},{"count",10}});QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);bool transmitted=false;for(const auto&f:w.snapshot_.at("frames"))transmitted|=f.at("ecu")=="Desktop Test ECU";QVERIFY(transmitted);
        bool routed=false;for(const auto&f:w.snapshot_.at("frames"))routed|=f.at("ecu")=="Desktop Test ECU"&&f.at("bus")=="extra_test_bus";QVERIFY(routed);
        w.findChild<QComboBox*>("schemaEcu")->setCurrentText("Desktop Test ECU");button("removeTopologyEcu")->click();button("applyTopology")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QCOMPARE(w.snapshot_.at("ecus").size(),size_t(9));
        QTimer::singleShot(20,[]{if(auto*d=qobject_cast<QInputDialog*>(QApplication::activeModalWidget())){d->setTextValue("extra_test_bus");d->accept();}});button("Remove unused bus")->click();button("applyTopology")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QCOMPARE(w.snapshot_.at("buses").size(),size_t(2));
        auto *view=w.findChild<QGraphicsView*>("ecuSchema");QGraphicsItem *node=nullptr;for(auto *item:view->scene()->items())if(item->data(0).toString()=="Motor ECU"){node=item;break;}QVERIFY(node);QVERIFY(node->flags()&QGraphicsItem::ItemIsMovable);
        const auto hash=w.snapshot_.at("project_hash");view->centerOn(node);QTest::qWait(50);const auto from=view->mapFromScene(node->sceneBoundingRect().center());
        QTest::mousePress(view->viewport(),Qt::LeftButton,Qt::NoModifier,from);
        QMouseEvent move(QEvent::MouseMove,QPointF(from+QPoint(30,40)),QPointF(view->viewport()->mapToGlobal(from+QPoint(30,40))),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(view->viewport(),&move);QTest::mouseRelease(view->viewport(),Qt::LeftButton,Qt::NoModifier,from+QPoint(30,40));
        QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QVERIFY(w.snapshot_.at("project_hash")==hash);QVERIFY(w.snapshot_["presentation"]["node_offsets"].contains("Motor ECU"));const auto position=w.snapshot_["presentation"]["node_offsets"]["Motor ECU"];QVERIFY(position!=Json({0,0}));
        w.enqueue({{"action","restart_project"}});QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QVERIFY(w.snapshot_["presentation"]["node_offsets"]["Motor ECU"]==position);
        auto oldPid=w.process_.processId();const auto savedHash=w.snapshot_.at("project_hash");button("reconnectRuntime")->click();
        QTRY_VERIFY_WITH_TIMEOUT(w.ready_&&w.process_.processId()!=oldPid&&idle(),15000);
        QVERIFY(w.snapshot_.at("project_hash")==savedHash);QVERIFY(w.snapshot_["presentation"]["node_offsets"]["Motor ECU"]==position);
        for(int i=0;i<3;++i)if(w.detached_[i])w.detached_[i]->close();
    }
    void navigationProgrammingFaultsAndSharedWindows(){
        DesktopWindow window(VCLAB_TEST_RUNTIME,"resources/scenarios/ev_lateral.json",VCLAB_TEST_ROOT);
        window.show();
        auto idle=[&]{return !window.snapshot_.empty()&&!window.busy_&&window.requests_.empty();};
        QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);
        const auto controllerPid=window.process_.processId();
        auto *tabs=window.findChild<QTabWidget*>("workspaces");QVERIFY(tabs);QCOMPARE(tabs->count(),3);QCOMPARE(tabs->currentIndex(),0);
        auto button=[&](const char*name){return window.findChild<QPushButton*>(name);};
        auto combo=[&](const char*name){return window.findChild<QComboBox*>(name);};
        auto ecu=[&](const std::string&name)->Json{for(const auto&e:window.snapshot_.at("ecus"))if(e.at("name")==name)return e;return nullptr;};
        // Every workspace remains navigable and scrollable on a small screen.
        window.resize(800,600);QTest::qWait(100);QCOMPARE(window.size(),QSize(800,600));
        for(int i=0;i<3;++i){
            QTest::mouseClick(tabs->tabBar(),Qt::LeftButton,Qt::NoModifier,tabs->tabBar()->tabRect(i).center());QCOMPARE(tabs->currentIndex(),i);
            auto *scroll=window.findChild<QScrollArea*>(QString("workspaceScroll%1").arg(i));QVERIFY(scroll);
            QTRY_VERIFY(scroll->verticalScrollBar()->maximum()>0);QVERIFY(scroll->horizontalScrollBar()->maximum()>0);
            QWheelEvent wheel(QPointF(40,40),QPointF(40,40),QPoint(),QPoint(0,-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QApplication::sendEvent(scroll->viewport(),&wheel);QVERIFY(scroll->verticalScrollBar()->value()>0);
            scroll->verticalScrollBar()->setValue(0);
        }
        window.resize(1440,960);window.navigate(0);combo("schemaEcu")->setCurrentText("EPS ECU");button("openEcuProgram")->click();QCOMPARE(tabs->currentIndex(),1);QCOMPARE(combo("programEcu")->currentText(),QString("EPS ECU"));
        // Wheel events over driving widgets scroll instead of changing an input.
        window.navigate(2);auto *throttle=window.findChild<QSlider*>("driveThrottle");
        QWheelEvent wheelOverControl(QPointF(5,5),QPointF(5,5),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(throttle,&wheelOverControl);QCOMPARE(throttle->value(),0);window.navigate(1);
        auto *code=window.findChild<QPlainTextEdit*>("programCode");auto *feedback=window.findChild<QLabel*>("programFeedback");QVERIFY(code);QVERIFY(feedback);
        auto program=Json::parse(code->toPlainText().toStdString());auto initial=window.snapshot_;
        auto invalid=program;invalid["tasks"][1]["assign"]["steering_angle"]="undefined_variable";
        code->setPlainText(QString::fromStdString(invalid.dump(2)));button("validateProgram")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);
        QVERIFY(feedback->text().contains("Unknown")||feedback->text().contains("unknown")||feedback->text().contains("reference"));QVERIFY(window.snapshot_==initial);
        // Switching ECU and workspace never overwrites an undeployed draft.
        auto draft=code->toPlainText();combo("programEcu")->setCurrentText("Motor ECU");window.navigate(0);window.navigate(1);combo("programEcu")->setCurrentText("EPS ECU");QCOMPARE(code->toPlainText(),draft);
        program["tasks"][1]["assign"]["steering_angle"]=0.2;
        code->setPlainText(QString::fromStdString(program.dump(2)));button("validateProgram")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QVERIFY(feedback->text().startsWith("VALID"));QVERIFY(window.snapshot_==initial);
        button("deployProgram")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QVERIFY(feedback->text().startsWith("DEPLOYED"));QVERIFY(window.snapshot_["project_hash"]!=initial["project_hash"]);QVERIFY(!window.editor_->hasDrafts());
        const auto editedHash=window.snapshot_["project_hash"];
        window.navigate(2);button("stepSimulation")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QCOMPARE(window.snapshot_["world"]["steering_angle"].get<double>(),0.2);
        // Inputs must be sent before a step, even without waiting for the UI timer.
        combo("driveGear")->setCurrentIndex(2);window.findChild<QSlider*>("driveThrottle")->setValue(55);button("stepSimulation")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QCOMPARE(window.snapshot_["controls"]["throttle"].get<double>(),.55);
        button("simulationRun")->click();QTRY_VERIFY_WITH_TIMEOUT(window.snapshot_.value("tick",0)>10,10000);button("simulationRun")->click();QTRY_VERIFY_WITH_TIMEOUT(idle()&&!window.playing_,10000);auto pausedTick=window.snapshot_.at("tick");QTest::qWait(150);QVERIFY(window.snapshot_.at("tick")==pausedTick);
        // Selected ECU traffic policy changes delivered traffic and loss counters.
        window.navigate(0);combo("schemaEcu")->setCurrentText("Radar ECU");window.findChild<QCheckBox*>("policyDrop")->setChecked(true);button("applyTrafficPolicy")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);const auto dropped=window.snapshot_.at("dropped_frames").get<int>();
        window.enqueue({{"action","step"},{"count",20}});QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QVERIFY(window.snapshot_.at("dropped_frames").get<int>()>dropped);
        button("clearTrafficPolicy")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QVERIFY(window.snapshot_.at("policies").empty());
        window.findChild<QCheckBox*>("policyDrop")->setChecked(false);window.findChild<QDoubleSpinBox*>("policyLatency")->setValue(50);button("applyTrafficPolicy")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);
        window.enqueue({{"action","step"},{"count",20}});QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);
        bool delayed=false;for(const auto&frame:window.snapshot_.at("frames"))if(frame.at("ecu")=="Radar ECU"&&!frame.at("delivered_at_us").is_null())delayed=delayed||(frame.at("delivered_at_us").get<double>()-frame.at("computed_at_us").get<double>()>=50000);QVERIFY(delayed);
        button("clearTrafficPolicy")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);
        // Worker halt freezes a partial tick; restore resumes that same tick.
        const auto tick=window.snapshot_.at("tick").get<int>();button("haltWorker")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QVERIFY(ecu("Radar ECU")["container"]["status"]=="fault");
        button("stepSimulation")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QCOMPARE(window.snapshot_.at("tick").get<int>(),tick);
        button("restoreWorker")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);button("stepSimulation")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QCOMPARE(window.snapshot_.at("tick").get<int>(),tick+1);
        // Fault value is editable and reaches the actual sensor and motor ECU.
        window.navigate(2);combo("faultSensor")->setCurrentText("temperature_sensor");combo("faultMode")->setCurrentText("stuck");window.findChild<QDoubleSpinBox*>("faultValue")->setValue(120);button("applySensorFault")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);
        window.enqueue({{"action","step"},{"count",20}});QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QVERIFY(ecu("Motor ECU")["state"]=="FAULT");QCOMPARE(window.snapshot_["world"]["fan"].get<double>(),1.0);
        combo("faultMode")->setCurrentText("none");button("applySensorFault")->click();QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);window.enqueue({{"action","step"},{"count",20}});QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QVERIFY(ecu("Motor ECU")["state"]=="RUN");
        // Restart the edited project instead of silently discarding its program.
        window.enqueue({{"action","restart_project"}});QTRY_VERIFY_WITH_TIMEOUT(idle(),10000);QVERIFY(window.snapshot_["project_hash"]==editedHash);QVERIFY(ecu("EPS ECU")["definition"]==program);
        const auto beforeWindows=window.snapshot_;
        button("openAllWindows")->click();
        for(int i=0;i<3;++i){QVERIFY(window.detached_[i]);QVERIFY(window.detached_[i]->isVisible());QVERIFY(window.detached_[i]->isAncestorOf(window.workspacePages_[i]));}
        QCOMPARE(window.process_.processId(),controllerPid);QVERIFY(window.snapshot_==beforeWindows);
        for(int i=0;i<3;++i){window.detached_[i]->close();QVERIFY(!window.detached_[i]);QVERIFY(window.workspaceHolders_[i]->isAncestorOf(window.workspacePages_[i]));}
        // Reproducible screenshots of all three real workspaces.
        if(const auto directory=qEnvironmentVariable("VCLAB_WORKSPACE_EVIDENCE");!directory.isEmpty()){
            QDir().mkpath(directory);
            for(int i=0;i<3;++i){window.navigate(i);QTest::qWait(100);QVERIFY(window.grab().save(directory+QString("/workspace-%1.png").arg(i+1)));}
        }
        window.close();
    }
};
QTEST_MAIN(DesktopWorkspaceTest)
#include "test_workspaces.moc"
