#include <cxxopts.hpp>
#include <valhalla/baldr/graphid.h>
#include <valhalla/baldr/graphreader.h>
#include <valhalla/mjolnir/graphtilebuilder.h>

#include "argparse_utils.h"
#include <traffic.h>

int main(int argc, char** argv) {
  const auto program = filesystem::path(__FILE__).stem().string();
  boost::property_tree::ptree pt;

  try {
    cxxopts::Options options(program, "removes predicted traffic from valhalla tiles.\n");

    // clang-format off
    options.add_options()
    ("h,help", "Print this help message.")
    ("j,concurrency", "Number of threads to use.", cxxopts::value<unsigned int>())
    ("c,config", "Path to the json configuration file.", cxxopts::value<std::string>())
    ("i,inline-config", "Inline json config.",cxxopts::value<std::string>());
    // clang-format on

    auto result = options.parse(argc, argv);
    options.custom_help("");
    if (!parse_common_args(program, options, result, pt, "mjolnir.logging", true))
      return EXIT_SUCCESS;

  } catch (cxxopts::exceptions::exception& e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  } catch (std::exception& e) {
    std::cerr << "Unable to parse command line options because: " << e.what() << "\n";
    return EXIT_FAILURE;
  }

  try {
    valhalla::tools::remove_predicted_traffic(pt);
  } catch (std::exception& e) {
    std::cout << "Failed to remove predicted traffic: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
}