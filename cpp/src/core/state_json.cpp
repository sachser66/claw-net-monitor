#include "state_json.hpp"

#include <sstream>

namespace {
std::string escape_json(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char ch : s) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += ch; break;
        }
    }
    return out;
}

void write_session_json(std::ostringstream& out, const OpenClawSession& s) {
    out << '{';
    out << "\"agent\":\"" << escape_json(s.agent) << "\",";
    out << "\"status\":\"" << escape_json(s.status) << "\",";
    out << "\"kind\":\"" << escape_json(s.kind) << "\",";
    out << "\"key\":\"" << escape_json(s.key) << "\",";
    out << "\"model\":\"" << escape_json(s.model) << "\",";
    out << "\"model_provider\":\"" << escape_json(s.model_provider) << "\",";
    out << "\"last_channel\":\"" << escape_json(s.last_channel) << "\",";
    out << "\"provider\":\"" << escape_json(s.provider) << "\",";
    out << "\"spawned_by\":\"" << escape_json(s.spawned_by) << "\",";
    out << "\"label\":\"" << escape_json(s.label) << "\",";
    out << "\"thinking_level\":\"" << escape_json(s.thinking_level) << "\",";
    out << "\"input_tokens\":" << s.input_tokens << ",";
    out << "\"output_tokens\":" << s.output_tokens << ",";
    out << "\"cache_read_tokens\":" << s.cache_read_tokens << ",";
    out << "\"cache_write_tokens\":" << s.cache_write_tokens << ",";
    out << "\"total_tokens\":" << s.total_tokens << ",";
    out << "\"remaining_tokens\":" << s.remaining_tokens << ",";
    out << "\"context_tokens\":" << s.context_tokens << ",";
    out << "\"percent_used\":" << s.percent_used << ",";
    out << "\"total_tokens_fresh\":" << (s.total_tokens_fresh ? "true" : "false") << ",";
    out << "\"aborted_last_run\":" << (s.aborted_last_run ? "true" : "false") << ",";
    out << "\"system_sent\":" << (s.system_sent ? "true" : "false");
    out << '}';
}

void write_session_nodes_json(std::ostringstream& out, const char* name, const std::vector<SessionNode>& nodes) {
    out << "\"" << name << "\":[";
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto& node = nodes[i];
        if (i) out << ',';
        out << '{';
        out << "\"session_name\":\"" << escape_json(node.session_name) << "\",";
        out << "\"channel\":\"" << escape_json(node.channel) << "\",";
        out << "\"is_orchestrator\":" << (node.is_orchestrator ? "true" : "false") << ',';
        out << "\"is_subagent\":" << (node.is_subagent ? "true" : "false") << ',';
        out << "\"session\":";
        write_session_json(out, node.session);
        out << '}';
    }
    out << ']';
}
}

std::string snapshot_to_json(const Snapshot& snapshot) {
    std::ostringstream out;
    out << "{";
    out << "\"openclaw\":{";
    out << "\"session_count\":" << snapshot.openclaw_session_count << ",";
    out << "\"socket_activity\":" << (snapshot.openclaw_socket_activity ? "true" : "false") << ",";

    out << "\"gateway\":{";
    out << "\"mode\":\"" << escape_json(snapshot.gateway.mode) << "\",";
    out << "\"bind\":\"" << escape_json(snapshot.gateway.bind) << "\",";
    out << "\"port\":\"" << escape_json(snapshot.gateway.port) << "\",";
    out << "\"probe_url\":\"" << escape_json(snapshot.gateway.probe_url) << "\"},";

    out << "\"health\":{";
    out << "\"available\":" << (snapshot.openclaw_health.available ? "true" : "false") << ",";
    out << "\"ok\":" << (snapshot.openclaw_health.ok ? "true" : "false") << ",";
    out << "\"status_text\":\"" << escape_json(snapshot.openclaw_health.status_text) << "\",";
    out << "\"configured_channels\":" << snapshot.openclaw_health.configured_channels << ",";
    out << "\"healthy_channels\":" << snapshot.openclaw_health.healthy_channels << ",";
    out << "\"running_channels\":" << snapshot.openclaw_health.running_channels << ",";
    out << "\"default_agent_id\":\"" << escape_json(snapshot.openclaw_health.default_agent_id) << "\",";
    out << "\"heartbeat_enabled_agents\":" << snapshot.openclaw_health.heartbeat_enabled_agents << "},";

    out << "\"status\":{";
    out << "\"available\":" << (snapshot.openclaw_status.available ? "true" : "false") << ",";
    out << "\"runtime_version\":\"" << escape_json(snapshot.openclaw_status.runtime_version) << "\",";
    out << "\"queued_system_events\":" << snapshot.openclaw_status.queued_system_events << ",";
    out << "\"default_model\":\"" << escape_json(snapshot.openclaw_status.default_model) << "\",";
    out << "\"session_count\":" << snapshot.openclaw_status.session_count << ",";
    out << "\"heartbeat_enabled_agents\":" << snapshot.openclaw_status.heartbeat_enabled_agents << ",";
    out << "\"session_pressure_high\":" << snapshot.openclaw_status.session_pressure_high << ",";
    out << "\"aborted_sessions\":" << snapshot.openclaw_status.aborted_sessions << ",";
    out << "\"system_sessions\":" << snapshot.openclaw_status.system_sessions << ",";
    out << "\"max_percent_used\":" << snapshot.openclaw_status.max_percent_used << ",";
    out << "\"hottest_session\":\"" << escape_json(snapshot.openclaw_status.hottest_session) << "\"},";

    out << "\"usage\":{";
    out << "\"available\":" << (snapshot.openclaw_usage.available ? "true" : "false") << ",";
    out << "\"updated_at\":" << snapshot.openclaw_usage.updated_at << ",";
    out << "\"providers\":[";
    for (std::size_t i = 0; i < snapshot.openclaw_usage.providers.size(); ++i) {
        const auto& provider = snapshot.openclaw_usage.providers[i];
        if (i) out << ',';
        out << '{';
        out << "\"provider\":\"" << escape_json(provider.provider) << "\",";
        out << "\"display_name\":\"" << escape_json(provider.display_name) << "\",";
        out << "\"plan\":\"" << escape_json(provider.plan) << "\",";
        out << "\"windows\":[";
        for (std::size_t j = 0; j < provider.windows.size(); ++j) {
            const auto& window = provider.windows[j];
            if (j) out << ',';
            out << '{';
            out << "\"label\":\"" << escape_json(window.label) << "\",";
            out << "\"used_percent\":" << window.used_percent << ",";
            out << "\"reset_at\":" << window.reset_at;
            out << '}';
        }
        out << "]}";
    }
    out << "]},";
    out << "\"usage_cost\":{";
    out << "\"available\":" << (snapshot.openclaw_usage_cost.available ? "true" : "false") << ",";
    out << "\"days\":" << snapshot.openclaw_usage_cost.days << ",";
    out << "\"currency\":\"" << escape_json(snapshot.openclaw_usage_cost.currency) << "\",";
    out << "\"total_cost\":" << snapshot.openclaw_usage_cost.total_cost << ",";
    out << "\"today_cost\":" << snapshot.openclaw_usage_cost.today_cost << ",";
    out << "\"total_cost_eur\":" << snapshot.openclaw_usage_cost.total_cost_eur << ",";
    out << "\"today_cost_eur\":" << snapshot.openclaw_usage_cost.today_cost_eur << ",";
    out << "\"total_tokens\":" << snapshot.openclaw_usage_cost.total_tokens << ",";
    out << "\"today_tokens\":" << snapshot.openclaw_usage_cost.today_tokens << ",";
    out << "\"cache_read_share\":" << snapshot.openclaw_usage_cost.cache_read_share << "},";

    out << "\"agents\":[";
    for (std::size_t i = 0; i < snapshot.openclaw_agents.size(); ++i) {
        const auto& a = snapshot.openclaw_agents[i];
        if (i) out << ',';
        out << '{';
        out << "\"id\":\"" << escape_json(a.id) << "\",";
        out << "\"name\":\"" << escape_json(a.name) << "\",";
        out << "\"emoji\":\"" << escape_json(a.emoji) << "\",";
        out << "\"workspace\":\"" << escape_json(a.workspace) << "\",";
        out << "\"model_primary\":\"" << escape_json(a.model_primary) << "\",";
        out << "\"fallbacks\":[";
        for (std::size_t j = 0; j < a.model_fallbacks.size(); ++j) {
            if (j) out << ',';
            out << '"' << escape_json(a.model_fallbacks[j]) << '"';
        }
        out << "],";
        out << "\"bound_accounts\":[";
        for (std::size_t j = 0; j < a.bound_accounts.size(); ++j) {
            if (j) out << ',';
            out << '"' << escape_json(a.bound_accounts[j]) << '"';
        }
        out << "],";
        out << "\"bound_channels\":[";
        for (std::size_t j = 0; j < a.bound_channels.size(); ++j) {
            if (j) out << ',';
            out << '"' << escape_json(a.bound_channels[j]) << '"';
        }
        out << "],";
        out << "\"primary_model_available\":" << (a.primary_model_available ? "true" : "false") << ",";
        out << "\"fallback_models_available\":" << a.fallback_models_available << ",";
        out << "\"fallback_models_total\":" << a.fallback_models_total;
        out << "}";
    }
    out << "],";

    out << "\"trigger_events\":[";
    for (std::size_t i = 0; i < snapshot.trigger_events.size(); ++i) {
        if (i) out << ',';
        out << '"' << escape_json(snapshot.trigger_events[i]) << '"';
    }
    out << "],";

    out << "\"sessions\":[";
    for (std::size_t i = 0; i < snapshot.openclaw_session_items.size(); ++i) {
        if (i) out << ',';
        write_session_json(out, snapshot.openclaw_session_items[i]);
    }
    out << "],";

    out << "\"session_hierarchy\":[";
    for (std::size_t i = 0; i < snapshot.openclaw_session_hierarchy.size(); ++i) {
        const auto& group = snapshot.openclaw_session_hierarchy[i];
        if (i) out << ',';
        out << '{';
        out << "\"agent_id\":\"" << escape_json(group.agent_id) << "\",";
        write_session_nodes_json(out, "orchestrators", group.orchestrators);
        out << ',';
        write_session_nodes_json(out, "unmatched_subagents", group.unmatched_subagents);
        out << ',';
        write_session_nodes_json(out, "others", group.others);
        out << '}';
    }
    out << "],";

    out << "\"models\":[";
    for (std::size_t i = 0; i < snapshot.openclaw_models.size(); ++i) {
        const auto& m = snapshot.openclaw_models[i];
        if (i) out << ',';
        out << '{';
        out << "\"key\":\"" << escape_json(m.key) << "\",";
        out << "\"name\":\"" << escape_json(m.name) << "\",";
        out << "\"provider\":\"" << escape_json(m.provider) << "\",";
        out << "\"auth_profile\":\"" << escape_json(m.auth_profile) << "\",";
        out << "\"auth_profile\":\"" << escape_json(m.auth_profile) << "\",";
        out << "\"auth_id\":\"" << escape_json(m.auth_id) << "\",";
        out << "\"auth_type\":\"" << escape_json(m.auth_type) << "\",";
        out << "\"available\":" << (m.available ? "true" : "false");
        out << '}';
    }
    out << "],";

    out << "\"channels\":[";
    for (std::size_t i = 0; i < snapshot.openclaw_channels.size(); ++i) {
        const auto& c = snapshot.openclaw_channels[i];
        if (i) out << ',';
        out << '{';
        out << "\"kind\":\"" << escape_json(c.kind) << "\",";
        out << "\"account_id\":\"" << escape_json(c.account_id) << "\",";
        out << "\"label\":\"" << escape_json(c.label) << "\"";
        out << '}';
    }
    out << "]},";

    out << "\"network\":{";
    out << "\"interfaces\":[";
    for (std::size_t i = 0; i < snapshot.interfaces.size(); ++i) {
        const auto& n = snapshot.interfaces[i];
        if (i) out << ',';
        out << '{';
        out << "\"name\":\"" << escape_json(n.name) << "\",";
        out << "\"group\":\"" << escape_json(n.group) << "\",";
        out << "\"rx_rate\":" << n.rx_rate << ',';
        out << "\"tx_rate\":" << n.tx_rate;
        out << '}';
    }
    out << "],";
    out << "\"conn_states\":[";
    for (std::size_t i = 0; i < snapshot.conn_states.size(); ++i) {
        if (i) out << ',';
        out << '{';
        out << "\"state\":\"" << escape_json(snapshot.conn_states[i].first) << "\",";
        out << "\"count\":" << snapshot.conn_states[i].second;
        out << '}';
    }
    out << "]},";

    out << "\"docker\":{";
    out << "\"networks\":[";
    for (std::size_t i = 0; i < snapshot.docker_networks.size(); ++i) {
        if (i) out << ',';
        out << '"' << escape_json(snapshot.docker_networks[i]) << '"';
    }
    out << "],";
    out << "\"containers\":[";
    for (std::size_t i = 0; i < snapshot.docker_containers.size(); ++i) {
        if (i) out << ',';
        out << '"' << escape_json(snapshot.docker_containers[i]) << '"';
    }
    out << "]}";

    out << '}';
    return out.str();
}
