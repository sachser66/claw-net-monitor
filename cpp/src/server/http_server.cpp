#include "http_server.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
std::atomic<bool> g_running{false};
std::thread g_thread;
std::mutex g_mutex;
std::string g_json = "{\"status\":\"starting\"}";
int g_server_fd = -1;

std::string make_response(const std::string& body, const std::string& content_type = "application/json") {
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: " + content_type + "\r\n"
           "Access-Control-Allow-Origin: *\r\n"
           "Cache-Control: no-store\r\n"
           "Connection: close\r\n"
           "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

std::string make_html() {
    return R"HTML(<!doctype html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>claw-net-monitor</title>
<style>
body{font-family:system-ui,sans-serif;background:#0b1020;color:#e5e7eb;margin:0;padding:12px}
.card{background:#121933;border:1px solid #26304d;border-radius:12px;padding:12px;margin:10px 0}
.small{color:#9ca3af;font-size:12px}
pre{white-space:pre-wrap;word-break:break-word}
</style>
</head>
<body>
<h2>claw-net-monitor</h2>
<div id="app" class="small">loading…</div>
<script>
async function load(){
  const r=await fetch('/api/state');
  const j=await r.json();
  document.getElementById('app').innerHTML = `
    <div class="card"><b>OpenClaw</b><br>Sessions: ${j.openclaw.session_count}<br>Agents: ${j.openclaw.agents.length}<br>Gateway: ${j.openclaw.gateway.mode} / ${j.openclaw.gateway.bind}:${j.openclaw.gateway.port}</div>
    <div class="card"><b>Agents</b><pre>${j.openclaw.agents.map(a=>`${a.emoji||''} ${a.id} | ${a.model_primary} | acct: ${(a.bound_accounts||[]).join(',')||'-'}`).join('\n')}</pre></div>
    <div class="card"><b>Sessions</b><pre>${j.openclaw.sessions.map(s=>`${s.agent} | ${s.kind} | ${s.status} | ${s.model}`).join('\n')}</pre></div>
    <div class="card"><b>Network</b><pre>${j.network.interfaces.slice(0,8).map(n=>`${n.name} | ${n.group} | RX ${n.rx_rate.toFixed(1)} | TX ${n.tx_rate.toFixed(1)}`).join('\n')}</pre></div>
    <div class="card"><b>Docker</b><pre>${(j.docker.networks||[]).join('\n') || '-'}\n\n${(j.docker.containers||[]).join('\n') || '-'}</pre></div>`;
}
load(); setInterval(load, 2000);
</script>
</body>
</html>)HTML";
}

void server_loop(int port) {
    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) return;
    int opt = 1;
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(g_server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) return;
    if (listen(g_server_fd, 8) < 0) return;

    while (g_running.load()) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client = accept(g_server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client < 0) continue;

        char buffer[2048];
        const ssize_t n = read(client, buffer, sizeof(buffer) - 1);
        if (n <= 0) {
            close(client);
            continue;
        }
        buffer[n] = '\0';
        std::string req(buffer);

        std::string body;
        std::string resp;
        if (req.rfind("GET /api/state", 0) != std::string::npos) {
            std::lock_guard<std::mutex> lock(g_mutex);
            body = g_json;
            resp = make_response(body, "application/json");
        } else {
            body = make_html();
            resp = make_response(body, "text/html; charset=utf-8");
        }
        send(client, resp.data(), resp.size(), 0);
        close(client);
    }

    close(g_server_fd);
    g_server_fd = -1;
}
}

void http_server_start(int port) {
    if (g_running.exchange(true)) return;
    g_thread = std::thread(server_loop, port);
}

void http_server_publish_json(const std::string& json) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_json = json;
}

void http_server_stop() {
    if (!g_running.exchange(false)) return;
    if (g_server_fd >= 0) shutdown(g_server_fd, SHUT_RDWR);
    if (g_thread.joinable()) g_thread.join();
}
