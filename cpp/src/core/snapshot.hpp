#pragma once

#include <string>
#include <vector>
#include <utility>

#include "types.hpp"

struct Snapshot {
    std::vector<InterfaceSample> interfaces;
    std::vector<std::pair<std::string, int>> conn_states;
    std::vector<OpenClawSession> openclaw_session_items;
    std::vector<AgentSessionHierarchy> openclaw_session_hierarchy;
    std::vector<OpenClawAgentConfig> openclaw_agents;
    std::vector<OpenClawModelInfo> openclaw_models;
    std::vector<OpenClawChannelInfo> openclaw_channels;
    std::vector<std::string> docker_networks;
    std::vector<std::string> docker_containers;
    std::vector<std::string> openclaw_sockets;
    GatewayInfo gateway;
    std::vector<std::string> trigger_events;
    long long openclaw_session_seq = 0;
    long long monitor_uptime_seconds = 0;
    int openclaw_session_count = 0;
    bool openclaw_socket_activity = false;
};
