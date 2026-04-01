#include "json.hpp"

#include <sstream>

std::string json_get_string_field(const std::string& block, const std::string& field) {
    std::string needle = "\"" + field + "\"";
    auto pos = block.find(needle);
    if (pos == std::string::npos) return "";
    auto colon = block.find(':', pos);
    if (colon == std::string::npos) return "";
    auto start = block.find('"', colon + 1);
    if (start == std::string::npos) return "";
    auto end = block.find('"', start + 1);
    if (end == std::string::npos) return "";
    return block.substr(start + 1, end - start - 1);
}

long long json_get_number_field(const std::string& block, const std::string& field) {
    std::string needle = "\"" + field + "\"";
    auto pos = block.find(needle);
    if (pos == std::string::npos) return 0;
    auto colon = block.find(':', pos);
    if (colon == std::string::npos) return 0;
    std::stringstream ss(block.substr(colon + 1));
    long long value = 0;
    ss >> value;
    return value;
}
