#include "openclaw_collector.hpp"

#include <algorithm>
#include <unordered_map>

#include "../util/exec.hpp"
#include "../util/json_vendor.hpp"

using nlohmann::json;


namespace {
struct SessionStoreMeta {
    std::string spawned_by;
    std::string subagent_role;
    std::string label;
    std::string last_channel;
    std::string provider;
};

json parse_json_loose(const std::string& text) {
    json root = json::parse(text, nullptr, false);
    if (!root.is_discarded()) return root;
    auto start = text.find('{');
    auto end = text.rfind('}');
    if (start != std::string::npos && end != std::string::npos && end > start) {
        return json::parse(text.substr(start, end - start + 1), nullptr, false);
    }
    return json();
}

std::unordered_map<std::string, SessionStoreMeta> parse_session_store_metadata(const std::string& text) {
    std::unordered_map<std::string, SessionStoreMeta> out;
    json root = json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) return out;

    for (auto it = root.begin(); it != root.end(); ++it) {
        if (!it.value().is_object()) continue;
        const std::string key = it.key();
        if (key.rfind("agent:", 0) != 0) continue;
        SessionStoreMeta meta;
        meta.spawned_by = it.value().value("spawnedBy", "");
        meta.subagent_role = it.value().value("subagentRole", "");
        meta.label = it.value().value("label", "");
        meta.last_channel = it.value().value("lastChannel", "");
        meta.provider = it.value().value("channel", "");
        out[key] = std::move(meta);
    }
    return out;
}
}

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
        s.status = item.value("status", "-");
        s.kind = item.value("kind", "?");
        s.display = item.value("displayName", "");
        s.model = item.value("model", "");
        s.model_provider = item.value("modelProvider", "");
        s.last_channel = item.value("lastChannel", "");
        s.provider = item.value("provider", "");
        s.spawned_by = item.value("spawnedBy", "");
        s.subagent_role = item.value("subagentRole", "");
        s.label = item.value("label", "");
        s.updated_at = item.value("updatedAt", 0LL);

        if (s.agent.empty() && s.key.rfind("agent:", 0) == 0) {
            auto first = s.key.find(':');
            auto second = s.key.find(':', first + 1);
            if (second != std::string::npos) s.agent = s.key.substr(first + 1, second - first - 1);
        }
        if (s.agent.empty()) s.agent = "?";
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
    std::vector<std::string> default_fallbacks;

    if (root.contains("agents") && root["agents"].is_object()) {
        const auto& agents_obj = root["agents"];

        if (agents_obj.contains("defaults") && agents_obj["defaults"].is_object()) {
            const auto& defaults = agents_obj["defaults"];
            default_workspace = defaults.value("workspace", "");
            if (defaults.contains("model")) {
                const auto& model = defaults["model"];
                if (model.is_string()) default_primary = model.get<std::string>();
                else if (model.is_object()) {
                    default_primary = model.value("primary", "");
                    if (model.contains("fallbacks") && model["fallbacks"].is_array()) {
                        for (const auto& fb : model["fallbacks"]) {
                            if (fb.is_string()) default_fallbacks.push_back(fb.get<std::string>());
                        }
                    }
                }
            }
        }

        if (agents_obj.contains("list") && agents_obj["list"].is_array()) {
            for (const auto& item : agents_obj["list"]) {
                OpenClawAgentConfig a;
                a.id = item.value("id", "");
                a.name = item.value("name", "");
                a.workspace = item.value("workspace", default_workspace);

                if (item.contains("identity") && item["identity"].is_object()) {
                    const auto& identity = item["identity"];
                    a.emoji = identity.value("emoji", "");
                    if (a.name.empty()) a.name = identity.value("name", "");
                }

                if (item.contains("model")) {
                    const auto& model = item["model"];
                    if (model.is_string()) {
                        a.model_primary = model.get<std::string>();
                    } else if (model.is_object()) {
                        a.model_primary = model.value("primary", "");
                        if (model.contains("fallbacks") && model["fallbacks"].is_array()) {
                            for (const auto& fb : model["fallbacks"]) {
                                if (fb.is_string()) a.model_fallbacks.push_back(fb.get<std::string>());
                            }
                        }
                    }
                }

                if (a.model_primary.empty()) a.model_primary = default_primary;
                if (a.model_fallbacks.empty()) a.model_fallbacks = default_fallbacks;
                if (a.name.empty()) a.name = a.id;
                if (!a.id.empty()) agents.push_back(std::move(a));
            }
        }
    }

    if (root.contains("bindings") && root["bindings"].is_array()) {
        for (const auto& binding : root["bindings"]) {
            const std::string agent_id = binding.value("agentId", "");
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
        info.bind = gateway.value("bindMode", gateway.value("bindHost", ""));
        info.mode = "local";
        if (gateway.contains("port")) {
            if (gateway["port"].is_number_integer()) info.port = std::to_string(gateway["port"].get<int>());
            else if (gateway["port"].is_string()) info.port = gateway["port"].get<std::string>();
        }
        if (gateway.contains("remote") && gateway["remote"].is_object()) {
            info.probe_url = gateway["remote"].value("url", "");
        }
        if (info.probe_url.empty()) {
            info.probe_url = gateway.value("probeUrl", "");
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


void merge_session_store_metadata(std::vector<OpenClawSession>& sessions, const std::string& sessions_json) {
    json root = json::parse(sessions_json, nullptr, false);
    if (root.is_discarded() || !root.contains("stores") || !root["stores"].is_array()) return;

    std::unordered_map<std::string, SessionStoreMeta> metadata;
    for (const auto& store : root["stores"]) {
        const std::string path = store.value("path", "");
        if (path.empty()) continue;
        auto parsed = parse_session_store_metadata(read_file_text(path));
        metadata.insert(parsed.begin(), parsed.end());
    }

    for (auto& s : sessions) {
        auto it = metadata.find(s.key);
        if (it == metadata.end()) continue;
        if (s.spawned_by.empty()) s.spawned_by = it->second.spawned_by;
        if (s.subagent_role.empty()) s.subagent_role = it->second.subagent_role;
        if (s.label.empty()) s.label = it->second.label;
        if (s.last_channel.empty()) s.last_channel = it->second.last_channel;
        if (s.provider.empty()) s.provider = it->second.provider;
    }
}


std::vector<OpenClawModelInfo> extract_models(const std::string& text) {
    std::vector<OpenClawModelInfo> items;
    json root = parse_json_loose(text);
    if (root.is_discarded() || !root.contains("models") || !root["models"].is_array()) return items;

    std::unordered_map<std::string, std::pair<std::string, std::string>> auth_by_provider;
    if (root.contains("auth") && root["auth"].is_array()) {
        for (const auto& item : root["auth"]) {
            const std::string provider = item.value("provider", "");
            const std::string auth_id = item.value("id", "");
            const std::string auth_type = item.value("type", "");
            if (!provider.empty() && !auth_by_provider.count(provider)) {
                auth_by_provider[provider] = {auth_id, auth_type};
            }
        }
    }

    for (const auto& item : root["models"]) {
        OpenClawModelInfo m;
        m.key = item.value("key", "");
        m.name = item.value("name", "");
        m.available = item.value("available", false) && !item.value("missing", false);
        auto slash = m.key.find('/');
        m.provider = slash == std::string::npos ? "" : m.key.substr(0, slash);
        auto auth_it = auth_by_provider.find(m.provider);
        if (auth_it != auth_by_provider.end()) {
            m.auth_id = auth_it->second.first;
            m.auth_type = auth_it->second.second;
        }
        if (!m.key.empty()) items.push_back(std::move(m));
    }
    return items;
}

std::vector<OpenClawChannelInfo> extract_channels(const std::string& text) {
    std::vector<OpenClawChannelInfo> items;
    json root = parse_json_loose(text);
    if (root.is_discarded() || !root.contains("chat") || !root["chat"].is_object()) return items;
    for (auto it = root["chat"].begin(); it != root["chat"].end(); ++it) {
        const std::string kind = it.key();
        if (!it.value().is_array()) continue;
        for (const auto& account : it.value()) {
            if (!account.is_string()) continue;
            OpenClawChannelInfo c;
            c.kind = kind;
            c.account_id = account.get<std::string>();
            c.label = c.kind + ":" + c.account_id;
            items.push_back(std::move(c));
        }
    }
    return items;
}

void enrich_agents_with_models(std::vector<OpenClawAgentConfig>& agents, const std::vector<OpenClawModelInfo>& models) {
    std::unordered_map<std::string, bool> available;
    for (const auto& m : models) available[m.key] = m.available;
    for (auto& a : agents) {
        a.primary_model_available = available.count(a.model_primary) ? available[a.model_primary] : false;
        a.fallback_models_total = static_cast<int>(a.model_fallbacks.size());
        a.fallback_models_available = 0;
        for (const auto& fb : a.model_fallbacks) {
            if (available.count(fb) && available[fb]) a.fallback_models_available++;
        }
    }
}

void enrich_agents_with_channels(std::vector<OpenClawAgentConfig>& agents, const std::vector<OpenClawChannelInfo>& channels) {
    for (auto& a : agents) {
        a.bound_channels.clear();
        for (const auto& account : a.bound_accounts) {
            for (const auto& ch : channels) {
                if (ch.account_id == account) a.bound_channels.push_back(ch.kind + ":" + ch.account_id);
            }
        }
        std::sort(a.bound_channels.begin(), a.bound_channels.end());
        a.bound_channels.erase(std::unique(a.bound_channels.begin(), a.bound_channels.end()), a.bound_channels.end());
    }
}
