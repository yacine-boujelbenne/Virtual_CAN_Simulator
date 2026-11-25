#include "DesktopWindow.hpp"
#include "SceneView.hpp"
#include "EcuEditor.hpp"
#include "Workroom.hpp"
#include "PhysicsPanel.hpp"
#include "TrafficPanel.hpp"
#include "HealthView.hpp"
#include <QInputDialog>
#include <QTreeWidget>
#include <QCheckBox>
#include <QShortcut>
#include <QApplication>
#include <QAbstractSpinBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QMessageBox>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <cmath>

namespace {
using Json = DesktopJson;
QString str(const Json &value) { return QString::fromStdString(value.is_string()?value.get<std::string>():value.dump()); }
void saveJson(const QString &path,const Json &value) {
    QSaveFile file(path);const auto data=QByteArray::fromStdString(value.dump(2)+"\n");
    if(!file.open(QIODevice::WriteOnly)||file.write(data)!=data.size()||!file.commit())throw std::runtime_error("Cannot save "+path.toStdString());
}
QTableWidget *table(const QStringList &names) {
    auto *t=new QTableWidget(0,names.size());t->setHorizontalHeaderLabels(names);t->verticalHeader()->hide();
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);t->setAlternatingRowColors(true);return t;
}
void cell(QTableWidget *t,int row,int col,const QString &text) { auto *v=t->item(row,col);if(!v){v=new QTableWidgetItem;t->setItem(row,col,v);}v->setText(text); }
}

DesktopWindow::DesktopWindow(const QString &runtime,const QString &scenario,const QString &root,const QString &smokeDir)
    : smokeDir_(smokeDir) {
    runtimePath_=runtime;scenarioPath_=scenario;resourceRoot_=root;
    setWindowTitle("Virtual CAN Laboratory · Workroom / Programming / Simulation");resize(1440,960);
    setStyleSheet("QWidget { background:#101a26; color:#dce8f0; font-family:Sans; font-size:12px; }"
        "QLabel#title { font-size:20px; font-weight:700; } QLabel#muted { color:#8da4b5; }"
        "QLabel#metric { font-size:22px; font-weight:600; color:#68e2ce; padding:2px; }"
        "QGroupBox { border:1px solid #2b3e50; border-radius:7px; margin-top:15px; padding:12px 8px 8px; }"
        "QGroupBox#metricGroup { margin-top:10px; padding:6px 5px 5px; }"
        "QGroupBox::title { subcontrol-origin:margin; left:12px; color:#8da4b5; }"
        "QPushButton { background:#233648; border:1px solid #38536b; padding:9px 14px; border-radius:5px; }"
        "QPushButton:hover { background:#34556a; } QPushButton:disabled { color:#526778; }"
        "QPushButton#primary { background:#206d65; border-color:#3ea593; }"
        "QComboBox { background:#1c2d3e; border:1px solid #38536b; padding:6px; }"
        "QSlider::groove:horizontal { background:#2c4254; height:5px; } QSlider::handle:horizontal { background:#57d9c3; width:14px; margin:-5px 0; border-radius:6px; }"
        "QTableWidget { background:#132131; alternate-background-color:#18293a; gridline-color:#263a4b; border:0; }"
        "QHeaderView::section { background:#233649; border:0; padding:8px; color:#a9c1d1; }"
        "QTabWidget::pane { border:1px solid #2c4052; } QTabBar::tab { background:#1c2d3e; padding:9px 20px; }"
        "QTabBar::tab:selected { color:#68e2ce; background:#2b4253; } QPlainTextEdit { background:#0c1520; border:0; }"
        "QScrollArea { border:0; } QScrollBar:vertical { background:#142432; width:14px; }"
        "QScrollBar::handle:vertical { background:#56758b; min-height:26px; border-radius:5px; }"
        "QScrollBar:horizontal { background:#142432; height:14px; } QScrollBar::handle:horizontal { background:#56758b; min-width:26px; border-radius:5px; }"
        "QPushButton:checked { background:#206d65; } QSpinBox,QDoubleSpinBox { background:#1c2d3e; padding:6px; }");
    auto *central=new QWidget;setCentralWidget(central);auto *layout=new QVBoxLayout(central);layout->setContentsMargins(12,10,12,10);layout->setSpacing(6);
    auto *head=new QHBoxLayout;auto *title=new QLabel("VIRTUAL CAN  /  VEHICLE LAB");title->setObjectName("title");title->setWordWrap(false);head->addWidget(title);head->addStretch();
    auto *badge=new QLabel("ONE SHARED LABORATORY\nWorkroom → Program → Simulate");badge->setObjectName("muted");badge->setWordWrap(true);head->addWidget(badge);layout->addLayout(head);
    auto *toolbar=new QGridLayout;int toolIndex=0;
    auto button=[&](const QString &text,auto callback){auto *b=new QPushButton(text);const int i=toolIndex++;toolbar->addWidget(b,i/6,i%6);connect(b,&QPushButton::clicked,this,callback);return b;};
    play_=button("▶  Run",[this]{flushControls();playing_=!playing_;play_->setText(playing_?"Ⅱ  Pause":"▶  Run");enqueue({{"action",playing_?"run":"pause"}});});play_->setObjectName("primary");
    auto *stepButton=button("Step 10 ms",[this]{playing_=false;play_->setText("▶  Run");flushControls();enqueue({{"action","pause"}});enqueue({{"action","step"},{"count",1}});});stepButton->setObjectName("stepSimulation");
    button("Restart project",[this]{playing_=false;play_->setText("▶  Run");controlsDirty_=false;stopRecord();enqueue({{"action","restart_project"}});});
    button("Open project",[this]{
        auto path=QFileDialog::getOpenFileName(this,"Open native project",{},"CAN project (*.vcanproj)");if(path.isEmpty())return;
        try{QFile f(path);if(!f.open(QIODevice::ReadOnly)||f.size()>16*1024*1024)throw std::runtime_error("Cannot read project (16 MiB limit)");
            auto project=Json::parse(f.readAll().toStdString());playing_=false;controlsDirty_=false;stopRecord();play_->setText("▶  Run");
            if((editor_->hasDrafts()||workroom_->hasDrafts())&&QMessageBox::question(this,"Undeployed drafts","Discard undeployed drafts and open this project?",QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Discard)return;
            enqueue({{"action","pause"}});enqueue({{"action","export_project"}});enqueue({{"action","import_project"},{"project",project}},[this](const Json&){activeSlot_="slot:"+QString::number(nextSlot_++);editor_->discardDrafts();refreshProjectDraft_=true;});
        }catch(const std::exception&e){error(e.what());}});
    button("Save project",[this]{auto path=QFileDialog::getSaveFileName(this,"Save native project","experiment.vcanproj","CAN project (*.vcanproj)");if(path.isEmpty())return;
        enqueue({{"action","export_project"}},[this,path](const Json&project){saveJson(path,project);log("Project saved: "+path);});});
    record_=button("Record telemetry",[this]{toggleRecord();});layout->addLayout(toolbar);
    auto*projectBar=new QHBoxLayout;projectPicker_=new QComboBox;projectPicker_->setObjectName("projectPicker");projectPicker_->addItem("New: Interactive traffic / proportional following","builtin:ev_interactive.json");projectPicker_->addItem("New: Physics laboratory","builtin:ev_physics.json");projectPicker_->addItem("New: Legacy lateral laboratory","builtin:ev_lateral.json");projectBar->addWidget(projectPicker_,1);auto*switchButton=new QPushButton("Load / switch project");switchButton->setObjectName("switchProject");projectBar->addWidget(switchButton);connect(switchButton,&QPushButton::clicked,this,[this]{switchProject(projectPicker_->currentData().toString());});layout->addLayout(projectBar);
    auto *windowBar=new QHBoxLayout;auto *detach=new QPushButton("Open current workspace in window");detach->setObjectName("detachWorkspace");windowBar->addWidget(detach);connect(detach,&QPushButton::clicked,this,[this]{detachWorkspace(workspaces_->currentIndex());});
    auto *all=new QPushButton("Open all three windows");all->setObjectName("openAllWindows");windowBar->addWidget(all);connect(all,&QPushButton::clicked,this,[this]{for(int i=0;i<3;++i)detachWorkspace(i);});windowBar->addStretch();layout->addLayout(windowBar);
    auto *reconnect=new QPushButton("Reconnect runtime");reconnect->setObjectName("reconnectRuntime");windowBar->addWidget(reconnect);connect(reconnect,&QPushButton::clicked,this,[this]{reconnectRuntime();});
    workspaces_=new QTabWidget;workspaces_->setObjectName("workspaces");layout->addWidget(workspaces_,1);
    workroom_=new Workroom;editor_=new EcuEditor;auto *simulation=new QWidget;simulation->setObjectName("simulationPage");simulation->setMinimumSize(880,500);auto *simulationLayout=new QVBoxLayout(simulation);
    auto *simulationBar=new QHBoxLayout;auto *runHere=new QPushButton("Run / pause");runHere->setObjectName("simulationRun");simulationBar->addWidget(runHere);connect(runHere,&QPushButton::clicked,play_,&QPushButton::click);
    auto *stepHere=new QPushButton("Step 10 ms");simulationBar->addWidget(stepHere);connect(stepHere,&QPushButton::clicked,stepButton,&QPushButton::click);
    auto *simState=new QLabel("PAUSED");simState->setObjectName("simulationState");simulationBar->addWidget(simState,1);simulationLayout->addLayout(simulationBar);
    workroom_->setMinimumSize(900,640);editor_->setMinimumSize(900,640);
    workspacePages_={workroom_,editor_,simulation};detached_.resize(3,nullptr);
    const QStringList names={"1  ECU workroom","2  ECU programming","3  Simulation"};
    for(int i=0;i<3;++i){auto *holder=new QWidget;auto *holderLayout=new QVBoxLayout(holder);holderLayout->setContentsMargins(0,0,0,0);auto *scroll=new QScrollArea;scroll->setObjectName(QString("workspaceScroll%1").arg(i));scroll->setWidgetResizable(true);scroll->setWidget(workspacePages_[i]);holderLayout->addWidget(scroll);workspaceHolders_.push_back(holder);workspaces_->addTab(holder,names[i]);}
    connect(workspaces_,&QTabWidget::currentChanged,this,[this](int i){if(detached_[i]){detached_[i]->show();detached_[i]->raise();detached_[i]->activateWindow();}});
    workroom_->program=[this](const QString&name){editor_->selectEcu(name);navigate(1);};
    workroom_->command=[this](Json command){if(command.at("action")=="node_fault"){playing_=false;play_->setText("▶  Run");}enqueue(std::move(command));};
    workroom_->request=[this](Json command,std::function<void(const Json&)>done){if(command.at("action")=="apply_project_draft"){stopRecord();controlsDirty_=false;enqueue({{"action","pause"}});}enqueue(std::move(command),std::move(done));};
    editor_->submit=[this](const QString&name,const Json&definition,bool deploy){submitProgram(name,definition,deploy);};
    auto *metrics=new QHBoxLayout;
    auto metric=[&](const QString &name){auto *g=new QGroupBox(name);g->setObjectName("metricGroup");auto *v=new QVBoxLayout(g);auto *l=new QLabel("—");l->setObjectName("metric");v->addWidget(l);metrics->addWidget(g);return l;};
    speed_=metric("VEHICLE SPEED");battery_=metric("BATTERY SOC");clock_=metric("SIMULATION CLOCK");traffic_=metric("CAN FRAMES");simulationLayout->addLayout(metrics);
    simulationLayout->removeItem(metrics);health_=new HealthView;auto*metricWidget=new QWidget;auto*metricColumn=new QVBoxLayout(metricWidget);metricColumn->setContentsMargins(0,0,0,0);metricColumn->setSpacing(4);metricColumn->addLayout(metrics);auto*shapeBar=new QHBoxLayout;metricColumn->addLayout(shapeBar);auto*importShape=new QPushButton("Import car shape (STL/OBJ/glTF)");importShape->setObjectName("importCarShape");shapeBar->addWidget(importShape);
    connect(importShape,&QPushButton::clicked,this,[this]{auto file=QFileDialog::getOpenFileName(this,"Import car mesh",{},"Car meshes (*.stl *.obj *.gltf *.glb)");if(file.isEmpty())return;try{bool ok=false;auto axis=QInputDialog::getItem(this,"Mesh orientation","Source up-axis",{"Y up (OBJ/glTF)","Z up (STL/CAD)"},file.endsWith(".stl",Qt::CaseInsensitive)?1:0,false,&ok);if(!ok)return;auto mesh=CarMesh::load(file,axis.startsWith("Z"));enqueue({{"action","set_car_mesh"},{"mesh",mesh.toJson()}});}catch(const std::exception&e){error(e.what());}});
    auto*defaultShape=new QPushButton("Default sedan");defaultShape->setObjectName("defaultCarShape");shapeBar->addWidget(defaultShape);connect(defaultShape,&QPushButton::clicked,this,[this]{enqueue({{"action","set_car_mesh"},{"mesh",nullptr}});});auto*camera=new QPushButton("Reset camera");shapeBar->addWidget(camera);connect(camera,&QPushButton::clicked,this,[this]{scene_->resetCamera();});
    auto*flipShape=new QPushButton("Turn shape 180°");flipShape->setObjectName("flipCarShape");shapeBar->addWidget(flipShape);connect(flipShape,&QPushButton::clicked,this,[this]{auto mesh=scene_->currentMesh();for(auto&t:mesh.triangles){for(auto*v:{&t.a,&t.b,&t.c,&t.pivot}){v->setX(-v->x());v->setY(-v->y());}}enqueue({{"action","set_car_mesh"},{"mesh",mesh.toJson()}});});
    auto *split=new QSplitter(Qt::Horizontal);simulationLayout->addWidget(split,1);
    auto *left=new QWidget;left->setMaximumWidth(300);left->setMinimumWidth(235);auto *side=new QVBoxLayout(left);side->setContentsMargins(0,0,10,0);
    auto *drive=new QGroupBox("DRIVER COMMANDS");auto *form=new QFormLayout(drive);
    gear_=new QComboBox;gear_->setObjectName("driveGear");gear_->addItems({"P  ·  Park","N  ·  Neutral","D  ·  Drive"});form->addRow("Gear",gear_);connect(gear_,&QComboBox::currentIndexChanged,this,[this]{controlsDirty_=true;});
    auto slider=[&](const QString &label,int min,int max){auto *s=new QSlider(Qt::Horizontal);s->setRange(min,max);form->addRow(label,s);connect(s,&QSlider::valueChanged,this,[this]{controlsDirty_=true;});return s;};
    throttle_=slider("Throttle %",0,100);brake_=slider("Brake %",0,100);steering_=slider("Steer ±100%",-100,100);
    throttle_->setObjectName("driveThrottle");brake_->setObjectName("driveBrake");steering_->setObjectName("driveSteering");
    auto *centre=new QPushButton("Centre steering");form->addRow(centre);connect(centre,&QPushButton::clicked,this,[this]{steering_->setValue(0);});
    auto *stop=new QPushButton("Brake + throttle zero");form->addRow(stop);connect(stop,&QPushButton::clicked,this,[this]{throttle_->setValue(0);brake_->setValue(100);});side->addWidget(drive);
    auto *keyboard=new QCheckBox("Keyboard W/S/A/D + Space");keyboard->setObjectName("keyboardDriving");form->addRow(keyboard);
    auto key=[&](const QString&sequence,auto callback){auto *shortcut=new QShortcut(QKeySequence(sequence),simulation);shortcut->setContext(Qt::WidgetWithChildrenShortcut);shortcut->setEnabled(false);connect(keyboard,&QCheckBox::toggled,shortcut,&QShortcut::setEnabled);connect(shortcut,&QShortcut::activated,this,callback);};
    key("W",[this]{brake_->setValue(0);throttle_->setValue(std::min(100,throttle_->value()+10));});key("S",[this]{throttle_->setValue(0);brake_->setValue(std::min(100,brake_->value()+10));});key("A",[this]{steering_->setValue(std::max(-100,steering_->value()-10));});key("D",[this]{steering_->setValue(std::min(100,steering_->value()+10));});key("Space",[this]{throttle_->setValue(0);brake_->setValue(100);});
    physics_=new PhysicsPanel;physics_->request=workroom_->request;side->addWidget(physics_);
    auto*trafficBox=new QGroupBox("INTERACTIVE TRAFFIC");auto*trafficLayout=new QVBoxLayout(trafficBox);trafficPanel_=new TrafficPanel;trafficPanel_->request=workroom_->request;trafficLayout->addWidget(trafficPanel_);side->addWidget(trafficBox);
    auto *faultBox=new QGroupBox("SENSOR FAULT INJECTION");auto *ff=new QVBoxLayout(faultBox);sensor_=new QComboBox;sensor_->setObjectName("faultSensor");fault_=new QComboBox;fault_->setObjectName("faultMode");fault_->addItems({"none","disconnected","stuck","bias","noisy"});ff->addWidget(sensor_);ff->addWidget(fault_);
    faultValue_=new QDoubleSpinBox;faultValue_->setObjectName("faultValue");faultValue_->setRange(-10000,10000);faultValue_->setValue(120);faultValue_->setPrefix("Value: ");faultValue_->setEnabled(false);faultValue_->setToolTip("Stuck reading or bias offset, in the selected sensor's units.");ff->addWidget(faultValue_);connect(fault_,&QComboBox::currentTextChanged,this,[this](const QString&mode){faultValue_->setEnabled(mode=="stuck"||mode=="bias");});
    auto *apply=new QPushButton("Apply fault");apply->setObjectName("applySensorFault");ff->addWidget(apply);connect(apply,&QPushButton::clicked,this,[this]{enqueue({{"action","fault"},{"sensor",sensor_->currentText().toStdString()},{"mode",fault_->currentText().toStdString()},{"value",faultValue_->value()}},[this](const Json&){log("Sensor fault applied: "+sensor_->currentText()+" / "+fault_->currentText());});});side->addWidget(faultBox);
    auto *diagBox=new QGroupBox("ISO-TP DIAGNOSTICS");auto *df=new QVBoxLayout(diagBox);diagnosticEcu_=new QComboBox;df->addWidget(diagnosticEcu_);auto *vin=new QPushButton("Read VIN  ·  22 F1 90");df->addWidget(vin);connect(vin,&QPushButton::clicked,this,[this]{enqueue({{"action","diagnostic"},{"ecu",diagnosticEcu_->currentText().toStdString()},{"bytes",{34,241,144}}},[this](const Json&r){log("ISO-TP: "+str(r));});});side->addWidget(diagBox);
    bus_=new QLabel("Waiting for the native controller…");bus_->setWordWrap(true);bus_->setObjectName("muted");side->addWidget(bus_);side->addStretch();auto *controlsScroll=new QScrollArea;controlsScroll->setWidgetResizable(true);controlsScroll->setMinimumWidth(260);controlsScroll->setWidget(left);controlsScroll->setObjectName("simulationControlsScroll");auto*leftStack=new QWidget;auto*leftLayout=new QVBoxLayout(leftStack);leftLayout->setContentsMargins(0,0,0,0);leftLayout->addWidget(health_);leftLayout->addWidget(controlsScroll,1);split->addWidget(leftStack);
    auto*rightColumn=new QWidget;auto*rightLayout=new QVBoxLayout(rightColumn);rightLayout->setContentsMargins(0,0,0,0);rightLayout->addWidget(metricWidget);auto *right=new QSplitter(Qt::Vertical);rightLayout->addWidget(right,1);split->addWidget(rightColumn);scene_=new SceneView;right->addWidget(scene_);
    auto *tabs=new QTabWidget;ecus_=table({"ECU","State","Worker PID","Link","DTCs"});frames_=table({"Bus","CAN ID","Producer","Payload","Time (s)"});performance_=table({"ECU","Task p95 µs","Delivery p95 µs","E2E p95 µs","TX","RX","Dropped"});events_=new QPlainTextEdit;events_->setReadOnly(true);events_->setMaximumBlockCount(300);
    tabs->addTab(ecus_,"ECU network");tabs->addTab(frames_,"CAN / CAN FD");tabs->addTab(performance_,"Performance");tabs->addTab(events_,"Events & diagnostics");right->addWidget(tabs);right->setSizes({430,230});split->setSizes({270,1120});
    liveInspector_=new QTreeWidget;liveInspector_->setObjectName("liveInspector");liveInspector_->setHeaderLabels({"Source / signal","Live value"});liveInspector_->setColumnWidth(0,270);tabs->addTab(liveInspector_,"All live signals");
    status_=new QLabel("Starting native runtime…");status_->setWordWrap(true);hash_=new QLabel;hash_->setObjectName("muted");hash_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);hash_->setMinimumWidth(0);layout->addWidget(status_);layout->addWidget(hash_);
    auto *errorBanner=new QLabel;errorBanner->setObjectName("sessionError");errorBanner->setWordWrap(true);errorBanner->setStyleSheet("color:#ff9292");layout->addWidget(errorBanner);
    // Wheel scrolling over a control must not silently change throttle, gear or a draft.
    for(auto *widget:findChildren<QWidget*>())if(qobject_cast<QSlider*>(widget)||qobject_cast<QComboBox*>(widget)||qobject_cast<QAbstractSpinBox*>(widget))widget->installEventFilter(this);
    if(!smokeDir_.isEmpty())navigate(2);
    connect(&process_,&QProcess::readyReadStandardOutput,this,[this]{receive();});
    connect(&process_,&QProcess::readyReadStandardError,this,[this]{log(QString::fromUtf8(process_.readAllStandardError()).trimmed());});
    connect(&process_,&QProcess::started,this,[this]{ready_=true;if(!recoveryProject_.empty())enqueue({{"action","import_project"},{"project",recoveryProject_}});enqueue({{"action","snapshot"}});enqueue({{"action","export_project"}});});
    connect(&process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){error(process_.errorString());});
    connect(&process_,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus){ready_=false;if(!closing_)error(QString("Native runtime exited (%1)").arg(code));});
    timeout_.setSingleShot(true);timeout_.setInterval(20000);connect(&timeout_,&QTimer::timeout,this,[this]{ready_=false;error("Native response timed out; restart the desktop to reconnect.");process_.closeWriteChannel();});
    connect(&timer_,&QTimer::timeout,this,[this]{try{tick();}catch(const std::exception&e){error(e.what());}});timer_.start(50);
    process_.setWorkingDirectory(root);process_.start(runtime,{"--scenario",scenario,"--realtime"});
}
DesktopWindow::~DesktopWindow(){closing_=true;timer_.stop();stopRecord();process_.closeWriteChannel();if(!process_.waitForFinished(4000)){process_.terminate();if(!process_.waitForFinished(2000)){process_.kill();process_.waitForFinished(1000);}}}
bool DesktopWindow::eventFilter(QObject*object,QEvent*event){
    if(event->type()==QEvent::Wheel){
        auto *widget=qobject_cast<QWidget*>(object);auto *wheel=static_cast<QWheelEvent*>(event);
        for(auto *parent=widget?widget->parentWidget():nullptr;parent;parent=parent->parentWidget())if(auto *scroll=qobject_cast<QScrollArea*>(parent)){
            auto *bar=(wheel->modifiers()&Qt::ShiftModifier)?scroll->horizontalScrollBar():scroll->verticalScrollBar();
            const int delta=wheel->pixelDelta().isNull()?wheel->angleDelta().y()/120*bar->singleStep()*3:wheel->pixelDelta().y();
            const int before=bar->value();bar->setValue(before-delta);if(before!=bar->value()){event->accept();return true;}
        }
        event->ignore();return true;
    }
    return QMainWindow::eventFilter(object,event);
}
void DesktopWindow::closeEvent(QCloseEvent *event){
    if((editor_->hasDrafts()||workroom_->hasDrafts())&&smokeDir_.isEmpty()&&QMessageBox::question(this,"Undeployed drafts","Close and discard undeployed ECU or topology drafts? Save program drafts or apply and save the project to retain them.",QMessageBox::Close|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Close){event->ignore();return;}
    event->accept();
}
void DesktopWindow::navigate(int index){workspaces_->setCurrentIndex(index);if(detached_[index]){detached_[index]->show();detached_[index]->raise();detached_[index]->activateWindow();}}
void DesktopWindow::switchProject(const QString&key){
    if(snapshot_.empty())return;
    if((editor_->hasDrafts()||workroom_->hasDrafts())&&QMessageBox::question(this,"Undeployed drafts","Discard undeployed drafts and switch project? The deployed project remains in this session's project list.",QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Discard)return;
    stopRecord();controlsDirty_=false;enqueue({{"action","pause"}});enqueue({{"action","export_project"}});
    Json command;QString next;
    if(key.startsWith("builtin:")){command={{"action","new_project"},{"scenario",key.mid(8).toStdString()}};next="slot:"+QString::number(nextSlot_++);}
    else{auto found=projectSlots_.find(key);if(found==projectSlots_.end()){error("Project session not found");return;}command={{"action","import_project"},{"project",found->second}};next=key;}
    enqueue(command,[this,next](const Json&){activeSlot_=next;editor_->discardDrafts();refreshProjectDraft_=true;});
}
void DesktopWindow::reconnectRuntime(){
    timer_.stop();timeout_.stop();ready_=false;playing_=false;busy_=false;controlsDirty_=false;controlWrites_=0;requests_.clear();buffer_.clear();stopRecord();
    process_.blockSignals(true);process_.closeWriteChannel();if(!process_.waitForFinished(2000)){process_.terminate();if(!process_.waitForFinished(1000)){process_.kill();process_.waitForFinished(1000);}}process_.blockSignals(false);
    process_.setWorkingDirectory(resourceRoot_);process_.start(runtimePath_,{"--scenario",scenarioPath_,"--realtime"});timer_.start(50);
    log("Reconnecting runtime; restoring last acknowledged project at tick zero. ECU drafts retained.");
}
void DesktopWindow::detachWorkspace(int index){
    if(detached_[index]){navigate(index);return;}
    auto *dialog=new QDialog(this,Qt::Window);dialog->setObjectName(QString("workspaceWindow%1").arg(index));dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle(workspaces_->tabText(index)+" · Virtual CAN Laboratory");dialog->resize(1100,830);dialog->move(pos()+QPoint(35+index*35,40+index*30));
    auto *layout=new QVBoxLayout(dialog);auto *scroll=workspaceHolders_[index]->findChild<QScrollArea*>(QString("workspaceScroll%1").arg(index));scroll->takeWidget();
    auto *banner=new QLabel(lastError_);banner->setObjectName("sessionError");banner->setWordWrap(true);banner->setStyleSheet("color:#ff9292");layout->addWidget(banner);
    auto *detachedScroll=new QScrollArea;detachedScroll->setWidgetResizable(true);detachedScroll->setWidget(workspacePages_[index]);layout->addWidget(detachedScroll);layout->addWidget(new QLabel("Shared live laboratory · close this window to return the workspace to its tab."));
    auto *placeholder=new QWidget;auto *message=new QVBoxLayout(placeholder);auto *label=new QLabel("This workspace is open in its own window.");label->setWordWrap(true);message->addWidget(label);auto *show=new QPushButton("Show workspace window");message->addWidget(show);connect(show,&QPushButton::clicked,this,[this,index]{navigate(index);});message->addStretch();scroll->setWidget(placeholder);detached_[index]=dialog;
    connect(dialog,&QDialog::finished,this,[this,index,scroll,detachedScroll](int){detachedScroll->takeWidget();auto *placeholder=scroll->takeWidget();placeholder->deleteLater();scroll->setWidget(workspacePages_[index]);detached_[index]=nullptr;});dialog->show();
}
void DesktopWindow::submitProgram(const QString&name,const Json&definition,bool deploy){
    editor_->setEnabled(false);
    if(deploy){playing_=false;play_->setText("▶  Run");controlsDirty_=false;stopRecord();enqueue({{"action","pause"}});}
    enqueue({{"action",deploy?"update_ecu":"validate_ecu_update"},{"name",name.toStdString()},{"definition",definition}},[this,name,deploy](const Json&result){
        editor_->setEnabled(true);editor_->result(deploy?"DEPLOYED · worker programs replaced · project restarted at tick 0":"VALID · full project checks passed · live workers unchanged",true,deploy,name);
        log((deploy?"ECU deployed: ":"ECU validated: ")+name+" · "+str(result.at("project_hash")));
    });
}
void DesktopWindow::flushControls(){
    if(!controlsDirty_||snapshot_.empty())return;
    controlsDirty_=false;
    Json values={{"throttle",throttle_->value()/100.0},{"brake",brake_->value()/100.0}};
    if(snapshot_.at("controls").contains("gear")){values["gear"]=gear_->currentIndex();values["steering"]=steering_->value()/100.0;}
    enqueue({{"action","controls"},{"values",values}});
}
void DesktopWindow::log(const QString &message){events_->appendPlainText(message);}
void DesktopWindow::error(const QString &message){lastError_="Last error: "+message;for(auto *label:findChildren<QLabel*>("sessionError"))label->setText(lastError_);status_->setText("ERROR · "+message);log("ERROR · "+message);workroom_->showError(message);editor_->setEnabled(true);if(current_.body.is_object()&&current_.body.value("action",std::string()).find("ecu")!=std::string::npos)editor_->result(message,false);if(!smokeDir_.isEmpty())QTimer::singleShot(0,qApp,[]{QCoreApplication::exit(1);});}
void DesktopWindow::enqueue(Json body,std::function<void(const Json&)> done){if(requests_.size()>=32){error("Command queue full; wait for the controller.");return;}if(body.value("action",std::string())=="controls")++controlWrites_;requests_.push_back({std::move(body),std::move(done)});pump();}
void DesktopWindow::pump(){if(!ready_||busy_||requests_.empty())return;current_=std::move(requests_.front());requests_.pop_front();busy_=true;roundTrip_.restart();timeout_.start();const auto bytes=QByteArray::fromStdString(current_.body.dump()+"\n");if(process_.write(bytes)!=bytes.size())error("Cannot write native command.");}
void DesktopWindow::receive(){
    buffer_+=process_.readAllStandardOutput();if(buffer_.size()>32*1024*1024){ready_=false;error("Native response exceeds 32 MiB limit.");process_.closeWriteChannel();return;}
    auto end=buffer_.indexOf('\n');if(end<0)return;auto line=buffer_.left(end);buffer_.remove(0,end+1);timeout_.stop();
    if(!busy_){error("Unexpected native response.");return;}
    if(current_.body.value("action",std::string())=="controls"&&controlWrites_>0)--controlWrites_;
    try{auto reply=Json::parse(line.toStdString());if(reply.contains("error")){
        const auto action=current_.body.value("action",std::string());
        error(str(reply.at("error")));
        // Rejections of edits do not stop a running experiment. A failed runtime
        // transition is reconciled with an authoritative snapshot and pauses pacing.
        if(action=="advance"||action=="step"||action=="run"||action=="controls"){
            playing_=false;play_->setText("▶  Run");requests_.clear();controlWrites_=0;controlsDirty_=false;
            requests_.push_back({{{"action","pause"}}, {}});
        }
    }
        else{
            if(reply.value("kind",std::string())=="vcanproj"){
                recoveryProject_=reply;scene_->setPresentation(reply.value("presentation",Json::object()));projectSlots_[activeSlot_]=reply;
                const auto label=str(reply.at("name"))+" ["+activeSlot_+"]";int index=projectPicker_->findData(activeSlot_);if(index<0){projectPicker_->addItem(label,activeSlot_);index=projectPicker_->count()-1;}else projectPicker_->setItemText(index,label);
                if(refreshProjectDraft_){workroom_->loadProjectDraft(reply);refreshProjectDraft_=false;projectPicker_->setCurrentIndex(index);}
            }
            if(reply.contains("world")&&reply.contains("ecus"))updateSnapshot(reply);
            const auto action=current_.body.value("action",std::string());
            if(action=="import_project"&&!reply.contains("world"))enqueue({{"action","snapshot"}});
            if(action=="controls"&&reply.contains("project_hash")&&!recoveryProject_.empty()&&reply.at("project_hash")!=recoveryProject_.at("project_hash"))enqueue({{"action","export_project"}});
            if(action=="apply_project_draft"||action=="update_ecu"||action=="import_project"||action=="new_project"||action=="network_policy"||action=="traffic_actor"||action=="set_traffic"||action=="set_ui_layout"||action=="set_car_mesh")enqueue({{"action","export_project"}});
            if(current_.done)current_.done(reply);
        }
    }catch(const std::exception&e){error(e.what());requests_.clear();}
    busy_=false;pump();
}
void DesktopWindow::tick(){
    if(!ready_||busy_||!requests_.empty()||snapshot_.empty())return;
    if(!smokeDir_.isEmpty()){smoke();return;}
    if(controlsDirty_)flushControls();
    else if(playing_)enqueue({{"action","snapshot"}});
}
void DesktopWindow::updateSnapshot(const Json &state){
    snapshot_=state;scene_->setSnapshot(state);workroom_->setSnapshot(state);editor_->setSnapshot(state);physics_->setSnapshot(state);health_->setSnapshot(state);trafficPanel_->setSnapshot(state);const auto &w=state.at("world");
    auto revision=str(state.at("project_hash"));if(revision!=inspectorRevision_){liveInspector_->clear();liveItems_.clear();inspectorRevision_=revision;}
    auto liveValue=[&](const QString&group,const QString&name,const Json&value){auto root=liveItems_.find(group);if(root==liveItems_.end())root=liveItems_.emplace(group,new QTreeWidgetItem(liveInspector_,{group})).first;auto key=group+"/"+name;auto item=liveItems_.find(key);if(item==liveItems_.end())item=liveItems_.emplace(key,new QTreeWidgetItem(root->second,{name})).first;auto text=str(value);if(item->second->text(1)!=text)item->second->setText(1,text);item->second->setToolTip(1,text);};
    for(const auto&v:state.at("world").items())liveValue("Plant ground truth",QString::fromStdString(v.key()),v.value());
    for(const auto&v:state.at("controls").items())liveValue("Acknowledged controls",QString::fromStdString(v.key()),v.value());
    for(const auto&s:state.at("sensors"))liveValue("Sampled sensors",str(s.at("name")),{{"value",s.at("measured")},{"unit",s.value("unit",Json(""))},{"validity",s.value("validity",Json("unknown"))},{"age_us",s.value("data_age_us",Json(0))}});
    for(const auto&e:state.at("ecus")){auto group=str(e.at("name"));for(const auto&v:e.at("values").items())liveValue(group+" / variables",QString::fromStdString(v.key()),v.value());for(const auto&h:e.at("hardware").items())liveValue(group+" / hardware",QString::fromStdString(h.key()),h.value());}
    if(state.contains("cluster")&&!state.at("cluster").at("last_error").is_null()){
        const auto message=str(state.at("cluster").at("last_error"));if(lastError_!="Last error: "+message)error(message);
    }
    playing_=state.value("running",false);play_->setText(playing_?"Ⅱ  Pause":"▶  Run");
    findChild<QLabel*>("simulationState")->setText(QString("%1   ·   Tick %2   ·   Applied gear %3").arg(playing_?"RUNNING":"PAUSED",str(state.at("tick")),str(w.value("gear_position",Json("—")))));
    speed_->setText(QString::number(w.value("speed",0.0)*3.6,'f',1)+" km/h");battery_->setText(QString::number(w.value("soc",0.0),'f',1)+" %");clock_->setText(QString::number(state.value("time",0.0),'f',2)+" s");traffic_->setText(str(state.at("frame_count")));
    const auto &cluster=state.at("cluster");status_->setText(QString("%1 / %2 ECU workers connected   ·   Epoch %3   ·   Tick %4   ·   Host response %5 ms").arg(str(cluster.at("connected")),str(cluster.at("required")),str(state.at("epoch")),str(state.at("tick"))).arg(roundTrip_.elapsed()));
    hash_->setText("PROJECT  "+str(state.at("project_hash")));hash_->setToolTip(hash_->text());
    QString info="BUS HEALTH\n";for(const auto &b:state.at("buses"))info+=QString("%1: %2% load\n").arg(str(b.at("id"))).arg(b.value("load_percent",0.0),0,'f',1);
    info+=QString("Queued %1  ·  Dropped %2\n\nPerspective visualization of the educational plant. No esmini world attached.").arg(str(state.at("queued_frames")),str(state.at("dropped_frames")));bus_->setText(info);
    if(!controlsDirty_&&controlWrites_==0){const auto &c=state.at("controls");QSignalBlocker a(throttle_),b(brake_),d(steering_),g(gear_);throttle_->setValue(qRound(c.value("throttle",0.0)*100));brake_->setValue(qRound(c.value("brake",0.0)*100));steering_->setValue(qRound(c.value("steering",0.0)*100));gear_->setCurrentIndex(int(c.value("gear",0.0)));gear_->setEnabled(c.contains("gear"));steering_->setEnabled(c.contains("steering"));}
    QStringList sensorNames,ecuNames;for(const auto &s:state.at("sensors"))sensorNames<<str(s.at("name"));for(const auto &e:state.at("ecus"))ecuNames<<str(e.at("name"));
    auto sync=[](QComboBox *box,const QStringList&items){QStringList old;for(int i=0;i<box->count();++i)old<<box->itemText(i);if(old!=items){auto chosen=box->currentText();box->clear();box->addItems(items);if(items.contains(chosen))box->setCurrentText(chosen);}};sync(sensor_,sensorNames);sync(diagnosticEcu_,ecuNames);
    ecus_->setRowCount(int(state.at("ecus").size()));int row=0;for(const auto &ecu:state.at("ecus")){auto node=ecu.value("container",Json::object());cell(ecus_,row,0,str(ecu.at("name")));cell(ecus_,row,1,str(ecu.at("state")));cell(ecus_,row,2,str(node.value("kernel_pid",Json("—"))));cell(ecus_,row,3,str(node.value("status",Json("unknown"))));cell(ecus_,row,4,QString::number(ecu.at("dtcs").size()));ecus_->item(row,4)->setToolTip(str(ecu.at("dtcs")));++row;}
    const auto &frames=state.at("frames");int count=std::min(60,int(frames.size()));frames_->setRowCount(count);for(int i=0;i<count;++i){const auto&f=frames[frames.size()-1-i];cell(frames_,i,0,str(f.value("bus",Json("—")))+(f.value("fd",false)?" FD":""));cell(frames_,i,1,QString("0x%1").arg(f.value("id",0U),3,16,QChar('0')));cell(frames_,i,2,str(f.value("ecu",Json("—"))));cell(frames_,i,3,str(f.value("bytes",Json("—"))));cell(frames_,i,4,QString::number(f.value("time",0.0),'f',6));}
    const auto &perf=state.at("performance").at("ecus");performance_->setRowCount(int(perf.size()));row=0;for(const auto &ecu:perf){cell(performance_,row,0,str(ecu.at("name")));int col=1;for(const auto*key:{"task","delivery","e2e"})cell(performance_,row,col++,QString::number(ecu.at(key).value("p95",0.0),'f',1));for(const auto*key:{"tx","rx","dropped"})cell(performance_,row,col++,str(ecu.at(key)));++row;}
    throttle_->setToolTip(QString::number(throttle_->value())+"%");brake_->setToolTip(QString::number(brake_->value())+"%");steering_->setToolTip(QString::number(steering_->value())+"%");
    if(recording_.isOpen()){auto compact=state.at("performance");for(auto&e:compact["ecus"])e.erase("timeline");Json sample={{"kind","telemetry_sample"},{"tick",state.at("tick")},{"epoch",state.at("epoch")},{"time",state.at("time")},{"project_hash",state.at("project_hash")},{"world",state.at("world")},{"controls",state.at("controls")},{"performance",compact}};auto data=QByteArray::fromStdString(sample.dump()+"\n");if(recording_.write(data)!=data.size()||!recording_.flush()){stopRecord();error("Telemetry recording failed; recording stopped.");}}
}
void DesktopWindow::toggleRecord(){if(recording_.isOpen()){stopRecord();return;}auto path=QFileDialog::getSaveFileName(this,"Record sampled telemetry","telemetry.jsonl","JSON lines (*.jsonl)");if(!path.isEmpty()){try{startRecord(path);}catch(const std::exception&e){error(e.what());}}}
void DesktopWindow::startRecord(const QString &path){recording_.setFileName(path);if(!recording_.open(QIODevice::WriteOnly|QIODevice::NewOnly))throw std::runtime_error("Choose a new writable telemetry filename.");record_->setText("Stop recording");log("Recording displayed snapshots to "+path+" (sampled telemetry, not full event replay).");}
void DesktopWindow::stopRecord(){if(recording_.isOpen()){recording_.close();if(record_)record_->setText("Record telemetry");}}
void DesktopWindow::smoke(){
    // Deterministic UI automation uses exactly the same async controller path as controls.
    if(smokeStage_==0){++smokeStage_;startRecord(smokeDir_+"/telemetry.jsonl");enqueue({{"action","controls"},{"values",{{"gear",2},{"throttle",0.55},{"steering",0.18}}}});return;}
    if(smokeStage_<=35){++smokeStage_;enqueue({{"action","step"},{"count",10}},[this](const Json&s){smokePeak_=std::max(smokePeak_,s.at("world").value("speed",0.0));smokeTurn_=std::max(smokeTurn_,std::abs(s.at("world").value("y",0.0)));});return;}
    if(smokeStage_==36){++smokeStage_;if(!grab().save(smokeDir_+"/desktop-driving.png"))throw std::runtime_error("Screenshot write failed");enqueue({{"action","diagnostic"},{"ecu","Motor ECU"},{"bytes",{34,241,144}}},[this](const Json&r){if(r.at("bytes").size()!=20)throw std::runtime_error("VIN check failed");saveJson(smokeDir_+"/diagnostic.json",r);});return;}
    if(smokeStage_==37){++smokeStage_;enqueue({{"action","export_project"}},[this](const Json&r){saveJson(smokeDir_+"/demo.vcanproj",r);});return;}
    if(smokeStage_==38){++smokeStage_;enqueue({{"action","controls"},{"values",{{"throttle",0.0},{"brake",1.0},{"steering",0.0}}}});return;}
    if(smokeStage_<=65){++smokeStage_;enqueue({{"action","step"},{"count",10}});return;}
    finishSmoke();
}
void DesktopWindow::finishSmoke(){
    timer_.stop();stopRecord();auto report=Json{{"controller","native-cpp"},{"worker_transport","native-process"},{"peak_speed_mps",smokePeak_},{"max_abs_y_m",smokeTurn_},{"final_speed_mps",snapshot_.at("world").at("speed")},{"ticks",snapshot_.at("tick")},{"connected_ecus",snapshot_.at("cluster").at("connected")},{"project_hash",snapshot_.at("project_hash")},{"frames",snapshot_.at("frame_count")},{"world_backend","synthetic perspective; no esmini"}};
    const bool pass=smokePeak_>2&&smokeTurn_>.5&&snapshot_.at("world").value("speed",99.0)<.1&&snapshot_.at("cluster").at("connected")==9;
    report["passed"]=pass;saveJson(smokeDir_+"/summary.json",report);saveJson(smokeDir_+"/final-snapshot.json",snapshot_);grab().save(smokeDir_+"/desktop-stopped.png");QCoreApplication::exit(pass?0:1);
}
