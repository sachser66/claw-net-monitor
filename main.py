from __future__ import annotations

import asyncio
import contextlib
import curses
import time

from collector import Collector, Snapshot

FLOW_CHARS = ['>', '»', '›', '→']


def fmt_rate(value: float) -> str:
    units = ['B/s', 'KB/s', 'MB/s', 'GB/s']
    size = float(value)
    for unit in units:
        if size < 1024.0 or unit == units[-1]:
            return f'{size:6.1f} {unit}'
        size /= 1024.0
    return f'{size:.1f} GB/s'


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


def flow_line(label: str, value: str, tick: int, width: int) -> str:
    width = max(12, width)
    phase = tick % max(4, width - 2)
    chars = ['-'] * width
    chars[phase % width] = FLOW_CHARS[tick % len(FLOW_CHARS)]
    return f'{label:<9} [{"".join(chars)}] {value}'


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


def draw(stdscr: curses.window, snapshot: Snapshot | None, tick: int) -> bool:
    stdscr.erase()
    height, width = stdscr.getmaxyx()
    put(stdscr, 0, 0, 'claw-net-monitor v3  |  q quit')

    if snapshot is None:
        put(stdscr, 2, 0, 'Warte auf ersten Snapshot...')
        stdscr.refresh()
        return True

    ts = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(snapshot.timestamp))
    put(stdscr, 1, 0, f'Snapshot: {ts}')

    left_w = max(38, width // 2 - 1)
    right_x = left_w + 2
    max_rate = max((i.rx_rate + i.tx_rate) for i in snapshot.interfaces) if snapshot.interfaces else 0.0

    put(stdscr, 3, 0, 'INTERFACES')
    row = 4
    bar_width = max(8, min(18, left_w // 5))
    for iface in snapshot.interfaces[: min(8, max(1, height - 10))]:
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

    put(stdscr, row + 1, 0, 'OPENCLAW SESSIONS')
    srow = row + 2
    sessions = (snapshot.openclaw or {}).get('sessions') or []
    put(stdscr, row + 1, 20, f'total={len(sessions)}')
    visible = max(1, height - srow - 2)
    for sess in sessions[:visible]:
        model = str(sess.get('model', '?')).split('/')[-1]
        put(stdscr, srow, 0, f"{sess['agentId']:<8} {sess['kind']:<8} {model:<12} {sess['key']}")
        srow += 1
        if srow >= height - 1:
            break

    put(stdscr, 3, right_x, 'FLOW MAP')
    flow_row = 4
    flow_width = max(12, min(20, width - right_x - 20))
    for idx, item in enumerate(snapshot.topology[:6]):
        put(stdscr, flow_row, right_x, flow_line(item['label'], item['value'], tick + idx * 3, flow_width))
        flow_row += 1

    put(stdscr, flow_row + 1, right_x, 'CONNECTION STATES')
    summary = ', '.join(f'{k}:{v}' for k, v in list(snapshot.connections_summary.items())[:6]) or '-'
    put(stdscr, flow_row + 2, right_x, summary)

    put(stdscr, flow_row + 4, right_x, 'TOP FLOWS')
    crow = flow_row + 5
    for item in snapshot.top_connections[: min(6, max(0, height - crow - 6))]:
        line = f"{item['state']:<11} {item['local']} -> {item['remote']}"
        put(stdscr, crow, right_x, line)
        crow += 1

    put(stdscr, crow + 1, right_x, 'DOCKER')
    docker = snapshot.docker or {}
    networks = docker.get('networks') or []
    if not docker.get('available'):
        put(stdscr, crow + 2, right_x, 'docker CLI nicht gefunden')
    elif not networks:
        put(stdscr, crow + 2, right_x, 'keine Netzwerke / kein Zugriff')
    else:
        drow = crow + 2
        for net in networks[: max(1, height - drow - 2)]:
            put(stdscr, drow, right_x, f"{net['name']:<18} {net['driver']:<8} {net['scope']}")
            drow += 1

    if snapshot.errors:
        put(stdscr, height - 1, 0, 'WARN: ' + ' | '.join(snapshot.errors[:2]))

    stdscr.refresh()
    ch = stdscr.getch()
    return ch not in (ord('q'), ord('Q'))


async def app(stdscr: curses.window) -> None:
    curses.curs_set(0)
    stdscr.nodelay(True)
    stdscr.timeout(150)

    queue: asyncio.Queue[Snapshot] = asyncio.Queue(maxsize=1)
    task = asyncio.create_task(producer(queue))
    snapshot: Snapshot | None = None
    tick = 0
    try:
        running = True
        while running:
            try:
                snapshot = queue.get_nowait()
            except asyncio.QueueEmpty:
                pass
            running = draw(stdscr, snapshot, tick)
            tick += 1
            await asyncio.sleep(0.12)
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
