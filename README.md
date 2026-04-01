# claw-net-monitor

Live-Netzwerk- und OpenClaw-Monitor mit **C++ Terminal-UI** plus **Mobile-Webansicht**.

## Morgen-früh-Start

```bash
./run.sh
```

Dann bekommst du gleichzeitig:
- Terminal-Monitor lokal
- Browser/iPhone-Ansicht über HTTP

Das Script zeigt dir beim Start direkt die aufrufbare URL an.

## Standard-URL

```text
http://<HOST-IP>:8080
```

JSON-State:

```text
http://<HOST-IP>:8080/api/state
```

## Falls du einen anderen Port willst

```bash
CLAW_MONITOR_PORT=8090 ./run.sh
```

## Was aktuell drin ist

- Host-Netztraffic aus `/proc/net/dev`
- Verbindungszustände via `ss`
- OpenClaw Sessions aller Agenten
- OpenClaw Agent-Config aus `~/.openclaw/openclaw.json`
- Gateway-Infos
- Docker-Netze + laufende Container
- gemeinsame Datenbasis für TUI + iPhone

## Kurzbedienung

- Start: `./run.sh`
- Beenden: `q`
