#include "session_hierarchy.hpp"

#include <algorithm>
#include <map>
#include <unordered_map>

std::string session_name_from_key(const std::string& key) {
    if (key.rfind("agent:", 0) != 0) return key;
    auto first = key.find(':');
    auto second = key.find(':', first + 1);
    if (second == std::string::npos || second + 1 >= key.size()) return key;
    return key.substr(second + 1);
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

bool is_transport_session_name(const std::string& name) {
    return name.rfind("telegram:", 0) == 0 ||
           name.rfind("tui-", 0) == 0 ||
           name.rfind("webchat", 0) == 0 ||
           name.rfind("discord", 0) == 0 ||
           name.rfind("signal", 0) == 0 ||
           name.rfind("whatsapp", 0) == 0;
}

bool is_subagent_session(const OpenClawSession& s) {
    const std::string name = session_name_from_key(s.key);
    return name.rfind("subagent:", 0) == 0 || !s.spawned_by.empty();
}

bool is_orchestrator_session(const OpenClawSession& s, const std::vector<OpenClawSession>& all_sessions) {
    const std::string name = session_name_from_key(s.key);
    if (name == "main" || name == s.agent) return true;
    if (is_subagent_session(s)) return false;
    if (is_transport_session_name(name)) return false;

    for (const auto& other : all_sessions) {
        if (other.spawned_by == s.key) return true;
    }
    return false;
}

std::vector<AgentSessionHierarchy> build_session_hierarchy(const std::vector<OpenClawSession>& sessions) {
    std::vector<OpenClawSession> sorted = sessions;
    std::sort(sorted.begin(), sorted.end(), [](const OpenClawSession& a, const OpenClawSession& b) {
        if (a.agent != b.agent) return a.agent < b.agent;
        if (a.updated_at != b.updated_at) return a.updated_at > b.updated_at;
        return a.key < b.key;
    });

    std::map<std::string, std::vector<OpenClawSession>> sessions_by_agent;
    for (const auto& s : sorted) sessions_by_agent[s.agent].push_back(s);

    std::vector<AgentSessionHierarchy> result;
    for (const auto& [agent_id, agent_sessions] : sessions_by_agent) {
        AgentSessionHierarchy group;
        group.agent_id = agent_id;

        std::unordered_map<std::string, std::vector<SessionNode>> subagents_by_parent;
        for (const auto& s : agent_sessions) {
            SessionNode node;
            node.session = s;
            node.session_name = !s.label.empty() ? s.label : session_name_from_key(s.key);
            node.channel = infer_session_channel(s);
            node.is_subagent = is_subagent_session(s);
            node.is_orchestrator = is_orchestrator_session(s, agent_sessions);

            if (node.is_orchestrator) {
                group.orchestrators.push_back(std::move(node));
            } else if (node.is_subagent) {
                if (!s.spawned_by.empty()) {
                    subagents_by_parent[s.spawned_by].push_back(node);
                } else {
                    group.unmatched_subagents.push_back(std::move(node));
                }
            } else {
                group.others.push_back(std::move(node));
            }
        }

        std::vector<SessionNode> matched_orchestrators;
        for (auto& orchestrator : group.orchestrators) {
            auto it = subagents_by_parent.find(orchestrator.session.key);
            if (it == subagents_by_parent.end()) {
                matched_orchestrators.push_back(std::move(orchestrator));
                continue;
            }
            matched_orchestrators.push_back(std::move(orchestrator));
            for (auto& sub : it->second) {
                group.unmatched_subagents.push_back(std::move(sub));
            }
        }
        group.orchestrators = std::move(matched_orchestrators);

        std::vector<SessionNode> still_unmatched;
        still_unmatched.reserve(group.unmatched_subagents.size());
        for (auto& sub : group.unmatched_subagents) {
            if (!sub.session.spawned_by.empty()) {
                bool found_parent = false;
                for (const auto& orchestrator : group.orchestrators) {
                    if (orchestrator.session.key == sub.session.spawned_by) {
                        found_parent = true;
                        break;
                    }
                }
                if (!found_parent) still_unmatched.push_back(std::move(sub));
            } else {
                still_unmatched.push_back(std::move(sub));
            }
        }
        group.unmatched_subagents = std::move(still_unmatched);

        result.push_back(std::move(group));
    }

    return result;
}
