#include "Workroom.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QGraphicsTextItem>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QPainter>
#include <QDialog>
#include <QInputDialog>
#include <QGraphicsSceneMouseEvent>
#include <QTextCursor>
#include <set>
using Json=nlohmann::json;
namespace {
class EcuNode:public QGraphicsRectItem{
public:using QGraphicsRectItem::QGraphicsRectItem;
    std::function<void()> moved;
    std::function<void()> reposition;
    QVariant itemChange(GraphicsItemChange change,const QVariant&value)override{auto result=QGraphicsRectItem::itemChange(change,value);if(change==ItemPositionHasChanged&&reposition)reposition();return result;}
    void mouseReleaseEvent(QGraphicsSceneMouseEvent*event)override{QGraphicsRectItem::mouseReleaseEvent(event);if(moved)moved();}
};
class GraphView:public QGraphicsView{
public:using QGraphicsView::QGraphicsView;
    void wheelEvent(QWheelEvent *e)override{if(e->modifiers()&Qt::ControlModifier){const double zoom=transform().m11()*(e->angleDelta().y()>0?1.15:1/1.15);if(zoom>=.35&&zoom<=2.5)scale(e->angleDelta().y()>0?1.15:1/1.15,e->angleDelta().y()>0?1.15:1/1.15);e->accept();}else QGraphicsView::wheelEvent(e);}
};
}
Workroom::Workroom(QWidget*parent):QWidget(parent){
    setObjectName("workroomPage");auto *layout=new QVBoxLayout(this);auto *title=new QLabel("ECU WORKROOM  /  NETWORK SCHEMA");title->setObjectName("title");layout->addWidget(title);
    auto *description=new QLabel("Select a live ECU in the schema to inspect its inputs, CAN messages and actuator bindings. Open its program, halt/restore its worker, or change its traffic policy.");description->setWordWrap(true);layout->addWidget(description);
    auto *bar=new QHBoxLayout;
    auto button=[&](const QString&text,auto fn){auto*b=new QPushButton(text);bar->addWidget(b);connect(b,&QPushButton::clicked,this,fn);return b;};
    button("Zoom +",[this]{if(view_->transform().m11()<2.2)view_->scale(1.2,1.2);});button("Zoom −",[this]{if(view_->transform().m11()>.4)view_->scale(1/1.2,1/1.2);});button("Fit schema",[this]{view_->fitInView(graph_->sceneRect(),Qt::KeepAspectRatio);});button("100%",[this]{view_->resetTransform();});auto *editTopologyButton=button("Edit topology",[this]{editTopology();});editTopologyButton->setObjectName("editTopology");bar->addStretch();layout->addLayout(bar);
    auto *split=new QSplitter;layout->addWidget(split,1);graph_=new QGraphicsScene(this);view_=new GraphView(graph_);view_->setObjectName("ecuSchema");view_->setRenderHint(QPainter::Antialiasing);view_->setDragMode(QGraphicsView::ScrollHandDrag);view_->setBackgroundBrush(QColor("#0b1622"));view_->setMinimumSize(450,350);split->addWidget(view_);
    auto *panel=new QWidget;auto *side=new QVBoxLayout(panel);selected_=new QComboBox;selected_->setObjectName("schemaEcu");side->addWidget(selected_);
    auto *edit=new QPushButton("Open ECU program →");edit->setObjectName("openEcuProgram");side->addWidget(edit);connect(edit,&QPushButton::clicked,this,[this]{if(program)program(selected_->currentText());});
    auto *workerButtons=new QHBoxLayout;auto *halt=new QPushButton("Halt worker"),*restore=new QPushButton("Restore worker");halt->setObjectName("haltWorker");restore->setObjectName("restoreWorker");workerButtons->addWidget(halt);workerButtons->addWidget(restore);side->addLayout(workerButtons);
    connect(halt,&QPushButton::clicked,this,[this]{if(command)command({{"action","node_fault"},{"name",selected_->currentText().toStdString()},{"mode","halt"}});});connect(restore,&QPushButton::clicked,this,[this]{if(command)command({{"action","node_fault"},{"name",selected_->currentText().toStdString()},{"mode","restore"}});});
    side->addWidget(new QLabel("OUTGOING CAN TRAFFIC"));latency_=new QDoubleSpinBox;latency_->setObjectName("policyLatency");latency_->setRange(0,1000);latency_->setSuffix(" ms added delay");side->addWidget(latency_);drop_=new QCheckBox("Drop this ECU's outgoing frames");drop_->setObjectName("policyDrop");side->addWidget(drop_);
    auto *apply=new QPushButton("Apply traffic policy");apply->setObjectName("applyTrafficPolicy");side->addWidget(apply);connect(apply,&QPushButton::clicked,this,[this]{policy(false);});auto *clear=new QPushButton("Clear this ECU's traffic rules");clear->setObjectName("clearTrafficPolicy");side->addWidget(clear);connect(clear,&QPushButton::clicked,this,[this]{policy(true);});
    policyStatus_=new QLabel;policyStatus_->setWordWrap(true);side->addWidget(policyStatus_);details_=new QPlainTextEdit;details_->setReadOnly(true);details_->setMinimumSize(280,260);details_->setObjectName("ecuDetails");side->addWidget(details_,1);
    auto *scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setWidget(panel);split->addWidget(scroll);split->setSizes({900,350});
    connect(selected_,&QComboBox::currentTextChanged,this,[this]{inspect();});connect(graph_,&QGraphicsScene::selectionChanged,this,[this]{auto items=graph_->selectedItems();if(!items.empty())selected_->setCurrentText(items.front()->data(0).toString());});
}
void Workroom::setSnapshot(const Json&s){state_=s;QStringList names;for(const auto&e:s.at("ecus"))names<<QString::fromStdString(e.at("name"));QStringList existing;for(int i=0;i<selected_->count();++i)existing<<selected_->itemText(i);
    if(names!=existing){QSignalBlocker block(selected_);auto chosen=selected_->currentText();selected_->clear();selected_->addItems(names);if(names.contains(chosen))selected_->setCurrentText(chosen);}
    auto key=QString::fromStdString(s.at("project_hash"))+QString::fromStdString(s.value("presentation",Json::object()).dump());if(key!=graphKey_){graphKey_=key;rebuild();}
    for(const auto&e:s.at("ecus")){auto name=QString::fromStdString(e.at("name"));auto worker=e.at("container");auto status=worker.value("status",std::string("unknown"));auto content=QString("%1\n%2 · PID %3\n%4 tasks   |   %5 CAN messages").arg(name,QString::fromStdString(e.at("state"))).arg(worker.value("kernel_pid",0)).arg(e.at("definition").at("tasks").size()).arg(e.at("definition").at("messages").size());labels_.at(name)->setPlainText(content);nodes_.at(name)->setPen(QPen(status=="connected"?QColor("#4dbda9"):QColor("#ff7676"),2));}
    inspect();
}
void Workroom::rebuild(){
    QSignalBlocker block(graph_);graph_->clear();labels_.clear();nodes_.clear();const int count=state_.at("ecus").size();const double width=1180,height=280+((count+2)/3)*200;graph_->setSceneRect(0,0,width,height);
    auto *heading=graph_->addText("ONE C++ CONTROLLER  /  CLOCK + BROKER + AUTHORITATIVE PLANT",QFont("Sans",13,QFont::Bold));heading->setDefaultTextColor(QColor("#dce8f0"));heading->setPos(30,15);
    std::map<std::string,double> buses;int b=0;for(const auto&bus:state_.at("buses")){double y=85+b++*55;buses[bus.at("id")]=y;QColor color=bus.value("fd",false)?QColor("#b18aff"):QColor("#54d7c2");graph_->addLine(25,y,width-25,y,QPen(color,4));auto *label=graph_->addText(QString::fromStdString(bus.at("name"))+QString(" · %1 kbit/s%2").arg(bus.value("bitrate",500000)/1000).arg(bus.value("fd",false)?QString(" / %1 kbit/s data").arg(bus.value("data_bitrate",2000000)/1000):""));label->setDefaultTextColor(color);label->setPos(25,y-31);}
    int index=0;for(const auto&e:state_.at("ecus")){auto name=QString::fromStdString(e.at("name"));const auto&d=e.at("definition");double x=40+(index%3)*380,y=std::max(240.0,140.0+(double(buses.size())-1)*55)+(index/3)*200;int nodeIndex=index++;
        std::vector<std::tuple<QGraphicsPathItem*,double,double,double>> wires;
        std::set<std::string> used;for(const auto&m:d.at("messages"))used.insert(m.value("bus","powertrain"));int port=0;for(const auto&bus:used){if(!buses.contains(bus))continue;double lane=25+nodeIndex*38+(port++)*12;QPainterPath path;path.moveTo(x+10,y+24+port*16);path.lineTo(lane,y+24+port*16);path.lineTo(lane,buses.at(bus));auto *wire=graph_->addPath(path,QPen(bus=="adas"?QColor("#8660bc"):QColor("#337f78"),1.5));wire->setZValue(-1);wires.emplace_back(wire,lane,buses.at(bus),24+port*16);}
        auto *node=new EcuNode(x,y,325,120);graph_->addItem(node);node->setPen(QPen(QColor("#4dbda9"),2));node->setBrush(QColor("#1b3042"));node->setFlag(QGraphicsItem::ItemIsSelectable);node->setFlag(QGraphicsItem::ItemIsMovable);node->setData(0,name);node->moved=[this]{saveLayout();};auto *label=new QGraphicsTextItem(node);label->setPos(x+14,y+14);label->setDefaultTextColor(QColor("#dce8f0"));label->setFont(QFont("Sans",11));label->setAcceptedMouseButtons(Qt::NoButton);nodes_[name]=node;labels_[name]=label;
        node->setFlag(QGraphicsItem::ItemSendsGeometryChanges);node->reposition=[node,wires,x,y]{for(const auto&[wire,lane,busY,portY]:wires){auto offset=node->pos();QPainterPath path;path.moveTo(x+10+offset.x(),y+portY+offset.y());path.lineTo(lane,y+portY+offset.y());path.lineTo(lane,busY);wire->setPath(path);}};
        auto offsets=state_.value("presentation",Json::object()).value("node_offsets",Json::object());if(offsets.contains(name.toStdString())){auto point=offsets.at(name.toStdString());node->setPos(point.at(0).get<double>(),point.at(1).get<double>());}
        auto *binding=new QGraphicsTextItem(node);binding->setPlainText(QString("%1 actuator bindings").arg(d.at("bindings").size()));binding->setDefaultTextColor(QColor("#8da4b5"));binding->setPos(x+10,y+125);
    }
}
void Workroom::inspect(){if(state_.empty())return;const auto name=selected_->currentText().toStdString();for(const auto&e:state_.at("ecus"))if(e.at("name")==name){const auto&d=e.at("definition");QString text="INPUTS\n"+QString::fromStdString(d.at("inputs").dump(2))+"\n\nACTUATOR BINDINGS\n"+QString::fromStdString(d.at("bindings").dump(2))+"\n\nCAN MESSAGES\n";for(const auto&m:d.at("messages"))text+=QString("%1  0x%2  %3\n  %4 · %5 signals\n").arg(QString::fromStdString(m.at("direction"))).arg(m.at("id").get<unsigned>(),3,16,QChar('0')).arg(QString::fromStdString(m.at("name")),QString::fromStdString(m.value("bus","powertrain"))).arg(m.at("signals").size());
        // Preserve the reader's scroll position during live updates.
        if(details_->toPlainText()!=text)details_->setPlainText(text);
        break;}
    QString text="Active traffic rules: ";int count=0;for(const auto&r:state_.at("policies"))if(r.value("source",std::string())==name){++count;text+=QString::fromStdString(r.dump())+" ";}policyStatus_->setText(count?text:"No traffic rule for this ECU.");
}
void Workroom::policy(bool clear){if(!command||state_.empty())return;auto rules=Json::array();const auto name=selected_->currentText().toStdString();for(const auto&r:state_.at("policies"))if(r.value("source",std::string())!=name)rules.push_back(r);if(!clear)rules.push_back({{"source",name},{"latency_ms",latency_->value()},{"drop",drop_->isChecked()}});command({{"action","network_policy"},{"rules",rules}});}

void Workroom::showError(const QString&message){policyStatus_->setText("ERROR: "+message);if(projectStatus_)projectStatus_->setText("ERROR: "+message);}
bool Workroom::hasDrafts()const{return projectCode_&&projectCode_->toPlainText()!=projectBase_;}
void Workroom::loadProjectDraft(const Json&p){if(projectCode_){replaceDraft(p);projectBase_=projectCode_->toPlainText();}}
void Workroom::replaceDraft(const Json&p){auto cursor=projectCode_->textCursor();cursor.beginEditBlock();cursor.select(QTextCursor::Document);cursor.insertText(QString::fromStdString(p.dump(2)));cursor.endEditBlock();cursor.movePosition(QTextCursor::Start);projectCode_->setTextCursor(cursor);}
void Workroom::saveLayout(){if(!command)return;Json positions=Json::object();for(const auto&[name,node]:nodes_)positions[name.toStdString()]={node->pos().x(),node->pos().y()};if(positions==state_.value("presentation",Json::object()).value("node_offsets",Json::object()))return;command({{"action","set_ui_layout"},{"positions",positions}});}
void Workroom::editTopology(){
    if(projectWindow_){projectWindow_->show();projectWindow_->raise();return;}
    projectWindow_=new QDialog(this,Qt::Window);projectWindow_->setWindowTitle("Editable project topology · Virtual CAN");projectWindow_->resize(1100,800);
    auto *layout=new QVBoxLayout(projectWindow_);auto *help=new QLabel("Load the current project, edit its topology, validate, then apply. Applying pauses and restarts the experiment. JSON includes buses, gateway routes, ECU definitions and messages. Save the project from the main window after deployment.");help->setWordWrap(true);layout->addWidget(help);
    auto *bar=new QHBoxLayout;layout->addLayout(bar);projectCode_=new QPlainTextEdit;projectCode_->setObjectName("topologyCode");projectCode_->setLineWrapMode(QPlainTextEdit::NoWrap);layout->addWidget(projectCode_,1);projectStatus_=new QLabel("Load the current project to begin.");projectStatus_->setWordWrap(true);layout->addWidget(projectStatus_);
    auto button=[&](const QString&text,const QString&name,auto fn){auto*b=new QPushButton(text);b->setObjectName(name);bar->addWidget(b);connect(b,&QPushButton::clicked,this,[this,fn]{try{fn();}catch(const std::exception&e){showError(e.what());}});};
    button("Load current","loadTopology",[this]{if(request)request({{"action","export_project"}},[this](const Json&p){replaceDraft(p);projectBase_=projectCode_->toPlainText();projectStatus_->setText("Loaded current project. Changes below are drafts until applied.");});});
    button("Add ECU","addTopologyEcu",[this]{
        auto p=Json::parse(projectCode_->toPlainText().toStdString());bool ok=false;auto name=QInputDialog::getText(projectWindow_,"Add ECU","Unique ECU name",QLineEdit::Normal,"Custom ECU",&ok);if(!ok||name.trimmed().isEmpty())return;
        std::set<unsigned> ids;for(const auto&d:p.at("ecu_definitions")){if(d.at("name")==name.toStdString())throw std::invalid_argument("ECU name already exists");for(const auto&m:d.at("messages"))if(m.at("direction")=="tx")ids.insert(m.at("id").get<unsigned>());}
        unsigned id=0x600;while(ids.contains(id)&&id<0x700)++id;if(id==0x700)throw std::invalid_argument("No free template message ID");
        auto key="desktop/custom_"+std::to_string(id)+".json";const auto bus=p.at("scenario").at("buses").at(0).at("id");
        Json d={{"schema_version",1},{"name",name.toStdString()},{"inputs",Json::array()},{"variables",{{"counter",0}}},{"tasks",Json::array({{{"name","count"},{"period_ms",10},{"cost_us",10},{"assign",{{"counter",{{"op","add"},{"args",{"counter",1}}}}}}}})},{"bindings",Json::object()},{"messages",Json::array({{{"name","CounterStatus"},{"id",id},{"direction","tx"},{"period_ms",10},{"dlc",8},{"bus",bus},{"signals",Json::array({{{"name","counter"},{"start",0},{"length",16},{"factor",1},{"offset",0},{"min",0},{"max",65535},{"unit","count"}}})}}})},{"state_machine",{{"initial","RUN"},{"states",{"RUN"}},{"transitions",Json::array()}}},{"dtcs",Json::array()}};
        p["ecu_definitions"][key]=d;p["scenario"]["ecus"].push_back(key);replaceDraft(p);projectStatus_->setText("Added a counter ECU draft with a free CAN ID. Validate and apply to start its worker.");
    });
    button("Remove selected ECU","removeTopologyEcu",[this]{auto p=Json::parse(projectCode_->toPlainText().toStdString());std::string key;for(const auto&e:p.at("ecu_definitions").items())if(e.value().at("name")==selected_->currentText().toStdString())key=e.key();if(key.empty())throw std::invalid_argument("Selected ECU is not in this draft");p["ecu_definitions"].erase(key);auto files=Json::array();for(const auto&file:p.at("scenario").at("ecus"))if(file!=key)files.push_back(file);p["scenario"]["ecus"]=files;replaceDraft(p);projectStatus_->setText("Selected ECU removed from draft. Validate before applying.");});
    button("Validate","validateTopology",[this]{auto p=Json::parse(projectCode_->toPlainText().toStdString());if(request)request({{"action","validate_project_draft"},{"project",p}},[this](const Json&){projectStatus_->setText("VALID · project and bus references checked; live workers unchanged.");});});
    button("Apply & restart","applyTopology",[this]{auto p=Json::parse(projectCode_->toPlainText().toStdString());if(request)request({{"action","apply_project_draft"},{"project",p}},[this](const Json&){projectBase_=projectCode_->toPlainText();projectStatus_->setText("APPLIED · new workers and topology active at tick zero.");});});
    auto *networkBar=new QHBoxLayout;layout->insertLayout(2,networkBar);
    auto networkButton=[&](const QString&text,auto fn){auto*b=new QPushButton(text);b->setObjectName(text);networkBar->addWidget(b);connect(b,&QPushButton::clicked,this,[this,fn]{try{fn();}catch(const std::exception&e){showError(e.what());}});};
    networkButton("Add CAN bus",[this]{auto p=Json::parse(projectCode_->toPlainText().toStdString());bool ok=false;auto id=QInputDialog::getText(projectWindow_,"New bus","Unique bus identifier",QLineEdit::Normal,"custom",&ok).trimmed();if(!ok||id.isEmpty())return;for(const auto&b:p["scenario"]["buses"])if(b.at("id")==id.toStdString())throw std::invalid_argument("Bus identifier already exists");auto mode=QInputDialog::getItem(projectWindow_,"Bus mode","CAN capability",{"Classic CAN","CAN FD"},0,false,&ok);if(!ok)return;const bool fd=mode=="CAN FD";p["scenario"]["buses"].push_back({{"id",id.toStdString()},{"name",id.toStdString()},{"bitrate",500000},{"data_bitrate",fd?2000000:500000},{"fd",fd}});replaceDraft(p);});
    networkButton("Connect selected ECU message",[this]{auto p=Json::parse(projectCode_->toPlainText().toStdString());Json *definition=nullptr;for(auto &d:p["ecu_definitions"])if(d.at("name")==selected_->currentText().toStdString())definition=&d;if(!definition)throw std::invalid_argument("Select a live ECU present in the draft");QStringList messages,buses;for(const auto&m:definition->at("messages"))messages<<QString::fromStdString(m.at("name"));for(const auto&b:p["scenario"]["buses"])buses<<QString::fromStdString(b.at("id"));if(messages.empty()||buses.empty())throw std::invalid_argument("ECU needs a message and project needs a bus");bool ok=false;auto message=QInputDialog::getItem(projectWindow_,"Connect message","Message",messages,0,false,&ok);if(!ok)return;auto bus=QInputDialog::getItem(projectWindow_,"Connect message","Destination bus",buses,0,false,&ok);if(!ok)return;for(auto &m:(*definition)["messages"])if(m.at("name")==message.toStdString())m["bus"]=bus.toStdString();replaceDraft(p);projectStatus_->setText("Connection changed in draft. Validation checks receiver dependencies before deployment.");});
    networkButton("Remove unused bus",[this]{auto p=Json::parse(projectCode_->toPlainText().toStdString());QStringList names;for(const auto&b:p["scenario"]["buses"])names<<QString::fromStdString(b.at("id"));bool ok=false;auto id=QInputDialog::getItem(projectWindow_,"Remove bus","Bus",names,0,false,&ok);if(!ok)return;for(const auto&d:p["ecu_definitions"])for(const auto&m:d.at("messages"))if(m.value("bus","powertrain")==id.toStdString())throw std::invalid_argument("Disconnect all messages before removing this bus");for(const auto&r:p["scenario"].value("gateway_routes",Json::array()))if(r.at("from")==id.toStdString()||r.at("to")==id.toStdString())throw std::invalid_argument("Remove gateway routes before removing this bus");auto remaining=Json::array();for(const auto&b:p["scenario"]["buses"])if(b.at("id")!=id.toStdString())remaining.push_back(b);p["scenario"]["buses"]=remaining;replaceDraft(p);});networkButton("Undo draft",[this]{projectCode_->undo();});networkButton("Redo draft",[this]{projectCode_->redo();});networkBar->addStretch();
    projectWindow_->show();if(request)request({{"action","export_project"}},[this](const Json&p){replaceDraft(p);projectBase_=projectCode_->toPlainText();});
}
