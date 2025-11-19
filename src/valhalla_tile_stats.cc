#include <algorithm>
#include <boost/property_tree/ptree_fwd.hpp>
#include <cxxopts.hpp>
#include <valhalla/baldr/graphid.h>
#include <valhalla/baldr/graphreader.h>

#include "argparse_utils.h"
#include <filesystem>
#include <future>
#include <random>

struct stats_t {
  uint32_t total_bytes_{0};
  uint32_t header_bytes_{0};
  uint32_t nodeinfo_bytes_{0};
  uint32_t nodetransition_bytes_{0};
  uint32_t directededge_bytes_{0};
  uint32_t directededge_ext_bytes_{0};
  uint32_t accessrestriction_bytes_{0};
  uint32_t transitdeparture_bytes_{0};
  uint32_t transitstop_bytes_{0};
  uint32_t transitroute_bytes_{0};
  uint32_t transitschedule_bytes_{0};
  uint32_t transittransfer_bytes_{0};
  uint32_t sign_bytes_{0};
  uint32_t turnlane_bytes_{0};
  uint32_t admin_bytes_{0};
  uint32_t bin_bytes_{0};
  uint32_t complex_restriction_forward_bytes_{0};
  uint32_t complex_restriction_reverse_bytes_{0};
  uint32_t edgeinfo_bytes_{0};
  uint32_t textlist_bytes_{0};
  uint32_t laneconnectivity_bytes_{0};
  uint32_t predicted_speed_bytes_{0};

  inline float as_percentage(uint32_t part) const {
    if (total_bytes_ == 0)
      return 0.f;
    return static_cast<float>(part) / static_cast<float>(total_bytes_) *
           100.f;
  }

  inline uint32_t calculated_size() const {
    return (header_bytes_ + nodeinfo_bytes_ + nodetransition_bytes_ +
            directededge_bytes_ + directededge_ext_bytes_ +
            accessrestriction_bytes_ + transitdeparture_bytes_ +
            transitstop_bytes_ + +transitroute_bytes_ +
            transitschedule_bytes_ + transittransfer_bytes_ + sign_bytes_ +
            turnlane_bytes_ + admin_bytes_ +
            complex_restriction_forward_bytes_ +
            complex_restriction_reverse_bytes_ + edgeinfo_bytes_ +
            textlist_bytes_ + bin_bytes_ + laneconnectivity_bytes_ +
            predicted_speed_bytes_);
  }

  inline bool validate() const {
    bool size_match = total_bytes_ == calculated_size();

    if (!size_match) {
      LOG_ERROR(
          "Mismatch: calculated ({}) vs. actual ({}) | diff: {} bytes",
          calculated_size(), total_bytes_,
          std::abs(static_cast<int>(total_bytes_ - calculated_size())));
    }
    return size_match;
  }

  void operator+=(const stats_t& other) {
    total_bytes_ += other.total_bytes_;
    header_bytes_ += other.header_bytes_;
    nodeinfo_bytes_ += other.nodeinfo_bytes_;
    nodetransition_bytes_ += other.nodetransition_bytes_;
    directededge_bytes_ += other.directededge_bytes_;
    directededge_ext_bytes_ += other.directededge_ext_bytes_;
    accessrestriction_bytes_ += other.accessrestriction_bytes_;
    transitdeparture_bytes_ += other.transitdeparture_bytes_;
    transitstop_bytes_ += other.transitstop_bytes_;
    transitroute_bytes_ += other.transitroute_bytes_;
    transitschedule_bytes_ += other.transitschedule_bytes_;
    transittransfer_bytes_ += other.transittransfer_bytes_;
    sign_bytes_ += other.sign_bytes_;
    turnlane_bytes_ += other.turnlane_bytes_;
    admin_bytes_ += other.admin_bytes_;
    bin_bytes_ += other.bin_bytes_;
    complex_restriction_forward_bytes_ +=
        other.complex_restriction_forward_bytes_;
    complex_restriction_reverse_bytes_ +=
        other.complex_restriction_reverse_bytes_;
    edgeinfo_bytes_ += other.edgeinfo_bytes_;
    textlist_bytes_ += other.textlist_bytes_;
    laneconnectivity_bytes_ += other.laneconnectivity_bytes_;
    predicted_speed_bytes_ += other.predicted_speed_bytes_;
  }
};
namespace std {
template <> struct formatter<stats_t> : std::formatter<std::string> {
  auto format(const stats_t& p, auto& ctx) const {
    return std::formatter<std::string>::
        format(std::format("Tile Stats:"
                           "\n\tTotal Size (Mb): {:.2f}"
                           "\n\tHeader: {:.2f}%"
                           "\n\tNodeInfo: {:.2f}%"
                           "\n\tTransition: {:.2f}%"
                           "\n\tDirectedEdge: {:.2f}%"
                           "\n\tDirectedEdgeExt: {:.2f}%"
                           "\n\tAccessRestriction: {:.2f}%"
                           "\n\tTransitDeparture: {:.2f}%"
                           "\n\tTransitStop: {:.2f}%"
                           "\n\tTransitRoute: {:.2f}%"
                           "\n\tTransitSchedule: {:.2f}%"
                           "\n\tTransitTransfer: {:.2f}%"
                           "\n\tSign: {:.2f}%"
                           "\n\tTurnLane: {:.2f}%"
                           "\n\tAdmin: {:.2f}%"
                           "\n\tComplexRestrictionFwd: {:.2f}%"
                           "\n\tComplexRestrictionRev: {:.2f}%"
                           "\n\tEdgeInfo: {:.2f}%"
                           "\n\tTextList: {:.2f}%"
                           "\n\tBins: {:.2f}%"
                           "\n\tLaneConnectivity: {:.2f}%"
                           "\n\tPredictedSpeed: {:.2f}%",
                           static_cast<float>(p.total_bytes_) /
                               static_cast<float>(std::pow(1024, 2)),
                           p.as_percentage(p.header_bytes_),
                           p.as_percentage(p.nodeinfo_bytes_),
                           p.as_percentage(p.nodetransition_bytes_),
                           p.as_percentage(p.directededge_bytes_),
                           p.as_percentage(p.directededge_ext_bytes_),
                           p.as_percentage(p.accessrestriction_bytes_),
                           p.as_percentage(p.transitdeparture_bytes_),
                           p.as_percentage(p.transitstop_bytes_),
                           p.as_percentage(p.transitroute_bytes_),
                           p.as_percentage(p.transitschedule_bytes_),
                           p.as_percentage(p.transittransfer_bytes_),
                           p.as_percentage(p.sign_bytes_),
                           p.as_percentage(p.turnlane_bytes_),
                           p.as_percentage(p.admin_bytes_),
                           p.as_percentage(
                               p.complex_restriction_forward_bytes_),
                           p.as_percentage(
                               p.complex_restriction_reverse_bytes_),
                           p.as_percentage(p.edgeinfo_bytes_),
                           p.as_percentage(p.textlist_bytes_),
                           p.as_percentage(p.bin_bytes_),
                           p.as_percentage(p.laneconnectivity_bytes_),
                           p.as_percentage(p.predicted_speed_bytes_)),
               ctx);
  }
};
} // namespace std
namespace {
using namespace valhalla::baldr;

void work(std::mutex& lock,
          std::deque<GraphId>& tiles,
          boost::property_tree::ptree& config,
          std::promise<stats_t>& stat) {
  // go through the tiles, peak into each header, update the count and set
  // the results

  GraphReader reader(config.get_child("mjolnir"));
  stats_t all_stats;
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

    stats_t stats;

    auto header = tile->header();
    stats.total_bytes_ = tile->header()->end_offset();
    stats.header_bytes_ = sizeof(GraphTileHeader);
    stats.nodeinfo_bytes_ = header->nodecount() * sizeof(NodeInfo);

    stats.nodetransition_bytes_ =
        header->transitioncount() * sizeof(NodeTransition);

    stats.directededge_bytes_ =
        header->directededgecount() * sizeof(DirectedEdge);

    stats.directededge_ext_bytes_ =
        header->has_ext_directededge()
            ? header->directededgecount() * sizeof(DirectedEdgeExt)
            : 0;

    stats.accessrestriction_bytes_ =
        header->access_restriction_count() * sizeof(AccessRestriction);

    stats.transitdeparture_bytes_ =
        header->departurecount() * sizeof(TransitDeparture);

    stats.transitstop_bytes_ = header->stopcount() * sizeof(TransitStop);
    stats.transitroute_bytes_ =
        header->routecount() * sizeof(TransitRoute);
    stats.transitschedule_bytes_ =
        header->schedulecount() * sizeof(TransitSchedule);
    stats.transittransfer_bytes_ =
        header->transfercount() * sizeof(TransitTransfer);

    stats.sign_bytes_ = header->signcount() * sizeof(Sign);
    stats.turnlane_bytes_ = header->turnlane_count() * sizeof(TurnLanes);
    stats.admin_bytes_ = header->admincount() * sizeof(Admin);
    const char* bin_start =
        reinterpret_cast<const char*>(tile->GetBin(0, 0).data());
    auto last_bin = tile->GetBin(kBinsDim - 1, kBinsDim - 1);
    const char* bin_end = reinterpret_cast<const char*>(
        last_bin.data() + last_bin.size() * sizeof(GraphId));
    stats.bin_bytes_ = bin_end - bin_start;

    stats.complex_restriction_forward_bytes_ =
        header->complex_restriction_reverse_offset() -
        header->complex_restriction_forward_offset();
    stats.complex_restriction_reverse_bytes_ =
        header->edgeinfo_offset() -
        header->complex_restriction_reverse_offset();

    stats.edgeinfo_bytes_ =
        header->textlist_offset() - header->edgeinfo_offset();

    stats.textlist_bytes_ =
        header->lane_connectivity_offset() - header->textlist_offset();

    if (header->predictedspeeds_count() > 0) {
      stats.laneconnectivity_bytes_ = header->predictedspeeds_offset() -
                                      header->lane_connectivity_offset();

      stats.predicted_speed_bytes_ =
          header->end_offset() - header->predictedspeeds_offset();
    } else {
      stats.laneconnectivity_bytes_ =
          header->end_offset() - header->lane_connectivity_offset();

      stats.predicted_speed_bytes_ = 0;
    }

    stats.validate();
    all_stats += stats;
  }
  stat.set_value(all_stats);
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
  std::cout << std::format("{}", stats);
}

} // namespace
  //

int main(int argc, char** argv) {
  const auto program = std::filesystem::path(__FILE__).stem().string();
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
