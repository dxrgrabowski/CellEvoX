#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include "core/application.hpp"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;

    CellEvoX::core::Application cancerSim;

    engine.load(QUrl(QStringLiteral("qrc:/CellEvoX/qml/main.qml")));
    
    return app.exec();
}
