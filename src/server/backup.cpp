#include "bds/server/backup.hpp"
#include "bds/world/store.hpp"
#include <chrono>
#include <random>
#include <stdexcept>
namespace bds {
void Backup::hold(const World& world) {
 if(state_!=State::idle)throw std::logic_error("backup already held; query or resume first");
 auto base=world_dir_/".bdsre-backups";std::filesystem::create_directories(base);
 std::random_device random;directory_.clear();
 for(unsigned i=0;i<32;++i){auto path=base/std::to_string(random());if(std::filesystem::create_directory(path)){directory_=path;break;}}
 if(directory_.empty())throw std::runtime_error("cannot allocate backup directory");
 try {
  auto snapshot=world;
  worker_=std::async(std::launch::async,[snapshot=std::move(snapshot),file=directory_/"world.snapshot"] {WorldStore::save(snapshot,file);});
  state_=State::preparing;error_.clear();
 } catch(...) {std::filesystem::remove_all(directory_);directory_.clear();throw;}
}
void Backup::poll() {
 if(state_!=State::preparing || worker_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
 try {worker_.get();length_=std::filesystem::file_size(directory_/"world.snapshot");state_=State::ready;}
 catch(const std::exception& e){error_=e.what();state_=State::failed;}
}
void Backup::resume() {
 poll();
 if(state_==State::preparing)throw std::logic_error("backup is still preparing");
 if(state_==State::idle)throw std::logic_error("no held backup");
 std::filesystem::remove_all(directory_);directory_.clear();length_=0;state_=State::idle;error_.clear();
}
std::filesystem::path Backup::file() const {
 if(state_!=State::ready)throw std::logic_error("backup is not ready");
 return directory_/"world.snapshot";
}
std::uintmax_t Backup::length() const {
 if(state_!=State::ready)throw std::logic_error("backup is not ready");
 return length_;
}
}
