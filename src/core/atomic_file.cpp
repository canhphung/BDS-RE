#include "bds/core/atomic_file.hpp"
#include <system_error>
#include <fstream>
#include <random>
#include <stdexcept>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace bds {
void replace_file(const std::filesystem::path& source, const std::filesystem::path& destination) {
#ifdef _WIN32
    if (!MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "replace world snapshot");
#else
    std::filesystem::rename(source, destination);
#endif
}
void write_file_atomically(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
 auto parent=path.parent_path();if(parent.empty())parent=".";
 std::random_device random;std::filesystem::path staging;
 for(unsigned i=0;i<32;++i) {
  staging=parent/(".bds-world-"+std::to_string(random())+".tmp");
  if(std::filesystem::create_directory(staging))break;
  staging.clear();
 }
 if(staging.empty())throw std::runtime_error("cannot create staging directory");
 struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove_all(path,ec);}} cleanup{staging};
 auto file=staging/"snapshot";std::ofstream output(file,std::ios::binary|std::ios::trunc);
 if(!output)throw std::runtime_error("cannot create staging file");
 output.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
 output.flush();if(!output)throw std::runtime_error("cannot write staging file");
 output.close();if(output.fail())throw std::runtime_error("cannot close staging file");
 replace_file(file,path);
}

}
