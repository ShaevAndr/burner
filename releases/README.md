# Releases

- `internal/device-workbench-internal.exe` — полная внутренняя редакция;
- `external/device-workbench-external.exe` — внешняя редакция только для прошивки.

Оба каталога являются самостоятельными переносимыми сборками: рядом с `.exe`
находятся Qt-библиотеки, `config` и `flash`. Журналы выполнения создаются в
`%LOCALAPPDATA%\Burner\DeviceWorkbenchInternal\logs` или
`%LOCALAPPDATA%\Burner\DeviceWorkbenchExternal\logs` и не хранятся в Git.

При сборке установщиков содержимое этих каталогов дополнительно упаковывается в
ZIP-файлы с пометкой `Переносимая` для передачи без установки.

Пересборка выполняется из корня проекта:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-releases.ps1 -Clean
```

Сборка использует текущие файлы из `app/config` и не изменяет их. Если нужно
сначала обновить каталог по файлам прошивок, запустите отдельно:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\sync-firmware-config.ps1
```

После этого конфигурацию можно проверить или отредактировать вручную и только
затем запускать сборку.

Параметр `-Clean` очищает только временный каталог компиляции и не затрагивает
пользовательские журналы.
