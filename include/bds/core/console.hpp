#pragma once
#include <string>
#include <vector>
namespace bds {
// Polled on the simulation thread. Never joins a thread blocked on stdin.
class ConsoleInput {
public: std::vector<std::string> poll();
private:
 void append(char c,std::vector<std::string>& result);
 std::string pending_;
 bool eof_=false, discard_=false;
};
std::vector<std::string> command_words(const std::string& line);
}
