#pragma once

#include <string>
#include <vector>

#include "types.hpp"

std::string session_name_from_key(const std::string& key);
std::string infer_session_channel(const OpenClawSession& session);
bool is_transport_session_name(const std::string& name);
bool is_subagent_session(const OpenClawSession& session);
bool is_orchestrator_session(const OpenClawSession& session, const std::vector<OpenClawSession>& all_sessions);
std::vector<AgentSessionHierarchy> build_session_hierarchy(const std::vector<OpenClawSession>& sessions);
