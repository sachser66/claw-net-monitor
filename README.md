# claw-net-monitor

Live-Netzwerk- und OpenClaw-Monitor mit **C++-TUI** plus **Webansicht** auf derselben Snapshot-Datenbasis.

## Start

```bash
./run.sh
```

Standardmäßig startet das Projekt:
- die lokale Terminal-Ansicht
- den eingebauten HTTP-Server für Browser/iPhone

Beim Start zeigt `run.sh` direkt die aufrufbare URL an.

## Modi

### TUI + Web

```bash
./run.sh
```

### Nur Headless + Web

```bash
./run.sh headless
```

oder:

```bash
CLAW_MONITOR_HEADLESS=1 ./run.sh
```

## Standard-URL

```text
http://<HOST-IP>:8080
```

JSON-State:

```text
http://<HOST-IP>:8080/api/state
```

## Port ändern

```bash
CLAW_MONITOR_PORT=8090 ./run.sh
```

## Verhalten von `run.sh`

- baut automatisch neu, wenn C++-Quellen neuer als das Binary sind
- verhindert Doppelstarts per PID-Lock
- startet nur die aktuelle C++-App

## Architektur

- **C++ Runtime** als einzige produktive App
- **gemeinsamer Snapshot-State** für TUI und Web
- OpenClaw-Daten aus:
  - `openclaw sessions --all-agents --json`
  - `openclaw gateway status --json`
  - `openclaw models list --json`
  - `openclaw channels list --json`
  - `~/.openclaw/openclaw.json`
- zusätzliche Session-Metadaten aus lokalen Session-Stores

## Manuell bauen

```bash
cd cpp
cmake -S . -B build
cmake --build build -j
./build/claw-net-monitor
```

## Bedienung

- `q` beendet die TUI
