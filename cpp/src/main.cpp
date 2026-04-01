#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <ncurses.h>

using Clock = std::chrono::steady_clock;

struct InterfaceSample {
    std::string name;
    unsigned long long rx_bytes = 0;
    unsigned long long tx_bytes = 0;
    double rx_rate = 0.0;
    double tx_rate = 0.0;
    std::string group = "Sonstiges";
};

struct Snapshot {
    std::vector<InterfaceSample> interfaces;
    std::vector<std::pair<std::string, int>> conn_states;
    std::vector<std::string> openclaw_sessions;
    std::vector<std::string> docker_networks;
    int openclaw_session_count = 0;
};

struct CachedText {
    std::string text;
    Clock::time_point fetched_at{};
    bool ready = false;
};

struct GroupStat {
    std::string name;
    double rx = 0.0;
    double tx = 0.0;
    std::vector<std::string> ifaces;
    double total() const { return rx + tx; }
};

static std::map<std::string, std::pair<unsigned long long, unsigned long long>> g_prev;

std::string exec_read(const std::string& cmd) {
    std::array<char, 4096> buffer{};
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return result;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        result += buffer.data();
    }
    pclose(pipe);
    return result;
}

std::string classify_group(const std::string& name) {
    if (name == "lo") return "Localhost";
    if (name.rfind("wg", 0) == 0 || name.rfind("tun", 0) == 0 || name.rfind("tap", 0) == 0) return "VPN";
    if (name.rfind("docker", 0) == 0 || name.rfind("br-", 0) == 0) return "Docker";
    if (name.rfind("en", 0) == 0 || name.rfind("eth", 0) == 0 || name.rfind("wl", 0) == 0) return "Internet/LAN";
    return "Sonstiges";
}

std::vector<InterfaceSample> read_interfaces(double dt_seconds) {
    std::ifstream file("/proc/net/dev");
    std::vector<InterfaceSample> items;
    std::string line;
    int line_no = 0;

    while (std::getline(file, line)) {
        line_no++;
        if (line_no <= 2) continue;
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string name = line.substr(0, colon);
        name.erase(0, name.find_first_not_of(" \t"));
        name.erase(name.find_last_not_of(" \t") + 1);

        std::istringstream iss(line.substr(colon + 1));
        InterfaceSample s;
        s.name = name;
        s.group = classify_group(name);
        unsigned long long rx_packets, rx_errs, rx_drop, rx_fifo, rx_frame, rx_compressed, rx_multicast;
        unsigned long long tx_packets, tx_errs, tx_drop, tx_fifo, tx_colls, tx_carrier, tx_compressed;
        iss >> s.rx_bytes >> rx_packets >> rx_errs >> rx_drop >> rx_fifo >> rx_frame >> rx_compressed >> rx_multicast
            >> s.tx_bytes >> tx_packets >> tx_errs >> tx_drop >> tx_fifo >> tx_colls >> tx_carrier >> tx_compressed;

        auto it = g_prev.find(s.name);
        if (it != g_prev.end() && dt_seconds > 0.0) {
            s.rx_rate = static_cast<double>(s.rx_bytes - it->second.first) / dt_seconds;
            s.tx_rate = static_cast<double>(s.tx_bytes - it->second.second) / dt_seconds;
        }
        g_prev[s.name] = {s.rx_bytes, s.tx_bytes};
        items.push_back(s);
    }

    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
        return (a.rx_rate + a.tx_rate) > (b.rx_rate + b.tx_rate);
    });
    return items;
}

std::vector<std::pair<std::string, int>> parse_ss_summary(const std::string& out) {
    std::map<std::string, int> counts;
    std::istringstream iss(out);
    std::string line;
    bool first = true;
    while (std::getline(iss, line)) {
        if (first) {
            first = false;
            continue;
        }
        std::istringstream ls(line);
        std::string state;
        ls >> state;
        if (!state.empty()) counts[state]++;
    }
    std::vector<std::pair<std::string, int>> items(counts.begin(), counts.end());
    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    return items;
}

std::vector<std::string> parse_docker_networks(const std::string& out) {
    std::vector<std::string> lines;
    std::istringstream iss(out);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        lines.push_back(line);
        if (lines.size() >= 6) break;
    }
    return lines;
}

std::vector<std::string> extract_session_lines(const std::string& json) {
    std::vector<std::string> lines;
    std::size_t pos = 0;
    while (lines.size() < 10) {
        auto key_pos = json.find("\"key\"", pos);
        if (key_pos == std::string::npos) break;
        auto key_start = json.find('"', key_pos + 5);
        if (key_start == std::string::npos) break;
        auto key_end = json.find('"', key_start + 1);
        if (key_end == std::string::npos) break;
        std::string key = json.substr(key_start + 1, key_end - key_start - 1);

        auto agent_pos = json.rfind("\"agentId\"", key_pos);
        std::string agent = "?";
        if (agent_pos != std::string::npos) {
            auto s = json.find('"', agent_pos + 9);
            auto e = s == std::string::npos ? std::string::npos : json.find('"', s + 1);
            if (s != std::string::npos && e != std::string::npos) agent = json.substr(s + 1, e - s - 1);
        }

        lines.push_back(agent + "  " + key);
        pos = key_end + 1;
    }
    return lines;
}

int parse_session_count(const std::string& out) {
    int count = 0;
    auto count_pos = out.find("\"count\"");
    if (count_pos != std::string::npos) {
        auto colon = out.find(':', count_pos);
        if (colon != std::string::npos) {
            std::stringstream ss(out.substr(colon + 1));
            ss >> count;
        }
    }
    return count;
}

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

std::vector<GroupStat> build_group_stats(const std::vector<InterfaceSample>& interfaces) {
    std::map<std::string, GroupStat> grouped;
    for (const auto& iface : interfaces) {
        auto& g = grouped[iface.group];
        g.name = iface.group;
        g.rx += iface.rx_rate;
        g.tx += iface.tx_rate;
        g.ifaces.push_back(iface.name);
    }
    std::vector<GroupStat> out;
    for (auto& [_, stat] : grouped) out.push_back(stat);
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.total() > b.total(); });
    return out;
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

std::string make_flow_line(const std::string& from, const std::string& to, int tick, int width) {
    width = std::max(8, width);
    std::string path(width, '-');
    int pos = tick % width;
    path[pos] = 'o';
    return from + " [" + path + "] " + to;
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

int main() {
    initscr();
    noecho();
    cbreak();
    curs_set(0);
    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);
    timeout(0);

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_CYAN, -1);
        init_pair(2, COLOR_GREEN, -1);
        init_pair(3, COLOR_YELLOW, -1);
        init_pair(4, COLOR_MAGENTA, -1);
        init_pair(5, COLOR_BLUE, -1);
    }

    auto last = Clock::now();
    CachedText ss_cache, openclaw_cache, docker_cache;
    int tick = 0;

    while (true) {
        int ch = getch();
        if (ch == 'q' || ch == 'Q') break;

        auto now = Clock::now();
        double dt = std::chrono::duration<double>(now - last).count();
        last = now;

        auto interfaces = read_interfaces(dt);
        if (!ss_cache.ready || now - ss_cache.fetched_at > std::chrono::seconds(3)) {
            ss_cache.text = exec_read("ss -tunap 2>/dev/null");
            ss_cache.fetched_at = now;
            ss_cache.ready = true;
        }
        if (!openclaw_cache.ready || now - openclaw_cache.fetched_at > std::chrono::seconds(5)) {
            openclaw_cache.text = exec_read("openclaw sessions --json 2>/dev/null");
            openclaw_cache.fetched_at = now;
            openclaw_cache.ready = true;
        }
        if (!docker_cache.ready || now - docker_cache.fetched_at > std::chrono::seconds(10)) {
            docker_cache.text = exec_read("docker network ls --format '{{.Name}}  {{.Driver}}  {{.Scope}}' 2>/dev/null");
            docker_cache.fetched_at = now;
            docker_cache.ready = true;
        }

        Snapshot snapshot;
        snapshot.interfaces = std::move(interfaces);
        snapshot.conn_states = parse_ss_summary(ss_cache.text);
        snapshot.openclaw_session_count = parse_session_count(openclaw_cache.text);
        snapshot.openclaw_sessions = extract_session_lines(openclaw_cache.text);
        snapshot.docker_networks = parse_docker_networks(docker_cache.text);
        auto groups = build_group_stats(snapshot.interfaces);

        erase();
        attron(COLOR_PAIR(1) | A_BOLD);
        mvprintw(0, 2, "claw-net-monitor C++ UX-V6");
        attroff(COLOR_PAIR(1) | A_BOLD);
        mvprintw(0, COLS - 22, "q quit | refresh 0.5s");

        int left_w = COLS / 2;
        int right_w = COLS - left_w - 1;
        box(2, 1, 10, left_w - 2, "WO IST TRAFFIC?", 1);
        box(12, 1, 7, left_w - 2, "PAKETFLUSS", 2);
        box(19, 1, LINES - 20, left_w - 2, "OPENCLAW", 4);
        box(2, left_w, 10, right_w, "VERBINDUNGEN", 3);
        box(12, left_w, LINES - 13, right_w, "DOCKER", 5);

        mvprintw(3, 2, "%s", shorten("Kurzsicht: welche Netzgruppen gerade am meisten bewegen", left_w - 6).c_str());
        int row = 4;
        for (std::size_t i = 0; i < groups.size() && row < 10; ++i) {
            std::string prefix = (i == 0 && groups[i].total() > 0.0) ? "* " : "  ";
            mvprintw(row++, 2, "%s", shorten(prefix + summarize_group(groups[i]), left_w - 6).c_str());
        }
        if (!groups.empty()) {
            mvprintw(10, 2, "%s", shorten("Staerkste Gruppe gerade: " + groups.front().name, left_w - 6).c_str());
        }

        mvprintw(13, 2, "%s", shorten("Animierte, vereinfachte Paketwege zwischen Bereichen", left_w - 6).c_str());
        row = 14;
        mvprintw(row++, 2, "%s", shorten(make_flow_line("Internet", "LAN", tick, 12), left_w - 6).c_str());
        mvprintw(row++, 2, "%s", shorten(make_flow_line("LAN", "Docker", tick + 4, 10), left_w - 6).c_str());
        mvprintw(row++, 2, "%s", shorten(make_flow_line("Docker", "OpenClaw", tick + 8, 8), left_w - 6).c_str());
        mvprintw(row++, 2, "%s", shorten(make_flow_line("Localhost", "OpenClaw", tick + 2, 8), left_w - 6).c_str());

        mvprintw(20, 2, "%s", shorten("OpenClaw-Sessions auf diesem Host", left_w - 6).c_str());
        mvprintw(21, 2, "Sessions gesamt: %d", snapshot.openclaw_session_count);
        row = 22;
        if (snapshot.openclaw_sessions.empty()) {
            mvprintw(row, 2, "Keine Sessiondaten gefunden.");
        } else {
            for (std::size_t i = 0; i < snapshot.openclaw_sessions.size() && row < LINES - 1; ++i) {
                mvprintw(row++, 2, "%s", shorten(snapshot.openclaw_sessions[i], left_w - 6).c_str());
            }
        }

        mvprintw(3, left_w + 2, "%s", shorten("Welche Verbindungsarten sieht der Host gerade?", right_w - 4).c_str());
        row = 5;
        for (std::size_t i = 0; i < snapshot.conn_states.size() && row < 10; ++i) {
            std::string line = snapshot.conn_states[i].first + ": " + std::to_string(snapshot.conn_states[i].second);
            mvprintw(row++, left_w + 2, "%s", shorten(line, right_w - 4).c_str());
        }
        mvprintw(10, left_w + 2, "%s", shorten("Noch grobe Host-Sicht, noch keine echte Paketfluss-Karte.", right_w - 4).c_str());

        mvprintw(13, left_w + 2, "%s", shorten("Vorhandene Docker-Netzwerke auf dem Host", right_w - 4).c_str());
        row = 15;
        if (snapshot.docker_networks.empty()) {
            mvprintw(row, left_w + 2, "Keine Docker-Netze oder kein Zugriff.");
        } else {
            for (std::size_t i = 0; i < snapshot.docker_networks.size() && row < LINES - 1; ++i) {
                mvprintw(row++, left_w + 2, "%s", shorten(snapshot.docker_networks[i], right_w - 4).c_str());
            }
        }

        refresh();
        tick++;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    endwin();
    return 0;
}
