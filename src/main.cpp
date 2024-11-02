#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include "cancer_sim/core/application.hpp"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;

    cancer_sim::Application cancerSim;
    
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));
    
    return app.exec();
}
