# claw-net-monitor C++

Dies ist die einzige produktive Runtime des Projekts.

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

## Start direkt aus `cpp/`

```bash
./build/claw-net-monitor
```

Für den normalen Betrieb bitte aus dem Projektroot starten:

```bash
./run.sh
```

## Hinweis

Die früheren Python-Prototypen wurden entfernt. TUI und Web hängen jetzt vollständig an derselben C++-Snapshot-Pipeline.
