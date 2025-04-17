#include <cxxopts.hpp>
#include <iostream>
#include <valhalla/baldr/graphid.h>
#include <valhalla/baldr/tilehierarchy.h>
#include <valhalla/midgard/aabb2.h>

namespace {
using namespace valhalla;

const std::string delim = ",";

midgard::AABB2<midgard::PointLL> parse_bbox_str(const std::string& s) {
  size_t last = 0;
  size_t next = 0;
  size_t idx = 0;
  std::array<double, 4> pts;

  while ((next = s.find(delim, last)) != std::string::npos) {
    std::string coord = s.substr(last, next - last);

    if (idx > 3)
      throw std::runtime_error("Too many coordinates provided for bbox");

    try {
      pts[idx] = std::stod(coord);
    } catch (std::exception& e) {
      throw std::runtime_error("Unable to parse bounding box");
    }
    last = next + 1;
    ++idx;
  }

  // get the last one
  std::string coord = s.substr(last);
  pts[idx] = std::stod(coord);

  if (idx != 3)
    std::runtime_error("Invalid bbox string provided");

  return midgard::AABB2<midgard::PointLL>(midgard::PointLL(pts[0], pts[1]),
                                          midgard::PointLL(pts[2],
                                                           pts[3]));
}
} // namespace

int main(int argc, char** argv) {
  const auto program = std::filesystem::path(__FILE__).stem().string();
  midgard::AABB2<midgard::PointLL> bbox;

  try {
    cxxopts::Options options(program,
                             "prints a list of Valhalla tile IDs that "
                             "intersect with a given bounding box.");

    // clang-format off
    options.add_options()
    ("h,help", "Print this help message.")
    ("b,bounding-box",
       "the bounding box to intersect with",
       cxxopts::value<std::string>());
    // clang-format on

    auto result = options.parse(argc, argv);
    options.custom_help("");
    if (result.count("help")) {
      std::cout << options.help() << "\n";
      return EXIT_SUCCESS;
    }
    if (result.count("bounding-box")) {
      bbox = parse_bbox_str(result["bounding-box"].as<std::string>());
    } else {
      throw cxxopts::exceptions::missing_argument("bounding-box");
    }
  }

  catch (cxxopts::exceptions::exception& e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  } catch (std::exception& e) {
    std::cerr << "Unable to parse command line options because: "
              << e.what() << "\n";
    return EXIT_FAILURE;
  }

  try {
    for (const auto& tile_id : baldr::TileHierarchy::GetGraphIds(bbox)) {
      std::cout << tile_id << "\n";
    }
  } catch (std::exception& e) {
    std::cerr << "Failed to remove predicted traffic: " << e.what()
              << "\n";
    return EXIT_FAILURE;
  }
}