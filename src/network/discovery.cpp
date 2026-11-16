#include "bds/network/discovery.hpp"
#include <algorithm>
#include <array>
#include <random>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#endif
namespace bds {
namespace {
constexpr std::array<std::uint8_t,16> magic={0x00,0xff,0xff,0x00,0xfe,0xfe,0xfe,0xfe,0xfd,0xfd,0xfd,0xfd,0x12,0x34,0x56,0x78};
void big_endian(std::vector<std::uint8_t>& out,std::uint64_t value,unsigned width) {
 for(unsigned i=width;i>0;--i)out.push_back(static_cast<std::uint8_t>(value>>(8*(i-1))));
}
std::string field(std::string text) {
 for(char& c:text)if(c==';' || static_cast<unsigned char>(c)<32)c=' ';
 if(text.size()>256) {
  std::size_t end=256;
  while(end>0 && (static_cast<unsigned char>(text[end])&0xc0)==0x80)--end;
  text.resize(end);
 }
 return text;
}
#ifdef _WIN32
using Socket=SOCKET;constexpr Socket invalid=INVALID_SOCKET;
void close_socket(Socket s){if(s!=invalid)closesocket(s);}
bool would_block(){const auto error=WSAGetLastError();return error==WSAEWOULDBLOCK || error==WSAEINTR || error==WSAEMSGSIZE || error==WSAECONNRESET;}
#else
using Socket=int;constexpr Socket invalid=-1;
void close_socket(Socket s){if(s!=invalid)::close(s);}
bool would_block(){return errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR;}
#endif
Socket bind_socket(int family,unsigned port) {
 Socket s=::socket(family,SOCK_DGRAM,IPPROTO_UDP);
 if(s==invalid)throw std::runtime_error("cannot create UDP socket");
 try {
#ifdef _WIN32
  BOOL exclusive=TRUE;
  if(setsockopt(s,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&exclusive),sizeof(exclusive))!=0)throw std::runtime_error("UDP exclusive bind failed");
  u_long nonblocking=1;if(ioctlsocket(s,FIONBIO,&nonblocking)!=0)throw std::runtime_error("UDP nonblocking failed");
#else
  if(fcntl(s,F_SETFL,O_NONBLOCK)!=0)throw std::runtime_error("UDP nonblocking failed");
#endif
  if(family==AF_INET) {
   sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(static_cast<unsigned short>(port));address.sin_addr.s_addr=htonl(INADDR_ANY);
   if(::bind(s,reinterpret_cast<const sockaddr*>(&address),sizeof(address))!=0)throw std::runtime_error("cannot bind configured IPv4 port");
  } else {
   int only=1;
   if(setsockopt(s,IPPROTO_IPV6,IPV6_V6ONLY,reinterpret_cast<const char*>(&only),sizeof(only))!=0)throw std::runtime_error("IPv6-only setting failed");
   sockaddr_in6 address{};address.sin6_family=AF_INET6;address.sin6_port=htons(static_cast<unsigned short>(port));address.sin6_addr=in6addr_any;
   if(::bind(s,reinterpret_cast<const sockaddr*>(&address),sizeof(address))!=0)throw std::runtime_error("cannot bind configured IPv6 port");
  }
 } catch(...){close_socket(s);throw;}
 return s;
}
}
std::vector<std::uint8_t> discovery_pong(std::span<const std::uint8_t> ping,std::uint64_t guid,const std::string& advertisement) {
 if((ping.size()!=25 && ping.size()!=33) || (ping[0]!=1 && ping[0]!=2) || !std::equal(magic.begin(),magic.end(),ping.begin()+9) || advertisement.size()>1024)return {};
 std::vector<std::uint8_t> out;out.reserve(35+advertisement.size());out.push_back(0x1c);
 out.insert(out.end(),ping.begin()+1,ping.begin()+9);big_endian(out,guid,8);
 out.insert(out.end(),magic.begin(),magic.end());big_endian(out,advertisement.size(),2);
 out.insert(out.end(),advertisement.begin(),advertisement.end());return out;
}
std::string server_advertisement(const Properties& p,std::uint64_t guid) {
 constexpr const char* modes[]={"Survival","Creative","Adventure"};
 return "MCPE;"+field(p.server_name)+";282;1.6.0;0;"+std::to_string(p.max_players)+";"+std::to_string(guid)+";"+field(p.level_name)+";"+modes[p.gamemode<=2?p.gamemode:0]+";0;";
}
struct DiscoveryServer::Impl {
 std::array<Socket,2> sockets{invalid,invalid};std::uint64_t guid=0;
#ifdef _WIN32
 bool initialized=false;
#endif
 Impl(const Properties& p) {
#ifdef _WIN32
  WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data)!=0)throw std::runtime_error("WSAStartup failed");initialized=true;
#endif
  try {
   std::random_device random;guid=(static_cast<std::uint64_t>(random())<<32)|random();
   sockets[0]=bind_socket(AF_INET,p.port);sockets[1]=bind_socket(AF_INET6,p.port_v6);
  }catch(...){cleanup();throw;}
 }
 void cleanup() {
  for(auto s:sockets)close_socket(s);
  sockets={invalid,invalid};
#ifdef _WIN32
  if(initialized){WSACleanup();initialized=false;}
#endif
 }
 ~Impl(){cleanup();}
};
DiscoveryServer::DiscoveryServer(const Properties& p):impl_(std::make_unique<Impl>(p)){}
DiscoveryServer::~DiscoveryServer()=default;
void DiscoveryServer::poll(const Properties& p) {
 const auto advertisement=server_advertisement(p,impl_->guid);
 for(auto socket:impl_->sockets)for(unsigned packet=0;packet<32;++packet) {
  std::array<std::uint8_t,2048> bytes{};sockaddr_storage sender{};
#ifdef _WIN32
  int length=sizeof(sender);
  const auto count=recvfrom(socket,reinterpret_cast<char*>(bytes.data()),static_cast<int>(bytes.size()),0,reinterpret_cast<sockaddr*>(&sender),&length);
#else
  socklen_t length=sizeof(sender);
  const auto count=recvfrom(socket,bytes.data(),bytes.size(),0,reinterpret_cast<sockaddr*>(&sender),&length);
#endif
  if(count<0){if(would_block())break;throw std::runtime_error("UDP receive failed");}
  auto response=discovery_pong(std::span(bytes).first(static_cast<std::size_t>(count)),impl_->guid,advertisement);
  if(response.empty())continue;
  // Unreliable offline discovery: failed sends are dropped, never retried in tick loop.
  sendto(socket,reinterpret_cast<const char*>(response.data()),static_cast<int>(response.size()),0,reinterpret_cast<const sockaddr*>(&sender),length);
 }
}
}
