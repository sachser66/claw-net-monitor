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
  --bg:#09101d;
  --bg2:#11182d;
  --panel:#121a30;
  --panel2:#18233f;
  --panel-dim:#0f1629;
  --line:#30436f;
  --line-dim:#243553;
  --text:#e8edf7;
  --muted:#94a3c3;
  --white:#ffffff;
  --cyan:#67e8f9;
  --green:#4ade80;
  --yellow:#facc15;
  --pink:#f472b6;
  --blue:#60a5fa;
  --red:#f87171;
}
*{box-sizing:border-box}
body{margin:0;background:linear-gradient(180deg,var(--bg),#0d1324 35%,var(--bg2));color:var(--text);font:14px/1.4 system-ui,sans-serif}
.wrap{max-width:980px;margin:0 auto;padding:14px 12px 36px}
.h1{font-size:24px;font-weight:800;letter-spacing:.2px}
.sub{color:var(--muted);margin-top:4px;margin-bottom:14px}
.stack{display:grid;gap:12px}
.panel{background:linear-gradient(180deg,var(--panel),var(--panel2));border:1px solid var(--line);border-radius:16px;padding:14px;box-shadow:0 10px 30px rgba(0,0,0,.18)}
.panel.dim{background:linear-gradient(180deg,var(--panel-dim),#131b31);border-color:var(--line-dim);opacity:.96}
.title{font-size:12px;color:var(--muted);text-transform:uppercase;letter-spacing:.08em;margin-bottom:10px}
.summary{display:flex;flex-wrap:wrap;gap:10px 14px;margin-bottom:8px}
.kv{display:flex;flex-wrap:wrap;gap:6px;align-items:baseline}
.key{color:var(--white);font-weight:700}
.value{font-weight:700}
.value-muted{color:var(--muted)}
.sep{color:var(--muted)}
.section{margin-top:10px}
.agent{border:1px solid var(--line);border-radius:14px;padding:12px;background:rgba(8,13,24,.26);margin-top:10px}
.agentHeader{font-weight:800;margin-bottom:8px}
.agentMeta,.agentMeta2{display:flex;flex-wrap:wrap;gap:6px 10px;font-size:13px;margin-bottom:4px}
.sessionLine{padding:7px 10px;border-left:2px solid #35517d;background:rgba(6,10,18,.25);border-radius:10px;margin-top:6px;font-size:13px}
.sessionLine.sub{margin-left:18px;border-left-color:#4a6fb1}
.sessionLine.other{margin-left:12px;border-left-color:#4e596f;opacity:.96}
.label{font-weight:700}
.valueBox{display:inline-block;padding:2px 7px;border-radius:999px;background:#20304f;color:#d9e5ff;font-size:12px;margin-left:4px}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.list{display:grid;gap:8px}
.row{padding:10px 12px;border:1px solid #2a375b;border-radius:12px;background:rgba(10,16,31,.35)}
.mono{font-family:ui-monospace,SFMono-Regular,Menlo,monospace}
.bar{height:8px;background:#1b2744;border-radius:999px;overflow:hidden;margin-top:6px}
.fill{height:100%;background:linear-gradient(90deg,var(--cyan),var(--blue));border-radius:999px}
.c-cyan{color:var(--cyan)} .c-green{color:var(--green)} .c-yellow{color:var(--yellow)} .c-pink{color:var(--pink)} .c-blue{color:var(--blue)} .c-red{color:var(--red)} .c-white{color:var(--white)} .c-muted{color:var(--muted)}
@media (max-width:760px){.grid2{grid-template-columns:1fr}.wrap{padding:10px 10px 24px}}
</style>
</head>
<body>
<div class="wrap">
  <div class="h1">claw-net-monitor</div>
  <div class="sub">OpenClaw: Config + Live-Sessions — darunter Traffic und Paketfluss</div>
  <div id="app" class="c-muted">loading…</div>
</div>
<script>
function rate(v){ let n=Number(v||0),u=['B/s','KB/s','MB/s','GB/s'],i=0; while(n>=1024&&i<u.length-1){n/=1024;i++} return `${n.toFixed(1)} ${u[i]}`; }
function esc(s){ return String(s??'').replace(/[&<>]/g,m=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[m])); }
function clsForValue(v){
  const s=String(v??'').toLowerCase();
  if(!s || s==='-') return 'c-muted';
  if(s.includes('telegram')) return 'c-yellow';
  if(s.includes('tui')) return 'c-green';
  if(s.includes('discord')) return 'c-pink';
  if(s.includes('signal')) return 'c-cyan';
  if(s.includes('webchat') || s.includes('web')) return 'c-blue';
  if(s.includes('openai') || s.includes('gpt')) return 'c-cyan';
  if(s.includes('deepseek')) return 'c-pink';
  if(s.includes('available') || s.includes('ok') || s.includes('active') || s.includes('running') || s.includes('local')) return 'c-green';
  if(s.includes('missing') || s.includes('error')) return 'c-red';
  if(s.includes('busy') || s.includes('warm') || s.includes('session-hot')) return 'c-yellow';
  return 'c-white';
}
function renderKv(key, value){ return `<span class="kv"><span class="key">${esc(key)}</span><span class="value ${clsForValue(value)}">${esc(value ?? '-')}</span></span>`; }
function renderSession(node, kind){
  const s=node.session||{};
  const lineClass = kind==='sub' ? 'sessionLine sub' : (kind==='other' ? 'sessionLine other' : 'sessionLine');
  const prefix = kind==='sub' ? '&rarr; subagent' : (kind==='orchestrator' ? '* orchestrator' : '-');
  return `<div class="${lineClass}">
    <span class="label c-white">${prefix}</span>
    <span class="sep"> | </span>${renderKv('channel', node.channel||'-')}
    <span class="sep"> | </span>${renderKv('session', node.session_name||'-')}
    <span class="sep"> | </span>${renderKv('model', s.model||'-')}
    <span class="sep"> | </span>${renderKv('provider', s.model_provider||'-')}
  </div>`;
}
function sessionCountForAgent(group){
  return (group.orchestrators||[]).length + (group.unmatched_subagents||[]).length + (group.others||[]).length;
}
async function load(){
  const r = await fetch('/api/state');
  const j = await r.json();
  if(!j.openclaw){ document.getElementById('app').innerHTML='<div class="panel">Noch keine Daten</div>'; return; }

  const open = j.openclaw;
  const net = j.network||{};
  const docker = j.docker||{};
  const hierarchy = open.session_hierarchy || [];
  const models = open.models || [];
  const channels = open.channels || [];
  const ifaces = (net.interfaces || []).slice(0,8);
  const conn = (net.conn_states || []).slice(0,6);
  const maxRate = Math.max(1, ...ifaces.map(x => (x.rx_rate||0) + (x.tx_rate||0)));

  document.getElementById('app').innerHTML = `
    <div class="stack">
      <div class="panel">
        <div class="title">OPENCLAW</div>
        <div class="summary">
          ${renderKv('Sessions:', open.session_count ?? '?')}
          <span class="sep">|</span>
          ${renderKv('Agents:', (open.agents||[]).length)}
          <span class="sep">|</span>
          ${renderKv('Gateway:', open.gateway?.mode || '?')}
          <span class="sep">/</span>
          ${renderKv('bind', open.gateway?.bind || '?')}
          <span class="sep">/</span>
          ${renderKv('port', open.gateway?.port || '?')}
        </div>
        <div class="summary">
          ${renderKv('socket', open.socket_activity ? 'active' : 'idle')}
          <span class="sep">|</span>
          ${renderKv('events', (open.trigger_events||[]).slice(0,4).join(', ') || '-')}
          <span class="sep">|</span>
          ${renderKv('health', open.health?.status_text || '-')}
          <span class="sep">|</span>
          ${renderKv('runtime', open.status?.runtime_version || '-')}
          <span class="sep">|</span>
          ${renderKv('queued', open.status?.queued_system_events ?? 0)}
        </div>

        <div class="section c-muted">Health / usage summary:</div>
        <div class="agent">
          <div class="agentMeta">${renderKv('channels ok:', `${open.health?.healthy_channels ?? 0}/${open.health?.configured_channels ?? 0}`)} <span class="sep">|</span> ${renderKv('heartbeat agents:', open.health?.heartbeat_enabled_agents ?? 0)} <span class="sep">|</span> ${renderKv('pressure >=80%:', open.status?.session_pressure_high ?? 0)}</div>
          <div class="agentMeta2">${renderKv('max used:', `${open.status?.max_percent_used ?? 0}%`)} <span class="sep">|</span> ${renderKv('cost 7d:', open.usage_cost?.available ? ('$' + Number(open.usage_cost.total_cost || 0).toFixed(2)) : '-')} <span class="sep">|</span> ${renderKv('today:', open.usage_cost?.available ? ('$' + Number(open.usage_cost.today_cost || 0).toFixed(2)) : '-')}</div>
        </div>

        <div class="section c-muted">Agents from openclaw.json:</div>
        ${(open.agents||[]).map(agent => {
          const auth = (models.find(m => m.key === agent.model_primary) || {});
          const group = hierarchy.find(g => g.agent_id === agent.id) || {orchestrators:[], unmatched_subagents:[], others:[]};
          return `<div class="agent">
            <div class="agentHeader">[<span class="${clsForValue(agent.id)}">${esc(agent.id)}</span>] <span class="valueBox">${sessionCountForAgent(group)} sessions</span></div>
            <div class="agentMeta">${renderKv('name:', agent.name || '-')} <span class="sep">|</span> ${renderKv('busy', group.orchestrators.length ? 'orchestrating' : (sessionCountForAgent(group) ? 'warm' : 'idle'))}</div>
            <div class="agentMeta2">${renderKv('model:', agent.model_primary || '-')} <span class="sep">|</span> ${renderKv('auth:', ((auth.auth_type||'-') + (auth.auth_id ? ' (' + auth.auth_id + ')' : '')))}</div>
            <div class="agentMeta2">${renderKv('model-status:', agent.primary_model_available ? 'primary ok' : 'primary missing')} <span class="sep">|</span> ${renderKv('accounts:', (agent.bound_accounts||[]).join(', ') || '-')}</div>
            <div class="agentMeta2">${renderKv('channels:', (agent.bound_channels||[]).join(', ') || '-')}</div>
            <div class="agentMeta2">${renderKv('workspace:', agent.workspace || '-')}</div>
            <div class="agentMeta2">${renderKv('fallbacks:', (agent.fallbacks||[]).join(', ') || '-')} <span class="sep">|</span> ${renderKv('fallback-status:', `${agent.fallback_models_available||0}/${agent.fallback_models_total||0} ok`)}</div>

            <div class="section c-muted">Session details: update 5s | shared hierarchy from snapshot</div>
            ${(group.orchestrators||[]).map(orchestrator => {
              const subs = (group.unmatched_subagents||[]).filter(sub => sub.session?.spawned_by === orchestrator.session?.key);
              return renderSession(orchestrator, 'orchestrator') + subs.map(sub => renderSession(sub, 'sub')).join('');
            }).join('')}
            ${(() => {
              const unmatched = (group.unmatched_subagents||[]).filter(sub => !(group.orchestrators||[]).some(orchestrator => orchestrator.session?.key === sub.session?.spawned_by));
              return unmatched.length ? `<div class="section c-muted">subagents (unmatched parent):</div>${unmatched.map(sub => renderSession(sub, 'sub')).join('')}` : '';
            })()}
            ${(group.others||[]).length ? `<div class="section c-muted">other sessions:</div>${(group.others||[]).map(other => renderSession(other, 'other')).join('')}` : ''}
          </div>`;
        }).join('') || '<div class="agent">Keine Agent-Daten</div>'}
      </div>

      <div class="panel dim">
        <div class="title">WO IST TRAFFIC?</div>
        <div class="list">
          ${ifaces.map(n => {
            const total = (n.rx_rate||0) + (n.tx_rate||0);
            const pct = Math.max(2, Math.min(100, (total / maxRate) * 100));
            return `<div class="row">
              <div class="mono">${renderKv('iface:', n.name || '-')} <span class="sep">|</span> ${renderKv('group:', n.group || '-')}</div>
              <div>${renderKv('RX', rate(n.rx_rate))} <span class="sep">|</span> ${renderKv('TX', rate(n.tx_rate))}</div>
              <div class="bar"><div class="fill" style="width:${pct}%"></div></div>
            </div>`;
          }).join('') || '<div class="row">Keine Interface-Daten</div>'}
        </div>
      </div>

      <div class="grid2">
        <div class="panel dim">
          <div class="title">PAKETFLUSS</div>
          <div class="list">
            ${conn.map(c => `<div class="row mono">${renderKv('state:', c.state)} <span class="sep">|</span> ${renderKv('count:', c.count)}</div>`).join('') || '<div class="row">Keine Verbindungsdaten</div>'}<div class="section c-muted">Connection states aus ss</div>
          </div>
        </div>

        <div class="panel dim">
          <div class="title">DOCKER / STATES</div>
          <div class="section c-muted">Docker networks</div>
          <div class="list">${(docker.networks||[]).map(x => `<div class="row mono">${esc(x)}</div>`).join('') || '<div class="row">Keine Docker-Netze</div>'}</div>
          <div class="section c-muted">Models</div>
          <div class="list">${models.slice(0,8).map(m => `<div class="row mono">${renderKv('model:', m.key||'-')} <span class="sep">|</span> ${renderKv('auth:', ((m.auth_type||'-') + (m.auth_id ? ' (' + m.auth_id + ')' : '')))} <span class="sep">|</span> ${renderKv('status:', m.available ? 'available' : 'missing')}</div>`).join('') || '<div class="row">Keine Model-Daten</div>'}</div>
          <div class="section c-muted">Channels</div>
          <div class="list">${channels.map(c => `<div class="row mono">${renderKv('kind:', c.kind||'-')} <span class="sep">|</span> ${renderKv('account:', c.account_id||'-')}</div>`).join('') || '<div class="row">Keine Channel-Daten</div>'}</div>
        </div>
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
