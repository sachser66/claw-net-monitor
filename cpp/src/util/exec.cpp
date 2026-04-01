#include "exec.hpp"

#include <array>
#include <cstdio>
#include <fstream>
#include <sstream>

std::string read_file_text(const std::string& path) {
    std::ifstream file(path);
    if (!file) return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::string exec_read(const std::string& cmd) {
    std::array<char, 4096> buffer{};
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return result;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        result += buffer.data();
    }
    pclose(pipe);
    return result;
}
