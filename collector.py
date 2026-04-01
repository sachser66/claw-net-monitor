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


@dataclass
class Snapshot:
    timestamp: float
    interfaces: list[InterfaceSample]
    connections_summary: dict[str, int]
    openclaw: dict[str, Any]
    errors: list[str] = field(default_factory=list)


class Collector:
    def __init__(self, interval: float = 1.0) -> None:
        self.interval = interval
        self._last_totals: dict[str, tuple[float, int, int]] = {}

    async def collect(self) -> Snapshot:
        errors: list[str] = []
        timestamp = time.time()

        interfaces = self._read_proc_net_dev(errors)
        addresses = await self._read_ip_addr(errors)
        for iface in interfaces:
            iface.addresses = addresses.get(iface.name, [])
            self._compute_rates(iface, timestamp)

        connections_summary = await self._read_ss_summary(errors)
        openclaw = await self._read_openclaw(errors)

        return Snapshot(
            timestamp=timestamp,
            interfaces=sorted(interfaces, key=lambda i: (i.rx_rate + i.tx_rate), reverse=True),
            connections_summary=connections_summary,
            openclaw=openclaw,
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

    async def _read_ip_addr(self, errors: list[str]) -> dict[str, list[str]]:
        if shutil.which('ip') is None:
            errors.append('command missing: ip')
            return {}
        data = await self._run_json(['ip', '-j', 'addr'], errors, 'ip -j addr')
        result: dict[str, list[str]] = {}
        if isinstance(data, list):
            for item in data:
                name = item.get('ifname')
                infos = item.get('addr_info') or []
                addrs = [entry.get('local') for entry in infos if entry.get('local')]
                if name:
                    result[name] = addrs
        return result

    async def _read_ss_summary(self, errors: list[str]) -> dict[str, int]:
        if shutil.which('ss') is None:
            errors.append('command missing: ss')
            return {}
        proc = await asyncio.create_subprocess_exec(
            'ss', '-tunap',
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        stdout, stderr = await proc.communicate()
        if proc.returncode != 0:
            msg = stderr.decode('utf-8', errors='replace').strip()
            errors.append(f"ss -tunap: {msg or 'failed'}")
            return {}
        summary: dict[str, int] = {}
        for line in stdout.decode('utf-8', errors='replace').splitlines()[1:]:
            parts = line.split()
            if not parts:
                continue
            state = parts[0]
            summary[state] = summary.get(state, 0) + 1
        return dict(sorted(summary.items(), key=lambda kv: kv[1], reverse=True))

    async def _read_openclaw(self, errors: list[str]) -> dict[str, Any]:
        if shutil.which('openclaw') is None:
            errors.append('command missing: openclaw')
            return {}
        sessions = await self._run_json(['openclaw', 'sessions', '--json'], errors, 'openclaw sessions --json')
        gateway = await self._run_json(['openclaw', 'gateway', 'status', '--json'], errors, 'openclaw gateway status --json')
        session_count = None
        if isinstance(sessions, dict):
            session_count = sessions.get('count')
        return {
            'session_count': session_count,
            'gateway': gateway,
        }

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
