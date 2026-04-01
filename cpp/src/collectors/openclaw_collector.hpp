#pragma once

#include <string>
#include <vector>

#include "../core/types.hpp"

std::vector<OpenClawSession> extract_sessions(const std::string& json);
std::vector<OpenClawAgentConfig> extract_agent_configs(const std::string& json);
GatewayInfo extract_gateway_info(const std::string& json);
int parse_session_count(const std::string& out);
std::string infer_session_channel(const OpenClawSession& s);
std::string session_name_from_key(const std::string& key);
