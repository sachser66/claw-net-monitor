#include "triggers.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace {
std::string gateway_fingerprint(const Snapshot& snapshot) {
    return snapshot.gateway.mode + "|" + snapshot.gateway.bind + "|" + snapshot.gateway.port + "|" + snapshot.gateway.probe_url;
}

std::string docker_fingerprint(const Snapshot& snapshot) {
    std::ostringstream out;
    for (const auto& n : snapshot.docker_networks) out << "N:" << n << "\n";
    for (const auto& c : snapshot.docker_containers) out << "C:" << c << "\n";
    return out.str();
}

bool has_traffic_change(const Snapshot& snapshot, double threshold) {
    for (const auto& iface : snapshot.interfaces) {
        if (std::fabs(iface.rx_rate) >= threshold || std::fabs(iface.tx_rate) >= threshold) {
            return true;
        }
    }
    return false;
}
}

std::vector<std::string> detect_trigger_events(
    TriggerState& state,
    const Snapshot& snapshot,
    std::size_t config_hash,
    double traffic_threshold_bytes_per_sec
) {
    std::vector<std::string> events;

    const auto gw_fp = gateway_fingerprint(snapshot);
    const auto dk_fp = docker_fingerprint(snapshot);

    std::unordered_set<std::string> current_keys;
    std::unordered_map<std::string, long long> current_updates;
    for (const auto& s : snapshot.openclaw_session_items) {
        current_keys.insert(s.key);
        current_updates[s.key] = s.updated_at;
    }

    if (!state.initialized) {
        state.initialized = true;
        state.config_hash = config_hash;
        state.gateway_fingerprint = gw_fp;
        state.docker_fingerprint = dk_fp;
        state.session_keys = current_keys;
        state.session_updates = current_updates;
        return events;
    }

    if (state.config_hash != config_hash) {
        events.push_back("config_changed");
        state.config_hash = config_hash;
    }

    if (state.gateway_fingerprint != gw_fp) {
        events.push_back("gateway_changed");
        state.gateway_fingerprint = gw_fp;
    }

    if (state.docker_fingerprint != dk_fp) {
        events.push_back("docker_changed");
        state.docker_fingerprint = dk_fp;
    }

    bool added = false;
    bool removed = false;
    bool updated = false;

    for (const auto& key : current_keys) {
        if (!state.session_keys.count(key)) {
            added = true;
        }
    }
    for (const auto& key : state.session_keys) {
        if (!current_keys.count(key)) {
            removed = true;
        }
    }
    for (const auto& [key, updated_at] : current_updates) {
        auto it = state.session_updates.find(key);
        if (it != state.session_updates.end() && it->second != updated_at) {
            updated = true;
            break;
        }
    }

    if (added) events.push_back("session_added");
    if (removed) events.push_back("session_removed");
    if (updated) events.push_back("session_updated");

    if (has_traffic_change(snapshot, traffic_threshold_bytes_per_sec)) {
        events.push_back("traffic_changed");
    }

    state.session_keys = std::move(current_keys);
    state.session_updates = std::move(current_updates);

    return events;
}
