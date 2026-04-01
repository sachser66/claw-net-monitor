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
    std::vector<std::string> docker_containers;
    std::vector<std::string> openclaw_sockets;
    int openclaw_session_count = 0;
    bool openclaw_socket_activity = false;
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

std::string read_file_text(const std::string& path) {
    std::ifstream file(path);
    if (!file) return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

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

std::vector<std::string> parse_openclaw_sockets(const std::string& out) {
    std::vector<std::string> lines;
    std::istringstream iss(out);
    std::string line;
    bool first = true;
    while (std::getline(iss, line)) {
        if (first) {
            first = false;
            continue;
        }
        if (line.find("openclaw") == std::string::npos) continue;
        lines.push_back(line);
        if (lines.size() >= 6) break;
    }
    return lines;
}

bool has_openclaw_local_activity(const std::vector<std::string>& socket_lines) {
    for (const auto& line : socket_lines) {
        if ((line.find("127.0.0.1") != std::string::npos || line.find("[::1]") != std::string::npos || line.find("localhost") != std::string::npos) &&
            (line.find("ESTAB") != std::string::npos || line.find("LISTEN") != std::string::npos || line.find("UNCONN") != std::string::npos)) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> parse_lines(const std::string& out, std::size_t limit) {
    std::vector<std::string> lines;
    std::istringstream iss(out);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        lines.push_back(line);
        if (lines.size() >= limit) break;
    }
    return lines;
}

std::string json_get_string_field(const std::string& block, const std::string& field) {
    std::string needle = "\"" + field + "\"";
    auto pos = block.find(needle);
    if (pos == std::string::npos) return "";
    auto colon = block.find(':', pos);
    if (colon == std::string::npos) return "";
    auto start = block.find('"', colon + 1);
    if (start == std::string::npos) return "";
    auto end = block.find('"', start + 1);
    if (end == std::string::npos) return "";
    return block.substr(start + 1, end - start - 1);
}

std::vector<std::string> extract_session_lines(const std::string& json) {
    std::vector<std::string> lines;
    auto sessions_pos = json.find("\"sessions\"");
    if (sessions_pos == std::string::npos) return lines;

    std::size_t pos = json.find("\"key\"", sessions_pos);
    while (pos != std::string::npos && lines.size() < 12) {
        auto obj_start = json.rfind('{', pos);
        auto obj_end = json.find('}', pos);
        if (obj_start == std::string::npos || obj_end == std::string::npos) break;
        std::string block = json.substr(obj_start, obj_end - obj_start + 1);

        std::string key = json_get_string_field(block, "key");
        std::string agent = json_get_string_field(block, "agentId");
        std::string status = json_get_string_field(block, "status");
        std::string kind = json_get_string_field(block, "kind");

        if (agent.empty()) {
            if (key.rfind("agent:", 0) == 0) {
                auto first = key.find(':');
                auto second = key.find(':', first + 1);
                if (second != std::string::npos) agent = key.substr(first + 1, second - first - 1);
            }
        }
        if (agent.empty()) agent = "?";
        if (status.empty()) status = "?";
        if (kind.empty()) kind = "?";
        if (!key.empty()) lines.push_back(agent + " | " + status + " | " + kind + " | " + key);

        pos = json.find("\"key\"", obj_end + 1);
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
            if (count > 0) return count;
        }
    }

    auto sessions_pos = out.find("\"sessions\"");
    if (sessions_pos == std::string::npos) return 0;
    std::size_t pos = out.find("\"key\"", sessions_pos);
    while (pos != std::string::npos) {
        count++;
        pos = out.find("\"key\"", pos + 1);
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

const GroupStat* find_group(const std::vector<GroupStat>& groups, const std::string& name) {
    for (const auto& g : groups) {
        if (g.name == name) return &g;
    }
    return nullptr;
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
    CachedText ss_cache, openclaw_cache, docker_net_cache, docker_ps_cache;
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
            openclaw_cache.text = exec_read("openclaw sessions --all-agents --json 2>/dev/null");
            openclaw_cache.fetched_at = now;
            openclaw_cache.ready = true;
        }
        if (!docker_net_cache.ready || now - docker_net_cache.fetched_at > std::chrono::seconds(10)) {
            docker_net_cache.text = exec_read("docker network ls --format '{{.Name}}  {{.Driver}}  {{.Scope}}' 2>/dev/null");
            docker_net_cache.fetched_at = now;
            docker_net_cache.ready = true;
        }
        if (!docker_ps_cache.ready || now - docker_ps_cache.fetched_at > std::chrono::seconds(5)) {
            docker_ps_cache.text = exec_read("docker ps --format '{{.Names}}  {{.Status}}' 2>/dev/null");
            docker_ps_cache.fetched_at = now;
            docker_ps_cache.ready = true;
        }

        Snapshot snapshot;
        snapshot.interfaces = std::move(interfaces);
        snapshot.conn_states = parse_ss_summary(ss_cache.text);
        snapshot.openclaw_session_count = parse_session_count(openclaw_cache.text);
        snapshot.openclaw_sessions = extract_session_lines(openclaw_cache.text);
        snapshot.openclaw_sockets = parse_openclaw_sockets(ss_cache.text);
        snapshot.openclaw_socket_activity = has_openclaw_local_activity(snapshot.openclaw_sockets);
        snapshot.docker_networks = parse_lines(docker_net_cache.text, 6);
        snapshot.docker_containers = parse_lines(docker_ps_cache.text, 6);
        auto groups = build_group_stats(snapshot.interfaces);

        const GroupStat* internet = find_group(groups, "Internet/LAN");
        const GroupStat* docker = find_group(groups, "Docker");
        const GroupStat* localhost = find_group(groups, "Localhost");

        double internet_activity = internet ? internet->total() : 0.0;
        double docker_activity = docker ? docker->total() : 0.0;
        double localhost_activity = localhost ? localhost->total() : 0.0;
        double openclaw_activity = snapshot.openclaw_socket_activity ? localhost_activity : 0.0;

        erase();
        attron(COLOR_PAIR(1) | A_BOLD);
        mvprintw(0, 2, "claw-net-monitor C++ UX-V10");
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

        mvprintw(21, 2, "%s", shorten("OpenClaw: alle Sessions aller Agenten", left_w - 6).c_str());
        mvprintw(22, 2, "Alle Sessions gesamt: %d", snapshot.openclaw_session_count);
        mvprintw(23, 2, "%s", shorten(snapshot.openclaw_socket_activity ? "Belegt: openclaw hat localhost-Socket(s) im ss-Output." : "Nicht belegt: kein eindeutiger openclaw localhost-Socket im ss-Output.", left_w - 6).c_str());
        row = 24;
        if (!snapshot.openclaw_sockets.empty()) {
            mvprintw(row++, 2, "%s", shorten("OpenClaw-Sockets:", left_w - 6).c_str());
            for (std::size_t i = 0; i < snapshot.openclaw_sockets.size() && row < LINES - 1; ++i) {
                mvprintw(row++, 2, "%s", shorten(snapshot.openclaw_sockets[i], left_w - 6).c_str());
            }
        }
        if (row < LINES - 1) {
            mvprintw(row++, 2, "%s", shorten("Sessions aller Agenten:", left_w - 6).c_str());
        }
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

        refresh();
        tick++;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    endwin();
    return 0;
}
