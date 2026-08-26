# Releases

- `internal/device-workbench-internal.exe` — полная внутренняя редакция;
- `external/device-workbench-external.exe` — внешняя редакция только для прошивки.

Оба каталога являются самостоятельными переносимыми сборками: рядом с `.exe`
находятся Qt-библиотеки, `config` и `flash`. Журналы выполнения создаются в подпапке
`logs` соответствующей редакции и не хранятся в Git.

Пересборка выполняется из корня проекта:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-releases.ps1 -Clean
```

Параметр `-Clean` очищает только временный каталог компиляции. Существующие журналы
в каталоге релиза сохраняются.
