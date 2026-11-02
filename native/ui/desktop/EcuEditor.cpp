#include "EcuEditor.hpp"
#include <QComboBox>
#include <QFileDialog>
#include <QFile>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QScrollArea>

using Json = nlohmann::json;
EcuEditor::EcuEditor(QWidget *parent) : QWidget(parent) {
    setObjectName("programmingPage");
    auto *layout=new QVBoxLayout(this);
    auto *title=new QLabel("ECU PROGRAMMING");title->setObjectName("title");layout->addWidget(title);
    auto *description=new QLabel("Edit the executable ECU definition: task expressions, state transitions, CAN messages, actuator bindings and hardware budgets. Deploy restarts this project at tick 0 with fresh workers.");description->setWordWrap(true);layout->addWidget(description);
    auto *bar=new QHBoxLayout;selector_=new QComboBox;selector_->setObjectName("programEcu");bar->addWidget(selector_,1);
    auto add=[&](const QString&text,const QString&name,auto fn){auto*b=new QPushButton(text);b->setObjectName(name);bar->addWidget(b);connect(b,&QPushButton::clicked,this,fn);};
    add("Validate","validateProgram",[this]{send(false);});add("Deploy & restart","deployProgram",[this]{send(true);});
    layout->addLayout(bar);
    auto *files=new QHBoxLayout;
    auto fileButton=[&](const QString&text,auto fn){auto*b=new QPushButton(text);files->addWidget(b);connect(b,&QPushButton::clicked,this,fn);};
    fileButton("Load JSON",[this]{auto path=QFileDialog::getOpenFileName(this,"Load ECU draft",{},"JSON (*.json)");if(path.isEmpty())return;QFile f(path);if(!f.open(QIODevice::ReadOnly)||f.size()>2*1024*1024){result("Cannot read draft (2 MiB limit).",false);return;}code_->setPlainText(QString::fromUtf8(f.readAll()));});
    fileButton("Save draft",[this]{auto path=QFileDialog::getSaveFileName(this,"Save ECU draft",selected_+".json","JSON (*.json)");if(path.isEmpty())return;QSaveFile f(path);auto data=code_->toPlainText().toUtf8();if(!f.open(QIODevice::WriteOnly)||f.write(data)!=data.size()||!f.commit())result("Cannot save draft.",false);else result("Draft saved. Deploy to change the running ECU.",true);});
    fileButton("Format JSON",[this]{try{code_->setPlainText(QString::fromStdString(Json::parse(code_->toPlainText().toStdString()).dump(2)));inspect();}catch(const std::exception&e){result(e.what(),false);}});
    fileButton("Reload live program",[this]{drafts_.erase(selected_);load(selected_);result("Loaded the current worker program.",true);});files->addStretch();layout->addLayout(files);
    auto *split=new QSplitter;layout->addWidget(split,1);
    code_=new QPlainTextEdit;code_->setObjectName("programCode");code_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));code_->setLineWrapMode(QPlainTextEdit::NoWrap);code_->setTabStopDistance(28);code_->setMinimumSize(360,330);split->addWidget(code_);
    auto *inspector=new QWidget;auto *side=new QVBoxLayout(inspector);auto *hardware=new QGroupBox("HARDWARE DRAFT");auto *form=new QFormLayout(hardware);
    auto number=[&](const QString&name,int max){auto*n=new QSpinBox;n->setRange(1,max);form->addRow(name,n);return n;};cpu_=number("CPU MHz",2000);ram_=number("RAM KiB",1048576);flash_=number("Flash KiB",1048576);
    auto *apply=new QPushButton("Write hardware into draft");form->addRow(apply);connect(apply,&QPushButton::clicked,this,[this]{try{auto d=Json::parse(code_->toPlainText().toStdString());d["hardware"]["cpu_mhz"]=cpu_->value();d["hardware"]["ram_kib"]=ram_->value();d["hardware"]["flash_kib"]=flash_->value();code_->setPlainText(QString::fromStdString(d.dump(2)));inspect();result("Hardware fields updated in draft. Validate, then deploy.",true);}catch(const std::exception&e){result(e.what(),false);}});side->addWidget(hardware);
    auto *note=new QLabel("The runtime JSON language executes in C++ workers. Hardware values are modeled budgets; resource-v1 enables cross-tick scheduling. This editor does not compile arbitrary C/C++ firmware.");note->setWordWrap(true);side->addWidget(note);
    structure_=new QTreeWidget;structure_->setHeaderLabels({"Program structure","Value"});structure_->setMinimumSize(250,260);side->addWidget(structure_,1);
    auto *scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setWidget(inspector);split->addWidget(scroll);split->setSizes({760,330});
    draftState_=new QLabel;draftState_->setObjectName("draftState");layout->addWidget(draftState_);feedback_=new QLabel("Select an ECU to inspect its live program.");feedback_->setObjectName("programFeedback");feedback_->setWordWrap(true);layout->addWidget(feedback_);
    connect(selector_,&QComboBox::currentTextChanged,this,[this](const QString&name){load(name);});
    connect(code_,&QPlainTextEdit::textChanged,this,[this]{if(selected_.isEmpty())return;drafts_[selected_]=code_->toPlainText();draftState_->setText(drafts_[selected_]==live_[selected_]?"Live program · no pending edits":"DRAFT · not deployed · retained when switching ECU or workspace");});
}
void EcuEditor::setSnapshot(const Json &s){
    std::map<QString,QString> next;QStringList names;
    for(const auto&e:s.at("ecus")){auto name=QString::fromStdString(e.at("name"));names<<name;next[name]=QString::fromStdString(e.at("definition").dump(2));}
    const bool selectedChanged=next.contains(selected_)&&(!live_.contains(selected_)||next[selected_]!=live_[selected_]);
    live_=std::move(next);QStringList current;for(int i=0;i<selector_->count();++i)current<<selector_->itemText(i);
    if(current!=names){QSignalBlocker block(selector_);selector_->clear();selector_->addItems(names);if(names.contains(selected_))selector_->setCurrentText(selected_);}
    if(selected_.isEmpty()||!live_.contains(selected_))load(selector_->currentText());
    else if(selectedChanged&&!drafts_.contains(selected_))load(selected_);
}
void EcuEditor::selectEcu(const QString &name){selector_->setCurrentText(name);}
void EcuEditor::load(const QString&name){
    selected_=name;if(!live_.contains(name))return;QSignalBlocker block(code_);code_->setPlainText(drafts_.contains(name)?drafts_[name]:live_[name]);
    draftState_->setText(code_->toPlainText()==live_[name]?"Live program · no pending edits":"DRAFT · not deployed · retained when switching ECU or workspace");inspect();
}
void EcuEditor::inspect(){try{
    auto d=Json::parse(code_->toPlainText().toStdString());auto h=d.value("hardware",Json::object());cpu_->setValue(h.value("cpu_mhz",120));ram_->setValue(h.value("ram_kib",128));flash_->setValue(h.value("flash_kib",512));structure_->clear();
    for(const auto*key:{"tasks","messages","bindings","state_machine"}){auto *group=new QTreeWidgetItem(structure_,{key});const auto &section=d.at(key);for(const auto&entry:section.items()){QString name=entry.value().is_object()?QString::fromStdString(entry.value().value("name",entry.key())):QString::fromStdString(entry.key());auto *item=new QTreeWidgetItem(group,{name,QString::fromStdString(entry.value().dump())});item->setToolTip(1,QString::fromStdString(entry.value().dump(2)));}group->setExpanded(true);}
    structure_->resizeColumnToContents(0);
}catch(const std::exception&e){result(e.what(),false);}}
void EcuEditor::send(bool deploy){try{if(!submit||selected_.isEmpty())return;auto d=Json::parse(code_->toPlainText().toStdString());inspect();feedback_->setText(deploy?"Validating project and deploying…":"Validating against the complete project…");submit(selected_,d,deploy);}catch(const std::exception&e){result(e.what(),false);}}
void EcuEditor::result(const QString&message,bool success,bool deployed,const QString&name){feedback_->setText(message);feedback_->setStyleSheet(success?"color:#68e2ce":"color:#ff9292");if(deployed){drafts_.erase(name);if(selected_==name)load(selected_);}}
bool EcuEditor::hasDrafts()const{for(const auto&[name,text]:drafts_){auto it=live_.find(name);if(it==live_.end()||text!=it->second)return true;}return false;}
