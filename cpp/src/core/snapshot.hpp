#pragma once

#include <string>
#include <vector>
#include <utility>

#include "types.hpp"

struct Snapshot {
    std::vector<InterfaceSample> interfaces;
    std::vector<std::pair<std::string, int>> conn_states;
    std::vector<OpenClawSession> openclaw_session_items;
    std::vector<OpenClawAgentConfig> openclaw_agents;
    std::vector<std::string> docker_networks;
    std::vector<std::string> docker_containers;
    std::vector<std::string> openclaw_sockets;
    GatewayInfo gateway;
    std::vector<std::string> trigger_events;
    int openclaw_session_count = 0;
    bool openclaw_socket_activity = false;
};
