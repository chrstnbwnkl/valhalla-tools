#include <filesystem>
#include <fstream>
#include <queue>
#include <thread>
#include <traffic.h>
#include <valhalla/baldr/graphreader.h>

namespace {
using namespace valhalla;

void work(std::queue<baldr::GraphId>& tile_queue,
          std::mutex& lock,
          const boost::property_tree::ptree& pt) {

  std::string tile_dir = pt.get<std::string>("mjolnir.tile_dir");
  baldr::GraphId tile_id;
  while (true) {
    {
      std::lock_guard l(lock);
      if (tile_queue.empty())
        break;
      tile_id = tile_queue.front();
      tile_queue.pop();
    }

    auto tile_path = tile_dir +
                     std::filesystem::path::preferred_separator +
                     baldr::GraphTile::FileSuffix(tile_id);
    if (!std::filesystem::exists(tile_path)) {
      LOG_ERROR("No tile at " + tile_path);
      continue;
    }

    // Get the tile and remove traffic
    tools::EnhancedGraphTileBuilder tile_builder(tile_dir, tile_id, false);
    tile_builder.RemovePredictedTraffic();
  }
}
} // namespace
namespace valhalla {

namespace tools {

void remove_predicted_traffic(boost::property_tree::ptree& pt) {

  pt.erase("mjolnir.tile_extract"); // ignore the extract
  baldr::GraphReader reader(pt.get_child("mjolnir"));

  std::queue<baldr::GraphId> tile_queue;
  for (const auto& tile_id : reader.GetTileSet()) {
    tile_queue.emplace(tile_id);
  }

  std::vector<std::shared_ptr<std::thread>> threads(
      pt.get<size_t>("mjolnir.concurrency"));
  std::mutex lock;

  for (size_t i = 0; i < threads.size(); ++i) {
    threads[i] =
        std::make_shared<std::thread>(work, std::ref(tile_queue),
                                      std::ref(lock), std::cref(pt));
  }

  for (const auto& thread : threads)
    thread->join();

  LOG_INFO("Finished removing traffic from tiles");
}

void EnhancedGraphTileBuilder::RemovePredictedTraffic() {
  // Get the name of the file
  std::filesystem::path filename =
      tile_dir_ + std::filesystem::path::preferred_separator +
      GraphTile::FileSuffix(header_builder_.graphid());

  // Make sure the directory exists on the system
  if (!std::filesystem::exists(filename.parent_path()))
    std::filesystem::create_directories(filename.parent_path());

  // Make copies of edges so we can mutate them
  size_t n = header_->directededgecount();
  directededges_builder_.reserve(n);
  std::copy(directededges_, directededges_ + n,
            std::back_inserter(directededges_builder_));

  std::for_each(directededges_builder_.begin(),
                directededges_builder_.end(), [](baldr::DirectedEdge& de) {
                  de.set_has_predicted_speed(false);
                  de.set_free_flow_speed(0);
                  de.set_constrained_flow_speed(0);
                });

  // update header
  header_builder_.set_end_offset(
      header_builder_.end_offset() -
      (header_builder_.directededgecount() * sizeof(uint32_t) +
       header_builder_.predictedspeeds_count() * sizeof(uint16_t) *
           kCoefficientCount));
  header_builder_.set_predictedspeeds_count(0);
  header_builder_.set_predictedspeeds_offset(0);

  // Open file and truncate
  std::ofstream file(filename.c_str(),
                     std::ios::out | std::ios::binary | std::ios::trunc);
  if (file.is_open()) {
    file.write(reinterpret_cast<const char*>(&header_builder_),
               sizeof(GraphTileHeader));

    // Copy the nodes (they are unchanged when adding predicted speeds).
    file.write(reinterpret_cast<const char*>(nodes_),
               header_->nodecount() * sizeof(NodeInfo));

    // Copy the node transitions (they are unchanged when adding predicted
    // speeds).
    file.write(reinterpret_cast<const char*>(transitions_),
               header_->transitioncount() * sizeof(NodeTransition));

    // Write the updated directed edges. Make sure edge count matches.
    if (directededges_builder_.size() != header_->directededgecount()) {
      throw std::runtime_error(
          "GraphTileBuilder::Update - directed edge count has changed");
    }
    file.write(reinterpret_cast<const char*>(
                   directededges_builder_.data()),
               directededges_builder_.size() * sizeof(DirectedEdge));

    // Write out data from access restrictions to the new end offset
    auto begin = reinterpret_cast<const char*>(&access_restrictions_[0]);
    auto end = reinterpret_cast<const char*>(header()) +
               header_builder_.end_offset();
    file.write(begin, end - begin);

    // Write the rest of the tiles. TBD (if anything is added after the
    // speed profiles then this will need to be updated)

    // Close the file
    file.close();
  }
}
} // namespace tools

} // namespace valhalla
