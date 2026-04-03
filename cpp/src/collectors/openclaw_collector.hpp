#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "../core/types.hpp"

std::vector<OpenClawSession> extract_sessions(const std::string& json);
std::vector<OpenClawAgentConfig> extract_agent_configs(const std::string& json);
GatewayInfo extract_gateway_info(const std::string& json);
int parse_session_count(const std::string& out);
void merge_session_store_metadata(std::vector<OpenClawSession>& sessions, const std::string& sessions_json);
std::vector<OpenClawModelInfo> extract_models(const std::string& json);
std::vector<OpenClawChannelInfo> extract_channels(const std::string& json);
void enrich_agents_with_models(std::vector<OpenClawAgentConfig>& agents, const std::vector<OpenClawModelInfo>& models);
void enrich_agents_with_channels(std::vector<OpenClawAgentConfig>& agents, const std::vector<OpenClawChannelInfo>& channels);
