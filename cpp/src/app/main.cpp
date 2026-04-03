#include <chrono>
#include <cstdlib>
#include <thread>

#include <ncurses.h>
#include <unistd.h>

#include "../collectors/docker_collector.hpp"
#include "../collectors/network_collector.hpp"
#include "../collectors/openclaw_collector.hpp"
#include "../core/snapshot.hpp"
#include "../core/state_json.hpp"
#include "../core/summary_text.hpp"
#include "../core/session_hierarchy.hpp"
#include "../core/triggers.hpp"
#include "../renderers/terminal_renderer.hpp"
#include "../util/exec.hpp"
#include "../server/http_server.hpp"

using Clock = std::chrono::steady_clock;

struct CachedText {
    std::string text;
    Clock::time_point fetched_at{};
    bool ready = false;
};

int main() {
    int http_port = 8080;
    if (const char* env_port = std::getenv("CLAW_MONITOR_PORT")) {
        int parsed = std::atoi(env_port);
        if (parsed > 0 && parsed < 65536) http_port = parsed;
    }

    bool headless = false;
    if (const char* env_headless = std::getenv("CLAW_MONITOR_HEADLESS")) {
        std::string value = env_headless;
        headless = (value == "1" || value == "true" || value == "yes" || value == "on");
    }
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        headless = true;
    }

    if (!headless) {
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
            init_pair(6, COLOR_GREEN, -1);
            init_pair(7, COLOR_YELLOW, -1);
            init_pair(8, COLOR_CYAN, -1);
            init_pair(9, COLOR_MAGENTA, -1);
            init_pair(10, COLOR_WHITE, -1);
            init_pair(11, COLOR_RED, -1);
        }
    }

    http_server_start(http_port);

    const auto app_started_at = Clock::now();
    auto last = Clock::now();
    auto next_tick = Clock::now();
    CachedText ss_cache, openclaw_cache, gateway_cache, config_cache, docker_net_cache, docker_ps_cache, models_cache, channels_cache, health_cache, status_cache, usage_cache, usage_cost_cache;
    TriggerState trigger_state;
    long long session_update_seq = 0;
    int tick = 0;

    while (true) {
        if (!headless) {
            int ch = getch();
            if (ch == 'q' || ch == 'Q') break;
        }

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
            session_update_seq++;
        }
        if (!gateway_cache.ready || now - gateway_cache.fetched_at > std::chrono::seconds(10)) {
            gateway_cache.text = exec_read("openclaw gateway status --json 2>/dev/null || true");
            gateway_cache.fetched_at = now;
            gateway_cache.ready = true;
        }
        if (!config_cache.ready || now - config_cache.fetched_at > std::chrono::seconds(30)) {
            config_cache.text = read_file_text("/home/tr4/.openclaw/openclaw.json");
            config_cache.fetched_at = now;
            config_cache.ready = true;
        }
        if (!models_cache.ready || now - models_cache.fetched_at > std::chrono::seconds(30)) {
            models_cache.text = exec_read("openclaw models list --json 2>/dev/null || true");
            models_cache.fetched_at = now;
            models_cache.ready = true;
        }
        if (!channels_cache.ready || now - channels_cache.fetched_at > std::chrono::seconds(10)) {
            channels_cache.text = exec_read("openclaw channels list --json 2>/dev/null || true");
            channels_cache.fetched_at = now;
            channels_cache.ready = true;
        }
        if (!health_cache.ready || now - health_cache.fetched_at > std::chrono::seconds(15)) {
            const auto next = exec_read("openclaw health --json 2>/dev/null || true");
            if (!next.empty()) health_cache.text = next;
            health_cache.fetched_at = now;
            health_cache.ready = true;
        }
        if (!status_cache.ready || now - status_cache.fetched_at > std::chrono::seconds(15)) {
            const auto next = exec_read("openclaw status --json 2>/dev/null || true");
            if (!next.empty()) status_cache.text = next;
            status_cache.fetched_at = now;
            status_cache.ready = true;
        }
        if (!usage_cache.ready || now - usage_cache.fetched_at > std::chrono::seconds(30)) {
            const auto next = exec_read("openclaw status --usage --json 2>/dev/null || true");
            if (!next.empty()) usage_cache.text = next;
            usage_cache.fetched_at = now;
            usage_cache.ready = true;
        }
        if (!usage_cost_cache.ready || now - usage_cost_cache.fetched_at > std::chrono::seconds(120)) {
            const auto next = exec_read("openclaw gateway usage-cost --json --days 7 2>/dev/null || true");
            if (!next.empty()) usage_cost_cache.text = next;
            usage_cost_cache.fetched_at = now;
            usage_cost_cache.ready = true;
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
        snapshot.openclaw_session_seq = session_update_seq;
        snapshot.monitor_uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(now - app_started_at).count();
        snapshot.openclaw_session_count = parse_session_count(openclaw_cache.text);
        snapshot.openclaw_session_items = extract_sessions(openclaw_cache.text);
        merge_session_store_metadata(snapshot.openclaw_session_items, openclaw_cache.text);
        merge_status_session_metrics(snapshot.openclaw_session_items, status_cache.text);
        snapshot.openclaw_session_hierarchy = build_session_hierarchy(snapshot.openclaw_session_items);
        snapshot.openclaw_agents = extract_agent_configs(config_cache.text);
        snapshot.openclaw_models = extract_models(models_cache.text);
        snapshot.openclaw_channels = extract_channels(channels_cache.text);
        enrich_agents_with_models(snapshot.openclaw_agents, snapshot.openclaw_models);
        enrich_agents_with_channels(snapshot.openclaw_agents, snapshot.openclaw_channels);
        snapshot.gateway = extract_gateway_info(gateway_cache.text);
        snapshot.openclaw_health = extract_health_summary(health_cache.text);
        snapshot.openclaw_status = extract_status_summary(status_cache.text);
        snapshot.openclaw_usage = extract_usage_summary(usage_cache.text);
        snapshot.openclaw_usage_cost = extract_usage_cost_summary(usage_cost_cache.text);
        if (snapshot.openclaw_agents.empty() && !config_cache.text.empty()) {
            snapshot.openclaw_agents = extract_agent_configs(read_file_text("/home/tr4/.openclaw/openclaw.json"));
        }
        if ((snapshot.gateway.mode.empty() || snapshot.gateway.bind.empty()) && !gateway_cache.text.empty()) {
            snapshot.gateway = extract_gateway_info(gateway_cache.text);
        snapshot.openclaw_health = extract_health_summary(health_cache.text);
        snapshot.openclaw_status = extract_status_summary(status_cache.text);
        snapshot.openclaw_usage = extract_usage_summary(usage_cache.text);
        snapshot.openclaw_usage_cost = extract_usage_cost_summary(usage_cost_cache.text);
        }
        snapshot.openclaw_sockets = parse_openclaw_sockets(ss_cache.text);
        snapshot.openclaw_socket_activity = has_openclaw_local_activity(snapshot.openclaw_sockets);
        snapshot.docker_networks = parse_lines(docker_net_cache.text, 6);
        snapshot.docker_containers = parse_lines(docker_ps_cache.text, 6);
        auto groups = build_group_stats(snapshot.interfaces);
        const auto config_hash = std::hash<std::string>{}(config_cache.text);
        snapshot.trigger_events = detect_trigger_events(trigger_state, snapshot, config_hash);

        http_server_publish_json(snapshot_to_json(snapshot));
        http_server_publish_summary_text(snapshot_to_summary_text(snapshot));
        if (!headless) {
            render_terminal(snapshot, groups, tick);
            refresh();
        }
        tick++;
        next_tick += std::chrono::milliseconds(500);
        std::this_thread::sleep_until(next_tick);
    }

    http_server_stop();
    if (!headless) endwin();
    return 0;
}
