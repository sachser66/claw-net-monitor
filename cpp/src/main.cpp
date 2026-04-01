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
    std::string kind = "iface";
};

struct Snapshot {
    std::vector<InterfaceSample> interfaces;
    std::vector<std::string> topology;
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

std::string classify_interface(const std::string& name) {
    if (name == "lo") return "loopback";
    if (name.rfind("wg", 0) == 0 || name.rfind("tun", 0) == 0 || name.rfind("tap", 0) == 0) return "vpn";
    if (name.rfind("docker", 0) == 0 || name.rfind("br-", 0) == 0) return "docker";
    if (name.rfind("en", 0) == 0 || name.rfind("eth", 0) == 0 || name.rfind("wl", 0) == 0) return "internet";
    return "iface";
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
        s.kind = classify_interface(name);
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
        if (lines.size() >= 8) break;
    }
    return lines;
}

std::vector<std::string> extract_session_lines(const std::string& json) {
    std::vector<std::string> lines;
    std::size_t pos = 0;
    while (lines.size() < 16) {
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

        auto kind_pos = json.rfind("\"kind\"", key_pos);
        std::string kind = "?";
        if (kind_pos != std::string::npos) {
            auto s = json.find('"', kind_pos + 6);
            auto e = s == std::string::npos ? std::string::npos : json.find('"', s + 1);
            if (s != std::string::npos && e != std::string::npos) kind = json.substr(s + 1, e - s - 1);
        }

        lines.push_back(agent + " " + kind + " " + key);
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

std::vector<std::string> build_topology(const std::vector<InterfaceSample>& interfaces, int session_count, const std::vector<std::string>& docker_networks) {
    std::vector<std::string> internet, docker, vpn, loopback, other;
    for (const auto& iface : interfaces) {
        if (iface.kind == "internet") internet.push_back(iface.name);
        else if (iface.kind == "docker") docker.push_back(iface.name);
        else if (iface.kind == "vpn") vpn.push_back(iface.name);
        else if (iface.kind == "loopback") loopback.push_back(iface.name);
        else other.push_back(iface.name);
    }

    auto join3 = [](const std::vector<std::string>& items) {
        std::string out;
        for (std::size_t i = 0; i < items.size() && i < 3; ++i) {
            if (!out.empty()) out += ", ";
            out += items[i];
        }
        return out.empty() ? std::string("-") : out;
    };

    std::string docker_info = join3(docker);
    if (!docker_networks.empty()) docker_info += " | nets=" + std::to_string(docker_networks.size());

    return {
        "Internet  -> " + join3(internet),
        "LAN/Other -> " + join3(other),
        "Docker    -> " + docker_info,
        "VPN       -> " + join3(vpn),
        "Loopback  -> " + join3(loopback),
        "OpenClaw  -> sessions=" + std::to_string(session_count),
    };
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

std::string make_bar(double value, double max_value, int width) {
    if (width <= 0) return "";
    if (max_value <= 0.0) return std::string(width, ' ');
    int filled = static_cast<int>((value / max_value) * width);
    filled = std::clamp(filled, 0, width);
    return std::string(filled, '#') + std::string(width - filled, '-');
}

void box(int y, int x, int h, int w, const std::string& title, int color_pair) {
    if (h < 3 || w < 4) return;
    attron(COLOR_PAIR(color_pair));
    mvprintw(y, x, "┌");
    for (int i = 1; i < w - 1; ++i) mvprintw(y, x + i, "─");
    mvprintw(y, x + w - 1, "┐");
    for (int row = y + 1; row < y + h - 1; ++row) {
        mvprintw(row, x, "│");
        mvprintw(row, x + w - 1, "│");
    }
    mvprintw(y + h - 1, x, "└");
    for (int i = 1; i < w - 1; ++i) mvprintw(y + h - 1, x + i, "─");
    mvprintw(y + h - 1, x + w - 1, "┘");
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
    int tick = 0;
    CachedText ss_cache, openclaw_cache, docker_cache;

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
        snapshot.topology = build_topology(snapshot.interfaces, snapshot.openclaw_session_count, snapshot.docker_networks);

        double max_rate = 0.0;
        for (const auto& iface : snapshot.interfaces) {
            max_rate = std::max(max_rate, iface.rx_rate + iface.tx_rate);
        }

        erase();
        attron(COLOR_PAIR(1) | A_BOLD);
        mvprintw(0, 2, "claw-net-monitor C++ v3");
        attroff(COLOR_PAIR(1) | A_BOLD);
        mvprintw(0, COLS - 18, "auto-build mode");

        int left_w = COLS / 2;
        box(2, 1, 12, left_w - 2, "INTERFACES", 1);
        box(14, 1, LINES - 15, left_w - 2, "OPENCLAW", 4);
        box(2, left_w, 9, COLS - left_w - 1, "TOPOLOGY", 2);
        box(11, left_w, 8, COLS - left_w - 1, "CONNECTIONS", 3);
        box(19, left_w, LINES - 20, COLS - left_w - 1, "DOCKER", 5);

        int row = 3;
        for (std::size_t i = 0; i < snapshot.interfaces.size() && row < 12; ++i) {
            const auto& iface = snapshot.interfaces[i];
            std::string line = " " + iface.name + "  RX " + fmt_rate(iface.rx_rate) +
                               "  TX " + fmt_rate(iface.tx_rate) +
                               "  [" + make_bar(iface.rx_rate + iface.tx_rate, max_rate, 16) + "]";
            mvprintw(row++, 2, "%s", line.c_str());
        }

        row = 3;
        for (std::size_t i = 0; i < snapshot.topology.size() && row < 10; ++i) {
            std::string prefix = ((tick + static_cast<int>(i)) % 3 == 0) ? ">" : ((tick + static_cast<int>(i)) % 3 == 1 ? ">>" : "-");
            mvprintw(row++, left_w + 2, "%s %s", prefix.c_str(), snapshot.topology[i].c_str());
        }

        row = 12;
        for (std::size_t i = 0; i < snapshot.conn_states.size() && row < 17; ++i) {
            mvprintw(row++, left_w + 2, "%s: %d", snapshot.conn_states[i].first.c_str(), snapshot.conn_states[i].second);
        }

        mvprintw(15, 2, " total sessions: %d", snapshot.openclaw_session_count);
        row = 16;
        for (std::size_t i = 0; i < snapshot.openclaw_sessions.size() && row < LINES - 1; ++i) {
            mvprintw(row++, 2, " %s", snapshot.openclaw_sessions[i].c_str());
        }

        row = 20;
        if (snapshot.docker_networks.empty()) {
            mvprintw(row, left_w + 2, " docker networks: none / no access");
        } else {
            for (std::size_t i = 0; i < snapshot.docker_networks.size() && row < LINES - 1; ++i) {
                mvprintw(row++, left_w + 2, " %s", snapshot.docker_networks[i].c_str());
            }
        }

        refresh();
        tick++;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    endwin();
    return 0;
}
