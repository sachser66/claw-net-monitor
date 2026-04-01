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


def put(stdscr: curses.window, y: int, x: int, text: str) -> None:
    h, w = stdscr.getmaxyx()
    if 0 <= y < h and x < w:
        stdscr.addnstr(y, x, text, max(0, w - x - 1))


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
    put(stdscr, 0, 0, 'claw-net-monitor v2  |  q quit')

    if snapshot is None:
        put(stdscr, 2, 0, 'Warte auf ersten Snapshot...')
        stdscr.refresh()
        return True

    ts = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(snapshot.timestamp))
    put(stdscr, 1, 0, f'Snapshot: {ts}')

    left_w = max(40, width // 2)
    right_x = left_w + 2
    max_rate = max((i.rx_rate + i.tx_rate) for i in snapshot.interfaces) if snapshot.interfaces else 0.0

    put(stdscr, 3, 0, 'INTERFACES')
    row = 4
    bar_width = max(8, min(18, left_w // 5))
    for iface in snapshot.interfaces[: max(1, height - 8)]:
        total_rate = iface.rx_rate + iface.tx_rate
        line = (
            f'{iface.name:<10} '
            f'{iface.state[:4]:<4} '
            f'R {fmt_rate(iface.rx_rate):>9} '
            f'T {fmt_rate(iface.tx_rate):>9} '
            f'{bar(total_rate, max_rate, bar_width)}'
        )
        put(stdscr, row, 0, line)
        row += 1
        if row >= height - 2:
            break

    put(stdscr, 3, right_x, 'TOPOLOGY')
    row = 4
    for line in snapshot.topology[:6]:
        put(stdscr, row, right_x, line)
        row += 1

    conn_row = row + 1
    put(stdscr, conn_row, right_x, 'CONNECTIONS')
    conn_row += 1
    summary = ', '.join(f'{k}:{v}' for k, v in list(snapshot.connections_summary.items())[:6]) or '-'
    put(stdscr, conn_row, right_x, summary)
    conn_row += 2

    put(stdscr, conn_row, right_x, 'TOP FLOWS')
    conn_row += 1
    for item in snapshot.top_connections[: min(6, max(0, height - conn_row - 1))]:
        line = f"{item['state']:<11} {item['local']} -> {item['remote']}"
        put(stdscr, conn_row, right_x, line)
        conn_row += 1

    bottom = max(row + 8, conn_row + 1)
    if bottom < height - 1:
        put(stdscr, bottom, 0, 'OPENCLAW')
        openclaw = snapshot.openclaw or {}
        gateway = openclaw.get('gateway') or {}
        service = gateway.get('service') or {}
        sessions = openclaw.get('sessions') or []
        put(stdscr, bottom + 1, 0, f"sessions={openclaw.get('session_count', '?')} service={service.get('label', '?')} loaded={service.get('loaded', '?')}")
        line_y = bottom + 2
        for sess in sessions[: max(0, height - line_y - 2)]:
            put(stdscr, line_y, 0, f"- {sess['agentId']} {sess['kind']} {sess['model']} {sess['key']}")
            line_y += 1

        if snapshot.errors and line_y < height:
            put(stdscr, line_y, 0, 'WARN: ' + ' | '.join(snapshot.errors[:2]))

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
