#include "ui/desktop/DesktopWindow.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <iostream>

int main(int argc,char **argv){
    QApplication app(argc,argv);QCoreApplication::setApplicationName("vehicle_lab_desktop");
    QCommandLineParser parser;parser.setApplicationDescription("Native C++ CAN vehicle laboratory; one process per ECU.");parser.addHelpOption();
    parser.addOption({"root","Repository/resource root","directory",VCLAB_SOURCE_ROOT});
    parser.addOption({"scenario","Scenario relative to root or absolute path","file","resources/scenarios/ev_interactive.json"});
    parser.addOption({"runtime","Native controller executable","file",QCoreApplication::applicationDirPath()+"/vclab"});
    parser.addOption({"smoke","Run driving/braking/diagnostic demo and save evidence to a new directory","directory"});
    parser.addOption({"workspace","Initial workspace: workroom, programming, simulation","name","simulation"});
    parser.addOption({"windows","Open workroom, programming and simulation as separate shared windows"});parser.process(app);
    const QStringList workspaces={"workroom","programming","simulation"};
    if(!workspaces.contains(parser.value("workspace"))){std::cerr<<"Unknown workspace\n";return 2;}
    const QString smoke=parser.value("smoke");
    if(!smoke.isEmpty()&&(QFileInfo::exists(smoke)||!QDir().mkpath(smoke))){std::cerr<<"Smoke evidence directory must be new and writable\n";return 2;}
    if(!QFileInfo(parser.value("runtime")).isExecutable()){std::cerr<<"Native vclab executable not found; build vclab and vclab_worker first\n";return 2;}
    try{DesktopWindow window(parser.value("runtime"),parser.value("scenario"),parser.value("root"),smoke.isEmpty()?QString():QFileInfo(smoke).absoluteFilePath());
        if(smoke.isEmpty())window.findChild<QTabWidget*>("workspaces")->setCurrentIndex(workspaces.indexOf(parser.value("workspace")));
        window.show();if(parser.isSet("windows"))QTimer::singleShot(0,&window,[&window]{window.findChild<QPushButton*>("openAllWindows")->click();});return app.exec();}
    catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
