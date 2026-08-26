# Device Workbench

Device Workbench — Windows-приложение на Qt 5 для поиска, диагностики и прошивки
устройств по UDP и RS-485. Операции выполняются независимо для каждого устройства,
поэтому групповая прошивка обрабатывает устройства параллельно.

## Редакции приложения

Проект собирается из общей кодовой базы в двух редакциях:

- **Internal** — внутренняя версия со всеми функциями, включая смену даты
  производства и номера устройства;
- **External** — внешняя версия только для поиска и прошивки. Смена даты, номера
  и остальные служебные действия недоступны как в интерфейсе, так и на уровне
  исполнителя workflow.

Готовые приложения находятся в каталогах:

```text
releases/internal/device-workbench-internal.exe
releases/external/device-workbench-external.exe
```

## Структура проекта

```text
app/
├── src/                  C++-код приложения
├── config/               каталог устройств, действия и workflow
├── flash/                образы прошивок
├── tests/                автоматические тесты
├── device-workbench-internal.pro
└── device-workbench-external.pro

build/                    временные файлы компиляции
releases/                 готовые переносимые сборки
scripts/                  сценарии сборки и запуска
```

Каталог `build` создаётся автоматически и не хранится в Git. Каждый каталог внутри
`releases` является самостоятельной переносимой сборкой: рядом с `.exe` находятся
необходимые Qt-библиотеки, конфигурация и прошивки.

## Требования для сборки

- Windows 10 или новее;
- Qt 5.12.12 MinGW 64-bit;
- `qmake.exe`;
- `mingw32-make.exe`;
- `windeployqt.exe`;
- PowerShell 5.1 или новее.

Сценарий сначала ищет `qmake.exe` в `PATH`, затем использует стандартный путь:

```text
C:\Qt\Qt5.12.12\5.12.12\mingw73_64\bin\qmake.exe
```

Если Qt установлен в другом месте, добавьте каталог `bin` нужной версии Qt в
переменную `PATH` либо измените резервный путь в `scripts/build-releases.ps1`.

## Сборка

Все команды выполняются из корня репозитория:

```powershell
cd C:\Users\Andrew\Desktop\projects\burner\burner
```

Собрать обе редакции:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-releases.ps1 -Clean
```

Собрать только внутреннюю редакцию:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-releases.ps1 -Edition internal -Clean
```

Собрать только внешнюю редакцию:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-releases.ps1 -Edition external -Clean
```

Перед пересборкой закройте запущенный экземпляр соответствующей редакции: Windows
не позволит заменить используемый `.exe`.

## Запуск

Запустить внутреннюю редакцию:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-release.ps1 -Edition internal
```

Запустить внешнюю редакцию:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-release.ps1 -Edition external
```

Сначала пересобрать выбранную редакцию, затем запустить её:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-release.ps1 -Edition internal -Build
```

Приложение также можно запустить непосредственно из `releases/internal` или
`releases/external`. Рабочим каталогом должен оставаться каталог соответствующего
релиза, чтобы приложение нашло `config` и `flash`.

## Конфигурация и прошивки

- `app/config/device-catalog.json` описывает поддерживаемые устройства и прошивки;
- `app/config/actions.json` описывает доступные действия;
- `app/config/workflows.json` описывает последовательности операций;
- `app/flash` содержит файлы прошивок.

При сборке актуальные `config` и `flash` автоматически копируются в выбранный
релиз. После ручного изменения файлов в `app/config` или `app/flash` необходимо
повторно запустить сборку.

## Журналы

Журналы создаются рядом с запущенным приложением:

```text
releases/<edition>/logs/
```

Каталоги журналов не хранятся в Git и не удаляются при обычной пересборке.

## Тесты

Основной набор тестов находится в `app/tests/tests.pro`. Ограничения внешней
редакции дополнительно проверяются проектом `app/tests/external-edition.pro`.

Архитектурные подробности приведены в `app/docs/architecture.md`.
