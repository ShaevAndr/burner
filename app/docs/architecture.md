# Архитектура обнови-БОЦ

Целевая переработка модели устройств, операций, workflow и параллельного
исполнения описана в
[`docs/superpowers/plans/2026-09-03-device-workflow-refactoring.md`](../../docs/superpowers/plans/2026-09-03-device-workflow-refactoring.md).
Документ имеет статус черновика для согласования; этот файл пока описывает
фактически реализованную архитектуру.

Стек: C++17, Qt Widgets, Qt Network, Qt SerialPort. Проект собирается через
`qmake`.

## Назначение

Приложение ищет устройства по выбранной линии связи, выводит в таблицу данные
identity-ответа устройства и позволяет запускать две MVP-команды:

- `flash.application.write`
- `flash.bootloader.write`

Файлы прошивок лежат в `flash/`. Описание устройств хранит относительный путь
к каждому файлу, target и SHA-256.

## Использованные идеи из Unicorn

Из соседнего проекта `../unicorn/unicorn` учтены следующие решения:

- устройство представлено через отдельный слой `IDevice`/controller, а UI не
  должен напрямую знать детали протокола;
- поиск поддерживает broadcast и iterative/address sweep сценарии;
- протокол из `plugin/modbus` в этом приложении называется `Unicorn ASCII`;
- IP discovery `Unicorn ASCII` использует запрос `FINE` на `239.1.2.1:5001`;
- второй протокол в UI называется `Modbus RTU`;
- ответ IP discovery содержит `magic`, `serverPort`, `modbusAddress`, `type`,
  `version`, `description`, `serialNumber`;
- flash-операции лучше держать отдельным workflow, аналогично `FlashAutomate`;
- модель устройства определяется по `type`; `version` считается информационной
  ревизией и не участвует в распознавании или повторном подключении;
- установленная версия прошивки определяется отдельно по описанию устройства.

## Основные слои

`MainWindow`

Qt Widgets UI. Содержит левое меню, панель поиска, таблицу устройств, меню
массовых действий и журнал. UI вызывает только сервисы из `ServiceContainer`.

`ServiceContainer`

Простой DI-контейнер приложения. Создает и предоставляет:

- `CatalogService`
- `ActionRepository`
- `WorkflowRunner`
- `UdpBroadcastDiscovery`
- `Rs485Discovery`

`CatalogService`

Читает встроенный ресурс `:/config/device-catalog.json`. Сопоставляет найденное устройство по
`type`, подставляет имя, capabilities, `deviceClass`, `flashWorkflows`,
`firmwareFiles` и сравнивает `description` с `expectedDescription`.

Каждый элемент `firmwareFiles` содержит:

- `target`: `application`, `bootloader`, `plis` и т.п.;
- `version`;
- `relativePath`: путь относительно каталога `app`;
- `sha256`.

Если описание отличается, строка получает статус `можно обновить`. Если
совпадает, статус `актуально`.

`ActionRepository`

Читает встроенный ресурс `:/config/actions.json`. В MVP содержит только две команды flash. Для
одного устройства и для группы возвращает действия, которые разрешены по
capabilities.

`UdpBroadcastDiscovery`

Реальный discovery для IP-сети по протоколу `Unicorn ASCII`. Отправляет `FINE` на multicast
`239.1.2.1:5001`, парсит ответы в формате AtomDevices/Unicorn и публикует
`DeviceIdentity`.

`Rs485Discovery`

Стратегия уже присутствует в DI и UI, но аппаратная транзакция в MVP не
включена. Сейчас она логирует выбранный порт, протокол и диапазон адресов.
Следующий шаг: добавить бинарный packet codec для команды `0xFF` и transport
на `QSerialPort`.

`DeviceBase`

Базовый класс устройства хранит identity и дает только атомарный доступ к
транспорту:

- `reset`
- `loadApplication`
- `disableLoadApplication`
- `writeProductionDate`
- `writeSerialNumber`
- `writeInt`
- `readInt`

Конкретный класс `BocV12Device` наследуется от него и переопределяет
поддерживаемые атомарные задачи БОЦ-В-12.

Код устройств разнесен по файлам: базовый контракт находится в
`src/base_device.h/.cpp`, фабрика в `src/device.h/.cpp`, конкретные устройства
в `src/devices/*_device.h/.cpp`.

`WorkflowRunner`

Исполняет action для одного или нескольких устройств. Порядок шагов не зашит в
runner: workflow описаны во встроенном `:/config/workflows.json` как список атомарных
операций `op`. `WorkflowRepository` из `src/workflow_definition.h/.cpp`
загружает этот JSON, runner создает `WorkflowExecution` и вызывает
`next(device)`, а уже `next` интерпретирует текущий шаг и вызывает операцию
устройства по ключу. Конкретный класс устройства выбирается раньше, на этапе
фабрики.

Кнопка `Обновить сценарии` перечитывает `config/workflows.json` во время работы
приложения. Если новый JSON невалиден, активные сценарии не заменяются.

Для flash workflow runner безопасно логирует шаги, находит firmware artifact по
`target`, считает SHA-256 файла и сравнивает его с каталогом. Запись flash в
железо пока не отправляется. Это сделано намеренно: следующий этап должен
подключить реальную transport/protocol реализацию и preflight перед записью.

## Поток данных

1. Пользователь выбирает `UDP broadcast` или `RS-485`.
2. UI собирает `DiscoverySettings`.
3. Выбранная discovery strategy возвращает `DeviceIdentity`.
4. `CatalogService` обогащает устройство данными из JSON.
5. `ActionRepository` вычисляет доступные команды.
6. `MainWindow` рисует строку таблицы и кнопки `A`/`B`.
7. При обнаружении устройства `MainWindow` создает конкретный `DeviceBase`
   класс через `DeviceFactory`.
8. При запуске команды `WorkflowRunner` находит workflow из
   `config/workflows.json` и исполняет его шаги через операции `DeviceBase`.
9. `WorkflowRunner` проверяет файл прошивки и его SHA-256.

## Сборка

```bash
cd app
qmake device-workbench.pro -o Makefile
make -j2
./device-workbench
```

Для headless-проверки инициализации:

```bash
QT_QPA_PLATFORM=offscreen timeout 3s ./device-workbench
```

## Следующие инженерные шаги

1. Реализовать `QSerialPort` transport для RS-485.
2. Реализовать binary packet codec AtomDevices: start/end byte, byte stuffing,
   CRC8 header, CRC16 command.
3. Подключить команду `0xFF` для RS-485 discovery.
4. Реализовать real flash write page через протокольные команды и preflight.
5. Добавить выбор firmware artifact в UI и проверку signature.
