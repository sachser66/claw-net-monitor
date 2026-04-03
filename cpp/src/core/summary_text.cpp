#include "summary_text.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <vector>

namespace {
std::string short_session_name(const OpenClawSession& s) {
    if (!s.label.empty()) return s.label;
    const std::string& key = s.key;
    auto pos = key.rfind(':');
    if (pos == std::string::npos || pos + 1 >= key.size()) return key;
    return key.substr(pos + 1);
}
}

std::string snapshot_to_summary_text(const Snapshot& snapshot) {
    std::ostringstream out;
    out << "OpenClaw Monitor\n";
    out << "• Health: " << (snapshot.openclaw_health.available ? (snapshot.openclaw_health.ok ? "ok" : "warn") : "-")
        << " | Sessions: " << snapshot.openclaw_session_count
        << " | Channels: " << snapshot.openclaw_health.healthy_channels << "/" << snapshot.openclaw_health.configured_channels << "\n";

    if (!snapshot.openclaw_usage.providers.empty()) {
        out << "• Usage: ";
        for (std::size_t i = 0; i < snapshot.openclaw_usage.providers.size(); ++i) {
            const auto& provider = snapshot.openclaw_usage.providers[i];
            if (i) out << " || ";
            out << (provider.display_name.empty() ? provider.provider : provider.display_name);
            if (!provider.plan.empty()) out << " " << provider.plan;
            for (std::size_t j = 0; j < provider.windows.size() && j < 2; ++j) {
                out << " | " << provider.windows[j].label << " " << provider.windows[j].used_percent << "%";
            }
        }
        out << "\n";
    }

    if (snapshot.openclaw_usage_cost.available) {
        out << "• Cost 7d: $" << std::fixed << std::setprecision(2) << snapshot.openclaw_usage_cost.total_cost
            << " (~€" << std::fixed << std::setprecision(2) << snapshot.openclaw_usage_cost.total_cost_eur << ")"
            << " | Today: $" << std::fixed << std::setprecision(2) << snapshot.openclaw_usage_cost.today_cost
            << " (~€" << std::fixed << std::setprecision(2) << snapshot.openclaw_usage_cost.today_cost_eur << ")\n";
    }

    out << "• Pressure >=80%: " << snapshot.openclaw_status.session_pressure_high
        << " | Queued events: " << snapshot.openclaw_status.queued_system_events << "\n";

    struct Item {
        std::string key;
        int percent = -1;
        long long total = -1;
        std::string agent;
    };
    std::vector<Item> hottest;
    for (const auto& s : snapshot.openclaw_session_items) {
        hottest.push_back({short_session_name(s), s.percent_used, s.total_tokens, s.agent});
    }
    std::sort(hottest.begin(), hottest.end(), [](const Item& a, const Item& b) {
        if (a.percent != b.percent) return a.percent > b.percent;
        return a.total > b.total;
    });

    if (!hottest.empty()) {
        out << "• Top sessions:\n";
        for (std::size_t i = 0; i < hottest.size() && i < 3; ++i) {
            out << "  - " << hottest[i].agent << ": " << hottest[i].key;
            if (hottest[i].percent >= 0) out << " | " << hottest[i].percent << "% used";
            if (hottest[i].total >= 0) out << " | " << hottest[i].total << " tok";
            out << "\n";
        }
    }

    return out.str();
}
