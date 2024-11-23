#include "core/application.hpp"
//#include <spdlog/spdlog.h>
#include <iostream>
#include <boost/program_options.hpp>
#include <QGuiApplication>
#include <QQmlApplicationEngine>


namespace po = boost::program_options;

int main(int argc, char *argv[]) 
{
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help", "produce help message")
        ("config", po::value<std::string>(), "path to config file");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        
        //std::cout<< desc << std::endl;
        return 1;
    }
    CellEvoX::core::Application cancerSim(vm);
    
    // QGuiApplication app(argc, argv);
    // QQmlApplicationEngine engine;


    // engine.load(QUrl(QStringLiteral("qrc:/CellEvoX/qml/main.qml")));
    
    return 0 ;//app.exec();
}
