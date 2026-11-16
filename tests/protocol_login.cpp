#include "bds/network/login.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
void check(bool ok){if(!ok)throw std::runtime_error("login codec invariant failed");}
template<class F>void rejects(F f){bool error=false;try{f();}catch(const std::invalid_argument&){error=true;}check(error);}
}
int main() {
 try {
  // Independent Python struct fixture; no signature or identity is asserted.
  std::vector<std::uint8_t> bytes={0,0,1,26,25,12,0,0,0,123,34,99,104,97,105,110,34,58,91,93,125,5,0,0,0,97,46,98,46,99};
  const auto body=bds::decode_login_body(bytes);
  check(body.certificate_chain=="{\"chain\":[]}" && body.client_token=="a.b.c");
  for(std::size_t n=0;n<bytes.size();++n)rejects([&]{bds::decode_login_body(std::span(bytes).first(n));});
  auto bad=bytes;bad[3]=27;rejects([&]{bds::decode_login_body(bad);});
  bad=bytes;bad.push_back(0);rejects([&]{bds::decode_login_body(bad);});
  bad=bytes;bad[5]=255;bad[6]=255;bad[7]=255;bad[8]=127;rejects([&]{bds::decode_login_body(bad);});
  bad=bytes;bad[5]=0;rejects([&]{bds::decode_login_body(bad);});
  bad=bytes;bad[4]=26;bad.push_back(0);rejects([&]{bds::decode_login_body(bad);});
  bad=bytes;bad[4]=0x99;bad.insert(bad.begin()+5,0);rejects([&]{bds::decode_login_body(bad);});
  bad={0,0,1,26,255,255,255,255,31};rejects([&]{bds::decode_login_body(bad);});
  bad.resize(1024*1024+1);rejects([&]{bds::decode_login_body(bad);});
  std::cout<<"unverified protocol-282 login body decoding and bounds passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
