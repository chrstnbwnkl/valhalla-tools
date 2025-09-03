#pragma once

#include <boost/property_tree/ptree.hpp>
#include <valhalla/mjolnir/graphtilebuilder.h>

namespace valhalla {

namespace tools {

/**
 * @brief Removes predicted traffic information (predicted speeds, freeflow
 * and constrained speeds) from tiles. Only works on the tile directory, no
 * the tile extract.
 *
 * @param pt the valhalla configuration
 */
void remove_predicted_traffic(boost::property_tree::ptree& pt);

/**
 * @brief Derived class to access protected members of GraphTileBuilder
 */
class EnhancedGraphTileBuilder : public mjolnir::GraphTileBuilder {

public:
  using mjolnir::GraphTileBuilder::GraphTileBuilder;
  /**
   * Removes predicted traffic data:
   *   1. predicted speeds
   *   2. freeflow & constrained speeds
   *   3. unsets has_predicted_speeds flag
   */
  void RemovePredictedTraffic();
};

} // namespace tools
} // namespace valhalla
