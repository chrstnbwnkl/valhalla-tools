#include <cstdlib>
#include <cxxopts.hpp>
#include <ogr_core.h>
#include <queue>
#include <valhalla/baldr/attributes_controller.h>
#include <valhalla/baldr/directededge.h>
#include <valhalla/baldr/graphid.h>
#include <valhalla/baldr/graphreader.h>
#include <valhalla/baldr/graphtileptr.h>
#include <valhalla/baldr/pathlocation.h>
#include <valhalla/baldr/rapidjson_utils.h>
#include <valhalla/mjolnir/graphtilebuilder.h>
#include <valhalla/proto/api.pb.h>
#include <valhalla/proto/options.pb.h>
#include <valhalla/proto_conversions.h>
#include <valhalla/sif/costfactory.h>
#include <valhalla/sif/dynamiccost.h>
#include <valhalla/third_party/rapidjson/document.h>

#include "argparse_utils.h"
#include <gdal_priv.h>
#include <ogrsf_frmts.h>

namespace {

const std::string kEdgePredictedSpeeds = "edge.predicted_speeds";

struct AttributeFilter {
  AttributeFilter(
      std::vector<std::string>&& includes_v,
      std::vector<std::string>&& excludes_v,
      std::vector<unsigned int>&& predspeedindices,
      valhalla::baldr::PathLocation::SearchFilter& searchfilter,
      bool only_shortcuts) {

    search_filter = searchfilter;

    pred_speed_indices = std::move(predspeedindices);
    std::unordered_set<std::string> includes;
    includes.reserve(includes_v.size());
    for (auto& inc : includes_v) {
      includes.insert(std::move(inc));
    }
    std::unordered_set<std::string> excludes;
    excludes.reserve(excludes_v.size());
    for (auto& exc : excludes_v) {
      excludes.insert(std::move(exc));
    }

    std::vector<std::pair<bool&, std::string>> edge_pairs = {
        {localidx, kEdgeId},
        {density, kEdgeDensity},
        {road_class, kEdgeRoadClass},
        {use, kEdgeUse},
        {speed, kEdgeSpeed},
        {tunnel, kEdgeTunnel},
        {bridge, kEdgeBridge},
        {traversability, kEdgeTraversability},
        {surface, kEdgeSurface},
        {urban, kEdgeIsUrban},
        {predicted_speeds, kEdgePredictedSpeeds},
        {country_crossing, kEdgeCountryCrossing},
    };

    std::vector<std::pair<bool&, std::string>> node_pairs = {
        {type, kNodeType},
    };

    for (auto& p : edge_pairs) {
      if (includes.find(p.second) != includes.end()) {
        edges = true;
        p.first = true;
      }

      if (excludes.find(p.second) != includes.end()) {
        edges = true;
        p.first = false;
      }
    }

    for (auto& p : node_pairs) {
      if (includes.find(p.second) != includes.end()) {
        nodes = true;
        p.first = true;
      }

      if (excludes.find(p.second) != includes.end()) {
        nodes = true;
        p.first = false;
      }
    }

    shortcuts_only = only_shortcuts;
  }

  /**
   * Taken from upstream valhalla (src/loki/search.cc)
   */
  bool is_filtered(const DirectedEdge* de,
                   graph_tile_ptr tile,
                   valhalla::sif::cost_ptr_t costing) const {
    // check if this edge matches any of the exclusion filters
    uint32_t road_class = static_cast<uint32_t>(de->classification());
    uint32_t min_road_class =
        static_cast<uint32_t>(search_filter.min_road_class_);
    uint32_t max_road_class =
        static_cast<uint32_t>(search_filter.max_road_class_);

    // Note that min_ and max_road_class are integers where, by default,
    // max_road_class is 0 and min_road_class is 7. This filter rejects
    // roads where the functional road class is outside of the min to max
    // range.
    return (road_class > min_road_class || road_class < max_road_class) ||
           (search_filter.exclude_tunnel_ && de->tunnel()) ||
           (search_filter.exclude_bridge_ && de->bridge()) ||
           (search_filter.exclude_toll_ && de->toll()) ||
           (search_filter.exclude_ramp_ && (de->use() == Use::kRamp)) ||
           (search_filter.exclude_ferry_ &&
            (de->use() == Use::kFerry || de->use() == Use::kRailFerry)) ||
           (search_filter.exclude_closures_ &&
            (costing->flow_mask() & kCurrentFlowMask) &&
            tile->IsClosed(de)) ||
           (search_filter.level_ != kMaxLevel &&
            !tile->edgeinfo(de).includes_level(search_filter.level_));
  }

  valhalla::baldr::PathLocation::SearchFilter search_filter;

  // edges
  bool localidx{false};
  bool road_class{false};
  bool use{false};
  bool speed{false};
  bool tunnel{false};
  bool bridge{false};
  bool traversability{false};
  bool surface{false};
  bool density{false};
  bool urban{false};
  bool country_crossing{false};
  bool predicted_speeds{false};
  std::vector<unsigned int> pred_speed_indices{};

  bool shortcuts_only{false};

  // nodes
  bool type{false};

  // which data sets does the user want
  bool edges{false};
  bool nodes{false};
};

OGRLineString* ConvertToOGRLineString(const std::vector<PointLL>& points) {
  OGRLineString* line = new OGRLineString();
  for (const auto& pt : points) {
    line->addPoint(pt.lng(), pt.lat());
  }
  return line;
}

valhalla::sif::cost_ptr_t create_costing(const std::string& costing_str) {
  valhalla::Options options;
  valhalla::Costing::Type costing;
  if (valhalla::Costing_Enum_Parse(costing_str, &costing)) {
    options.set_costing_type(costing);
  } else {
    options.set_costing_type(valhalla::Costing::none_);
  }
  auto& co = (*options.mutable_costings())[costing];
  co.set_type(costing);
  return valhalla::sif::CostFactory{}.Create(options);
}
enum class FeatureType : uint8_t { kEdges = 0, kNodes = 1 };

/**
 * Exports features that match the passed tileid to the specified
 * directory.
 */
void export_tile(valhalla::baldr::GraphReader& reader,
                 const valhalla::baldr::GraphId tile_id,
                 const std::string& output_dir,
                 const std::string& file_suffix,
                 valhalla::sif::cost_ptr_t costing,
                 GDALDriver* gdal_driver,
                 char** dataset_options,
                 const AttributeFilter& filter) {
  // get the file path
  auto edge_suffix =
      valhalla::baldr::GraphTile::FileSuffix(tile_id.Tile_Base(),
                                             "_edges" + file_suffix +
                                                 ".fgb");
  auto node_suffix =
      valhalla::baldr::GraphTile::FileSuffix(tile_id.Tile_Base(),
                                             "_nodes" + file_suffix +
                                                 ".fgb");
  auto edge_location =
      output_dir + filesystem::path::preferred_separator + edge_suffix;
  auto node_location =
      output_dir + filesystem::path::preferred_separator + node_suffix;

  // make sure all the subdirectories exist
  auto dir = filesystem::path(edge_location);
  dir.replace_filename("");
  filesystem::create_directories(dir);
  GDALDataset* edge_data = nullptr;
  GDALDataset* node_data = nullptr;

  if (filter.edges) {
    LOG_INFO("Writing edges to disk at " + edge_location);
    edge_data = gdal_driver->Create(edge_location.c_str(), 0, 0, 0,
                                    GDT_Unknown, nullptr);
  } else {
    LOG_INFO("No edges will be written");
  }

  if (filter.nodes) {
    LOG_INFO("Writing edges to disk at " + node_location);
    node_data = gdal_driver->Create(node_location.c_str(), 0, 0, 0,
                                    GDT_Unknown, nullptr);
  } else {
    LOG_INFO("No nodes will be written");
  }

  if (!edge_data && !node_data) {
    LOG_INFO("No attributes specified, skipping export");
    return;
  }

  // now go through the tile and convert the features
  if (!reader.DoesTileExist(tile_id)) {
    LOG_ERROR("Tile " + std::to_string(tile_id) +
              " does not exist. Skipping...");
    if (edge_data)
      GDALClose(edge_data);
    if (node_data)
      GDALClose(node_data);
    return;
  }
  // Trim reader if over-committed
  if (reader.OverCommitted()) {
    reader.Trim();
  }

  OGRSpatialReference spatialRef;
  spatialRef.SetWellKnownGeogCS("WGS84");

  OGRLayer* edges_layer;
  OGRLayer* nodes_layer;
  if (edge_data)
    edges_layer = edge_data->CreateLayer("edges", &spatialRef,
                                         wkbLineString, dataset_options);

  if (node_data)
    nodes_layer = node_data->CreateLayer("nodes", &spatialRef, wkbPoint,
                                         dataset_options);

  // create fields
  if (filter.localidx) {
    OGRFieldDefn field_name("edgeid", OFTInteger);
    edges_layer->CreateField(&field_name);
  }
  if (filter.road_class) {
    OGRFieldDefn field_name("road_class", OFTString);
    edges_layer->CreateField(&field_name);
  }

  if (filter.density) {
    OGRFieldDefn field_name("density", OFTInteger);
    edges_layer->CreateField(&field_name);
  }

  if (filter.urban) {
    OGRFieldDefn field_name("urban", OFTInteger);
    edges_layer->CreateField(&field_name);
  }

  if (filter.country_crossing) {
    OGRFieldDefn field_name("country_crossing", OFTInteger);
    edges_layer->CreateField(&field_name);
  }

  if (filter.predicted_speeds) {
    for (const auto& i : filter.pred_speed_indices) {
      std::string name = "predspeed_" + std::to_string(i);
      OGRFieldDefn field_name(name.c_str(), OFTInteger);
      edges_layer->CreateField(&field_name);
    }
  }

  if (filter.type) {
    OGRFieldDefn field_name("type", OFTString);
    nodes_layer->CreateField(&field_name);
  }

  auto tile = reader.GetGraphTile(tile_id);

  GraphId nodeid = tile_id;

  if (filter.nodes) {
    // export nodes
    for (size_t idx = 0; idx < tile->header()->nodecount();
         ++idx, nodeid++) {
      auto ni = tile->node(idx);
      if (!costing->Allowed(ni))
        continue;
      auto ll = tile->get_node_ll(nodeid);
      OGRFeature* feature =
          OGRFeature::CreateFeature(nodes_layer->GetLayerDefn());
      auto point = new OGRPoint();
      point->setX(ll.lng());
      point->setY(ll.lat());
      feature->SetGeometryDirectly(point);

      if (filter.type) {
        feature->SetField("type",
                          valhalla::baldr::to_string(ni->type()).c_str());
      }
      if (nodes_layer->CreateFeature(feature) != OGRERR_NONE) {
        LOG_ERROR("Failed to create feature");
      }

      OGRFeature::DestroyFeature(feature);
    }
  }

  if (!filter.edges)
    return;

  // export edges
  for (size_t idx = 0; idx < tile->header()->directededgecount(); ++idx) {
    auto de = tile->directededge(idx);

    // it's a shortcut but we want none or it's not but we only want
    // shortcuts
    if ((!filter.shortcuts_only && de->is_shortcut()) ||
        (filter.shortcuts_only && !de->is_shortcut()))
      continue;

    if (!costing->Allowed(de, tile, valhalla::sif::kDisallowNone) ||
        filter.is_filtered(de, tile, costing))
      continue;

    auto ei = tile->edgeinfo(de);

    auto shape = ei.shape();
    OGRLineString* line = ConvertToOGRLineString(shape);
    OGRFeature* feature =
        OGRFeature::CreateFeature(edges_layer->GetLayerDefn());
    feature->SetGeometryDirectly(line);

    if (filter.localidx) {
      feature->SetField("edgeid", static_cast<int>(idx));
    }
    if (filter.road_class) {
      feature->SetField("road_class",
                        valhalla::baldr::to_string(de->classification())
                            .c_str());
    }
    if (filter.density) {
      feature->SetField("density", static_cast<int>(de->density()));
    }
    if (filter.urban) {
      feature->SetField("urban", static_cast<int>(de->density() > 8));
    }
    if (filter.country_crossing) {
      feature->SetField("country_crossing",
                        static_cast<int>(de->ctry_crossing()));
    }
    if (filter.predicted_speeds) {
      for (const auto& i : filter.pred_speed_indices) {
        std::string field_name = "predspeed_" + std::to_string(i);
        if (de->has_predicted_speed()) {
          uint8_t sources = 0;
          auto s =
              tile->GetSpeed(de, valhalla::baldr::kPredictedFlowMask,
                             i * valhalla::baldr::kSpeedBucketSizeSeconds,
                             costing->is_hgv(), &sources);
          if (sources & valhalla::baldr::kPredictedFlowMask) {
            feature->SetField(field_name.c_str(), static_cast<int>(s));

          } else {
            feature->SetField(field_name.c_str(), static_cast<int>(0));
          }
        } else {
          feature->SetField(field_name.c_str(), static_cast<int>(0));
        }
      }
    }
    if (edges_layer->CreateFeature(feature) != OGRERR_NONE) {
      LOG_ERROR("Failed to create feature");
    }

    OGRFeature::DestroyFeature(feature);
  }

  if (edge_data)
    GDALClose(edge_data);

  if (node_data)
    GDALClose(node_data);
};

void work(boost::property_tree::ptree& config,
          const std::string& output_dir,
          const std::string& file_suffix,
          valhalla::sif::cost_ptr_t costing,
          const AttributeFilter& filter,
          std::queue<valhalla::baldr::GraphId>& tile_queue,
          std::mutex& lock) {
  valhalla::baldr::GraphReader reader(config.get_child("mjolnir"));
  valhalla::baldr::GraphId tile_id;
  const char* driver_name = "FlatGeobuf";
  GDALDriver* driver =
      GetGDALDriverManager()->GetDriverByName(driver_name);
  if (!driver) {
    LOG_ERROR("FlatGeoBuf driver not available");
    return;
  }
  // create some options
  char** dataset_options = NULL;
  dataset_options =
      CSLSetNameValue(dataset_options, "SPATIAL_INDEX", "YES");

  while (true) {
    {
      std::lock_guard l(lock);
      if (tile_queue.empty())
        break;
      tile_id = tile_queue.front();
      tile_queue.pop();
    }
    export_tile(reader, tile_id, output_dir, file_suffix, costing, driver,
                dataset_options, filter);
  }
}

/**
 * Exports features that match the passed tileids to the specified
 * directory
 *
 * @param config the config object
 * @param output_dir the directory to which the files will be written
 * (follows graph tile structuring within the directory)
 * @param file_suffix file suffix to be applied to each file prior to the
 * file extension
 * @param costing the costing to filter allowed/disallowed edges
 * @param filter which attributes to include/exclude
 * @param tile_ids which tiles to export
 */
int export_tiles(boost::property_tree::ptree& config,
                 const std::string& output_dir,
                 const std::string& file_suffix,
                 valhalla::sif::cost_ptr_t costing,
                 const AttributeFilter& filter,
                 std::vector<std::string>& tile_ids) {

  std::queue<valhalla::baldr::GraphId> tile_queue;

  // fill up a queue
  for (const auto& tile_id : tile_ids) {
    try {
      tile_queue.push(GraphId(tile_id));
    } catch (std::exception& e) {
      LOG_ERROR("Error converting tile ID: " + tile_id);
      throw e;
    }
  }
  // no need for this anymore
  tile_ids.resize(0);
  tile_ids.shrink_to_fit();

  // multithread it
  std::vector<std::shared_ptr<std::thread>> threads(
      config.get<size_t>("mjolnir.concurrency"));
  std::mutex lock;

  for (size_t i = 0; i < threads.size(); ++i) {
    threads[i] = std::make_shared<std::thread>(work, std::ref(config),
                                               std::cref(output_dir),
                                               std::cref(file_suffix),
                                               costing, std::cref(filter),
                                               std::ref(tile_queue),
                                               std::ref(lock));
  }

  for (const auto& thread : threads)
    thread->join();

  return EXIT_SUCCESS;
};

} // namespace
int main(int argc, char** argv) {
  const auto program = filesystem::path(__FILE__).stem().string();
  boost::property_tree::ptree pt;
  std::vector<std::string> tile_ids;
  std::string output_dir;
  std::string costing_str;
  std::string search_filter_str;
  std::string file_suffix;
  valhalla::baldr::PathLocation::SearchFilter search_filter;
  std::vector<std::string> includes;
  std::vector<std::string> excludes;
  std::vector<unsigned int> predicted_speed_indices;
  bool shortcuts_only = false;
  bool complete_graph = false;

  try {
    cxxopts::Options
        options(program,
                "exports edges and/or nodes into FlatGeoBuf files.\n");

    // clang-format off
    options.add_options()
    ("h,help", "Print this help message.")
    ("j,concurrency", "Number of threads to use.", cxxopts::value<unsigned int>())
    ("c,config", "Path to the json configuration file.", cxxopts::value<std::string>())
    ("i,inline-config", "Inline json config.",cxxopts::value<std::string>())
    ("o,costing", "Costing to use", cxxopts::value<std::string>()->default_value("none"))
    ("e,exclude-attributes", "Attributes to exclude", cxxopts::value<std::vector<std::string>>())
    ("a,include-attributes", "Attributes to include", cxxopts::value<std::vector<std::string>>())
    ("d,output-directory", "Directory in which output files will be written", cxxopts::value<std::string>())
    ("g,complete-graph", "Export the complete graph", cxxopts::value<bool>())
    ("f,search-filter", "Edge search filter as JSON. For more info see https://valhalla.github.io/valhalla/api/turn-by-turn/api-reference/#locations", cxxopts::value<std::string>())
    ("ss,predicted-speed-index-start", "At which bucket index to start exporting predicted speeds", cxxopts::value<unsigned int>())
    ("se,predicted-speed-index-end", "At which bucket index to end exporting predicted speeds", cxxopts::value<unsigned int>())
    ("t,shortcuts-only", "Whether to only output shortcut edges", cxxopts::value<bool>())
    ("u,file-suffix", "suffix to apply prior to the file extension", cxxopts::value<std::string>())
    ("TILEID", "If provided, only export features matching the passed tile IDs. Can alternatively be passed via stdin", cxxopts::value<std::vector<std::string>>());
    // clang-format on

    options.positional_help("TILEID");
    options.parse_positional({"TILEID"});

    auto result = options.parse(argc, argv);
    if (!parse_common_args(program, options, result, pt, "mjolnir.logging",
                           true))
      return EXIT_SUCCESS;

    // try from positional arguments
    if (result["TILEID"].count() != 0) {
      tile_ids = result["TILEID"].as<std::vector<std::string>>();
    }

    if (result["file-suffix"].count()) {
      file_suffix = result["file-suffix"].as<std::string>();
    }

    if (result["search-filter"].count() != 0) {
      try {
        // parse some json
        rapidjson::Document doc;
        doc.Parse(result["search-filter"].as<std::string>().c_str());

        auto min_road_class =
            rapidjson::get<std::string>(doc, "/min_road_class",
                                        "service_other");
        valhalla::RoadClass min_rc;
        if (RoadClass_Enum_Parse(min_road_class, &min_rc)) {
          search_filter.min_road_class_ = min_rc;
        }

        auto max_road_class =
            rapidjson::get<std::string>(doc, "/max_road_class",
                                        "motorway");
        valhalla::RoadClass max_rc;
        if (RoadClass_Enum_Parse(max_road_class, &max_rc)) {
          search_filter.max_road_class_ = max_rc;
        }

        search_filter.exclude_tunnel_ =
            rapidjson::get<bool>(doc, "/exclude_tunnel", false);

        search_filter.exclude_bridge_ =
            rapidjson::get<bool>(doc, "/exclude_bridge", false);

        search_filter.exclude_toll_ =
            rapidjson::get<bool>(doc, "/exclude_toll", false);

        search_filter.exclude_ramp_ =
            rapidjson::get<bool>(doc, "/exclude_ramp", false);

        search_filter.exclude_ferry_ =
            rapidjson::get<bool>(doc, "/exclude_ferry", false);

        search_filter.level_ =
            rapidjson::get<float>(doc, "/level",
                                  valhalla::baldr::kMaxLevel);

        search_filter.exclude_closures_ =
            rapidjson::get<bool>(doc, "/exclude_closures", false);
      } catch (std::exception& e) {
        LOG_ERROR("Failed to parse search filter JSON: " +
                  std::string(e.what()));
        return EXIT_FAILURE;
      }
    }

    if (result["shortcuts-only"].count() != 0) {
      shortcuts_only = true;
    }

    if (result["complete-graph"].count() != 0) {
      // collect available tiles from graph
      valhalla::baldr::GraphReader reader(pt.get_child("mjolnir"));
      for (const auto& tile : reader.GetTileSet()) {
        tile_ids.push_back(std::to_string(tile));
      }
    }

    if ((result["predicted-speed-index-start"].count() != 0) &&
        (result["predicted-speed-index-end"].count() != 0)) {
      auto start =
          result["predicted-speed-index-start"].as<unsigned int>();
      auto end = result["predicted-speed-index-end"].as<unsigned int>();

      for (unsigned int i = start; i <= end; ++i) {
        predicted_speed_indices.push_back(i);
      }
    }

    // read tile ids from stdin
    if (tile_ids.size() == 0) {
      std::string tileid;
      while (std::getline(std::cin, tileid)) {
        if (!tileid.empty()) {
          tile_ids.push_back(tileid);
        }
      }
    }

    if (tile_ids.size() == 0) {
      LOG_INFO("No Tile IDs passed, exporting all tiles");
    }

    if (result["output-directory"].count() > 0) {
      output_dir = result["output-directory"].as<std::string>();
    } else {
      throw cxxopts::exceptions::missing_argument("output-dir");
    }

    if (result["include-attributes"].count() > 0) {
      includes =
          result["include-attributes"].as<std::vector<std::string>>();
    }

    if (result["exclude-attributes"].count() > 0) {
      excludes =
          result["exclude-attributes"].as<std::vector<std::string>>();
    }

    costing_str = result["costing"].as<std::string>();

  } catch (cxxopts::exceptions::exception& e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  } catch (std::exception& e) {
    std::cerr << "Unable to parse command line options because: "
              << e.what() << "\n";
    return EXIT_FAILURE;
  }

  try {
    // register gdal drivers
    GDALAllRegister();

    AttributeFilter filter(std::move(includes), std::move(excludes),
                           std::move(predicted_speed_indices),
                           search_filter, shortcuts_only);
    valhalla::sif::cost_ptr_t costing = create_costing(costing_str);
    return export_tiles(pt, output_dir, file_suffix, costing, filter,
                        tile_ids);
  } catch (std::exception& e) {
    std::cout << "Failed to export tiles: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
}
