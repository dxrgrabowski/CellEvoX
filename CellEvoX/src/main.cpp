#include "core/application.hpp"
// #include <spdlog/spdlog.h>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <boost/program_options.hpp>
#include <exception>
#include <iostream>

namespace po = boost::program_options;

int main(int argc, char *argv[]) {
  po::options_description desc("Allowed options");
  desc.add_options()("help", "produce help message")(
      "config", po::value<std::string>(), "path to config file")(
      "analyze", po::value<std::string>(), "path to existing output directory to analyze");

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);

  if (vm.count("help")) {
    // std::cout<< desc << std::endl;
    return 1;
  }
  try {
    CellEvoX::core::Application cancerSim(vm);
  } catch (const std::exception& e) {
    std::cerr << "CellEvoX error: " << e.what() << std::endl;
    return 1;
  }

  // QGuiApplication app(argc, argv);
  // QQmlApplicationEngine engine;

  // engine.load(QUrl(QStringLiteral("qrc:/CellEvoX/qml/main.qml")));

  return 0;  // app.exec();
}
