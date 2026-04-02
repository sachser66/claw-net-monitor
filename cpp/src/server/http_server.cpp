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
std::string g_json = "";
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
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover" />
<title>claw-net-monitor</title>
<style>
:root{
  --bg:#0a0f1f;--panel:#121a30;--panel2:#18233f;--line:#2a3a63;--text:#e8edf7;--muted:#98a5c3;
  --cyan:#67e8f9;--green:#4ade80;--yellow:#facc15;--pink:#f472b6;--blue:#60a5fa;
}
*{box-sizing:border-box} body{margin:0;background:linear-gradient(180deg,#09101d,#0d1324 35%,#11182d);color:var(--text);font:14px/1.4 system-ui,sans-serif}
.wrap{max-width:900px;margin:0 auto;padding:14px 12px 32px}
.h1{font-size:24px;font-weight:800;letter-spacing:.2px}.sub{color:var(--muted);margin-top:4px;margin-bottom:14px}
.grid{display:grid;gap:12px}.hero{display:grid;grid-template-columns:1fr 1fr;gap:12px}.stats{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}
.card{background:linear-gradient(180deg,var(--panel),var(--panel2));border:1px solid var(--line);border-radius:16px;padding:14px;box-shadow:0 10px 30px rgba(0,0,0,.18)}
.k{font-size:12px;color:var(--muted);text-transform:uppercase;letter-spacing:.08em}.v{font-size:24px;font-weight:800;margin-top:4px}.pill{display:inline-block;padding:4px 8px;border-radius:999px;background:#20304f;color:#d9e5ff;font-size:12px;margin:2px 6px 0 0}
.list{display:grid;gap:8px;margin-top:8px}.row{display:flex;justify-content:space-between;gap:10px;align-items:center;padding:10px 12px;border:1px solid #2a375b;border-radius:12px;background:rgba(10,16,31,.35)}
.left{min-width:0}.title{font-weight:700}.meta{font-size:12px;color:var(--muted)}.mono{font-family:ui-monospace,SFMono-Regular,Menlo,monospace}.agentGroup{border:1px solid #30436f;border-radius:14px;padding:12px;background:rgba(12,19,38,.55)}
.tag{font-size:11px;border-radius:999px;padding:3px 8px;background:#243253;color:#cfe0ff}.tag-telegram{background:#173b63;color:#8fd3ff}.tag-tui{background:#234b23;color:#95f29b}.tag-webchat{background:#4b2348;color:#ffb3ef}.tag-discord{background:#2d2d74;color:#c2c8ff}.tag-signal{background:#3b4b58;color:#b8deff}.tag-other{background:#3d3d3d;color:#ddd}.good{color:var(--green)}.warn{color:var(--yellow)}.pink{color:var(--pink)}.cyan{color:var(--cyan)}.blue{color:var(--blue)}
.bar{height:8px;background:#1b2744;border-radius:999px;overflow:hidden;margin-top:6px}.fill{height:100%;background:linear-gradient(90deg,var(--cyan),var(--blue));border-radius:999px}
.small{font-size:12px;color:var(--muted)}
@media (max-width:700px){.hero,.stats{grid-template-columns:1fr}.wrap{padding:10px 10px 24px}.v{font-size:22px}}
</style>
</head>
<body>
<div class="wrap">
  <div class="h1">claw-net-monitor</div>
  <div class="sub">Terminal + iPhone auf derselben Live-Datenbasis</div>
  <div id="app" class="small">loading…</div>
</div>
<script>
function rate(v){
  let n=Number(v||0),u=['B/s','KB/s','MB/s','GB/s'],i=0; while(n>=1024&&i<u.length-1){n/=1024;i++} return `${n.toFixed(1)} ${u[i]}`;
}
function esc(s){return String(s??'').replace(/[&<>]/g,m=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[m]))}
function sessionName(s){ const parts=String(s?.key||'').split(':'); return parts[parts.length-1]||s?.key||'-'; }
function channelClass(ch){ return 'tag-' + ({telegram:'telegram',tui:'tui',webchat:'webchat',discord:'discord',signal:'signal'}[ch]||'other'); }
async function load(){
  const r=await fetch('/api/state');
  const j=await r.json();
  const ifaces=(j.network.interfaces||[]).slice(0,8);
  const agents=j.openclaw.agents||[];
  const sessions=j.openclaw.sessions||[];
  const conn=(j.network.conn_states||[]).slice(0,6);
  const groupedSessions = Object.entries((sessions||[]).reduce((acc,s)=>{(acc[s.agent] ||= []).push(s); return acc;}, {}));
  const maxRate=Math.max(1,...ifaces.map(x=>(x.rx_rate||0)+(x.tx_rate||0)));

  document.getElementById('app').innerHTML = `
    <div class="hero">
      <div class="card">
        <div class="k">OpenClaw</div>
        <div class="stats" style="grid-template-columns:repeat(5,1fr)">
          <div><div class="k">Sessions</div><div class="v">${j.openclaw.session_count}</div></div>
          <div><div class="k">Agents</div><div class="v">${agents.length}</div></div>
          <div><div class="k">Gateway</div><div class="v" style="font-size:16px">${esc(j.openclaw.gateway.mode||'?')}</div></div>
          <div><div class="k">Models</div><div class="v">${(j.openclaw.models||[]).length}</div></div>
          <div><div class="k">Channels</div><div class="v">${(j.openclaw.channels||[]).length}</div></div>
        </div>
        <div style="margin-top:10px">
          <span class="pill">bind ${esc(j.openclaw.gateway.bind||'?')}</span>
          <span class="pill">port ${esc(j.openclaw.gateway.port||'?')}</span>
          <span class="pill">socket ${j.openclaw.socket_activity ? 'active' : 'idle'}</span> <span class="pill">events ${(j.openclaw.trigger_events||[]).slice(0,3).join(', ')||'-'}</span>
        </div>
      </div>
      <div class="card">
        <div class="k">Host</div>
        <div class="stats">
          <div><div class="k">Interfaces</div><div class="v">${j.network.interfaces.length}</div></div>
          <div><div class="k">Conn States</div><div class="v">${conn.length}</div></div>
          <div><div class="k">Docker Nets</div><div class="v">${(j.docker.networks||[]).length}</div></div>
        </div>
        <div class="small" style="margin-top:12px">Auto refresh alle 2s</div>
      </div>
    </div>

    <div class="grid" style="margin-top:12px">
      <div class="card">
        <div class="k">OpenClaw Agents + Sessions</div>
        <div class="list">
          ${agents.map(a=>{
            const own = sessions.filter(s=>s.agent===a.id);
            return `<div class="agentGroup">
              <div class="title">${esc(a.emoji||'')} ${esc(a.id)} <span class="tag">${own.length} sessions</span></div>
              <div class="meta">name: ${esc(a.name||'-')}</div>
              <div class="meta mono">workspace: ${esc(a.workspace||'-')}</div>
              <div class="meta mono">model: ${esc(a.model_primary||'-')} ${a.primary_model_available ? '✅' : '❌'}</div>
              <div class="meta mono">auth: ${esc(((j.openclaw.models||[]).find(m=>m.key===a.model_primary)?.auth_type)||'-')} ${esc(((j.openclaw.models||[]).find(m=>m.key===a.model_primary)?.auth_id)||'')}</div>
              <div class="meta mono">fallbacks: ${esc((a.fallbacks||[]).join(', ')||'-')} (${esc(a.fallback_models_available||0)}/${esc(a.fallback_models_total||0)} ok)</div>
              <div class="meta">accounts: ${esc((a.bound_accounts||[]).join(', ')||'-')}</div>
              <div class="meta">channels: ${esc((a.bound_channels||[]).join(', ')||'-')}</div>
              <div class="list">
                ${own.map(s=>`<div class="row"><div class="left"><div class="title">${esc(sessionName(s))} <span class="tag ${channelClass(s.last_channel||s.provider||'other')}">${esc(s.last_channel||s.provider||'other')}</span> <span class="tag">${esc(s.kind)}</span></div><div class="meta mono">${esc(s.model||'-')} | ${esc(s.model_provider||'-')}</div></div><div class="meta">${esc(s.status||'-')}</div></div>`).join('') || '<div class="small">Keine Sessions für diesen Agenten</div>'}
              </div>
            </div>`;
          }).join('') || '<div class="small">Keine Agent-Daten</div>'}
          ${groupedSessions.filter(([agent])=>!agents.find(a=>a.id===agent)).map(([agent, own])=>`<div class="agentGroup"><div class="title">${esc(agent)} <span class="tag">${own.length} sessions</span></div><div class="meta">kein Config-Eintrag gefunden</div><div class="list">${own.map(s=>`<div class="row"><div class="left"><div class="title">${esc(sessionName(s))} <span class="tag ${channelClass(s.last_channel||s.provider||'other')}">${esc(s.last_channel||s.provider||'other')}</span> <span class="tag">${esc(s.kind)}</span></div><div class="meta mono">${esc(s.model||'-')} | ${esc(s.model_provider||'-')}</div></div><div class="meta">${esc(s.status||'-')}</div></div>`).join('')}</div></div>`).join('')}
        </div>
      </div>

      <div class="card">
        <div class="k">Model Inventory</div>
        <div class="list">
          ${(j.openclaw.models||[]).map(m=>`<div class="row"><div class="left"><div class="title mono">${esc(m.key)}</div><div class="meta">${esc(m.name||'-')} | ${esc(m.provider||'-')} | auth ${esc(m.auth_type||'-')} ${esc(m.auth_id||'')}</div></div><div class="meta">${m.available ? 'available' : 'missing'}</div></div>`).join('') || '<div class="small">Keine Model-Daten</div>'}
        </div>
      </div>

      <div class="card">
        <div class="k">Channels</div>
        <div class="list">
          ${(j.openclaw.channels||[]).map(c=>`<div class="row"><div class="left"><div class="title mono">${esc(c.kind)}</div><div class="meta">${esc(c.account_id||'-')}</div></div><div class="meta">${esc(c.label||'-')}</div></div>`).join('') || '<div class="small">Keine Channel-Daten</div>'}
        </div>
      </div>

      <div class="card">
        <div class="k">Interfaces</div>
        <div class="list">
          ${ifaces.map(n=>{const total=(n.rx_rate||0)+(n.tx_rate||0); const pct=Math.max(2,Math.min(100,(total/maxRate)*100)); return `<div class="row"><div class="left" style="flex:1"><div class="title mono">${esc(n.name)} <span class="meta">${esc(n.group)}</span></div><div class="meta">RX ${rate(n.rx_rate)} · TX ${rate(n.tx_rate)}</div><div class="bar"><div class="fill" style="width:${pct}%"></div></div></div></div>`}).join('') || '<div class="small">Keine Interface-Daten</div>'}
        </div>
      </div>

      <div class="card">
        <div class="k">Connection States</div>
        <div class="list">
          ${conn.map(c=>`<div class="row"><div class="title mono">${esc(c.state)}</div><div class="title">${esc(c.count)}</div></div>`).join('') || '<div class="small">Keine Verbindungsdaten</div>'}
        </div>
      </div>

      <div class="card">
        <div class="k">Docker</div>
        <div class="small">Netzwerke</div>
        <div class="list" style="margin-bottom:10px">${(j.docker.networks||[]).map(x=>`<div class="row"><div class="title mono">${esc(x)}</div></div>`).join('') || '<div class="small">Keine Docker-Netze</div>'}</div>
        <div class="small">Container</div>
        <div class="list">${(j.docker.containers||[]).map(x=>`<div class="row"><div class="title mono">${esc(x)}</div></div>`).join('') || '<div class="small">Keine laufenden Container</div>'}</div>
      </div>
    </div>`;
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
        if (req.find("GET /api/state ") != std::string::npos || req.find("GET /api/state?") != std::string::npos) {
            std::lock_guard<std::mutex> lock(g_mutex);
            body = g_json.empty() ? "{\"ready\":false}" : g_json;
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
