#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include "core/application.hpp"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;

    CellEvoX::core::Application cancerSim;
    
    //engine.addImportPath("/home/dxr/qtcreator-14.0.2/lib/Qt/qml");


    engine.load(QUrl(QStringLiteral("qrc:/CellEvoX/qml/main.qml")));
    
    return app.exec();
}
