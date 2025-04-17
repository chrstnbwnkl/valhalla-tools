
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

struct AttributeFilter {
  AttributeFilter(const std::vector<std::string>& includes,
                  const std::vector<std::string>& excludes) {
    for (const auto& include : includes) {
      if (include == valhalla::baldr::kEdgeId) {
        localidx = true;
        continue;
      }
      if (include == valhalla::baldr::kEdgeRoadClass) {
        road_class = true;
        continue;
      }
      if (include == valhalla::baldr::kEdgeUse) {
        use = true;
        continue;
      }
      if (include == valhalla::baldr::kEdgeSpeed) {
        speed = true;
        continue;
      }
      if (include == valhalla::baldr::kEdgeTunnel) {
        tunnel = true;
        continue;
      }
      if (include == valhalla::baldr::kEdgeBridge) {
        bridge = true;
        continue;
      }
      if (include == valhalla::baldr::kEdgeTraversability) {
        traversability = true;
        continue;
      }
      if (include == valhalla::baldr::kEdgeSurface) {
        surface = true;
        continue;
      }
    }

    for (const auto& exclude : excludes) {
      if (exclude == valhalla::baldr::kEdgeId) {
        localidx = false;
        continue;
      }
      if (exclude == valhalla::baldr::kEdgeRoadClass) {
        road_class = false;
        continue;
      }
      if (exclude == valhalla::baldr::kEdgeUse) {
        use = false;
        continue;
      }
      if (exclude == valhalla::baldr::kEdgeSpeed) {
        speed = false;
        continue;
      }
      if (exclude == valhalla::baldr::kEdgeTunnel) {
        tunnel = false;
        continue;
      }
      if (exclude == valhalla::baldr::kEdgeBridge) {
        bridge = false;
        continue;
      }
      if (exclude == valhalla::baldr::kEdgeTraversability) {
        traversability = false;
        continue;
      }
      if (exclude == valhalla::baldr::kEdgeSurface) {
        surface = false;
        continue;
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
};

OGRLineString* ConvertToOGRLineString(const std::vector<PointLL>& points) {
  OGRLineString* line = new OGRLineString();
  for (const auto& pt : points) {
    line->addPoint(pt.lng(), pt.lat()); // Note: OGR uses (X=lon, Y=lat)
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

  if ((edges && !edges_layer) || (nodes && !nodes_layer)) {
    LOG_ERROR("Failed to create layer.");
    GDALClose(dataset);
    return;
  }

  // now go through the tile and convert the features
  if (!reader.DoesTileExist(tile_id)) {
    LOG_ERROR("Tile " + std::to_string(tile_id) +
              " does not exist. Skipping...");
    return;
  }
  // Trim reader if over-committed
  if (reader.OverCommitted()) {
    reader.Trim();
  }

  auto tile = reader.GetGraphTile(tile_id);

  for (size_t idx = 0; idx < tile->header()->directededgecount(); ++idx) {
    auto de = tile->directededge(idx);
    // if (!de->is_shortcut())
    //   continue;
    auto ei = tile->edgeinfo(de);

    auto shape = ei.shape();
    OGRLineString* line = ConvertToOGRLineString(shape);
    OGRFeature* feature =
        OGRFeature::CreateFeature(edges_layer->GetLayerDefn());
    feature->SetGeometry(line);

    if (filter.localidx) {
      feature->SetField("edgeid", static_cast<int>(idx));
    }
    if (filter.road_class) {
      feature->SetField("road_class",
                        valhalla::baldr::to_string(de->classification())
                            .c_str());
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
  bool edges = false;
  bool nodes = false;

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

    AttributeFilter filter(includes, excludes);
    valhalla::sif::cost_ptr_t costing = create_costing(costing_str);
    return export_tiles(pt, output_dir, edges, nodes, request, costing,
                        filter, tile_ids);
  } catch (std::exception& e) {
    std::cout << "Failed to export tiles: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
}