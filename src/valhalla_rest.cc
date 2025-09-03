#include "rest.h"

#include "argparse_utils.h"
#include <cxxopts.hpp>
#include <prime_server/prime_server.hpp>

using namespace prime_server;

int main(int argc, char** argv) {
  // store some options
  const auto program = std::filesystem::path(__FILE__).stem().string();
  boost::property_tree::ptree pt;
  std::string port;

  // read args
  // clang-format off
  cxxopts::Options options(
    program + " is a dead simple HTTP server that serves objects from a Valhalla graphs via a REST API.");

  options.add_options()
    ("h,help", "Print this help message.")
    ("c,config", "Path to the configuration file", cxxopts::value<std::string>())
    ("i,inline-config", "Inline JSON config", cxxopts::value<std::string>())
    ("p,port", "Port to listen to", cxxopts::value<std::string>(port)->default_value("8004"));

  // clang-format on
  auto result = options.parse(argc, argv);
  if (!parse_common_args(program, options, result, pt, "mjolnir.logging",
                         true))
    return EXIT_SUCCESS;

  try {
    prime_server::
        quiesce(pt.get<unsigned int>("httpd.service.drain_seconds", 28U),
                pt.get<unsigned int>("httpd.service.shutting_seconds",
                                     1U));

    std::string listen = "tcp://*:" + port;
    std::string loopback = "ipc:///tmp/loopback";
    std::string interrupt = "ipc:///tmp/interrupt";

    // setup the server & proxy within this process
    zmq::context_t context;
    std::thread server_thread = std::thread(
        std::bind(&http_server_t::serve,
                  http_server_t(context, listen, "ipc:///tmp/rest_in",
                                loopback, interrupt, true,
                                DEFAULT_MAX_REQUEST_SIZE * 30, 5)));

    std::thread proxy_thread(
        std::bind(&proxy_t::forward, proxy_t(context, "ipc:///tmp/rest_in",
                                             "ipc:///tmp/rest_out")));
    proxy_thread.detach();

    // only allow one thread
    auto worker_thread = std::thread(tools::run_service, std::cref(pt));
    worker_thread.detach();

    // wait forever (or for interrupt)
    server_thread.join();

    return EXIT_SUCCESS;
  } catch (const std::exception& e) {
    LOG_ERROR("Failed to  set up REST service: " + std::string(e.what()));
    return EXIT_FAILURE;
  }
}
