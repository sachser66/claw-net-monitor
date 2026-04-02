#include "terminal_renderer.hpp"

#include <algorithm>
#include <iomanip>
#include <map>
#include <sstream>
#include <unordered_map>

#include <ncurses.h>

#include "../collectors/network_collector.hpp"
#include "../collectors/openclaw_collector.hpp"

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
        return {34, 40, 45, 51, 69, 75, 81, 87, 99, 111, 117, 123, 135, 141, 147, 153, 171, 177, 183, 190, 197, 203, 209, 215, 221, 227};
    }
    return {COLOR_GREEN, COLOR_YELLOW, COLOR_MAGENTA, COLOR_BLUE, COLOR_CYAN, COLOR_RED, COLOR_WHITE};
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
    return name.rfind("subagent:", 0) == 0;
}

bool is_orchestrator_session(const OpenClawSession& s, const std::vector<OpenClawSession>& all_sessions) {
    const std::string name = session_name_from_key(s.key);
    if (name == "main" || name == s.agent) return true;
    if (is_subagent_session(s)) return false;
    if (is_transport_session_name(name)) return false;

    bool agent_has_subagents = false;
    for (const auto& other : all_sessions) {
        if (other.agent == s.agent && is_subagent_session(other)) {
            agent_has_subagents = true;
            break;
        }
    }
    return agent_has_subagents;
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
    attron(COLOR_PAIR(1) | A_BOLD);
    mvprintw(0, 2, "claw-net-monitor C++ UX-V21 | copyright by Thomas Riedel");
    attroff(COLOR_PAIR(1) | A_BOLD);
    mvprintw(0, COLS - 22, "q quit | refresh 0.5s");

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
    const int conn_h = 6;
    const int oc_h = std::max(12, LINES - (2 + traffic_h + flow_h + conn_h) - 1);

    const int oc_y = draw_box(oc_h, "OPENCLAW", 4);
    const int traffic_y = draw_box(traffic_h, "WO IST TRAFFIC?", 1);
    const int flow_y = draw_box(flow_h, "PAKETFLUSS", 2);
    const int conn_y = draw_box(std::max(5, LINES - y - 1), "VERBINDUNGEN", 3);

    std::map<std::string, int> agent_counts;
    for (const auto& s : snapshot.openclaw_session_items) agent_counts[s.agent]++;

    int row = oc_y + 1;
    const int oc_bottom = oc_y + oc_h - 1;
    print_segments(row++, 2, w - 4, {{1, "OpenClaw: "}, {4, "Config"}, {1, " + "}, {5, "Live-Sessions"}});
    if (row < oc_bottom) {
        print_segments(row++, 2, w - 4, {
            {10, "Sessions: "},
            {color_for_value(std::to_string(snapshot.openclaw_session_count)), std::to_string(snapshot.openclaw_session_count)},
            {10, " | Agents: "},
            {color_for_value(std::to_string(snapshot.openclaw_agents.size())), std::to_string(snapshot.openclaw_agents.size())}
        });
    }
    if (row < oc_bottom) {
        print_segments(row++, 2, w - 4, {
            {10, "Gateway: "},
            {color_for_value(snapshot.gateway.mode.empty() ? "?" : snapshot.gateway.mode), snapshot.gateway.mode.empty() ? "?" : snapshot.gateway.mode},
            {10, " / bind "},
            {color_for_value(snapshot.gateway.bind.empty() ? "?" : snapshot.gateway.bind), snapshot.gateway.bind.empty() ? "?" : snapshot.gateway.bind},
            {10, " / port "},
            {color_for_value(snapshot.gateway.port.empty() ? "?" : snapshot.gateway.port), snapshot.gateway.port.empty() ? "?" : snapshot.gateway.port}
        });
    }
    if (!snapshot.trigger_events.empty() && row < oc_bottom) {
        std::string ev = "Events: ";
        for (std::size_t i = 0; i < snapshot.trigger_events.size() && i < 4; ++i) {
            if (i) ev += ", ";
            ev += snapshot.trigger_events[i];
        }
        mvprintw(row++, 2, "%s", shorten(ev, w - 4).c_str());
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
        if (row < oc_bottom) {
            const std::string agent_id = a.id.empty() ? "-" : a.id;
            print_segments(row++, 2, w - 4, {
                {color_for_value(agent_id), agent_id},
                {10, " | name: "},
                {color_for_value(a.name.empty() ? "-" : a.name), a.name.empty() ? "-" : a.name},
                {10, " | sessions: "},
                {color_for_value(std::to_string(agent_counts[a.id])), std::to_string(agent_counts[a.id])}
            });
        }
        if (row < oc_bottom) {
            const std::string model = a.model_primary.empty() ? "-" : a.model_primary;
            const std::string account_text = accounts.empty() ? "-" : accounts;
            print_segments(row++, 4, w - 6, {
                {10, "model: "},
                {color_for_value(model), model},
                {10, " | accounts: "},
                {color_for_value(account_text), account_text}
            });
        }
        if (row < oc_bottom) {
            const std::string workspace = a.workspace.empty() ? "-" : a.workspace;
            print_segments(row++, 4, w - 6, {
                {10, "workspace: "},
                {color_for_value(workspace), workspace}
            });
        }
        if (row < oc_bottom) {
            print_segments(row++, 4, w - 6, {
                {10, "fallbacks: "},
                {color_for_value(fallback_summary), fallback_summary}
            });
        }
        if (row < oc_bottom && i + 1 < snapshot.openclaw_agents.size()) {
            row++;
        }
    }

    std::vector<OpenClawSession> sessions = snapshot.openclaw_session_items;
    std::sort(sessions.begin(), sessions.end(), [](const OpenClawSession& a, const OpenClawSession& b) {
        if (a.agent != b.agent) return a.agent < b.agent;
        const bool a_orch = is_orchestrator_session(a, sessions);
        const bool b_orch = is_orchestrator_session(b, sessions);
        if (a_orch != b_orch) return a_orch > b_orch;
        const bool a_sub = is_subagent_session(a);
        const bool b_sub = is_subagent_session(b);
        if (a_sub != b_sub) return a_sub > b_sub;
        return a.key < b.key;
    });

    if (row < oc_bottom) row++;
    if (row < oc_bottom) mvprintw(row++, 2, "%s", shorten("Session details:", w - 4).c_str());
    const int remaining_rows = std::max(0, oc_bottom - row);
    const int page_size = std::max(1, remaining_rows);
    const int total_pages = sessions.empty() ? 1 : static_cast<int>((sessions.size() + page_size - 1) / page_size);
    const int page = sessions.empty() ? 0 : ((tick / 8) % total_pages);
    const int start = page * page_size;
    const int end = std::min<int>(start + page_size, sessions.size());
    if (row < oc_bottom) {
        std::string page_line = "Page " + std::to_string(page + 1) + "/" + std::to_string(total_pages) + " | sort: agent";
        mvprintw(row++, 2, "%s", shorten(page_line, w - 4).c_str());
    }
    std::string current_agent;
    for (int i = start; i < end && row < oc_bottom; ++i) {
        const auto& s = sessions[i];
        if (s.agent != current_agent && row < oc_bottom) {
            current_agent = s.agent;
            attron(A_BOLD);
            print_segments(row++, 2, w - 4, {
                {1, "["},
                {color_for_value(current_agent), current_agent},
                {1, "] ("},
                {color_for_value(std::to_string(agent_counts[current_agent])), std::to_string(agent_counts[current_agent])},
                {1, ")"}
            });
            attroff(A_BOLD);
        }
        if (row >= oc_bottom) break;
        std::string session_name = session_name_from_key(s.key);
        std::string channel = infer_session_channel(s);
        std::string provider_short = s.model_provider.empty() ? "-" : s.model_provider;
        const bool orchestrator = is_orchestrator_session(s, sessions);
        const bool subagent = is_subagent_session(s);
        const int indent = subagent ? 10 : 2;
        const std::string prefix = orchestrator ? "* orchestrator | " : (subagent ? "-> subagent | " : "- ");
        print_segments(row++, indent, w - indent - 2, {
            {10, prefix},
            {color_for_value(channel), channel},
            {10, " | "},
            {color_for_value(session_name), session_name},
            {10, " | "},
            {color_for_value(s.model), s.model},
            {10, " | "},
            {color_for_value(provider_short), provider_short}
        });
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

    row = conn_y + 1;
    const int conn_bottom = conn_y + conn_h - 1;
    attron(A_DIM);
    mvprintw(row++, 2, "%s", shorten("Welche Verbindungsarten sieht der Host gerade?", w - 4).c_str());
    for (std::size_t i = 0; i < snapshot.conn_states.size() && row < conn_bottom; ++i) {
        std::string line = snapshot.conn_states[i].first + ": " + std::to_string(snapshot.conn_states[i].second);
        mvprintw(row++, 2, "%s", shorten(line, w - 4).c_str());
    }
    attroff(A_DIM);

}
