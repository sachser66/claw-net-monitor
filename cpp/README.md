# claw-net-monitor C++

Live-Monitor für **Terminal + iPhone gleichzeitig**.

## Schnellstart

Am einfachsten aus dem Projektroot starten:

```bash
./run.sh
```

Das Script:
- baut bei Bedarf automatisch
- startet die Terminal-Ansicht
- startet zusätzlich die Mobile-Webansicht

## iPhone / Browser

Standard-Port ist `8080`.

Im Browser öffnen:

```bash
http://<DEINE-IP>:8080
```

API-JSON direkt:

```bash
http://<DEINE-IP>:8080/api/state
```

## Port ändern

```bash
CLAW_MONITOR_PORT=8090 ./run.sh
```

## Manuell bauen

```bash
cd cpp
cmake -S . -B build
cmake --build build -j
./build/claw-net-monitor
```

## Bedienung

- `q` beendet das Programm
- Terminal und Mobile-Ansicht laufen parallel
