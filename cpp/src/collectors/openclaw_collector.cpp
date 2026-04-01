#include "openclaw_collector.hpp"

#include <algorithm>

#include "../util/json_vendor.hpp"

using nlohmann::json;

std::string infer_session_channel(const OpenClawSession& s) {
    auto has = [&](const std::string& needle) {
        return s.key.find(needle) != std::string::npos ||
               s.display.find(needle) != std::string::npos ||
               s.last_channel.find(needle) != std::string::npos ||
               s.provider.find(needle) != std::string::npos;
    };
    if (has("telegram")) return "telegram";
    if (has("whatsapp")) return "whatsapp";
    if (has("discord")) return "discord";
    if (has("signal")) return "signal";
    if (has("webchat")) return "webchat";
    if (has("tui")) return "tui";
    if (has("web")) return "webchat";
    return "tui";
}

std::vector<OpenClawSession> extract_sessions(const std::string& text) {
    std::vector<OpenClawSession> items;
    json root = json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.contains("sessions") || !root["sessions"].is_array()) return items;

    for (const auto& item : root["sessions"]) {
        OpenClawSession s;
        s.key = item.value("key", "");
        s.agent = item.value("agentId", "");
        s.status = item.value("status", "");
        s.kind = item.value("kind", "");
        s.display = item.value("displayName", "");
        s.model = item.value("model", "");
        s.model_provider = item.value("modelProvider", "");
        s.last_channel = item.value("lastChannel", "");
        s.provider = item.value("provider", "");
        s.updated_at = item.value("updatedAt", 0LL);

        if (s.agent.empty() && s.key.rfind("agent:", 0) == 0) {
            auto first = s.key.find(':');
            auto second = s.key.find(':', first + 1);
            if (second != std::string::npos) s.agent = s.key.substr(first + 1, second - first - 1);
        }
        if (s.agent.empty()) s.agent = "?";
        if (s.status.empty()) s.status = "-";
        if (s.kind.empty()) s.kind = "?";
        if (!s.key.empty()) items.push_back(std::move(s));
    }

    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
        if (a.agent != b.agent) return a.agent < b.agent;
        if (a.updated_at != b.updated_at) return a.updated_at > b.updated_at;
        return a.key < b.key;
    });
    return items;
}

std::vector<OpenClawAgentConfig> extract_agent_configs(const std::string& text) {
    std::vector<OpenClawAgentConfig> agents;
    json root = json::parse(text, nullptr, false);
    if (root.is_discarded()) return agents;

    std::string default_primary;
    std::string default_workspace;
    if (root.contains("agents") && root["agents"].is_object()) {
        const auto& agents_obj = root["agents"];
        if (agents_obj.contains("defaults") && agents_obj["defaults"].is_object()) {
            const auto& defaults = agents_obj["defaults"];
            if (defaults.contains("model")) {
                if (defaults["model"].is_string()) default_primary = defaults["model"].get<std::string>();
                else if (defaults["model"].is_object()) default_primary = defaults["model"].value("primary", "");
            }
            default_workspace = defaults.value("workspace", "");
        }

        if (agents_obj.contains("list") && agents_obj["list"].is_array()) {
            for (const auto& item : agents_obj["list"]) {
                OpenClawAgentConfig a;
                a.id = item.value("id", "");
                a.name = item.value("name", "");
                a.workspace = item.value("workspace", default_workspace);
                if (item.contains("identity") && item["identity"].is_object()) {
                    a.emoji = item["identity"].value("emoji", "");
                    if (a.name.empty()) a.name = item["identity"].value("name", "");
                }
                if (item.contains("model")) {
                    if (item["model"].is_string()) a.model_primary = item["model"].get<std::string>();
                    else if (item["model"].is_object()) {
                        a.model_primary = item["model"].value("primary", "");
                        if (item["model"].contains("fallbacks") && item["model"]["fallbacks"].is_array()) {
                            for (const auto& fb : item["model"]["fallbacks"]) {
                                if (fb.is_string()) a.model_fallbacks.push_back(fb.get<std::string>());
                            }
                        }
                    }
                }
                if (a.model_primary.empty()) a.model_primary = default_primary;
                if (a.name.empty()) a.name = a.id;
                if (!a.id.empty()) agents.push_back(std::move(a));
            }
        }
    }

    if (root.contains("bindings") && root["bindings"].is_array()) {
        for (const auto& binding : root["bindings"]) {
            std::string agent_id = binding.value("agentId", "");
            std::string account_id;
            if (binding.contains("match") && binding["match"].is_object()) {
                account_id = binding["match"].value("accountId", "");
            }
            if (!agent_id.empty() && !account_id.empty()) {
                for (auto& a : agents) {
                    if (a.id == agent_id) a.bound_accounts.push_back(account_id);
                }
            }
        }
    }

    return agents;
}

GatewayInfo extract_gateway_info(const std::string& text) {
    GatewayInfo info;
    json root = json::parse(text, nullptr, false);
    if (root.is_discarded()) return info;

    if (root.contains("gateway") && root["gateway"].is_object()) {
        const auto& gateway = root["gateway"];
        info.port = gateway.contains("port")
            ? (gateway["port"].is_number_integer() ? std::to_string(gateway["port"].get<int>()) : gateway["port"].dump())
            : "";
        if (!info.port.empty() && info.port.front() == '"' && info.port.back() == '"') {
            info.port = info.port.substr(1, info.port.size() - 2);
        }
        info.bind = gateway.value("bind", "");
        info.mode = gateway.value("mode", "");
        if (gateway.contains("remote") && gateway["remote"].is_object()) {
            info.probe_url = gateway["remote"].value("url", "");
        }
    }

    if (info.mode.empty()) info.mode = "unknown";
    return info;
}

int parse_session_count(const std::string& text) {
    json root = json::parse(text, nullptr, false);
    if (root.is_discarded()) return 0;
    if (root.contains("count") && root["count"].is_number_integer()) return root["count"].get<int>();
    if (root.contains("sessions") && root["sessions"].is_array()) return static_cast<int>(root["sessions"].size());
    return 0;
}

std::string session_name_from_key(const std::string& key) {
    if (key.rfind("agent:", 0) != 0) return key;
    auto first = key.find(':');
    auto second = key.find(':', first + 1);
    if (second == std::string::npos || second + 1 >= key.size()) return key;
    return key.substr(second + 1);
}
