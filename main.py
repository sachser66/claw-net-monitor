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


def put(stdscr: curses.window, y: int, x: int, text: str, attr: int = 0) -> None:
    h, w = stdscr.getmaxyx()
    if 0 <= y < h and x < w:
        stdscr.addnstr(y, x, text, max(0, w - x - 1), attr)


def box(stdscr: curses.window, y: int, x: int, h: int, w: int, title: str, color: int) -> None:
    if h < 3 or w < 4:
        return
    put(stdscr, y, x, '┌' + '─' * (w - 2) + '┐', color)
    for row in range(y + 1, y + h - 1):
        put(stdscr, row, x, '│', color)
        put(stdscr, row, x + w - 1, '│', color)
    put(stdscr, y + h - 1, x, '└' + '─' * (w - 2) + '┘', color)
    put(stdscr, y, x + 2, f' {title} ', color | curses.A_BOLD)


def flow_line(label: str, value: str, tick: int, width: int) -> str:
    width = max(12, width)
    phase = tick % max(4, width - 2)
    chars = ['-'] * width
    chars[phase % width] = FLOW_CHARS[tick % len(FLOW_CHARS)]
    return f'{label:<9} [{"".join(chars)}] {value}'


def init_colors() -> None:
    if not curses.has_colors():
        return
    curses.start_color()
    curses.use_default_colors()
    curses.init_pair(1, curses.COLOR_CYAN, -1)
    curses.init_pair(2, curses.COLOR_GREEN, -1)
    curses.init_pair(3, curses.COLOR_YELLOW, -1)
    curses.init_pair(4, curses.COLOR_MAGENTA, -1)
    curses.init_pair(5, curses.COLOR_RED, -1)
    curses.init_pair(6, curses.COLOR_BLUE, -1)
    curses.init_pair(7, curses.COLOR_WHITE, -1)


def c(idx: int) -> int:
    return curses.color_pair(idx) if curses.has_colors() else 0


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
    title_attr = c(1) | curses.A_BOLD
    put(stdscr, 0, 2, 'claw-net-monitor v4', title_attr)
    put(stdscr, 0, max(24, width - 22), 'q quit', c(7))

    if snapshot is None:
        put(stdscr, 2, 2, 'Warte auf ersten Snapshot...', c(3))
        stdscr.refresh()
        return True

    ts = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(snapshot.timestamp))
    put(stdscr, 1, 2, f'Snapshot: {ts}', c(7))

    top_y = 3
    inner_h = height - top_y - 1
    left_w = max(42, width // 2 - 1)
    right_w = max(30, width - left_w - 4)

    iface_h = min(14, max(8, inner_h // 2))
    sess_h = max(7, inner_h - iface_h - 1)

    box(stdscr, top_y, 1, iface_h, left_w, 'INTERFACES', c(1))
    box(stdscr, top_y + iface_h, 1, sess_h, left_w, 'OPENCLAW SESSIONS', c(4))
    box(stdscr, top_y, left_w + 2, 10, right_w, 'FLOW MAP', c(2))
    box(stdscr, top_y + 10, left_w + 2, 8, right_w, 'TOP FLOWS', c(3))
    box(stdscr, top_y + 18, left_w + 2, max(6, inner_h - 18), right_w, 'DOCKER / STATES', c(6))

    max_rate = max((i.rx_rate + i.tx_rate) for i in snapshot.interfaces) if snapshot.interfaces else 0.0
    row = top_y + 1
    bar_width = max(8, min(16, left_w // 6))
    for iface in snapshot.interfaces[: max(1, iface_h - 2)]:
        total_rate = iface.rx_rate + iface.tx_rate
        attr = c(2) if total_rate > 0 else c(7)
        if total_rate > 1024 * 1024:
            attr = c(5) | curses.A_BOLD
        line = (
            f' {iface.name:<10} {iface.state[:4]:<4} '
            f'R {fmt_rate(iface.rx_rate):>9} '
            f'T {fmt_rate(iface.tx_rate):>9} '
            f'{bar(total_rate, max_rate, bar_width)}'
        )
        put(stdscr, row, 2, line, attr)
        row += 1

    sessions = (snapshot.openclaw or {}).get('sessions') or []
    put(stdscr, top_y + iface_h + 1, 3, f'total={len(sessions)}', c(4) | curses.A_BOLD)
    srow = top_y + iface_h + 2
    for sess in sessions[: max(1, sess_h - 3)]:
        model = str(sess.get('model', '?')).split('/')[-1]
        put(stdscr, srow, 2, f" {sess['agentId']:<8} {sess['kind']:<8} {model:<12} {sess['key']}", c(7))
        srow += 1

    flow_row = top_y + 1
    flow_width = max(12, min(18, right_w - 18))
    for idx, item in enumerate(snapshot.topology[:6]):
        attr = c(2)
        if item['label'] == 'OpenClaw':
            attr = c(4) | curses.A_BOLD
        elif item['label'] == 'Docker':
            attr = c(6)
        put(stdscr, flow_row, left_w + 4, flow_line(item['label'], item['value'], tick + idx * 3, flow_width), attr)
        flow_row += 1

    put(stdscr, top_y + 11, left_w + 4, 'states: ' + (', '.join(f'{k}:{v}' for k, v in list(snapshot.connections_summary.items())[:6]) or '-'), c(3))
    crow = top_y + 12
    for item in snapshot.top_connections[:5]:
        line = f" {item['state']:<11} {item['local']} -> {item['remote']}"
        put(stdscr, crow, left_w + 4, line, c(7))
        crow += 1

    drow = top_y + 19
    docker = snapshot.docker or {}
    networks = docker.get('networks') or []
    if not docker.get('available'):
        put(stdscr, drow, left_w + 4, 'docker CLI nicht gefunden', c(5))
        drow += 1
    elif not networks:
        put(stdscr, drow, left_w + 4, 'keine Netzwerke / kein Zugriff', c(3))
        drow += 1
    else:
        for net in networks[:3]:
            put(stdscr, drow, left_w + 4, f" {net['name']:<18} {net['driver']:<8} {net['scope']}", c(6))
            drow += 1

    put(stdscr, drow + 1, left_w + 4, 'connection states', c(1) | curses.A_BOLD)
    drow += 2
    for key, value in list(snapshot.connections_summary.items())[:6]:
        put(stdscr, drow, left_w + 4, f' {key:<12} {value}', c(7))
        drow += 1

    if snapshot.errors:
        put(stdscr, height - 1, 2, 'WARN: ' + ' | '.join(snapshot.errors[:2]), c(5) | curses.A_BOLD)

    stdscr.refresh()
    ch = stdscr.getch()
    return ch not in (ord('q'), ord('Q'))


async def app(stdscr: curses.window) -> None:
    curses.curs_set(0)
    stdscr.nodelay(True)
    stdscr.timeout(150)
    init_colors()

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
