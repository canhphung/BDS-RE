#include "bds/core/runtime.hpp"
#include <stdexcept>
#include "bds/world/world.hpp"
#include "bds/world/store.hpp"
#include "bds/world/seed.hpp"
#include "bds/core/logger.hpp"
#include <string>
#include <thread>
#include "bds/core/console.hpp"
#include "bds/server/control.hpp"
#include "bds/network/discovery.hpp"
namespace bds {
TickScheduler::TickScheduler(Duration step,unsigned limit):step_(step),limit_(limit) {
 if(step<=Duration::zero() || limit==0) throw std::invalid_argument("invalid scheduler");
}
unsigned TickScheduler::advance(Duration elapsed) {
 if(elapsed<Duration::zero()) throw std::invalid_argument("negative elapsed time");
 // Decompose before addition, so even Duration::max() cannot overflow.
 auto whole=elapsed/step_;
 auto fraction=elapsed%step_;
 bool carry=fraction>=step_-pending_;
 pending_=carry ? fraction-(step_-pending_) : pending_+fraction;
 if(whole>=limit_) return limit_; // discard excess whole ticks, keep phase
 auto due=static_cast<unsigned>(whole)+(carry?1U:0U);
 return due>limit_?limit_:due;
}
std::uint64_t ServerRuntime::run(const std::function<bool()>& stop) {
 using Clock=std::chrono::steady_clock;
 TickScheduler scheduler(std::chrono::milliseconds(config_.tick_ms), config_.max_catch_up);
 auto properties=Properties::load(config_.root/"server.properties");
 for(const auto& diagnostic:properties.diagnostics)log(diagnostic);
 auto world_dir=config_.world_file.empty()?properties.world_directory(config_.root):std::filesystem::absolute(config_.world_file).parent_path();
 if(std::filesystem::exists(world_dir/"level.dat") || std::filesystem::exists(world_dir/"db"))
  throw std::runtime_error("original Bedrock storage is not supported; refusing to modify this world");
 std::filesystem::create_directories(world_dir);
 auto world_file=config_.world_file.empty()?world_dir/"bdsre-world.snapshot":config_.world_file;
 World world;
 if(std::filesystem::exists(world_file))world=WorldStore::load(world_file);
 // A version-2 snapshot stores its generator settings, so its kind is nonzero
 // and wins. A version-1 snapshot has explicit columns and no settings: those
 // columns become legacy_columns_ and the configured generator fills the rest,
 // which is the behaviour World::configure_terrain already provides.
 if(config_.network && properties.level_type!="LEGACY" && !world.terrain().kind) {
  // DedicatedServer::run 0x1808d3d: LevelSettings::parseSeedString(getLevelSeed(),
  // Level::createRandomSeed()). A missing, one-character or otherwise unusable
  // seed resolves to a fresh random_device draw, not to a fixed hash; see
  // src/world/seed.cpp and tests/reference/seed_bds_1.6.0.15.json.
  // FLAT keeps the simple layered generator and DEFAULT keeps the independent
  // rolling terrain, which is what the integration fixtures expect. The
  // translated OverworldGenerator (kind 3, loadChunk 0x1b38610) is selected
  // with the non-documented level-type=OVERWORLD so it can be exercised
  // without changing the default world's behaviour. It is slower because the
  // native carvers sweep a 17x17 chunk neighbourhood per chunk (about 110 ms
  // per chunk in a Debug build), and the original hides that on worker threads.
  const unsigned generator_kind = properties.level_type == "FLAT" ? 1U
                                 : properties.level_type == "OVERWORLD" ? 3U
                                                                        : 2U;
  world.configure_terrain({generator_kind,
                           static_cast<std::uint64_t>(resolve_level_seed(properties.seed))});
 }
 AccessLists access(config_.root);access.reload_whitelist();access.reload_operators();
 Backup backup(world_dir);ServerControl control(properties,access,backup);ConsoleInput console;
 std::unique_ptr<DiscoveryServer> network;
 if(config_.network) {
  network=std::make_unique<DiscoveryServer>(properties);
  // Wording follows RakPeerHelper::LogIPSupport (0x192fce0).
  for(const bool ipv6:{false,true}) {
   const auto port=network->bound_port(ipv6);
   log(std::string(ipv6?"IPv6 ":"IPv4 ")+(port?"supported, port: "+std::to_string(*port):"not supported"));
  }
 }
 log("world directory: "+world_dir.string());
 log("console ready; storage is BDS-RE internal; basic gameplay ready");
 auto last=Clock::now();auto saved_at=last;
 std::uint64_t ticks=0;
 try {
 while(!stop() && !control.stopping() && (config_.ticks==0 || ticks<config_.ticks)) {
  if(network)network->poll(properties,access,world);
  backup.poll();
  if(network && Clock::now()-saved_at>=std::chrono::seconds(5)){WorldStore::save(world,world_file);saved_at=Clock::now();}
  for(const auto& line:console.poll()) {
   auto output=control.execute(line,world);if(!output.empty())log(output);
   if(control.stopping())break;
  }
  if(control.stopping())break;
  auto now=Clock::now(); auto due=scheduler.advance(now-last); last=now;
  for(unsigned i=0;i<due && !stop() && (config_.ticks==0 || ticks<config_.ticks);++i) {
   world.tick();
   if(network)network->tick();
   ++ticks;
  }
  if(config_.ticks!=0 && ticks==config_.ticks) break;
  std::this_thread::sleep_until(last+scheduler.remaining());
 }
 } catch(const std::exception& e) {
  // Only the simulation thread mutates World, between whole ticks, so the
  // state is consistent here even when networking or console handling failed.
  log(std::string("runtime failure: ")+e.what()+"; attempting emergency save");
  try {WorldStore::save(world,world_file);log("emergency save world age="+std::to_string(world.age()));}
  catch(const std::exception& save) {log(std::string("emergency save failed: ")+save.what());}
  throw;
 }
 WorldStore::save(world,world_file);
 log("saved world age="+std::to_string(world.age()));
 return ticks;
}
}
