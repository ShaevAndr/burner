# Device Workbench

Исходный код Qt-приложения. Проект собирается в двух редакциях из общей кодовой базы:

- `device-workbench-internal.pro` — внутренняя версия со всеми функциями;
- `device-workbench-external.pro` — внешняя версия для поиска и прошивки устройств.

Во внешней версии не создаются страницы смены даты и номера. Эти действия также
отфильтрованы в репозитории действий и отклоняются исполнителем workflow.

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

Скрипт запускает `qmake`, `mingw32-make`, `windeployqt`, а затем копирует в релиз
актуальные каталоги `config` и `flash`. Обычный `device-workbench.pro` оставлен как
совместимый псевдоним внутренней редакции.

## Тесты

```powershell
qmake app/tests/tests.pro -o build/tests/Makefile
mingw32-make -C build/tests -j4
build/tests/release/device-workbench-tests.exe
```

Ограничения внешней редакции проверяются отдельным быстрым тестом
`app/tests/external-edition.pro`.

Архитектура описана в `docs/architecture.md`.
