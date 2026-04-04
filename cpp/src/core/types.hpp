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
    std::string thinking_level;
    long long updated_at = 0;
    long long input_tokens = -1;
    long long output_tokens = -1;
    long long cache_read_tokens = -1;
    long long cache_write_tokens = -1;
    long long total_tokens = -1;
    long long remaining_tokens = -1;
    long long context_tokens = -1;
    int percent_used = -1;
    bool total_tokens_fresh = false;
    bool aborted_last_run = false;
    bool system_sent = false;
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
    std::string auth_id;
    std::string auth_type;
    bool available = false;
};

struct OpenClawChannelInfo {
    std::string kind;
    std::string account_id;
    std::string label;
    bool enabled = true;
    bool connected = true;
};

struct SessionNode {
    OpenClawSession session;
    std::string session_name;
    std::string channel;
    bool is_orchestrator = false;
    bool is_subagent = false;
};

struct AgentSessionHierarchy {
    std::string agent_id;
    std::vector<SessionNode> orchestrators;
    std::vector<SessionNode> unmatched_subagents;
    std::vector<SessionNode> others;
};

struct GatewayInfo {
    std::string bind;
    std::string port;
    std::string mode;
    std::string probe_url;
};

struct OpenClawHealthSummary {
    bool ok = false;
    bool available = false;
    std::string status_text;
    int configured_channels = 0;
    int healthy_channels = 0;
    int running_channels = 0;
    std::string default_agent_id;
    int heartbeat_enabled_agents = 0;
};

struct OpenClawStatusSummary {
    bool available = false;
    std::string runtime_version;
    int queued_system_events = 0;
    std::string default_model;
    int session_count = 0;
    int heartbeat_enabled_agents = 0;
    int session_pressure_high = 0;
    int aborted_sessions = 0;
    int system_sessions = 0;
    int max_percent_used = 0;
    std::string hottest_session;
};

struct OpenClawUsageWindow {
    std::string label;
    int used_percent = -1;
    long long reset_at = 0;
};

struct OpenClawProviderUsage {
    std::string provider;
    std::string display_name;
    std::string plan;
    std::vector<OpenClawUsageWindow> windows;
};

struct OpenClawUsageSummary {
    bool available = false;
    long long updated_at = 0;
    std::vector<OpenClawProviderUsage> providers;
};

struct OpenClawUsageCostSummary {
    bool available = false;
    int days = 0;
    std::string currency = "USD";
    double total_cost = 0.0;
    double today_cost = 0.0;
    double total_cost_eur = 0.0;
    double today_cost_eur = 0.0;
    long long total_tokens = 0;
    long long today_tokens = 0;
    double cache_read_share = 0.0;
};

struct GroupStat {
    std::string name;
    double rx = 0.0;
    double tx = 0.0;
    std::vector<std::string> ifaces;
    double total() const { return rx + tx; }
};
