#include "core/application.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <utility>

namespace {

void printUsage(std::ostream& out) {
  out << "Allowed options:\n"
      << "  --help, -h             produce help message\n"
      << "  --config <path>        path to config file\n"
      << "  --analyze <path>       path to existing output directory to analyze\n";
}

bool readOptionValue(int& index, int argc, char* argv[], const std::string& option, std::string& value) {
  if (index + 1 >= argc) {
    std::cerr << "Missing value for " << option << std::endl;
    return false;
  }
  value = argv[++index];
  return true;
}

}  // namespace

int main(int argc, char *argv[]) {
  CellEvoX::core::CliOptions options;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      printUsage(std::cout);
      return 0;
    }
    if (arg == "--config") {
      std::string value;
      if (!readOptionValue(i, argc, argv, arg, value)) return 1;
      options.config_path = value;
      continue;
    }
    if (arg.rfind("--config=", 0) == 0) {
      options.config_path = arg.substr(std::string("--config=").size());
      continue;
    }
    if (arg == "--analyze") {
      std::string value;
      if (!readOptionValue(i, argc, argv, arg, value)) return 1;
      options.analyze_path = value;
      continue;
    }
    if (arg.rfind("--analyze=", 0) == 0) {
      options.analyze_path = arg.substr(std::string("--analyze=").size());
      continue;
    }

    std::cerr << "Unknown option: " << arg << std::endl;
    printUsage(std::cerr);
    return 1;
  }

  try {
    CellEvoX::core::Application cancerSim(std::move(options));
  } catch (const std::exception& e) {
    std::cerr << "CellEvoX error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
