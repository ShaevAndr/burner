# обнови-БОЦ

Исходный код Qt-приложения. Проект собирается в двух редакциях из общей кодовой базы:

- `device-workbench-internal.pro` — внутренняя версия со всеми функциями;
- `device-workbench-external.pro` — внешняя версия для поиска, пинга и прошивки устройств.

Во внешней версии не создаются страницы смены даты и номера. Эти действия также
отфильтрованы в репозитории действий и отклоняются исполнителем workflow. Выбор
произвольного файла прошивки во внешней версии отключён.

Для БОЦ-В-6 внешняя версия разрешает только обновление с
`BOCv6_ADCVibr_Digital20260721_1228.hex` на
`BOCv6_ADCVibr_Digital20260831_1007.hex`; неизвестная или другая текущая версия
блокирует прошивку. Во внутренней версии неизвестная текущая версия позволяет
выбрать любую встроенную прошивку, а известная — только разрешённый переход из
каталога.

## Структура

- `app/src` — C++-код;
- `app/config` — каталог устройств, действия и workflow;
- `app/flash` — образы прошивок;
- `app/tests` — автоматические тесты;
- `build/internal`, `build/external` — временные файлы компиляции (не хранятся в Git);
- `releases/internal`, `releases/external` — готовые переносимые сборки.

## Сборка обеих редакций

Из корня репозитория:

```powershell
.\scripts\build-releases.ps1 -Clean
```

Только одна редакция:

```powershell
.\scripts\build-releases.ps1 -Edition internal
.\scripts\build-releases.ps1 -Edition external
```

Скрипт синхронизирует каталог прошивок, создаёт `embedded_resources.qrc`, запускает
`qmake`, `mingw32-make` и `windeployqt`. JSON-конфигурация и образы прошивок
компилируются внутрь `.exe`; каталоги `config` и `flash` в релиз не копируются.
Обычный `device-workbench.pro` оставлен как совместимый псевдоним внутренней
редакции.

## Тесты

```powershell
qmake app/tests/tests.pro -o build/tests/Makefile
mingw32-make -C build/tests -j4
build/tests/release/device-workbench-tests.exe
```

Ограничения внешней редакции проверяются отдельным быстрым тестом
`app/tests/external-edition.pro`.

Архитектура описана в `docs/architecture.md`.
