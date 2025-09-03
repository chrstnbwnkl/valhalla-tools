#include <algorithm>
#include <boost/property_tree/ptree_fwd.hpp>
#include <cxxopts.hpp>
#include <valhalla/baldr/graphid.h>
#include <valhalla/baldr/graphreader.h>

#include "argparse_utils.h"
#include <future>
#include <random>

namespace {
using namespace valhalla::baldr;

class PublicGraphtile : public GraphTile {
public:
  uint32_t complex_restriction_count() const {
    return complex_restriction_forward_size_ +
           complex_restriction_reverse_size_;
  }
};

struct stats_t {
  uint32_t node_count{0};
  uint32_t directededge_count{0};
  uint32_t shortcut_count{0};
  uint32_t acceessrestriction_count{0};
  uint32_t complexrestriction_count{0};

  void operator+=(const stats_t& other) {
    node_count += other.node_count;
    directededge_count += other.directededge_count;
    shortcut_count += other.shortcut_count;
    acceessrestriction_count += other.acceessrestriction_count;
    complexrestriction_count += other.complexrestriction_count;
  }
};

void work(std::mutex& lock,
          std::deque<GraphId>& tiles,
          boost::property_tree::ptree& config,
          std::promise<stats_t>& stat) {
  // go through the tiles, peak into each header, update the count and set
  // the results

  GraphReader reader(config.get_child("mjolnir"));
  stats_t stats;
  while (true) {
    GraphId tile_id;
    {
      std::lock_guard l(lock);
      if (tiles.empty()) {
        break;
      }

      tile_id = tiles.back();
      tiles.pop_back();
    }

    auto tile = reader.GetGraphTile(tile_id);

    if (!tile) {
      continue;
    }

    auto header = tile->header();
    stats.node_count += header->nodecount();
    stats.directededge_count += header->directededgecount();
    stats.acceessrestriction_count += header->access_restriction_count();
    auto public_tile = static_cast<const PublicGraphtile*>(tile.get());
    stats.complexrestriction_count +=
        public_tile->complex_restriction_count();

    GraphId edgeid = tile_id;
    for (size_t i = 0; i < header->directededgecount(); ++i, ++edgeid) {
      auto* de = tile->directededge(i);
      stats.shortcut_count += de->is_shortcut();
    }
  }

  stat.set_value(stats);
}

void tile_stats(boost::property_tree::ptree& config) {
  std::list<std::promise<stats_t>> results;
  std::deque<GraphId> tiles;

  GraphReader reader(config.get_child("mjolnir"));

  for (const auto& tile : reader.GetTileSet()) {
    tiles.push_back(tile);
  }

  std::shuffle(tiles.begin(), tiles.end(), std::mt19937(0));

  std::vector<std::shared_ptr<std::thread>> threads(
      std::max(static_cast<unsigned int>(1),
               config.get<
                   unsigned int>("mjolnir.concurrency",
                                 std::thread::hardware_concurrency())));

  std::mutex lock;
  for (auto& thread : threads) {
    auto& s = results.emplace_back();
    thread = std::make_shared<std::thread>(work, std::ref(lock),
                                           std::ref(tiles),
                                           std::ref(config), std::ref(s));
  }

  for (auto& thread : threads) {
    thread->join();
  }

  stats_t stats;

  for (auto& result : results) {
    stats += result.get_future().get();
  }

  LOG_INFO("Finished tile stats");
  LOG_INFO("Node count: " + std::to_string(stats.node_count));
  LOG_INFO("Directededge count: " +
           std::to_string(stats.directededge_count));
  LOG_INFO("Shortcut count: " + std::to_string(stats.shortcut_count));
  LOG_INFO("Access restriction count: " +
           std::to_string(stats.acceessrestriction_count));
  LOG_INFO("Complex restriction count: " +
           std::to_string(stats.complexrestriction_count));
}
} // namespace

int main(int argc, char** argv) {
  const auto program = filesystem::path(__FILE__).stem().string();
  boost::property_tree::ptree pt;

  try {
    cxxopts::Options
        options(program,
                "spits out some statistics for a valhalla graph.\n");

    // clang-format off
    options.add_options()
    ("h,help", "Print this help message.")
    ("j,concurrency", "Number of threads to use.", cxxopts::value<unsigned int>())
    ("c,config", "Path to the json configuration file.", cxxopts::value<std::string>())
    ("i,inline-config", "Inline json config.",cxxopts::value<std::string>());
    // clang-format on

    auto result = options.parse(argc, argv);
    options.custom_help("");
    if (!parse_common_args(program, options, result, pt, "mjolnir.logging",
                           true))
      return EXIT_SUCCESS;

  } catch (cxxopts::exceptions::exception& e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  } catch (std::exception& e) {
    std::cerr << "Unable to parse command line options because: "
              << e.what() << "\n";
    return EXIT_FAILURE;
  }

  try {
    tile_stats(pt);
  } catch (std::exception& e) {
    LOG_ERROR("Failed to create tileset stats: " + std::string(e.what()));
  }
}
