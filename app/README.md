# Device Workbench

Qt Widgets MVP для поиска устройств и запуска двух flash-команд.

## Build

```bash
qmake device-workbench.pro -o Makefile
make -j2
```

## Run

```bash
./device-workbench
```

Конфигурация лежит в `config/device-catalog.json` и `config/actions.json`.
Бинарники прошивок лежат в `flash/`, а относительные пути и SHA-256 указаны в
`device-catalog.json`.
Архитектура описана в `docs/architecture.md`.
