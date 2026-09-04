# обнови-БОЦ

обнови-БОЦ — Windows-приложение на Qt 5 для поиска, диагностики и прошивки
устройств по UDP и RS-485. Операции выполняются независимо для каждого устройства,
поэтому групповая прошивка обрабатывает устройства параллельно.

## Редакции приложения

Проект собирается из общей кодовой базы в двух редакциях:

- **Внутренняя (`internal`)** — версия со всеми функциями, включая смену даты
  производства и номера устройства;
- **Внешняя (`external`)** — версия для поиска, пинга и прошивки. Смена даты, номера
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
`releases` является самостоятельной переносимой сборкой. Рядом с `.exe` находятся
только необходимые Qt-библиотеки: конфигурация и прошивки встраиваются в
исполняемый файл при компиляции.

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

## Консольное приложение сборки

Автономный CLI объединяет проверку прошивок, сборку приложений и создание
установщиков. Сначала соберите сам инструмент:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-cli.ps1
```

CLI выпускается отдельно от основных приложений: готовый EXE находится в
`releases/tools`, а архив для передачи — в `releases/obnovi-BOC-CLI-1.0.0.zip`.

В графическом окне CLI подготовка конфигурации и сборка разделены. Можно сначала
обновить каталог по файлам прошивок, вручную проверить или изменить JSON, а затем
собрать приложение либо установщики. Те же операции доступны из командной строки:

```powershell
.\releases\tools\obnovi-boc-cli.exe config
.\releases\tools\obnovi-boc-cli.exe build
.\releases\tools\obnovi-boc-cli.exe installer
```

Команды `build` и `installer` не синхронизируют и не изменяют JSON: они используют
готовые файлы из `app/config`.

Обе сборочные операции (`build` и `installer`) всегда обрабатывают внутреннюю и
внешнюю редакции. Команда `installer` сначала пересобирает приложения;
`--dry-run` показывает
PowerShell-команду без её выполнения. Полная справка находится в
`tools/obnovi-boc-cli/README.md` и выводится командой `obnovi-boc-cli.exe --help`.

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
`releases/external`. Внешние каталоги `config` и `flash` для запуска не нужны.

## Установщики Windows

Установщики создаются через Inno Setup 6 и включают готовое приложение и Qt DLL.
Конфигурация и все файлы прошивок уже находятся внутри `.exe`.

Если Inno Setup 6 ещё не установлен:

```powershell
winget install --id JRSoftware.InnoSetup --exact --scope user
```

Собрать оба установщика из существующих релизов:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-installers.ps1 -Version 1.0.0
```

Пересобрать приложения, а затем создать установщики:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-installers.ps1 -Version 1.0.0 -BuildApplication
```

Результат создаётся в `installers`:

```text
обнови-БОЦ-Внутренняя-Setup-1.0.0.exe
обнови-БОЦ-Setup-1.0.0.exe
```

Одновременно сохраняются обычные переносимые сборки в каталогах
`releases/internal` и `releases/external`, а также создаются архивы для передачи:

```text
releases/obnovi-BOC-Internal-Portable-1.0.0.zip
releases/obnovi-BOC-External-Portable-1.0.0.zip
```

## Конфигурация и прошивки

- `app/config/device-catalog.json` описывает поддерживаемые устройства и прошивки;
- `app/config/actions.json` описывает доступные действия;
- `app/config/workflows.json` описывает последовательности операций;
- `app/flash` содержит файлы прошивок.

В навигации есть отдельная страница прошивки bootloader. Она доступна для любой
распознанной модели в состоянии основного приложения, если в каталоге этой модели
есть подпапка `bootloader` с прошивкой и текущая версия application разрешена для
этого образа. Bootloader записывается непосредственно из работающего приложения и
затем обязательно проверяется чтением flash. При выборе нескольких устройств
каждая операция выполняется в собственном потоке. Для текущего bootloader
БОЦ-В-12 разрешены application `2026-07-08 12:51:18`, `2026-07-16 09:24:19` и
`2026-08-31 17:24:51`.

Основное приложение БОЦ-В-12 обновляется только до версии
`2026-08-31 17:24:51` и только с версий `2026-07-08 12:51:18` или
`2026-07-16 09:24:19`. Неизвестная и любая другая исходная версия блокируются.

При сборке актуальные `config` и `flash` автоматически добавляются в Qt Resource
System и компилируются внутрь `.exe`. В готовом релизе открытых каталогов `config`
и `flash` нет, а изменить встроенные данные без пересборки приложения нельзя.
После ручного изменения файлов в `app/config` или `app/flash` необходимо повторно
запустить сборку.

Политика БОЦ-В-6 различается по редакциям. Внешняя сборка разрешает только переход
`BOCv6_ADCVibr_Digital20260721_1228.hex` →
`BOCv6_ADCVibr_Digital20260831_1007.hex` и блокирует неизвестную либо другую
текущую версию. Внутренняя сборка при неизвестной версии предлагает все встроенные
прошивки, а при известной использует разрешённые переходы из каталога.

Каталог прошивок обновляется только отдельной командой синхронизации. Сборка его
не изменяет, чтобы перед встраиванием в EXE конфигурацию можно было проверить или
отредактировать вручную:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\sync-firmware-config.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\sync-firmware-config.ps1 -Check
```

## Журналы

Журналы создаются в локальном каталоге данных текущего пользователя:

```text
%LOCALAPPDATA%\Burner\DeviceWorkbenchInternal\logs\
%LOCALAPPDATA%\Burner\DeviceWorkbenchExternal\logs\
```

Такой путь доступен для записи и переносимой версии, и приложения, установленного в
`Program Files`. Журналы не удаляются при обновлении или удалении приложения.

## Тесты

Основной набор тестов находится в `app/tests/tests.pro`. Ограничения внешней
редакции дополнительно проверяются проектом `app/tests/external-edition.pro`.

Архитектурные подробности приведены в `app/docs/architecture.md`.
