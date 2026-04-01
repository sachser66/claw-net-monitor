#include "network_collector.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

static std::map<std::string, std::pair<unsigned long long, unsigned long long>> g_prev;

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
