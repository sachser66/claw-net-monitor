from __future__ import annotations

import asyncio
import json
import shutil
import time
from dataclasses import dataclass, field
from typing import Any


@dataclass
class InterfaceSample:
    name: str
    rx_bytes: int
    tx_bytes: int
    rx_rate: float = 0.0
    tx_rate: float = 0.0
    addresses: list[str] = field(default_factory=list)
    state: str = "unknown"
    kind: str = "iface"


@dataclass
class Snapshot:
    timestamp: float
    interfaces: list[InterfaceSample]
    connections_summary: dict[str, int]
    top_connections: list[dict[str, str]]
    docker: dict[str, Any]
    openclaw: dict[str, Any]
    topology: list[dict[str, str]]
    errors: list[str] = field(default_factory=list)


class Collector:
    def __init__(self, interval: float = 1.0) -> None:
        self.interval = interval
        self._last_totals: dict[str, tuple[float, int, int]] = {}

    async def collect(self) -> Snapshot:
        errors: list[str] = []
        timestamp = time.time()

        interfaces = self._read_proc_net_dev(errors)
        addr_data = await self._read_ip_addr(errors)
        for iface in interfaces:
            meta = addr_data.get(iface.name, {})
            iface.addresses = meta.get("addresses", [])
            iface.state = meta.get("state", "unknown")
            iface.kind = self._classify_interface(iface.name, iface.addresses)
            self._compute_rates(iface, timestamp)

        connections_summary, top_connections = await self._read_ss_summary(errors)
        docker = await self._read_docker(errors)
        openclaw = await self._read_openclaw(errors)
        topology = self._build_topology(interfaces, docker, openclaw)

        return Snapshot(
            timestamp=timestamp,
            interfaces=sorted(interfaces, key=lambda i: (i.rx_rate + i.tx_rate), reverse=True),
            connections_summary=connections_summary,
            top_connections=top_connections,
            docker=docker,
            openclaw=openclaw,
            topology=topology,
            errors=errors,
        )

    def _read_proc_net_dev(self, errors: list[str]) -> list[InterfaceSample]:
        interfaces: list[InterfaceSample] = []
        try:
            with open('/proc/net/dev', 'r', encoding='utf-8') as f:
                lines = f.readlines()[2:]
            for line in lines:
                if ':' not in line:
                    continue
                left, right = line.split(':', 1)
                name = left.strip()
                fields = right.split()
                if len(fields) < 16:
                    continue
                rx_bytes = int(fields[0])
                tx_bytes = int(fields[8])
                interfaces.append(InterfaceSample(name=name, rx_bytes=rx_bytes, tx_bytes=tx_bytes))
        except Exception as exc:
            errors.append(f'/proc/net/dev: {exc}')
        return interfaces

    def _compute_rates(self, iface: InterfaceSample, timestamp: float) -> None:
        prev = self._last_totals.get(iface.name)
        if prev is not None:
            prev_ts, prev_rx, prev_tx = prev
            dt = max(timestamp - prev_ts, 0.001)
            iface.rx_rate = max(0.0, (iface.rx_bytes - prev_rx) / dt)
            iface.tx_rate = max(0.0, (iface.tx_bytes - prev_tx) / dt)
        self._last_totals[iface.name] = (timestamp, iface.rx_bytes, iface.tx_bytes)

    async def _read_ip_addr(self, errors: list[str]) -> dict[str, dict[str, Any]]:
        if shutil.which('ip') is None:
            errors.append('command missing: ip')
            return {}
        data = await self._run_json(['ip', '-j', 'addr'], errors, 'ip -j addr')
        result: dict[str, dict[str, Any]] = {}
        if isinstance(data, list):
            for item in data:
                name = item.get('ifname')
                infos = item.get('addr_info') or []
                addrs = [entry.get('local') for entry in infos if entry.get('local')]
                if name:
                    result[name] = {
                        'addresses': addrs,
                        'state': item.get('operstate', 'unknown'),
                    }
        return result

    async def _read_ss_summary(self, errors: list[str]) -> tuple[dict[str, int], list[dict[str, str]]]:
        if shutil.which('ss') is None:
            errors.append('command missing: ss')
            return {}, []
        proc = await asyncio.create_subprocess_exec(
            'ss', '-tunap',
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        stdout, stderr = await proc.communicate()
        if proc.returncode != 0:
            msg = stderr.decode('utf-8', errors='replace').strip()
            errors.append(f"ss -tunap: {msg or 'failed'}")
            return {}, []
        summary: dict[str, int] = {}
        top_connections: list[dict[str, str]] = []
        for line in stdout.decode('utf-8', errors='replace').splitlines()[1:]:
            parts = line.split()
            if len(parts) < 5:
                continue
            state = parts[0]
            summary[state] = summary.get(state, 0) + 1
            if len(top_connections) < 10:
                top_connections.append({
                    'state': state,
                    'local': parts[3],
                    'remote': parts[4],
                })
        return dict(sorted(summary.items(), key=lambda kv: kv[1], reverse=True)), top_connections

    async def _read_docker(self, errors: list[str]) -> dict[str, Any]:
        if shutil.which('docker') is None:
            return {'available': False, 'networks': []}
        proc = await asyncio.create_subprocess_exec(
            'docker', 'network', 'ls', '--format', '{{json .}}',
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        stdout, stderr = await proc.communicate()
        if proc.returncode != 0:
            msg = stderr.decode('utf-8', errors='replace').strip()
            errors.append(f"docker network ls: {msg or 'failed'}")
            return {'available': True, 'networks': []}
        networks = []
        for line in stdout.decode('utf-8', errors='replace').splitlines():
            line = line.strip()
            if not line:
                continue
            try:
                item = json.loads(line)
                networks.append({
                    'name': item.get('Name', '?'),
                    'driver': item.get('Driver', '?'),
                    'scope': item.get('Scope', '?'),
                })
            except json.JSONDecodeError:
                continue
        return {'available': True, 'networks': networks[:8]}

    async def _read_openclaw(self, errors: list[str]) -> dict[str, Any]:
        if shutil.which('openclaw') is None:
            errors.append('command missing: openclaw')
            return {}
        sessions = await self._run_json(['openclaw', 'sessions', '--json'], errors, 'openclaw sessions --json')
        gateway = await self._run_json(['openclaw', 'gateway', 'status', '--json'], errors, 'openclaw gateway status --json')

        session_count = None
        session_items: list[dict[str, Any]] = []
        if isinstance(sessions, dict):
            session_count = sessions.get('count')
            raw = sessions.get('sessions') or []
            for item in raw:
                session_items.append({
                    'key': item.get('key', '?'),
                    'kind': item.get('kind', '?'),
                    'agentId': item.get('agentId', '?'),
                    'model': item.get('model', '?'),
                })

        return {
            'session_count': session_count,
            'sessions': session_items,
            'gateway': gateway,
        }

    def _build_topology(self, interfaces: list[InterfaceSample], docker: dict[str, Any], openclaw: dict[str, Any]) -> list[dict[str, str]]:
        internet = []
        lan = []
        vpn = []
        local = []
        for iface in interfaces:
            if iface.name == 'lo':
                local.append(iface.name)
            elif iface.kind == 'vpn':
                vpn.append(iface.name)
            elif iface.kind == 'internet':
                internet.append(iface.name)
            else:
                lan.append(iface.name)

        docker_names = [n['name'] for n in (docker.get('networks') or [])[:3]]
        sessions = openclaw.get('session_count') if openclaw else None
        return [
            {'label': 'Internet', 'value': ', '.join(internet[:3]) if internet else '?'},
            {'label': 'LAN', 'value': ', '.join(lan[:4]) if lan else '?'},
            {'label': 'VPN', 'value': ', '.join(vpn[:3]) if vpn else '-'},
            {'label': 'Docker', 'value': ', '.join(docker_names) if docker_names else '-'},
            {'label': 'Localhost', 'value': ', '.join(local[:2]) if local else 'lo?'},
            {'label': 'OpenClaw', 'value': f'sessions={sessions if sessions is not None else "?"}'},
        ]

    def _classify_interface(self, name: str, addresses: list[str]) -> str:
        lname = name.lower()
        if lname == 'lo':
            return 'loopback'
        if lname.startswith(('wg', 'tun', 'tap', 'tailscale')):
            return 'vpn'
        if lname.startswith(('eth', 'en', 'wlan', 'wl')):
            return 'internet'
        if any(addr.startswith(('10.', '192.168.', '172.16.', '172.17.', '172.18.', '172.19.', '172.20.', '172.21.', '172.22.', '172.23.', '172.24.', '172.25.', '172.26.', '172.27.', '172.28.', '172.29.', '172.30.', '172.31.')) for addr in addresses):
            return 'lan'
        return 'iface'

    async def _run_json(self, cmd: list[str], errors: list[str], label: str) -> Any:
        proc = await asyncio.create_subprocess_exec(
            *cmd,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        stdout, stderr = await proc.communicate()
        if proc.returncode != 0:
            msg = stderr.decode('utf-8', errors='replace').strip()
            errors.append(f"{label}: {msg or 'failed'}")
            return None
        try:
            return json.loads(stdout.decode('utf-8', errors='replace'))
        except json.JSONDecodeError as exc:
            errors.append(f'{label}: invalid json ({exc})')
            return None
