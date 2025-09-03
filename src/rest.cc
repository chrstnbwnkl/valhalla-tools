#include "rest.h"
#include <prime_server/http_protocol.hpp>
#include <string>
#include <valhalla/baldr/rapidjson_utils.h>

namespace {

using namespace valhalla::baldr;

enum class ObjectType : uint8_t { EDGE = 0, NODE = 1 };

bool object_type_from_string(const std::string& object_type_str,
                             ObjectType* object_type) {
  if (!object_type)
    throw std::runtime_error("Object type pointer is null");

  static std::unordered_map<std::string, ObjectType> types{
      {"edge", ObjectType::EDGE},
      {"node", ObjectType::NODE},
  };

  auto it = types.find(object_type_str);
  if (it == types.end())
    return false;

  *object_type = it->second;
  return true;
}

// these serializers are not part of the lib
// so these are just copied from upstream

void get_access_restrictions(const graph_tile_ptr& tile,
                             rapidjson::writer_wrapper_t& writer,
                             uint32_t edge_idx) {
  for (const auto& res :
       tile->GetAccessRestrictions(edge_idx, kAllAccess)) {
    res.json(writer);
  }
}

void serialize_traffic_speed(
    const volatile valhalla::baldr::TrafficSpeed& traffic_speed,
    rapidjson::writer_wrapper_t& writer) {
  if (traffic_speed.speed_valid()) {
    writer.set_precision(2);
    writer("overall_speed",
           static_cast<uint64_t>(traffic_speed.get_overall_speed()));
    auto speed = static_cast<uint64_t>(traffic_speed.get_speed(0));
    if (speed == UNKNOWN_TRAFFIC_SPEED_KPH)
      writer("speed_0", nullptr);
    else
      writer("speed_0", speed);
    auto congestion = (traffic_speed.congestion1 - 1.0) / 62.0;
    if (congestion < 0)
      writer("congestion_0", nullptr);
    else {
      writer("congestion_0", congestion);
    }
    writer("breakpoint_0", traffic_speed.breakpoint1 / 255.0);

    speed = static_cast<uint64_t>(traffic_speed.get_speed(1));
    if (speed == UNKNOWN_TRAFFIC_SPEED_KPH)
      writer("speed_1", nullptr);
    else
      writer("speed_1", speed);
    congestion = (traffic_speed.congestion2 - 1.0) / 62.0;
    if (congestion < 0)
      writer("congestion_1", nullptr);
    else {
      writer("congestion_1", congestion);
    }
    writer("breakpoint_1", traffic_speed.breakpoint2 / 255.0);

    speed = static_cast<uint64_t>(traffic_speed.get_speed(2));
    if (speed == UNKNOWN_TRAFFIC_SPEED_KPH)
      writer("speed_2", nullptr);
    else
      writer("speed_2", speed);
    congestion = (traffic_speed.congestion3 - 1.0) / 62.0;
    if (congestion < 0)
      writer("congestion_2", nullptr);
    else {
      writer("congestion_2", congestion);
    }
    writer.set_precision(3);
  }
}
std::string serialize_edge(valhalla::baldr::GraphReader& reader,
                           const valhalla::baldr::GraphId id) {
  rapidjson::writer_wrapper_t writer;
  writer.start_object();
  try {
    // get the osm way id
    auto tile = reader.GetGraphTile(id);
    auto* directed_edge = tile->directededge(id.id());
    auto edge_info = tile->edgeinfo(directed_edge);
    // they want MOAR!
    // live traffic information
    const volatile auto& traffic = tile->trafficspeed(directed_edge);

    // incident information
    if (traffic.has_incidents) {
      // TODO: incidents
    }
    writer.start_array("access_restrictions");
    get_access_restrictions(tile, writer, id.id());
    writer.end_array();
    // write live_speed
    writer.start_object("live_speed");
    serialize_traffic_speed(traffic, writer);
    writer.end_object();

    // basic rest of it plus edge metadata
    writer.set_precision(6);

    writer.set_precision(5);
    writer.set_precision(1);
    writer("shoulder", directed_edge->shoulder());

    writer.set_precision(6);
    writer.start_object("edge_info");
    edge_info.json(writer);
    writer.end_object();

    writer.start_object("edge");
    directed_edge->json(writer);
    writer.end_object();

    writer.start_object("edge_id");
    id.json(writer);
    writer.end_object();

    // historical traffic information
    writer.start_array("predicted_speeds");
    if (directed_edge->has_predicted_speed()) {
      for (auto sec = 0; sec < valhalla::midgard::kSecondsPerWeek;
           sec += 5 * valhalla::midgard::kSecPerMinute) {
        writer(static_cast<uint64_t>(
            tile->GetSpeed(directed_edge, kPredictedFlowMask, sec)));
      }
    }
    writer.end_array();
  } catch (const std::exception& e) {
    throw std::runtime_error("Unable to serialize edge: " +
                             std::string(e.what()));
  }
  writer.end_object();

  return writer.get_buffer();
}

std::string answer(const prime_server::http_request_t& request,
                   valhalla::baldr::GraphReader& reader) {
  if (request.path.empty() || request.path.size() <= 1)
    throw std::runtime_error("Path cannot be empty");

  if (!request.path.starts_with("/"))
    throw std::runtime_error("Invalid path: " + request.path);

  auto idx = request.path.find("/", 1);

  if (idx == std::string::npos)
    throw std::runtime_error("Invalid path: " + request.path);

  std::string obj_type = request.path.substr(1, idx - 1);

  ObjectType type;
  if (!object_type_from_string(obj_type, &type))
    std::runtime_error("Invalid object type: " + obj_type);

  std::string id_str =
      request.path.substr(idx + 1, request.path.size() - 1);
  uint64_t id = 0;
  try {
    id = stoull(id_str);
  } catch (std::exception& e) {
    throw std::runtime_error("Invalid ID: " + id_str + "; " + e.what());
  }

  switch (type) {
    case ObjectType::EDGE:
      return serialize_edge(reader, valhalla::baldr::GraphId(id));
    default:
      return "Not yet implemented: " + obj_type;
  }
}
using namespace prime_server;
using namespace tools;
worker_t::result_t
serialize_error(const std::exception& exception,
                prime_server::http_request_info_t& request_info) {
  std::stringstream body;
  rapidjson::writer_wrapper_t writer(4096);

  // do the writer
  writer.start_object();

  writer("error", std::string(exception.what()));

  writer.end_object();

  worker_t::result_t result{false, std::list<std::string>(), ""};
  http_response_t response(400, "Bad Request", writer.get_buffer(),
                           headers_t{CORS, prime_server::http::JSON_MIME});
  response.from_info(request_info);
  result.messages.emplace_back(response.to_string());

  return result;
}

} // namespace

namespace tools {
rest_worker_t::rest_worker_t(const boost::property_tree::ptree& pt)
    : reader(pt.get_child("mjolnir")) {

  started();
}

rest_worker_t::~rest_worker_t() {
}

void rest_worker_t::started() {
}

prime_server::worker_t::result_t
rest_worker_t::work(const std::list<zmq::message_t>& job,
                    void* request_info,
                    const std::function<void()>& interrupt_function) {
  auto& info =
      *static_cast<prime_server::http_request_info_t*>(request_info);
  LOG_INFO("Got Rest Request " + std::to_string(info.id));
  prime_server::worker_t::result_t result{false, {}, {}};
  try {

    // request parsing
    auto http_request =
        prime_server::http_request_t::from_string(static_cast<const char*>(
                                                      job.front().data()),
                                                  job.front().size());

    result = to_response(answer(http_request, reader), info);

    if (http_request.method != prime_server::method_t::GET) {
      throw std::runtime_error("Only GET requests are allowed");
    }
  } catch (const std::exception& e) {
    LOG_WARN("400::" + std::string(e.what()) +
             " request_id=" + std::to_string(info.id));
    result = serialize_error(e, info);
  }

  return result;
}

void rest_worker_t::cleanup() {
}

void run_service(const boost::property_tree::ptree& pt) {
  // gracefully shutdown when asked via SIGTERM
  quiesce(pt.get<unsigned int>("httpd.service.drain_seconds", 28U),
          pt.get<unsigned int>("httpd.service.shutting_seconds", 1U));

  // or returns just location information back to the server
  std::string loopback = "ipc:///tmp/loopback";
  std::string interrupt = "ipc:///tmp/interrupt";

  // listen for requests
  zmq::context_t context;
  rest_worker_t rest_worker(pt);
  worker_t worker(context, "ipc:///tmp/rest_out", "ipc:///dev/null",
                  loopback, interrupt,
                  std::bind(&rest_worker_t::work, std::ref(rest_worker),
                            std::placeholders::_1, std::placeholders::_2,
                            std::placeholders::_3),
                  std::bind(&rest_worker_t::cleanup,
                            std::ref(rest_worker)));
  worker.work();
}

} // namespace tools
