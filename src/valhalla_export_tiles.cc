
#include <cxxopts.hpp>
#include <valhalla/baldr/attributes_controller.h>
#include <valhalla/baldr/graphid.h>
#include <valhalla/baldr/graphreader.h>
#include <valhalla/mjolnir/graphtilebuilder.h>
#include <valhalla/proto/api.pb.h>
#include <valhalla/proto/options.pb.h>
#include <valhalla/proto_conversions.h>
#include <valhalla/sif/costfactory.h>
#include <valhalla/sif/dynamiccost.h>

#include "argparse_utils.h"
#include <gdal_priv.h>
#include <ogrsf_frmts.h>

namespace {

const std::string kEdgePredictedSpeeds = "edge.predicted_speeds";

struct AttributeFilter {
  AttributeFilter(std::vector<std::string>&& includes_v,
                  std::vector<std::string>&& excludes_v,
                  std::vector<unsigned int>&& predspeedindices) {
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

    std::vector<std::pair<bool&, std::string>> pairs = {
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
    };

    for (auto& p : pairs) {
      if (includes.find(p.second) != includes.end()) {
        p.first = true;
      }

      if (excludes.find(p.second) != includes.end()) {
        p.first = false;
      }
    }
  }

  bool localidx{true};
  bool road_class{true};
  bool use{true};
  bool speed{false};
  bool tunnel{false};
  bool bridge{false};
  bool traversability{false};
  bool surface{false};
  bool density{false};
  bool urban{false};
  bool predicted_speeds{false};
  std::vector<unsigned int> pred_speed_indices{};
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
                 const bool edges,
                 const bool nodes,
                 valhalla::Api& request,
                 valhalla::sif::cost_ptr_t costing,
                 GDALDriver* gdal_driver,
                 char** dataset_options,
                 const AttributeFilter& filter) {
  // get the file path
  auto suffix =
      valhalla::baldr::GraphTile::FileSuffix(tile_id.Tile_Base(), ".fgb");
  auto disk_location =
      output_dir + filesystem::path::preferred_separator + suffix;

  // make sure all the subdirectories exist
  auto dir = filesystem::path(disk_location);
  dir.replace_filename("");
  filesystem::create_directories(dir);
  LOG_INFO("Writing to disk at " + disk_location);
  GDALDataset* dataset = gdal_driver->Create(disk_location.c_str(), 0, 0,
                                             0, GDT_Unknown, nullptr);
  if (!dataset) {
    printf("Failed to create output file.\n");
    return;
  }

  // now go through the tile and convert the features
  if (!reader.DoesTileExist(tile_id)) {
    LOG_ERROR("Tile " + std::to_string(tile_id) +
              " does not exist. Skipping...");
    GDALClose(dataset);
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
  if (edges)
    edges_layer = dataset->CreateLayer("edges", &spatialRef, wkbLineString,
                                       dataset_options);

  if (nodes)
    nodes_layer = dataset->CreateLayer("nodes", &spatialRef, wkbPoint,
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

  if (filter.predicted_speeds) {
    for (const auto& i : filter.pred_speed_indices) {
      std::string name = "predspeed_" + std::to_string(i);
      OGRFieldDefn field_name(name.c_str(), OFTInteger);
      edges_layer->CreateField(&field_name);
    }
  }

  if ((edges && !edges_layer) || (nodes && !nodes_layer)) {
    LOG_ERROR("Failed to create layer.");
    GDALClose(dataset);
    return;
  }

  auto tile = reader.GetGraphTile(tile_id);

  for (size_t idx = 0; idx < tile->header()->directededgecount(); ++idx) {
    auto de = tile->directededge(idx);
    if (de->is_shortcut())
      continue;
    auto ei = tile->edgeinfo(de);

    auto shape = ei.shape();
    OGRLineString* line = ConvertToOGRLineString(shape);
    OGRFeature* feature =
        OGRFeature::CreateFeature(edges_layer->GetLayerDefn());
    feature->SetGeometryDirectly(
        line); // valhalla edge shapes are good right? right?

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
    if (filter.predicted_speeds) {
      for (const auto& i : filter.pred_speed_indices) {
        std::string field_name = "predspeed_" + std::to_string(i);
        if (de->has_predicted_speed()) {
          auto s =
              tile->GetSpeed(de, valhalla::baldr::kPredictedFlowMask,
                             i * valhalla::baldr::kSpeedBucketSizeSeconds);
          feature->SetField(field_name.c_str(), static_cast<int>(s));
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

  GDALClose(dataset);
};

void work(boost::property_tree::ptree& config,
          const std::string& output_dir,
          bool edges,
          bool nodes,
          valhalla::Api& request,
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
    export_tile(reader, tile_id, output_dir, edges, nodes, request,
                costing, driver, dataset_options, filter);
  }
}

/**
 * Exports features that match the passed tileids to the specified
 * directory
 *
 * @param config the config object
 * @param output_dir the directory to which the files will be written
 * (follows graph tile structuring within the directory)
 * @param edges whether to export edges
 * @param nodes whether to export nodes
 * @param request some request options, right now only used for the costing
 * and attribute controlling
 * @param costing the costing to filter allowed/disallowed edges
 * @param filter which attributes to include/exclude
 * @param tile_ids which tiles to export
 */
int export_tiles(boost::property_tree::ptree& config,
                 const std::string& output_dir,
                 const bool edges,
                 const bool nodes,
                 valhalla::Api& request,
                 valhalla::sif::cost_ptr_t costing,
                 const AttributeFilter& filter,
                 std::vector<std::string>& tile_ids) {

  std::queue<valhalla::baldr::GraphId> tile_queue;

  // fill up a queue
  for (const auto& tile_id : tile_ids) {
    tile_queue.emplace(tile_id);
  }
  // no need for this anymore
  tile_ids.resize(0);
  tile_ids.shrink_to_fit();

  // multithread it
  std::vector<std::shared_ptr<std::thread>> threads(
      config.get<size_t>("mjolnir.concurrency"));
  std::mutex lock;

  for (size_t i = 0; i < threads.size(); ++i) {
    threads[i] =
        std::make_shared<std::thread>(work, std::ref(config),
                                      std::cref(output_dir), edges, nodes,
                                      std::ref(request), costing,
                                      std::cref(filter),
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
  std::vector<std::string> ftypes = {"edges"};
  valhalla::Api request;
  std::string costing_str;
  std::vector<std::string> includes;
  std::vector<std::string> excludes;
  std::vector<unsigned int> predicted_speed_indices;
  bool edges = false;
  bool nodes = false;
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
    ("o,costing", "Costing to use", cxxopts::value<std::string>())
    ("e,exclude-attributes", "Attributes to exclude", cxxopts::value<std::vector<std::string>>())
    ("a,include-attributes", "Attributes to include", cxxopts::value<std::vector<std::string>>())
    ("f,feature-type", "Feature types to output (currently supports node and edges, defaults to edges only)", cxxopts::value<std::vector<std::string>>())
    ("d,output-directory", "Directory in which output files will be written", cxxopts::value<std::string>())
    ("g,complete-graph", "Export the complete graph", cxxopts::value<bool>())
    ("s,predicted-speed-indices", "Which predicted speed buckets to include, if any", cxxopts::value<std::vector<unsigned int>>())
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

    if (result["complete-graph"].count() != 0) {
      // collect available tiles from graph
      valhalla::baldr::GraphReader reader(pt.get_child("mjolnir"));
      for (const auto& tile : reader.GetTileSet()) {
        tile_ids.push_back(std::to_string(tile));
      }
    }

    if (result["predicted-speed-indices"].count() != 0) {
      predicted_speed_indices = result["predicted-speed-indices"]
                                    .as<std::vector<unsigned int>>();
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

    if (result["feature-type"].count() > 0) {
      ftypes = result["feature-type"].as<std::vector<std::string>>();
    } else {
      LOG_INFO("No feature-type argument detected, defaulting to edges");
    }
    for (const auto& ftype : ftypes) {
      if (ftype == "edges") {
        edges = true;
      } else if (ftype == "nodes") {
        nodes = true;
      } else {
        LOG_WARN("Detected unsupported feature-type " + ftype);
      }
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
                           std::move(predicted_speed_indices));
    valhalla::sif::cost_ptr_t costing = create_costing(costing_str);
    return export_tiles(pt, output_dir, edges, nodes, request, costing,
                        filter, tile_ids);
  } catch (std::exception& e) {
    std::cout << "Failed to export tiles: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
}