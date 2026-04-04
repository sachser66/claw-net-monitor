#include "summary_text.hpp"

#include <algorithm>
#include <ctime>
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


std::string format_reset_at(long long reset_at_ms) {
    if (reset_at_ms <= 0) return "";
    const std::time_t reset_at = static_cast<std::time_t>(reset_at_ms / 1000);
    std::tm local_tm{};
    if (!localtime_r(&reset_at, &local_tm)) return "";
    std::ostringstream out;
    out << std::put_time(&local_tm, "%a %H:%M");
    return out.str();
}

std::string format_duration_short(long long ms) {
    if (ms <= 0) return "";
    long long total_sec = ms / 1000;
    long long days = total_sec / 86400;
    long long hours = (total_sec % 86400) / 3600;
    long long mins = (total_sec % 3600) / 60;
    std::ostringstream out;
    if (days > 0) {
        out << days << "d";
        if (hours > 0) out << " " << hours << "h";
        return out.str();
    }
    if (hours > 0) {
        out << hours << "h";
        if (mins > 0) out << " " << mins << "m";
        return out.str();
    }
    out << std::max(1LL, mins) << "m";
    return out.str();
}

long long now_ms() {
    return static_cast<long long>(std::time(nullptr)) * 1000;
}

std::string format_int_de(long long value) {
    std::string s = std::to_string(value);
    int insert_pos = static_cast<int>(s.size()) - 3;
    while (insert_pos > 0) {
        s.insert(insert_pos, ".");
        insert_pos -= 3;
    }
    return s;
}

std::string money(double usd, double eur) {
    std::ostringstream out;
    out << "$" << std::fixed << std::setprecision(2) << usd
        << " (~€" << std::fixed << std::setprecision(2) << eur << ")";
    return out.str();
}

std::string format_session_line(const OpenClawSession& s) {
    std::ostringstream out;
    out << short_session_name(s);
    if (!s.model.empty()) out << " | model " << s.model;
    if (!s.thinking_level.empty()) out << " | think " << s.thinking_level;
    if (s.percent_used >= 0) out << " | left " << std::max(0, 100 - s.percent_used) << "%";
    if (s.total_tokens >= 0) out << " | tok " << format_int_de(s.total_tokens);
    if (s.remaining_tokens >= 0) out << " | remaining " << format_int_de(s.remaining_tokens);
    out << " | " << (s.total_tokens_fresh ? "fresh" : "stale");
    return out.str();
}
}

std::string snapshot_to_summary_text(const Snapshot& snapshot, bool full) {
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
                out << " | " << provider.windows[j].label << " " << std::max(0, 100 - provider.windows[j].used_percent) << "% left"; const auto remaining = format_duration_short(provider.windows[j].reset_at - now_ms()); if (!remaining.empty()) out << " ⏱" << remaining; const auto reset = format_reset_at(provider.windows[j].reset_at); if (!reset.empty()) out << " (resets " << reset << ")";
            }
        }
        out << "\n";
    }

    if (snapshot.openclaw_usage_cost.available) {
        out << "• Cost 7d: " << money(snapshot.openclaw_usage_cost.total_cost, snapshot.openclaw_usage_cost.total_cost_eur)
            << " | Today: " << money(snapshot.openclaw_usage_cost.today_cost, snapshot.openclaw_usage_cost.today_cost_eur) << "\n";
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
            if (hottest[i].percent >= 0) out << " | " << std::max(0, 100 - hottest[i].percent) << "% left";
            if (hottest[i].total >= 0) out << " | " << format_int_de(hottest[i].total) << " tok";
            out << "\n";
        }
    }

    if (!full) return out.str();

    out << "\nAgents:\n";
    for (const auto& agent : snapshot.openclaw_agents) {
        long long total_tokens = 0;
        int pressured = 0;
        int stale = 0;
        int count = 0;
        bool has_tokens = false;
        for (const auto& s : snapshot.openclaw_session_items) {
            if (s.agent != agent.id) continue;
            count++;
            if (s.total_tokens >= 0) {
                total_tokens += s.total_tokens;
                has_tokens = true;
            }
            if (s.percent_used >= 80) pressured++;
            if (!s.total_tokens_fresh) stale++;
        }
        out << "• " << agent.id << ": sessions " << count;
        out << " | tokens " << (has_tokens ? format_int_de(total_tokens) : std::string("-"));
        out << " | pressured " << pressured;
        out << " | stale " << stale << "\n";
    }

    out << "\nSessions:\n";
    for (const auto& group : snapshot.openclaw_session_hierarchy) {
        out << "[" << group.agent_id << "]\n";
        for (const auto& node : group.orchestrators) {
            out << "  * orchestrator: " << format_session_line(node.session) << "\n";
            for (const auto& sub : group.unmatched_subagents) {
                if (sub.session.spawned_by == node.session.key) {
                    out << "    -> subagent: " << format_session_line(sub.session) << "\n";
                }
            }
        }
        bool printed_unmatched = false;
        for (const auto& sub : group.unmatched_subagents) {
            bool matched = false;
            for (const auto& orch : group.orchestrators) {
                if (sub.session.spawned_by == orch.session.key) {
                    matched = true;
                    break;
                }
            }
            if (matched) continue;
            if (!printed_unmatched) {
                out << "  subagents (unmatched parent):\n";
                printed_unmatched = true;
            }
            out << "    -> " << format_session_line(sub.session) << "\n";
        }
        if (!group.others.empty()) {
            out << "  other sessions:\n";
            for (const auto& other : group.others) {
                out << "    - " << format_session_line(other.session) << "\n";
            }
        }
    }

    return out.str();
}
