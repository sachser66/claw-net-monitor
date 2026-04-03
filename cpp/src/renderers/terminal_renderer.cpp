#include "terminal_renderer.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <map>
#include <sstream>
#include <unordered_map>

#include <ncurses.h>

#include "../collectors/network_collector.hpp"
#include "../core/session_hierarchy.hpp"

std::string fmt_rate(double value) {
    static const char* units[] = {"B/s", "KB/s", "MB/s", "GB/s"};
    int idx = 0;
    while (value >= 1024.0 && idx < 3) {
        value /= 1024.0;
        idx++;
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << value << ' ' << units[idx];
    return out.str();
}

std::string format_duration_short(long long total_seconds) {
    long long h = total_seconds / 3600;
    long long m = (total_seconds % 3600) / 60;
    long long s = total_seconds % 60;
    std::ostringstream out;
    out << std::setfill('0') << std::setw(2) << h << ':'
        << std::setw(2) << m << ':'
        << std::setw(2) << s;
    return out.str();
}

std::string shorten(const std::string& text, int width) {
    if (width <= 0) return "";
    if (static_cast<int>(text.size()) <= width) return text;
    if (width <= 3) return text.substr(0, width);
    return text.substr(0, width - 3) + "...";
}

std::string summarize_group(const GroupStat& g) {
    std::string joined;
    for (std::size_t i = 0; i < g.ifaces.size() && i < 2; ++i) {
        if (!joined.empty()) joined += ", ";
        joined += g.ifaces[i];
    }
    if (joined.empty()) joined = "-";
    return g.name + ": RX " + fmt_rate(g.rx) + " | TX " + fmt_rate(g.tx) + " | " + joined;
}

std::string make_real_flow_line(const std::string& from, const std::string& to, int tick, int width, double activity, const std::string& evidence) {
    width = std::max(8, width);
    if (activity <= 1.0) {
        return from + " [inactive] " + to + " | " + evidence;
    }
    std::string path(width, '-');
    int pos = tick % width;
    path[pos] = 'o';
    return from + " [" + path + "] " + to + " | " + evidence;
}

std::string ascii_spinner(int tick) {
    static const char* frames[] = {"|", "/", "-", "\\"};
    return frames[(tick / 2) % 4];
}

void print_segments(int y, int x, int width, const std::vector<std::pair<int, std::string>>& segments) {
    if (width <= 0) return;
    int col = x;
    int remaining = width;
    for (const auto& seg : segments) {
        if (remaining <= 0) break;
        const auto text = shorten(seg.second, remaining);
        attron(COLOR_PAIR(seg.first));
        mvaddnstr(y, col, text.c_str(), remaining);
        attroff(COLOR_PAIR(seg.first));
        col += static_cast<int>(text.size());
        remaining -= static_cast<int>(text.size());
    }
}

std::vector<int> value_color_palette() {
    if (has_colors() && COLORS >= 256) {
        return {34, 37, 39, 40, 44, 45, 51, 69, 75, 81, 87, 99, 111, 117, 123, 135, 141, 147, 153, 171, 177, 183, 190, 221, 227, 230};
    }
    return {COLOR_GREEN, COLOR_YELLOW, COLOR_MAGENTA, COLOR_BLUE, COLOR_CYAN, COLOR_WHITE, COLOR_GREEN};
}

void ensure_value_color_pairs() {
    static bool initialized = false;
    if (initialized || !has_colors()) return;
    const auto palette = value_color_palette();
    for (std::size_t i = 0; i < palette.size(); ++i) {
        init_pair(20 + static_cast<short>(i), static_cast<short>(palette[i]), -1);
    }
    initialized = true;
}

int color_for_value(const std::string& value) {
    ensure_value_color_pairs();
    if (value.empty() || value == "-") return 7;
    static std::unordered_map<std::string, int> assigned;
    static std::size_t next = 0;

    auto it = assigned.find(value);
    if (it != assigned.end()) return it->second;

    for (const auto& [known, pair_id] : assigned) {
        if (known.find(value) != std::string::npos || value.find(known) != std::string::npos) {
            assigned[value] = pair_id;
            return pair_id;
        }
    }

    const auto palette = value_color_palette();
    if (next < palette.size()) {
        int pair_id = 20 + static_cast<int>(next++);
        assigned[value] = pair_id;
        return pair_id;
    }

    int pair_id = 20 + static_cast<int>(std::hash<std::string>{}(value) % palette.size());
    assigned[value] = pair_id;
    return pair_id;
}


int color_for_status(const std::string& status) {
    if (status == "running") return 6;
    if (status == "idle") return 7;
    return 1;
}

int color_for_kind(const std::string& kind) {
    if (kind == "direct") return 8;
    if (kind == "other") return 9;
    return 1;
}

int color_for_channel(const std::string& channel) {
    if (channel == "tui") return 6;
    if (channel == "telegram") return 3;
    if (channel == "whatsapp") return 6;
    if (channel == "discord") return 4;
    if (channel == "signal") return 8;
    if (channel == "webchat") return 5;
    return 1;
}

long long age_ms_from_update(long long updated_at_ms) {
    if (updated_at_ms <= 0) return -1;
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return now_ms - updated_at_ms;
}

std::string busy_label(long long updated_at_ms) {
    const long long age_ms = age_ms_from_update(updated_at_ms);
    if (age_ms < 0) return "unknown";
    if (age_ms <= 5LL * 60LL * 1000LL) return "session-hot";
    if (age_ms <= 15LL * 60LL * 1000LL) return "busy";
    if (age_ms <= 60LL * 60LL * 1000LL) return "warm";
    return "idle";
}

int busy_color(long long updated_at_ms) {
    const long long age_ms = age_ms_from_update(updated_at_ms);
    if (age_ms < 0) return 7;
    if (age_ms <= 5LL * 60LL * 1000LL) return 11;
    if (age_ms <= 15LL * 60LL * 1000LL) return 6;
    if (age_ms <= 60LL * 60LL * 1000LL) return 3;
    return 7;
}

void box(int y, int x, int h, int w, const std::string& title, int color_pair) {
    if (h < 3 || w < 4) return;
    attron(COLOR_PAIR(color_pair));
    mvprintw(y, x, "+");
    for (int i = 1; i < w - 1; ++i) mvprintw(y, x + i, "-");
    mvprintw(y, x + w - 1, "+");
    for (int row = y + 1; row < y + h - 1; ++row) {
        mvprintw(row, x, "|");
        mvprintw(row, x + w - 1, "|");
    }
    mvprintw(y + h - 1, x, "+");
    for (int i = 1; i < w - 1; ++i) mvprintw(y + h - 1, x + i, "-");
    mvprintw(y + h - 1, x + w - 1, "+");
    attron(A_BOLD);
    mvprintw(y, x + 2, " %s ", title.c_str());
    attroff(A_BOLD);
    attroff(COLOR_PAIR(color_pair));
}

void render_terminal(const Snapshot& snapshot, const std::vector<GroupStat>& groups, int tick) {
    const GroupStat* internet = find_group(groups, "Internet/LAN");
    const GroupStat* docker = find_group(groups, "Docker");
    const GroupStat* localhost = find_group(groups, "Localhost");

    double internet_activity = internet ? internet->total() : 0.0;
    double docker_activity = docker ? docker->total() : 0.0;
    double localhost_activity = localhost ? localhost->total() : 0.0;
    double openclaw_activity = snapshot.openclaw_socket_activity ? localhost_activity : 0.0;

    erase();
    const bool startup_loading = snapshot.monitor_uptime_seconds < 12 &&
                                 (snapshot.openclaw_session_seq <= 1 ||
                                  snapshot.openclaw_agents.empty() ||
                                  snapshot.openclaw_session_items.empty());
    const std::string spinner = ascii_spinner(tick);

    attron(COLOR_PAIR(1) | A_BOLD);
    mvprintw(0, 2, "claw-net-monitor C++ UX-V21 | copyright by Thomas Riedel");
    attroff(COLOR_PAIR(1) | A_BOLD);
    std::string top_right = startup_loading ? ("loading " + spinner + " | q quit | refresh 5s")
                                            : "q quit | refresh 5s";
    mvprintw(0, std::max(2, COLS - static_cast<int>(top_right.size()) - 2), "%s", top_right.c_str());

    const int x = 1;
    const int w = COLS - 2;
    int y = 2;

    auto draw_box = [&](int h, const std::string& title, int color) {
        box(y, x, h, w, title, color);
        int top = y;
        y += h;
        return top;
    };

    const int traffic_h = 7;
    const int flow_h = 5;
    const int oc_h = std::max(12, LINES - (2 + traffic_h + flow_h) - 1);

    const int oc_y = draw_box(oc_h, "OPENCLAW", 4);
    const int traffic_y = draw_box(traffic_h, "WO IST TRAFFIC?", 1);
    const int flow_y = draw_box(std::max(5, LINES - y - 1), "PAKETFLUSS", 2);

    std::map<std::string, int> agent_counts;
    for (const auto& s : snapshot.openclaw_session_items) agent_counts[s.agent]++;

    int row = oc_y + 1;
    const int oc_bottom = oc_y + oc_h - 1;
    print_segments(row++, 2, w - 4, {{1, "OpenClaw: "}, {4, "Config"}, {1, " + "}, {5, "Live-Sessions"}});
    if (row < oc_bottom) {
        std::vector<std::pair<int, std::string>> segments = {
            {10, "Sessions: "},
            {color_for_value(std::to_string(snapshot.openclaw_session_count)), std::to_string(snapshot.openclaw_session_count)},
            {10, " | Agents: "},
            {color_for_value(std::to_string(snapshot.openclaw_agents.size())), std::to_string(snapshot.openclaw_agents.size())},
            {10, " | Gateway: "},
            {color_for_value(snapshot.gateway.mode.empty() ? "?" : snapshot.gateway.mode), snapshot.gateway.mode.empty() ? "?" : snapshot.gateway.mode},
            {10, " / bind "},
            {color_for_value(snapshot.gateway.bind.empty() ? "?" : snapshot.gateway.bind), snapshot.gateway.bind.empty() ? "?" : snapshot.gateway.bind},
            {10, " / port "},
            {color_for_value(snapshot.gateway.port.empty() ? "?" : snapshot.gateway.port), snapshot.gateway.port.empty() ? "?" : snapshot.gateway.port}
        };
        if (!snapshot.trigger_events.empty()) {
            std::string ev;
            for (std::size_t i = 0; i < snapshot.trigger_events.size() && i < 4; ++i) {
                if (i) ev += ", ";
                ev += snapshot.trigger_events[i];
            }
            segments.push_back({10, " | Events: "});
            segments.push_back({color_for_value(ev), ev});
        }
        print_segments(row++, 2, w - 4, segments);
        if (!snapshot.trigger_events.empty() && row < oc_bottom) row++;
    }
    if (row < oc_bottom) {
        const auto& h = snapshot.openclaw_health;
        const auto& s = snapshot.openclaw_status;
        std::string health_text = h.available ? (h.ok ? "ok" : "warn") : "-";
        std::string runtime_text = s.available ? (s.runtime_version.empty() ? "-" : s.runtime_version) : "-";
        print_segments(row++, 2, w - 4, {
            {10, "Health: "},
            {h.ok ? 6 : 3, health_text},
            {10, " | Runtime: "},
            {color_for_value(runtime_text), runtime_text},
            {10, " | Queued events: "},
            {color_for_value(std::to_string(s.queued_system_events)), std::to_string(s.queued_system_events)},
            {10, " | Heartbeat agents: "},
            {color_for_value(std::to_string(h.heartbeat_enabled_agents)), std::to_string(h.heartbeat_enabled_agents)}
        });
    }
    if (row < oc_bottom) {
        const auto& h = snapshot.openclaw_health;
        const auto& s = snapshot.openclaw_status;
        const auto& c = snapshot.openclaw_usage_cost;
        std::string hottest = s.hottest_session.empty() ? "-" : s.hottest_session;
        std::string cost7d;
        {
            std::ostringstream usd;
            std::ostringstream eur;
            usd << std::fixed << std::setprecision(2) << c.total_cost;
            eur << std::fixed << std::setprecision(2) << c.total_cost_eur;
            cost7d = c.available ? ("$" + usd.str() + " (€" + eur.str() + ")") : "-";
        }
        print_segments(row++, 2, w - 4, {
            {10, "Channels ok: "},
            {color_for_value(std::to_string(h.healthy_channels) + "/" + std::to_string(h.configured_channels)), std::to_string(h.healthy_channels) + "/" + std::to_string(h.configured_channels)},
            {10, " | Pressure>=80%: "},
            {s.session_pressure_high > 0 ? 3 : 6, std::to_string(s.session_pressure_high)},
            {10, " | Max used: "},
            {color_for_value(std::to_string(s.max_percent_used) + "%"), std::to_string(s.max_percent_used) + "%"},
            {10, " | Cost 7d: "},
            {color_for_value(cost7d), cost7d}
        });
        if (row < oc_bottom && !hottest.empty()) {
            print_segments(row++, 2, w - 4, {
                {10, "Hottest session: "},
                {color_for_value(hottest), hottest}
            });
        }
        if (row < oc_bottom) row++;
    }

    if (row < oc_bottom) mvprintw(row++, 2, "%s", shorten("Agents from openclaw.json:", w - 4).c_str());
    for (std::size_t i = 0; i < snapshot.openclaw_agents.size() && row < oc_bottom; ++i) {
        const auto& a = snapshot.openclaw_agents[i];
        std::string accounts;
        for (std::size_t j = 0; j < a.bound_accounts.size(); ++j) {
            if (!accounts.empty()) accounts += ", ";
            accounts += a.bound_accounts[j];
        }
        std::string fallback_summary = "-";
        if (!a.model_fallbacks.empty()) {
            fallback_summary.clear();
            for (std::size_t j = 0; j < a.model_fallbacks.size(); ++j) {
                if (j) fallback_summary += ", ";
                fallback_summary += a.model_fallbacks[j];
            }
        }
        std::string channel_summary = "-";
        if (!a.bound_channels.empty()) {
            channel_summary.clear();
            for (std::size_t j = 0; j < a.bound_channels.size(); ++j) {
                if (j) channel_summary += ", ";
                channel_summary += a.bound_channels[j];
            }
        }
        long long latest_agent_update = 0;
        for (const auto& s : snapshot.openclaw_session_items) {
            if (s.agent == a.id && s.updated_at > latest_agent_update) latest_agent_update = s.updated_at;
        }
        if (row < oc_bottom) {
            const std::string agent_id = a.id.empty() ? "-" : a.id;
            print_segments(row++, 2, w - 4, {
                {color_for_value(agent_id), agent_id},
                {10, " | name: "},
                {color_for_value(a.name.empty() ? "-" : a.name), a.name.empty() ? "-" : a.name},
                {10, " | sessions: "},
                {color_for_value(std::to_string(agent_counts[a.id])), std::to_string(agent_counts[a.id])},
                {10, " | "},
                {busy_color(latest_agent_update), busy_label(latest_agent_update)}
            });
        }
        if (row < oc_bottom) {
            const std::string model = a.model_primary.empty() ? "-" : a.model_primary;
            const std::string account_text = accounts.empty() ? "-" : accounts;
            const std::string model_health = a.primary_model_available ? "primary ok" : "primary missing";
            std::string auth_text = "-";
            for (const auto& m : snapshot.openclaw_models) {
                if (m.key == model) {
                    auth_text = m.auth_type.empty() ? "-" : m.auth_type;
                    if (!m.auth_id.empty()) auth_text += " (" + m.auth_id + ")";
                    break;
                }
            }
            print_segments(row++, 4, w - 6, {
                {10, "model: "},
                {color_for_value(model), model},
                {10, " | auth: "},
                {color_for_value(auth_text), auth_text}
            });
            if (row < oc_bottom) {
                print_segments(row++, 4, w - 6, {
                    {10, "model-status: "},
                    {a.primary_model_available ? 6 : 11, model_health},
                    {10, " | accounts: "},
                    {color_for_value(account_text), account_text}
                });
            }
            if (row < oc_bottom) {
                print_segments(row++, 4, w - 6, {
                    {10, "channels: "},
                    {color_for_value(channel_summary), channel_summary}
                });
            }
        }
        if (row < oc_bottom) {
            const std::string workspace = a.workspace.empty() ? "-" : a.workspace;
            print_segments(row++, 4, w - 6, {
                {10, "workspace: "},
                {color_for_value(workspace), workspace}
            });
        }
        if (row < oc_bottom) {
            const std::string fb_health = std::to_string(a.fallback_models_available) + "/" + std::to_string(a.fallback_models_total) + " ok";
            print_segments(row++, 4, w - 6, {
                {10, "fallbacks: "},
                {color_for_value(fallback_summary), fallback_summary}
            });
            if (row < oc_bottom) {
                print_segments(row++, 4, w - 6, {
                    {10, "fallback-status: "},
                    {a.fallback_models_available == a.fallback_models_total ? 6 : 3, fb_health}
                });
            }
        }
        if (row < oc_bottom && i + 1 < snapshot.openclaw_agents.size()) {
            row++;
        }
    }

    if (row < oc_bottom) row++;
    if (row < oc_bottom) {
        std::string details = "Session details: update 5s | seq " + std::to_string(snapshot.openclaw_session_seq) + " | monitor " + format_duration_short(snapshot.monitor_uptime_seconds);
        mvprintw(row++, 2, "%s", shorten(details, w - 4).c_str());
    }
    if (row < oc_bottom) {
        std::string page_line = "All sessions on one page | grouped by agent/orchestrator";
        mvprintw(row++, 2, "%s", shorten(page_line, w - 4).c_str());
    }

    auto print_session = [&](const SessionNode& node, bool orchestrator, bool subagent) {
        if (row >= oc_bottom) return;
        const auto& s = node.session;
        std::string provider_short = s.model_provider.empty() ? "-" : s.model_provider;
        const int indent = subagent ? 12 : 6;
        const std::string prefix = orchestrator ? "* orchestrator | " : (subagent ? "  -> subagent | " : "- ");
        print_segments(row++, indent, w - indent - 2, {
            {10, prefix},
            {color_for_value(node.channel), node.channel},
            {10, " | "},
            {color_for_value(node.session_name), node.session_name},
            {10, " | "},
            {color_for_value(s.model), s.model},
            {10, " | "},
            {color_for_value(provider_short), provider_short},
            {10, " | "},
            {busy_color(s.updated_at), busy_label(s.updated_at)}
        });
    };

    for (const auto& group : snapshot.openclaw_session_hierarchy) {
        if (row >= oc_bottom) break;
        const std::string& agent_id = group.agent_id;
        attron(A_BOLD);
        print_segments(row++, 2, w - 4, {
            {1, "["},
            {color_for_value(agent_id), agent_id},
            {1, "] ("},
            {color_for_value(std::to_string(agent_counts[agent_id])), std::to_string(agent_counts[agent_id])},
            {1, ")"}
        });
        attroff(A_BOLD);

        std::unordered_map<std::string, std::vector<const SessionNode*>> subagents_by_parent;
        for (const auto& sub : group.unmatched_subagents) {
            if (!sub.session.spawned_by.empty()) {
                subagents_by_parent[sub.session.spawned_by].push_back(&sub);
            }
        }

        for (const auto& orchestrator : group.orchestrators) {
            print_session(orchestrator, true, false);
            auto it = subagents_by_parent.find(orchestrator.session.key);
            if (it != subagents_by_parent.end()) {
                for (const auto* sub : it->second) print_session(*sub, false, true);
            }
        }

        bool printed_unmatched_header = false;
        for (const auto& sub : group.unmatched_subagents) {
            if (!sub.session.spawned_by.empty() && subagents_by_parent.find(sub.session.spawned_by) != subagents_by_parent.end()) {
                bool parent_exists = false;
                for (const auto& orchestrator : group.orchestrators) {
                    if (orchestrator.session.key == sub.session.spawned_by) {
                        parent_exists = true;
                        break;
                    }
                }
                if (parent_exists) continue;
            }
            if (!printed_unmatched_header && row < oc_bottom) {
                print_segments(row++, 6, w - 8, {{10, "subagents (unmatched parent):"}});
                printed_unmatched_header = true;
            }
            print_session(sub, false, true);
        }

        if (!group.others.empty() && row < oc_bottom) {
            print_segments(row++, 6, w - 8, {{10, "other sessions:"}});
            for (const auto& other : group.others) print_session(other, false, false);
        }
    }

    row = traffic_y + 1;
    const int traffic_bottom = traffic_y + traffic_h - 1;
    attron(A_DIM);
    mvprintw(row++, 2, "%s", shorten("Gemessene Netzgruppen nach Aktivitaet", w - 4).c_str());
    for (std::size_t i = 0; i < groups.size() && row < traffic_bottom; ++i) {
        std::string prefix = (i == 0 && groups[i].total() > 0.0) ? "* " : "  ";
        mvprintw(row++, 2, "%s", shorten(prefix + summarize_group(groups[i]), w - 4).c_str());
    }
    attroff(A_DIM);

    row = flow_y + 1;
    const int flow_bottom = flow_y + flow_h - 1;
    attron(A_DIM);
    if (row < flow_bottom) mvprintw(row++, 2, "%s", shorten(make_real_flow_line("Internet", "Host", tick, 14, internet_activity, internet_activity > 1.0 ? "gemessen via Internet/LAN-Ifaces" : "kein Aktivitaetswert"), w - 4).c_str());
    if (row < flow_bottom) mvprintw(row++, 2, "%s", shorten(make_real_flow_line("Host", "Docker", tick + 4, 12, docker_activity, docker_activity > 1.0 ? "gemessen via docker/br-Ifaces" : "kein Aktivitaetswert"), w - 4).c_str());
    if (row < flow_bottom) mvprintw(row++, 2, "%s", shorten(make_real_flow_line("Localhost", "OpenClaw", tick + 2, 10, openclaw_activity, snapshot.openclaw_socket_activity ? "openclaw Socket auf localhost" : "kein belegter openclaw localhost-Socket"), w - 4).c_str());
    attroff(A_DIM);

}
