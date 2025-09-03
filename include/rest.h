#pragma once
#include <absl/strings/str_format.h>
#include <boost/property_tree/ptree.hpp>
#include <prime_server/http_protocol.hpp>
#include <prime_server/http_util.hpp>
#include <prime_server/prime_server.hpp>
#include <valhalla/proto/api.pb.h>
#include <valhalla/sif/dynamiccost.h>
#include <valhalla/valhalla.h>

namespace tools {
static prime_server::headers_t::value_type
    CORS{"Access-Control-Allow-Origin", "*"};

class rest_worker_t {
public:
  rest_worker_t(const boost::property_tree::ptree& pt);

  ~rest_worker_t();

  prime_server::worker_t::result_t
  work(const std::list<zmq::message_t>& job,
       void* request_info,
       const std::function<void()>& interrupt);

  void cleanup();

protected:
  /**
   * Signals the start of the worker, sends statsd message if so configured
   */
  void started();

  inline prime_server::worker_t::result_t
  to_response(const std::string& data,
              prime_server::http_request_info_t& request_info) const {

    prime_server::headers_t headers{CORS};
    auto status_code = 204U;
    if (!data.empty()) {
      headers.emplace(prime_server::http::JSON_MIME);
      status_code = 200U;
    }
    prime_server::worker_t::result_t result{false,
                                            std::list<std::string>(), ""};
    prime_server::http_response_t response(status_code, "OK", data,
                                           headers);
    response.from_info(request_info);
    result.messages.emplace_back(response.to_string());
    return result;
  }
  valhalla::baldr::GraphReader reader;
};

void run_service(const boost::property_tree::ptree& pt);

} // namespace tools
