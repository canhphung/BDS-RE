#include "bds/core/console.hpp"
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <poll.h>
#include <unistd.h>
#include <cerrno>
#endif
namespace bds {
void ConsoleInput::append(char c,std::vector<std::string>& result) {
 if(c=='\r')return;
 if(c=='\n') {
  if(!discard_)result.push_back(pending_);
  pending_.clear();discard_=false;
 } else if(pending_.size()<4096 && !discard_)pending_+=c;
 else {pending_.clear();discard_=true;}
}
std::vector<std::string> ConsoleInput::poll() {
 std::vector<std::string> result;
 if(eof_)return result;
 char bytes[4096];unsigned count=0;
#ifdef _WIN32
 HANDLE input=GetStdHandle(STD_INPUT_HANDLE);
 if(input==INVALID_HANDLE_VALUE || input==nullptr){eof_=true;return result;}
 DWORD mode=0,available=0,read=0;
 if(GetConsoleMode(input,&mode)) {
  if(!GetNumberOfConsoleInputEvents(input,&available) || available==0)return result;
  INPUT_RECORD events[128];
  if(!ReadConsoleInputA(input,events,available>128?128:available,&read)){eof_=true;return result;}
  for(DWORD i=0;i<read;++i) {
   if(events[i].EventType!=KEY_EVENT || !events[i].Event.KeyEvent.bKeyDown)continue;
   const auto& key=events[i].Event.KeyEvent;
   char c=key.uChar.AsciiChar;
   if(c=='\b'){if(!pending_.empty()){pending_.pop_back();DWORD written=0;WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE),"\b \b",3,&written,nullptr);}continue;}
   if(c=='\r')c='\n';
   if(c)for(unsigned n=0;n<key.wRepeatCount;++n){append(c,result);DWORD written=0;const char* text=c=='\n'?"\r\n":&c;WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE),text,c=='\n'?2:1,&written,nullptr);}
  }
  return result;
 }
 if(GetFileType(input)==FILE_TYPE_PIPE) {
  if(!PeekNamedPipe(input,nullptr,0,nullptr,&available,nullptr)){eof_=true;}
  else if(available==0)return result;
  else if(ReadFile(input,bytes,available>sizeof(bytes)?sizeof(bytes):available,&read,nullptr))count=read;
  else eof_=true;
 } else {
  if(ReadFile(input,bytes,sizeof(bytes),&read,nullptr))count=read;
  if(count==0)eof_=true;
 }
#else
 pollfd input{STDIN_FILENO,POLLIN,0};
 const auto ready=::poll(&input,1,0);
 if(ready<0 && errno!=EINTR)throw std::runtime_error("stdin poll failed");
 if(ready>0 && (input.revents&(POLLIN|POLLHUP))) {
  const auto size=::read(STDIN_FILENO,bytes,sizeof(bytes));
  if(size>0)count=static_cast<unsigned>(size);
  else if(size==0)eof_=true;
  else if(errno!=EINTR && errno!=EAGAIN)throw std::runtime_error("stdin read failed");
 } else if(ready>0 && (input.revents&(POLLERR|POLLNVAL)))eof_=true;
#endif
 for(unsigned i=0;i<count;++i)append(bytes[i],result);
 if(eof_ && !pending_.empty() && !discard_){result.push_back(pending_);pending_.clear();}
 return result;
}
std::vector<std::string> command_words(const std::string& line) {
 std::vector<std::string> words;std::string word;bool quoted=false,started=false;
 for(char c:line) {
  if(c=='"'){quoted=!quoted;started=true;}
  else if(!quoted && (c==' ' || c=='\t')) {if(started){words.push_back(word);word.clear();started=false;}}
  else {word+=c;started=true;}
 }
 if(quoted)throw std::invalid_argument("unterminated quote");
 if(started)words.push_back(word);
 return words;
}
}
