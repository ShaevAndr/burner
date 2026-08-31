# Конфигурация обнови-БОЦ

Этот каталог содержит конфигурацию, которую приложение загружает при запуске.
Все три JSON-файла обязательны и работают совместно:

| Файл | Назначение |
|---|---|
| `device-catalog.json` | Поддерживаемые устройства, их возможности, версии и файлы прошивок |
| `actions.json` | Действия, которые можно показать пользователю |
| `workflows.json` | Последовательности низкоуровневых операций для действий |

JSON не поддерживает комментарии. Перед изменением рекомендуется сохранить копию
рабочего файла и менять за один раз только одну логическую часть.

## Откуда загружается конфигурация

В готовом приложении используются файлы из каталога `config`, расположенного рядом
с `.exe`. Исходные файлы находятся в `app/config` и копируются в обе редакции при
запуске `scripts/build-releases.ps1`.

Правильный порядок работы:

1. Изменить файлы в `app/config`.
2. Проверить синтаксис JSON.
3. Пересобрать нужную редакцию приложения.
4. Проверить новую конфигурацию сначала на одном устройстве.

Не следует считать файлы внутри `releases/*/config` исходными: следующая сборка
перезапишет их содержимым `app/config`.

## `device-catalog.json`

Текущая версия схемы — `schemaVersion: 4`. Корневые разделы:

- `devices` — правила распознавания устройств;
- `firmwareCatalogs` — известные версии прошивок, файлы и разрешённые переходы.

### Описание устройства

Основные поля элемента `devices`:

| Поле | Описание |
|---|---|
| `id` | Уникальный постоянный идентификатор модели |
| `protocol` | Протокол связи, сейчас используется `unicorn-ascii` |
| `type`, `version` | Тип основного приложения и информационная версия в шестнадцатеричном виде |
| `bootloaderType`, `bootloaderVersion` | Тип загрузчика и его информационная версия |
| `name` | Название в интерфейсе |
| `descriptionKeywords` | Слова для дополнительного распознавания по описанию |
| `operationParameters` | Номера служебных регистров |
| `capabilities` | Разрешённые действия для этой модели |
| `deviceClass` | C++-реализация устройства, например `DeviceBase` или `BocV12Device` |

Модель определяется только по полю `type` (или по `descriptionKeywords`, если
тип недоступен). Поля `version` и `bootloaderVersion` не участвуют в
распознавании и повторном подключении: прошивка может изменить их значения.
Установленная версия прошивки определяется отдельно по `descriptionRegex`.

Пример минимального описания:

```json
{
  "id": "boc.v6",
  "protocol": "unicorn-ascii",
  "type": "0x0A02",
  "version": "0x0001",
  "bootloaderType": "0x1000",
  "bootloaderVersion": "0x0000",
  "name": "БОЦ-В-6",
  "descriptionKeywords": ["БОЦ-В-6"],
  "capabilities": [
    "identity.read",
    "flash.application.write",
    "device.application.load"
  ],
  "deviceClass": "DeviceBase"
}
```

Для возможностей `device.productionDate.update` и `device.serialNumber.update`
должны быть заданы соответственно `productionDateRegister` и
`serialNumberRegister`. Эти операции дополнительно требуют рассчитанный
`factorySettingsKey`. Если ключ отсутствует, приложение намеренно пропускает запись.

### Каталог прошивок

Каждый элемент `firmwareCatalogs` связан с моделью через `deviceId`.

- `versions` содержит распознаваемые версии основного приложения;
- `artifacts` содержит файлы, не привязанные к версии приложения, например
  загрузчик;
- `transitions` задаёт разрешённые переходы между версиями.

Основные поля версии:

| Поле | Описание |
|---|---|
| `id` | Уникальный машинный идентификатор версии |
| `title` | Название в списке прошивок |
| `version` | Отображаемая версия |
| `descriptionRegex` | Регулярное выражение для определения установленной версии |
| `artifact` | Файл, формат и параметры записи |
| `installation.workflow` | Workflow установки |
| `installation.strategy` | Стратегия прошивки |

Если модель устройства известна, но установленная версия не распознана по
`descriptionRegex`, интерфейс предлагает все доступные для этой модели прошивки.
Для известной версии учитывается граф `transitions`.

Пример артефакта:

```json
{
  "target": "application",
  "relativePath": "flash/boc-v6/application.hex",
  "sha256": "ПОЛНЫЙ_SHA256_В_ВЕРХНЕМ_РЕГИСТРЕ",
  "format": "intelHex",
  "addressBase": "0x08040000",
  "default": true,
  "flashNum": 1
}
```

Путь `relativePath` задаётся относительно каталога приложения: исходный файл
`app/flash/boc-v6/application.hex` при сборке станет
`releases/<edition>/flash/boc-v6/application.hex`.

Записи прошивок не требуется обновлять вручную после каждой замены файла. Скрипт
`scripts/sync-firmware-config.ps1` сканирует `app/flash`, извлекает дату и время
компиляции из HEX/BIN, рассчитывает SHA-256 и синхронизирует `versions`, `default`
и `transitions`. Параметры памяти и способ установки он наследует от существующей
записи соответствующей модели. Подробнее см. `app/flash/README.md`.

`default: true` следует назначать только одному предпочтительному артефакту одного
назначения. Запрещённый или отсутствующий переход не будет предложен интерфейсом.

## `actions.json`

Каждое действие связывает условия доступности, пользовательские параметры и
workflow:

```json
{
  "id": "flash.application.write",
  "title": "Прошить application",
  "selection": "many",
  "dangerLevel": "high",
  "when": {
    "capabilitiesAll": ["flash.application.write"]
  },
  "inputs": [
    {
      "name": "artifact",
      "type": "firmwareArtifact",
      "target": "application",
      "required": true
    }
  ],
  "workflow": "firmware.application.standard"
}
```

- `id` должен быть уникальным;
- `selection` принимает `single` или `many`;
- `capabilitiesAll` должны присутствовать у устройства в `device-catalog.json`;
- `statesAny` при необходимости ограничивает действие состоянием `application`
  или `bootloader`;
- `workflow` должен существовать в `workflows.json`.

Внешняя редакция на уровне скомпилированного кода допускает только действия
прошивки и `device.application.load`. Добавление служебного действия в JSON не
откроет его во внешней версии.

## `workflows.json`

Workflow — упорядоченный массив шагов. Пример:

```json
{
  "id": "device.application.load",
  "steps": [
    {
      "op": "device.loadApplicationNoReply",
      "message": "load main application, response is not expected"
    },
    {
      "op": "device.waitForApplication",
      "timeoutMs": 30000,
      "pollIntervalMs": 500
    },
    {
      "op": "workflow.finish"
    }
  ]
}
```

Часто используемые поля шага:

- `op` — имя операции;
- `message` — сообщение журнала;
- `ms` — задержка;
- `timeoutMs`, `pollIntervalMs`, `settleMs` — тайм-ауты ожидания;
- `retry`, `retryLoadAttempts`, `retryDelayMs` — повторные попытки;
- `skipIfState` — пропуск шага в заданном состоянии;
- `valueFrom` — значение из контекста операции.

Используйте только операции, уже реализованные в C++ или присутствующие в текущем
`workflows.json`. Неизвестная операция завершит workflow ошибкой. Прошивочные
workflow должны сохранять порядок проверки артефакта, записи, verify, загрузки
основного приложения и проверки установленной версии.

## Проверка изменений

Проверить синтаксис всех JSON-файлов в PowerShell:

```powershell
Get-ChildItem .\app\config\*.json | ForEach-Object {
    Get-Content -LiteralPath $_.FullName -Raw -Encoding UTF8 | ConvertFrom-Json | Out-Null
    Write-Host "OK: $($_.Name)"
}
```

После проверки пересобрать, например, внутреннюю редакцию:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-releases.ps1 -Edition internal
```

Если конфигурация некорректна, приложение показывает предупреждение при запуске и
пишет подробности в журнал. Для операций записи сначала используйте одно тестовое
устройство и контролируйте итоговый поиск после завершения workflow.
