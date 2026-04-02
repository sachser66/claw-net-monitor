#include "terminal_renderer.hpp"

#include <algorithm>
#include <iomanip>
#include <map>
#include <sstream>

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
    mvprintw(0, 2, "claw-net-monitor C++ UX-V20");
    attroff(COLOR_PAIR(1) | A_BOLD);
    mvprintw(0, COLS - 22, "q quit | refresh 0.5s");

    int left_w = COLS / 2;
    int right_w = COLS - left_w - 1;
    box(2, 1, 10, left_w - 2, "WO IST TRAFFIC?", 1);
    box(12, 1, 8, left_w - 2, "PAKETFLUSS (NUR BEI MESSWERT)", 2);
    box(20, 1, LINES - 21, left_w - 2, "OPENCLAW", 4);
    box(2, left_w, 10, right_w, "VERBINDUNGEN", 3);
    box(12, left_w, LINES - 13, right_w, "DOCKER", 5);

    mvprintw(3, 2, "%s", shorten("Gemessene Netzgruppen nach Aktivitaet", left_w - 6).c_str());
    int row = 4;
    for (std::size_t i = 0; i < groups.size() && row < 10; ++i) {
        std::string prefix = (i == 0 && groups[i].total() > 0.0) ? "* " : "  ";
        mvprintw(row++, 2, "%s", shorten(prefix + summarize_group(groups[i]), left_w - 6).c_str());
    }
    if (!groups.empty()) {
        mvprintw(10, 2, "%s", shorten("Staerkste Gruppe gerade: " + groups.front().name, left_w - 6).c_str());
    }

    mvprintw(13, 2, "%s", shorten("Animation nur, wenn passende Host-Gruppe gerade Traffic zeigt", left_w - 6).c_str());
    row = 15;
    mvprintw(row++, 2, "%s", shorten(make_real_flow_line("Internet", "Host", tick, 12, internet_activity, internet_activity > 1.0 ? "gemessen via Internet/LAN-Ifaces" : "kein Aktivitaetswert"), left_w - 6).c_str());
    mvprintw(row++, 2, "%s", shorten(make_real_flow_line("Host", "Docker", tick + 4, 10, docker_activity, docker_activity > 1.0 ? "gemessen via docker/br-Ifaces" : "kein Aktivitaetswert"), left_w - 6).c_str());
    mvprintw(row++, 2, "%s", shorten(make_real_flow_line("Localhost", "OpenClaw", tick + 2, 8, openclaw_activity, snapshot.openclaw_socket_activity ? "gemessen: openclaw Socket auf localhost" : "kein belegter openclaw localhost-Socket"), left_w - 6).c_str());

    mvprintw(21, 2, "%s", shorten("OpenClaw: Config + Live-Sessions", left_w - 6).c_str());
    mvprintw(22, 2, "Sessions: %d | Agents: %d", snapshot.openclaw_session_count, static_cast<int>(snapshot.openclaw_agents.size()));
    mvprintw(23, 2, "%s", shorten((std::string("Gateway: ") + (snapshot.gateway.mode.empty() ? "?" : snapshot.gateway.mode) + " / bind " + (snapshot.gateway.bind.empty() ? "?" : snapshot.gateway.bind) + " / port " + (snapshot.gateway.port.empty() ? "?" : snapshot.gateway.port)).c_str(), left_w - 6).c_str());
    row = 24;
    if (!snapshot.openclaw_sockets.empty()) {
        mvprintw(row++, 2, "%s", shorten("OpenClaw-Sockets:", left_w - 6).c_str());
        for (std::size_t i = 0; i < snapshot.openclaw_sockets.size() && row < LINES - 1; ++i) {
            mvprintw(row++, 2, "%s", shorten(snapshot.openclaw_sockets[i], left_w - 6).c_str());
        }
    }
    if (!snapshot.openclaw_agents.empty() && row < LINES - 1) {
        mvprintw(row++, 2, "%s", shorten("Configured agents:", left_w - 6).c_str());
        for (std::size_t i = 0; i < snapshot.openclaw_agents.size() && row < LINES - 1; ++i) {
            const auto& a = snapshot.openclaw_agents[i];
            std::string accounts;
            for (std::size_t j = 0; j < a.bound_accounts.size() && j < 2; ++j) {
                if (!accounts.empty()) accounts += ",";
                accounts += a.bound_accounts[j];
            }
            std::string fallback = a.model_fallbacks.empty() ? "-" : a.model_fallbacks.front();
            std::string line = (a.emoji.empty() ? "" : a.emoji + " ") + a.id + " | " + (a.model_primary.empty() ? "-" : a.model_primary) + " | fb: " + fallback + " | acct: " + (accounts.empty() ? "-" : accounts);
            mvprintw(row++, 2, "%s", shorten(line, left_w - 6).c_str());
        }
    }
    if (row < LINES - 1) {
        mvprintw(row++, 2, "%s", shorten("Sessions gruppiert nach Agent:", left_w - 6).c_str());
    }
    if (row < LINES - 1) {
        attron(A_BOLD | COLOR_PAIR(1));
        mvprintw(row++, 2, "%s", shorten("      STATUS     | TYP        | KANAL     | SESSION         | MODELL      | PROVIDER", left_w - 6).c_str());
        attroff(A_BOLD | COLOR_PAIR(1));
    }
    if (snapshot.openclaw_session_items.empty()) {
        mvprintw(row, 2, "Keine Sessiondaten gefunden.");
    } else {
        std::map<std::string, int> agent_counts;
        for (const auto& s : snapshot.openclaw_session_items) agent_counts[s.agent]++;

        std::string current_agent;
        for (std::size_t i = 0; i < snapshot.openclaw_session_items.size() && row < LINES - 1; ++i) {
            const auto& s = snapshot.openclaw_session_items[i];
            if (s.agent != current_agent) {
                current_agent = s.agent;
                std::string header = "[" + current_agent + "] (" + std::to_string(agent_counts[current_agent]) + ")";
                attron(A_BOLD | COLOR_PAIR(4));
                mvprintw(row++, 2, "%s", shorten(header, left_w - 6).c_str());
                attroff(A_BOLD | COLOR_PAIR(4));
                if (row >= LINES - 1) break;
            }
            std::string session_name = session_name_from_key(s.key);
            mvprintw(row, 2, "  - ");
            attron(COLOR_PAIR(color_for_status(s.status)) | A_BOLD);
            mvprintw(row, 6, "%s", shorten(s.status, 10).c_str());
            attroff(COLOR_PAIR(color_for_status(s.status)) | A_BOLD);
            mvprintw(row, 16, " | ");
            attron(COLOR_PAIR(color_for_kind(s.kind)));
            mvprintw(row, 19, "%s", shorten(s.kind, 10).c_str());
            attroff(COLOR_PAIR(color_for_kind(s.kind)));
            std::string channel = infer_session_channel(s);
            mvprintw(row, 29, " | ");
            attron(COLOR_PAIR(color_for_channel(channel)) | A_BOLD);
            mvprintw(row, 32, "%s", shorten(channel, 9).c_str());
            attroff(COLOR_PAIR(color_for_channel(channel)) | A_BOLD);
            std::string model_short = s.model.empty() ? "-" : s.model;
            std::string provider_short = s.model_provider.empty() ? "-" : s.model_provider;
            mvprintw(row, 42, " | %s", shorten(session_name, 15).c_str());
            mvprintw(row, 60, " | %s", shorten(model_short, 10).c_str());
            mvprintw(row, 73, " | %s", shorten(provider_short, left_w - 79).c_str());
            row++;
        }
    }

    mvprintw(3, left_w + 2, "%s", shorten("Welche Verbindungsarten sieht der Host gerade?", right_w - 4).c_str());
    row = 5;
    for (std::size_t i = 0; i < snapshot.conn_states.size() && row < 10; ++i) {
        std::string line = snapshot.conn_states[i].first + ": " + std::to_string(snapshot.conn_states[i].second);
        mvprintw(row++, left_w + 2, "%s", shorten(line, right_w - 4).c_str());
    }
    mvprintw(10, left_w + 2, "%s", shorten("Host-Sicht aus ss -tunap, keine Topologie-Aussage.", right_w - 4).c_str());

    mvprintw(13, left_w + 2, "%s", shorten("Gemessen: Docker ist auf dem Host vorhanden.", right_w - 4).c_str());
    mvprintw(14, left_w + 2, "%s", shorten("Docker-Netzwerke:", right_w - 4).c_str());
    row = 15;
    if (snapshot.docker_networks.empty()) {
        mvprintw(row++, left_w + 2, "Keine Docker-Netze oder kein Zugriff.");
    } else {
        for (std::size_t i = 0; i < snapshot.docker_networks.size() && row < 19; ++i) {
            mvprintw(row++, left_w + 2, "%s", shorten(snapshot.docker_networks[i], right_w - 4).c_str());
        }
    }
    mvprintw(row++, left_w + 2, "%s", shorten("Laufende Container:", right_w - 4).c_str());
    if (snapshot.docker_containers.empty()) {
        mvprintw(row, left_w + 2, "Keine laufenden Container oder kein Zugriff.");
    } else {
        for (std::size_t i = 0; i < snapshot.docker_containers.size() && row < LINES - 1; ++i) {
            mvprintw(row++, left_w + 2, "%s", shorten(snapshot.docker_containers[i], right_w - 4).c_str());
        }
    }
}
