#include "openclaw_collector.hpp"

#include <algorithm>
#include <sstream>

#include "../util/json.hpp"

namespace {
std::string extract_string_array_block(const std::string& block, const std::string& field) {
    std::string needle = "\"" + field + "\"";
    auto pos = block.find(needle);
    if (pos == std::string::npos) return "";
    auto start = block.find('[', pos);
    auto end = block.find(']', start);
    if (start == std::string::npos || end == std::string::npos) return "";
    return block.substr(start + 1, end - start - 1);
}

std::vector<std::string> extract_quoted_strings(const std::string& text) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (true) {
        auto start = text.find('"', pos);
        if (start == std::string::npos) break;
        auto end = text.find('"', start + 1);
        if (end == std::string::npos) break;
        out.push_back(text.substr(start + 1, end - start - 1));
        pos = end + 1;
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

std::vector<OpenClawSession> extract_sessions(const std::string& json) {
    std::vector<OpenClawSession> items;
    auto sessions_pos = json.find("\"sessions\"");
    if (sessions_pos == std::string::npos) return items;

    std::size_t pos = json.find("\"key\"", sessions_pos);
    while (pos != std::string::npos) {
        auto obj_start = json.rfind('{', pos);
        auto obj_end = json.find('}', pos);
        if (obj_start == std::string::npos || obj_end == std::string::npos) break;
        std::string block = json.substr(obj_start, obj_end - obj_start + 1);

        OpenClawSession s;
        s.key = json_get_string_field(block, "key");
        s.agent = json_get_string_field(block, "agentId");
        s.status = json_get_string_field(block, "status");
        s.kind = json_get_string_field(block, "kind");
        s.display = json_get_string_field(block, "displayName");
        s.model = json_get_string_field(block, "model");
        s.model_provider = json_get_string_field(block, "modelProvider");
        s.last_channel = json_get_string_field(block, "lastChannel");
        s.provider = json_get_string_field(block, "provider");
        s.updated_at = json_get_number_field(block, "updatedAt");

        if (s.agent.empty() && s.key.rfind("agent:", 0) == 0) {
            auto first = s.key.find(':');
            auto second = s.key.find(':', first + 1);
            if (second != std::string::npos) s.agent = s.key.substr(first + 1, second - first - 1);
        }
        if (s.agent.empty()) s.agent = "?";
        if (s.status.empty()) s.status = "?";
        if (s.kind.empty()) s.kind = "?";
        if (!s.key.empty()) items.push_back(s);

        pos = json.find("\"key\"", obj_end + 1);
    }

    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
        if (a.agent != b.agent) return a.agent < b.agent;
        if (a.updated_at != b.updated_at) return a.updated_at > b.updated_at;
        if (a.status != b.status) return a.status < b.status;
        return a.key < b.key;
    });
    return items;
}

std::vector<OpenClawAgentConfig> extract_agent_configs(const std::string& json) {
    std::vector<OpenClawAgentConfig> agents;
    auto list_pos = json.find("\"list\"");
    if (list_pos == std::string::npos) return agents;

    std::size_t pos = json.find("\"id\"", list_pos);
    while (pos != std::string::npos) {
        auto obj_start = json.rfind('{', pos);
        auto obj_end = json.find('}', pos);
        if (obj_start == std::string::npos || obj_end == std::string::npos) break;
        std::string block = json.substr(obj_start, obj_end - obj_start + 1);

        OpenClawAgentConfig a;
        a.id = json_get_string_field(block, "id");
        a.name = json_get_string_field(block, "name");
        a.workspace = json_get_string_field(block, "workspace");
        a.emoji = json_get_string_field(block, "emoji");
        a.model_primary = json_get_string_field(block, "primary");
        if (a.model_primary.empty()) {
            a.model_primary = json_get_string_field(block, "model");
        }
        a.model_fallbacks = extract_quoted_strings(extract_string_array_block(block, "fallbacks"));

        if (!a.id.empty()) agents.push_back(a);
        pos = json.find("\"id\"", obj_end + 1);
    }

    auto bindings_pos = json.find("\"bindings\"");
    if (bindings_pos != std::string::npos) {
        std::size_t pos2 = json.find("\"agentId\"", bindings_pos);
        while (pos2 != std::string::npos) {
            auto obj_start = json.rfind('{', pos2);
            auto obj_end = json.find('}', pos2);
            if (obj_start == std::string::npos || obj_end == std::string::npos) break;
            std::string block = json.substr(obj_start, obj_end - obj_start + 1);
            std::string agent_id = json_get_string_field(block, "agentId");
            std::string account_id = json_get_string_field(block, "accountId");
            if (!agent_id.empty() && !account_id.empty()) {
                for (auto& a : agents) {
                    if (a.id == agent_id) a.bound_accounts.push_back(account_id);
                }
            }
            pos2 = json.find("\"agentId\"", obj_end + 1);
        }
    }

    return agents;
}

GatewayInfo extract_gateway_info(const std::string& json) {
    GatewayInfo info;
    auto gateway_pos = json.find("\"gateway\"");
    if (gateway_pos == std::string::npos) return info;

    auto block_end = json.find("\"port\"", gateway_pos + 10);
    std::string block = json.substr(gateway_pos, block_end == std::string::npos ? std::string::npos : block_end - gateway_pos + 80);
    info.bind = json_get_string_field(block, "bind");
    info.mode = json_get_string_field(block, "mode");
    info.probe_url = json_get_string_field(block, "probeUrl");

    auto port_pos = block.find("\"port\"");
    if (port_pos != std::string::npos) {
        auto colon = block.find(':', port_pos);
        if (colon != std::string::npos) {
            std::stringstream ss(block.substr(colon + 1));
            int port = 0;
            ss >> port;
            if (port > 0) info.port = std::to_string(port);
        }
    }
    return info;
}

int parse_session_count(const std::string& out) {
    int count = 0;
    auto count_pos = out.find("\"count\"");
    if (count_pos != std::string::npos) {
        auto colon = out.find(':', count_pos);
        if (colon != std::string::npos) {
            std::stringstream ss(out.substr(colon + 1));
            ss >> count;
            if (count > 0) return count;
        }
    }

    auto sessions_pos = out.find("\"sessions\"");
    if (sessions_pos == std::string::npos) return 0;
    std::size_t pos = out.find("\"key\"", sessions_pos);
    while (pos != std::string::npos) {
        count++;
        pos = out.find("\"key\"", pos + 1);
    }
    return count;
}

std::string session_name_from_key(const std::string& key) {
    if (key.rfind("agent:", 0) != 0) return key;
    auto first = key.find(':');
    auto second = key.find(':', first + 1);
    if (second == std::string::npos || second + 1 >= key.size()) return key;
    return key.substr(second + 1);
}
