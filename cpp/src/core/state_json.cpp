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
        const auto& s = snapshot.openclaw_session_items[i];
        if (i) out << ',';
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
        out << "\"label\":\"" << escape_json(s.label) << "\"";
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
