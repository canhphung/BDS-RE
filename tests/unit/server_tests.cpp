#include "bds/server/control.hpp"
#include "bds/core/console.hpp"
#include "bds/world/store.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <random>
#include <thread>
namespace {
void check(bool value){if(!value)throw std::runtime_error("server invariant failed");}
template<class F>void rejects(F f){bool error=false;try{f();}catch(const std::exception&){error=true;}check(error);}
struct Temp {
 std::filesystem::path path;
 Temp(){std::random_device r;for(unsigned i=0;i<32;++i){auto p=std::filesystem::temp_directory_path()/("bds-server-"+std::to_string(r()));if(std::filesystem::create_directory(p)){path=p;return;}}throw std::runtime_error("temp allocation failed");}
 ~Temp(){std::error_code e;std::filesystem::remove_all(path,e);}
};
void write(const std::filesystem::path& p,const std::string& s){std::ofstream f(p,std::ios::binary);f<<s;check(static_cast<bool>(f));}
}
int main(int argc,char** argv) {
 try {
  Temp temp;
  check(argc==2);
  auto shipped=bds::Properties::load(argv[1]);
  check(shipped.server_name=="Dedicated Server" && shipped.level_name=="Bedrock level" && shipped.view_distance==32 && shipped.max_threads==8);
  check(shipped.port==19132 && shipped.port_v6==19133 && shipped.permission=="member");
  check(shipped.diagnostics==std::vector<std::string>{"unknown property: default-player-permission"});
  std::istringstream input("#comment\r\nserver-name=Test = Name\r\nlevel-name=World With Spaces\nview-distance=32\ntick-distance=12\nmax-threads=0\ngamemode=2\ndifficulty=3\nwhite-list=true\ndefault-player-permission-level=operator\nlevel-seed=hello\n ignored=ignored\n");
  auto properties=bds::Properties::parse(input);
  check(properties.server_name=="Test = Name" && properties.level_name=="World With Spaces");
  check(properties.max_threads==0 && properties.gamemode==2 && properties.difficulty==3 && properties.tick_distance==12 && properties.whitelist && properties.permission=="operator");
  check(properties.world_directory(temp.path)==std::filesystem::weakly_canonical(temp.path/"worlds"/"World With Spaces"));
  const auto old_cwd=std::filesystem::current_path();
  std::filesystem::current_path(temp.path);
  try {check(properties.world_directory(".")==properties.world_directory(temp.path));}
  catch(...){std::filesystem::current_path(old_cwd);throw;}
  std::filesystem::current_path(old_cwd);
  for(const auto& text:{"server-port=0","server-portv6=65536","tick-distance=3","tick-distance=13","max-players=-1","view-distance=0","max-threads=1x","allow-cheats=maybe"})
   rejects([&]{std::istringstream bad(text);bds::Properties::parse(bad);});
  std::istringstream alias("default-player-permission=operator\n");auto original=bds::Properties::parse(alias);
  check(original.permission=="member" && original.diagnostics.size()==1);
  for(const auto& name:{"../escape","a/b","a\\b","..","","C:escape"}){auto bad=properties;bad.level_name=name;rejects([&]{bad.world_directory(temp.path);});}
  check(bds::command_words("whitelist add \"Example Name\"")==std::vector<std::string>({"whitelist","add","Example Name"}));
  rejects([]{bds::command_words("whitelist add \"unfinished");});
  bds::AccessLists access(temp.path);access.reload_whitelist();access.reload_operators();check(access.whitelist().empty());
  check(access.add("Example Name") && !access.add("Example Name"));access.reload_whitelist();check(access.find("Example Name","")!=nullptr);
  write(temp.path/"whitelist.json","[{\"name\":\"Player\",\"xuid\":\"123\",\"ignoresPlayerLimit\":true}]");
  access.reload_whitelist();check(access.find("Wrong Name","123")->ignores_limit);check(access.find("Player","999")==nullptr);
  write(temp.path/"whitelist.json","[{\"name\":1}]");rejects([&]{access.reload_whitelist();});check(access.whitelist().size()==1 && access.whitelist()[0].xuid=="123");
  write(temp.path/"opsfile.json","[{\"xuid\":\"123\"}]");access.reload_operators();check(access.is_operator("123") && !access.is_operator(""));
  write(temp.path/"opsfile.json","[{\"xuid\":\"123\"},{\"xuid\":\"123\"}]");rejects([&]{access.reload_operators();});check(access.is_operator("123"));
  check(access.remove("Player") && !access.remove("Player"));access.reload_whitelist();check(access.whitelist().empty());
  auto world_dir=properties.world_directory(temp.path);std::filesystem::create_directories(world_dir);
  bds::World world;world.set_block({-1,255,16},42);world.tick();
  bds::Backup backup(world_dir);bds::ServerControl control(properties,access,backup);
  auto result=control.execute("save query",world);check(result.starts_with("error:"));
  check(control.execute("save hold",world)=="backup preparing");rejects([&]{backup.hold(world);});
  auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  while(backup.state()==bds::Backup::State::preparing && std::chrono::steady_clock::now()<deadline){world.tick();backup.poll();std::this_thread::yield();}
  check(backup.state()==bds::Backup::State::ready && world.age()>1);
  const auto file=backup.file();check(std::filesystem::file_size(file)==backup.length());
  auto captured=bds::WorldStore::load(file);check(captured.age()==1 && captured.block({-1,255,16})==42);
  world.set_block({-1,255,16},7);check(bds::WorldStore::load(file).block({-1,255,16})==42);
  check(control.execute("save query",world).starts_with("backup ready:"));
  check(control.execute("changesetting allow-cheats true",world).starts_with("setting changed"));check(properties.cheats);
  check(control.execute("changesetting difficulty peaceful",world).starts_with("setting changed"));check(properties.difficulty==0);
  check(control.execute("changesetting max-players 4",world).starts_with("error:"));
  check(control.execute("whitelist off",world).find("runtime only")!=std::string::npos && !properties.whitelist);
  check(control.execute("save resume",world)=="backup resumed" && !std::filesystem::exists(file));
  check(control.execute("stop extra",world).starts_with("error:") && !control.stopping());
  check(control.execute("stop",world)=="stopping server" && control.stopping());
  std::cout<<"properties, access lists, commands and asynchronous backup passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
