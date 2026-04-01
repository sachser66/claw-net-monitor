from __future__ import annotations

import asyncio
import contextlib
import curses
import time

from collector import Collector, Snapshot


def fmt_rate(value: float) -> str:
    units = ['B/s', 'KB/s', 'MB/s', 'GB/s']
    size = float(value)
    for unit in units:
        if size < 1024.0 or unit == units[-1]:
            return f'{size:6.1f} {unit}'
        size /= 1024.0
    return f'{size:.1f} GB/s'


def fmt_bytes(value: int) -> str:
    units = ['B', 'KB', 'MB', 'GB', 'TB']
    size = float(value)
    for unit in units:
        if size < 1024.0 or unit == units[-1]:
            return f'{size:,.1f} {unit}'
        size /= 1024.0
    return f'{size:.1f} TB'


def bar(value: float, max_value: float, width: int) -> str:
    if width <= 0:
        return ''
    if max_value <= 0:
        return ' ' * width
    filled = max(0, min(width, round((value / max_value) * width)))
    return '█' * filled + '░' * (width - filled)


async def producer(queue: asyncio.Queue[Snapshot]) -> None:
    collector = Collector(interval=1.0)
    while True:
        snapshot = await collector.collect()
        while not queue.empty():
            try:
                queue.get_nowait()
            except asyncio.QueueEmpty:
                break
        await queue.put(snapshot)
        await asyncio.sleep(collector.interval)


def draw(stdscr: curses.window, snapshot: Snapshot | None) -> bool:
    stdscr.erase()
    height, width = stdscr.getmaxyx()
    stdscr.addnstr(0, 0, 'claw-net-monitor MVP  |  q quit', width - 1)

    if snapshot is None:
        stdscr.addnstr(2, 0, 'Warte auf ersten Snapshot...', width - 1)
        stdscr.refresh()
        return True

    ts = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(snapshot.timestamp))
    stdscr.addnstr(1, 0, f'Snapshot: {ts}', width - 1)

    max_rate = max((i.rx_rate + i.tx_rate) for i in snapshot.interfaces) if snapshot.interfaces else 0.0
    row = 3
    bar_width = max(10, min(28, width // 4))

    stdscr.addnstr(row, 0, 'Interfaces', width - 1)
    row += 1
    for iface in snapshot.interfaces[: max(1, height - 12)]:
        total_rate = iface.rx_rate + iface.tx_rate
        addr = ', '.join(iface.addresses[:2]) if iface.addresses else '-'
        line = (
            f'{iface.name:<12} '
            f'RX {fmt_rate(iface.rx_rate):>10}  '
            f'TX {fmt_rate(iface.tx_rate):>10}  '
            f'[{bar(total_rate, max_rate, bar_width)}]  '
            f'tot {fmt_bytes(iface.rx_bytes + iface.tx_bytes):>12}  '
            f'addr {addr}'
        )
        stdscr.addnstr(row, 0, line, width - 1)
        row += 1
        if row >= height - 6:
            break

    row += 1
    conn = ', '.join(f'{k}:{v}' for k, v in list(snapshot.connections_summary.items())[:6]) or '-'
    stdscr.addnstr(row, 0, f'Connections: {conn}', width - 1)
    row += 1

    openclaw = snapshot.openclaw or {}
    session_count = openclaw.get('session_count')
    gateway = openclaw.get('gateway') or {}
    service = gateway.get('service') or {}
    line = f"OpenClaw: sessions={session_count if session_count is not None else '?'} service={service.get('label', '?')} loaded={service.get('loaded', '?')}"
    stdscr.addnstr(row, 0, line, width - 1)
    row += 2

    if snapshot.errors:
        stdscr.addnstr(row, 0, 'Hinweise/Fehler:', width - 1)
        row += 1
        for err in snapshot.errors[: max(1, height - row - 1)]:
            stdscr.addnstr(row, 0, f'- {err}', width - 1)
            row += 1
            if row >= height:
                break

    stdscr.refresh()
    ch = stdscr.getch()
    return ch not in (ord('q'), ord('Q'))


async def app(stdscr: curses.window) -> None:
    curses.curs_set(0)
    stdscr.nodelay(True)
    stdscr.timeout(200)

    queue: asyncio.Queue[Snapshot] = asyncio.Queue(maxsize=1)
    task = asyncio.create_task(producer(queue))
    snapshot: Snapshot | None = None
    try:
        running = True
        while running:
            try:
                snapshot = queue.get_nowait()
            except asyncio.QueueEmpty:
                pass
            running = draw(stdscr, snapshot)
            await asyncio.sleep(0.1)
    finally:
        task.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await task


def main() -> None:
    def runner(stdscr: curses.window) -> None:
        asyncio.run(app(stdscr))
    curses.wrapper(runner)


if __name__ == '__main__':
    main()
