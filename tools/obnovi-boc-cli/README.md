# обнови-БОЦ — менеджер сборки и прошивок

Windows-приложение управляет настройками прошивок, сборкой основных приложений
и созданием установщиков. Готовый файл создаётся здесь:

```text
releases/tools/obnovi-boc-cli.exe
```

## Сборка CLI

Из корня проекта:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-cli.ps1
```

Для чистой пересборки добавьте `-Clean`. Полученный EXE собран статически и не
требует Qt DLL, но для выполнения команд ему нужны каталог проекта и PowerShell.
Если программа находится внутри проекта, корень определяется автоматически.
Скрипт также создаёт отдельный архив `releases/obnovi-BOC-CLI-1.0.0.zip`.

## Графический интерфейс

Запустите `releases/tools/obnovi-boc-cli.exe` двойным щелчком. В окне можно:

- выбрать папку проекта и редакцию приложения;
- обновить `device-catalog.json` по файлам из `app/flash`;
- проверить прошивки без изменения каталога;
- собрать приложения и установщики;
- открыть каталог прошивок или его JSON-настройки.

Ход и результат операции отображаются в нижней части окна. Во время длительной
операции окно остаётся доступным, а закрытие блокируется до её завершения.

## Команды CLI

Командная строка сохранена для автоматизации. Обновить настройки прошивок:

```powershell
.\releases\tools\obnovi-boc-cli.exe sync
```

Проверить все прошивки и актуальность каталога без его изменения:

```powershell
.\releases\tools\obnovi-boc-cli.exe check
```

Собрать обе редакции приложения:

```powershell
.\releases\tools\obnovi-boc-cli.exe build
```

Собрать одну редакцию с очисткой промежуточных файлов:

```powershell
.\releases\tools\obnovi-boc-cli.exe build --edition internal --clean
```

Собрать установщики версии 1.0.0 из готовых релизов:

```powershell
.\releases\tools\obnovi-boc-cli.exe installer --version 1.0.0
```

Сначала пересобрать приложения, затем установщики:

```powershell
.\releases\tools\obnovi-boc-cli.exe installer --edition all --version 1.0.0 --build
```

Проверить, какой PowerShell-вызов будет выполнен, не запуская его:

```powershell
.\releases\tools\obnovi-boc-cli.exe installer --version 1.0.0 --dry-run
```

Если EXE скопирован за пределы проекта, укажите корень явно:

```powershell
obnovi-boc-cli.exe check --root C:\path\to\burner
```

Команда возвращает код `0` при успехе, `1` при ошибке выполняемой операции и `2`
при неправильной командной строке или невозможности найти проект.
