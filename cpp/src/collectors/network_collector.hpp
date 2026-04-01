#pragma once

#include <string>
#include <vector>
#include <utility>

#include "../core/types.hpp"

std::string classify_group(const std::string& name);
std::vector<InterfaceSample> read_interfaces(double dt_seconds);
std::vector<std::pair<std::string, int>> parse_ss_summary(const std::string& out);
std::vector<std::string> parse_openclaw_sockets(const std::string& out);
bool has_openclaw_local_activity(const std::vector<std::string>& socket_lines);
std::vector<GroupStat> build_group_stats(const std::vector<InterfaceSample>& interfaces);
const GroupStat* find_group(const std::vector<GroupStat>& groups, const std::string& name);
