#include "bds/network/login.hpp"
#include <stdexcept>
namespace bds {
namespace {
constexpr std::size_t limit=1024*1024;
class Reader {
public:
 explicit Reader(std::span<const std::uint8_t> data):data_(data){}
 std::span<const std::uint8_t> take(std::size_t n) {
  if(n>data_.size())throw std::invalid_argument("truncated login field");
  auto result=data_.first(n);data_=data_.subspan(n);return result;
 }
 std::uint32_t integer(bool big_endian) {
  const auto bytes=take(4);std::uint32_t result=0;
  for(unsigned i=0;i<4;++i)result|=static_cast<std::uint32_t>(bytes[i])<<(8*(big_endian?3-i:i));
  return result;
 }
 std::uint32_t varint() {
  std::uint32_t value=0;
  for(unsigned i=0;i<5;++i) {
   const auto byte=take(1)[0];
   if(i==4 && (byte&0xf0)!=0)throw std::invalid_argument("login length overflow");
   value|=static_cast<std::uint32_t>(byte&0x7f)<<(7*i);
   if((byte&0x80)==0){if(i>0 && byte==0)throw std::invalid_argument("noncanonical login length");return value;}
  }
  throw std::invalid_argument("unterminated login length");
 }
 std::string long_string() {
  const auto length=integer(false);
  if(length==0 || length>limit)throw std::invalid_argument("invalid login string length");
  auto bytes=take(length);return std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size());
 }
 void finish() const{if(!data_.empty())throw std::invalid_argument("trailing login bytes");}
private:std::span<const std::uint8_t> data_;
};
}
UnverifiedLoginBody decode_login_body(std::span<const std::uint8_t> payload) {
 if(payload.size()>limit)throw std::invalid_argument("login body exceeds limit");
 Reader outer(payload);
 if(outer.integer(true)!=282)throw std::invalid_argument("incompatible Bedrock protocol");
 const auto length=outer.varint();Reader inner(outer.take(length));outer.finish();
 UnverifiedLoginBody result{inner.long_string(),inner.long_string()};inner.finish();return result;
}
}
