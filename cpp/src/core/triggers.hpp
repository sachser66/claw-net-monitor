#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "snapshot.hpp"

struct TriggerState {
    bool initialized = false;
    std::size_t config_hash = 0;
    std::string gateway_fingerprint;
    std::string docker_fingerprint;
    std::unordered_map<std::string, long long> session_updates;
    std::unordered_set<std::string> session_keys;
};

std::vector<std::string> detect_trigger_events(
    TriggerState& state,
    const Snapshot& snapshot,
    std::size_t config_hash,
    double traffic_threshold_bytes_per_sec = 4096.0
);
