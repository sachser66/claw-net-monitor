#include "docker_collector.hpp"

#include <sstream>

std::vector<std::string> parse_lines(const std::string& out, std::size_t limit) {
    std::vector<std::string> lines;
    std::istringstream iss(out);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        lines.push_back(line);
        if (lines.size() >= limit) break;
    }
    return lines;
}
