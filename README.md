# claw-net-monitor

MVP für Live-Netzwerk-Monitoring im Terminal.

## Status

Aktuell als **stdlib-MVP** umgesetzt, weil auf dem Host weder `pip` noch `ensurepip` verfügbar sind. Dadurch konnten `textual`, `psutil` und `docker` noch nicht installiert werden.

Trotzdem enthält das Projekt bereits:

- asynchronen Collector (`asyncio`)
- Interface-Traffic aus `/proc/net/dev`
- Interface-Adressen via `ip -j addr`
- Connection-Snapshot via `ss -tunap`
- OpenClaw-Snapshot via `openclaw sessions --json` und `openclaw gateway status --json`
- einfache Live-TUI mit ASCII-Balken via `curses`

## Start

```bash
python3 main.py
```

Beenden mit `q` oder `Ctrl+C`.

## Nächste Schritte

Sobald Python-Paketinstallation möglich ist:

1. `textual`-UI statt `curses`
2. `psutil`-Fallbacks/Erweiterungen
3. Docker-Stats via SDK/CLI
4. Topologie-Erkennung und animierte Paket-Flows
