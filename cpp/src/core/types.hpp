#pragma once

#include <string>
#include <vector>
#include <utility>

struct InterfaceSample {
    std::string name;
    unsigned long long rx_bytes = 0;
    unsigned long long tx_bytes = 0;
    double rx_rate = 0.0;
    double tx_rate = 0.0;
    std::string group = "Sonstiges";
};

struct OpenClawSession {
    std::string agent;
    std::string status;
    std::string kind;
    std::string key;
    std::string display;
    std::string model;
    std::string model_provider;
    std::string last_channel;
    std::string provider;
    std::string spawned_by;
    std::string subagent_role;
    std::string label;
    long long updated_at = 0;
};

struct OpenClawAgentConfig {
    std::string id;
    std::string name;
    std::string emoji;
    std::string workspace;
    std::string model_primary;
    std::vector<std::string> model_fallbacks;
    std::vector<std::string> bound_accounts;
    std::vector<std::string> bound_channels;
    bool primary_model_available = false;
    int fallback_models_available = 0;
    int fallback_models_total = 0;
};

struct OpenClawModelInfo {
    std::string key;
    std::string name;
    std::string provider;
    bool available = false;
};

struct OpenClawChannelInfo {
    std::string kind;
    std::string account_id;
    std::string label;
    bool enabled = true;
    bool connected = true;
};

struct GatewayInfo {
    std::string bind;
    std::string port;
    std::string mode;
    std::string probe_url;
};

struct GroupStat {
    std::string name;
    double rx = 0.0;
    double tx = 0.0;
    std::vector<std::string> ifaces;
    double total() const { return rx + tx; }
};
