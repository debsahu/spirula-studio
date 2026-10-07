// Whole-file reads and writes shared by the sibling writer and the edit writer.
#pragma once

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace roma {
namespace file {

inline std::string slurp(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + p.string());
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

inline void put(const std::filesystem::path& p, const std::string& bytes) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(bytes.data(), (std::streamsize)bytes.size());
    if (!f) throw std::runtime_error("cannot write " + p.string());
}

}  // namespace file
}  // namespace roma
