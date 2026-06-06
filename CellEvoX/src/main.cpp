#include "core/application.hpp"

#include <cstddef>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

void printUsage(std::ostream& out) {
  out << "Allowed options:\n"
      << "  --help, -h                 produce help message\n"
      << "  --config <path>            path to config file\n"
      << "  --analyze <path>           path to existing output directory to analyze\n"
      << "  --threads <count>          cap TBB worker parallelism for this process\n"
      << "  --postprocess <mode>       post-run work: full, exports, none\n"
      << "  --simulate-only            alias for --postprocess none\n";
}

bool readOptionValue(int& index, int argc, char* argv[], const std::string& option, std::string& value) {
  if (index + 1 >= argc) {
    std::cerr << "Missing value for " << option << std::endl;
    return false;
  }
  value = argv[++index];
  return true;
}

bool parseThreadCount(const std::string& raw, std::size_t& value) {
  try {
    std::size_t parsed_chars = 0;
    const unsigned long long parsed = std::stoull(raw, &parsed_chars);
    if (parsed_chars != raw.size() || parsed == 0) {
      return false;
    }
    value = static_cast<std::size_t>(parsed);
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

bool parsePostprocessMode(const std::string& raw, CellEvoX::core::PostprocessMode& mode) {
  if (raw == "full") {
    mode = CellEvoX::core::PostprocessMode::Full;
    return true;
  }
  if (raw == "exports" || raw == "csv") {
    mode = CellEvoX::core::PostprocessMode::Exports;
    return true;
  }
  if (raw == "none" || raw == "off") {
    mode = CellEvoX::core::PostprocessMode::None;
    return true;
  }
  return false;
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
    if (arg == "--threads") {
      std::string value;
      if (!readOptionValue(i, argc, argv, arg, value)) return 1;
      std::size_t parsed = 0;
      if (!parseThreadCount(value, parsed)) {
        std::cerr << "Invalid --threads value: " << value << std::endl;
        return 1;
      }
      options.max_threads = parsed;
      continue;
    }
    if (arg.rfind("--threads=", 0) == 0) {
      const std::string value = arg.substr(std::string("--threads=").size());
      std::size_t parsed = 0;
      if (!parseThreadCount(value, parsed)) {
        std::cerr << "Invalid --threads value: " << value << std::endl;
        return 1;
      }
      options.max_threads = parsed;
      continue;
    }
    if (arg == "--postprocess") {
      std::string value;
      if (!readOptionValue(i, argc, argv, arg, value)) return 1;
      if (!parsePostprocessMode(value, options.postprocess_mode)) {
        std::cerr << "Invalid --postprocess mode: " << value << std::endl;
        return 1;
      }
      continue;
    }
    if (arg.rfind("--postprocess=", 0) == 0) {
      const std::string value = arg.substr(std::string("--postprocess=").size());
      if (!parsePostprocessMode(value, options.postprocess_mode)) {
        std::cerr << "Invalid --postprocess mode: " << value << std::endl;
        return 1;
      }
      continue;
    }
    if (arg == "--simulate-only") {
      options.postprocess_mode = CellEvoX::core::PostprocessMode::None;
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
