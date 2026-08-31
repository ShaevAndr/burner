# Установщики Windows

Установщики создаются с помощью Inno Setup 6 из готовых переносимых релизов.
Каждый установщик рекурсивно включает всё содержимое соответствующего каталога
`releases`, в том числе `config`, `flash`, Qt-библиотеки и README.

- `internal.iss` — внутренняя редакция;
- `external.iss` — внешняя редакция;
- `common.iss` — общие параметры установки.

Установка Inno Setup 6:

```powershell
winget install --id JRSoftware.InnoSetup --exact --scope user
```

Сборка выполняется из корня проекта:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-installers.ps1 -Version 1.0.0
```

Готовые Setup-файлы создаются в корневом каталоге `installers`. Обычные переносимые
сборки остаются в `releases/internal` и `releases/external`, а их ZIP-архивы создаются
в корне `releases`. Перед компиляцией сценарий проверяет наличие `.exe`, всех
JSON-файлов и хотя бы одного файла прошивки.
