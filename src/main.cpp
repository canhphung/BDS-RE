#include "bds/core/config.hpp"
#include "bds/core/logger.hpp"
#include "bds/core/runtime.hpp"
#include <csignal>
#include <exception>
#include <string>
#include <vector>
namespace { volatile std::sig_atomic_t stopping=0; extern "C" void stop_handler(int) { stopping=1; } }
int main(int argc,char** argv) {
 try {
  std::vector<std::string_view> args;
  for(int i=1;i<argc;++i) args.emplace_back(argv[i]);
  auto config=bds::parse_config(args);
  std::signal(SIGINT,stop_handler); std::signal(SIGTERM,stop_handler);
  bds::log("independent runtime starting; Minecraft login/world streaming not implemented");
  bds::ServerRuntime runtime(config);
  auto ticks=runtime.run([]{return stopping!=0;});
  bds::log("stopped ticks="+std::to_string(ticks));
  return 0;
 } catch(const std::exception& e) { bds::log(e.what()); return 1; }
}
