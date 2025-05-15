#include <cxxopts.hpp>
#include <iomanip>
#include <iostream>
#include <valhalla/baldr/predictedspeeds.h>

namespace {

inline constexpr std::array<const char[4], 7> days_of_week{
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
};

inline constexpr uint32_t kBucketsPerDay = 288;
void print_bucket_speeds(const std::string& encoded) {
  std::array<int16_t, 200> coefs =
      valhalla::baldr::decode_compressed_speeds(encoded);
  for (size_t i = 0; i < valhalla::baldr::kBucketsPerWeek; ++i) {
    auto day = i / kBucketsPerDay;
    auto minutes_of_day = i % kBucketsPerDay * 5;
    auto hour = minutes_of_day / 60;
    auto minutes = minutes_of_day % 60;
    auto speed = valhalla::baldr::decompress_speed_bucket(&coefs[0], i);
    std::cout << days_of_week[day] << " " << std::setw(2)
              << std::setfill('0') << hour << ":" << std::setw(2)
              << std::setfill('0') << minutes << " " << speed << "\n";
  }
}
} // namespace

int main(int argc, char** argv) {
  // store some options
  std::vector<std::string> encoded;
  // clang-format off
  cxxopts::Options options(
    "valhalla_decode_buckets",
    "valhalla_decode_buckets"
    "\n\nvalhalla_decode_buckets is a program that decodes\n"
    "encoded speed buckets.\n");
  
  options.add_options()
    ("h,help", "Print this help message.")
    ("ENCODED", "The encoded string to process", cxxopts::value<std::vector<std::string>>());
  // clang-format on

  options.custom_help("ENCODED");
  options.positional_help("The encoded string to process");
  options.parse_positional({"ENCODED"});
  auto vm = options.parse(argc, argv);

  if (vm.count("help")) {
    std::cout << options.help() << "\n";
    return EXIT_SUCCESS;
  }

  if (vm.count("ENCODED") == 1) {
    encoded = vm["ENCODED"].as<std::vector<std::string>>();
  } else {
    std::cerr << "Single encoded speeds string required\n";
    return EXIT_FAILURE;
  }

  print_bucket_speeds(encoded[0]);
  return EXIT_SUCCESS;
}