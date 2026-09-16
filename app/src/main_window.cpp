#include "main_window.h"
#include "app_edition.h"
#include "firmware_access_policy.h"
#include "execution_journal.h"
#include "workers.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QProgressBar>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardPaths>
#include <QThread>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <utility>

enum DiscoveryColumns
{
    DiscoveryCheck = 0,
    DiscoveryDevice,
    DiscoveryNumber,
    DiscoveryAddress,
    DiscoveryChannel,
    DiscoveryFirmware,
    DiscoveryState,
    DiscoveryActions,
    DiscoveryColumnCount
};

static void configureCombo(QComboBox* combo)
{
    combo->setView(new QListView(combo));
    combo->setMaxVisibleItems(12);
    combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
}

static bool isFlashAction(const QString& actionId)
{
    return actionId.startsWith(QStringLiteral("flash."));
}

static QVector<FirmwareArtifact> artifactsForTarget(const QVector<std::shared_ptr<DeviceBase>>& devices, const QString& target)
{
    QVector<FirmwareArtifact> artifacts;
    if (devices.isEmpty() || !devices.first())
        return artifacts;

    const DeviceIdentity& firstIdentity = devices.first()->identity();
    if (target == QStringLiteral("application") && !devices.first()->firmwareVersions().isEmpty())
    {
        for (const FirmwareVersionSpec& targetFirmware : devices.first()->firmwareVersions())
        {
            if (targetFirmware.artifact.target != target
                || !FirmwareAccessPolicy::isTargetAllowed(*devices.first(), targetFirmware.id))
                continue;

            bool availableForAll = true;
            for (const std::shared_ptr<DeviceBase>& device : devices)
            {
                if (!device)
                {
                    availableForAll = false;
                    break;
                }
                const FirmwareVersionSpec* deviceTarget = device->firmwareVersionById(targetFirmware.id);
                if (!deviceTarget || deviceTarget->artifact.target != target
                    || !FirmwareAccessPolicy::isTargetAllowed(*device, targetFirmware.id))
                {
                    availableForAll = false;
                    break;
                }
            }
            if (availableForAll)
                artifacts.append(targetFirmware.artifact);
        }
        return artifacts;
    }

    for (const FirmwareArtifact& artifact : devices.first()->firmwareArtifacts())
    {
        if (artifact.target == target
            && artifact.isAllowedFromFirmware(firstIdentity.currentFirmwareId))
            artifacts.append(artifact);
    }

    for (int deviceIndex = 1; deviceIndex < devices.size() && !artifacts.isEmpty(); ++deviceIndex)
    {
        if (!devices.at(deviceIndex))
        {
            artifacts.clear();
            break;
        }

        const QVector<FirmwareArtifact>& otherArtifacts = devices.at(deviceIndex)->firmwareArtifacts();
        QVector<FirmwareArtifact> common;
        for (const FirmwareArtifact& candidate : artifacts)
        {
            for (const FirmwareArtifact& other : otherArtifacts)
            {
                const bool sameFirmwareId = !candidate.firmwareId.isEmpty()
                    && candidate.firmwareId == other.firmwareId;
                const bool samePath = !candidate.relativePath.isEmpty()
                    && candidate.relativePath.compare(other.relativePath, Qt::CaseInsensitive) == 0;
                const bool sameHash = !candidate.sha256.isEmpty()
                    && candidate.sha256.compare(other.sha256, Qt::CaseInsensitive) == 0;
                if (other.target == target
                    && other.isAllowedFromFirmware(
                        devices.at(deviceIndex)->identity().currentFirmwareId)
                    && (sameFirmwareId || samePath || sameHash))
                {
                    common.append(candidate);
                    break;
                }
            }
        }
        artifacts = common;
    }
    return artifacts;
}

static QString artifactLabel(const FirmwareArtifact& artifact)
{
    const QString title = !artifact.title.isEmpty() ? artifact.title : QFileInfo(artifact.relativePath).fileName();
    QStringList parts;
    parts.append(title);
    if (!artifact.version.isEmpty())
        parts.append(artifact.version);
    if (artifact.isDefault)
        parts.append(QStringLiteral("по умолчанию"));
    return parts.join(QStringLiteral(" · "));
}

static QVariantMap artifactToMap(const FirmwareArtifact& artifact)
{
    QVariantMap map;
    map.insert(QStringLiteral("firmwareId"), artifact.firmwareId);
    map.insert(QStringLiteral("target"), artifact.target);
    map.insert(QStringLiteral("title"), artifact.title);
    map.insert(QStringLiteral("version"), artifact.version);
    map.insert(QStringLiteral("relativePath"), artifact.relativePath);
    map.insert(QStringLiteral("sha256"), artifact.sha256);
    map.insert(QStringLiteral("format"), artifact.format);
    map.insert(QStringLiteral("addressBase"), artifact.addressBase);
    map.insert(QStringLiteral("default"), artifact.isDefault);
    map.insert(QStringLiteral("flashNum"), artifact.flashNum);
    map.insert(QStringLiteral("offset"), artifact.offset);
    map.insert(QStringLiteral("pageSize"), artifact.pageSize);
    map.insert(QStringLiteral("pagesCount"), artifact.pagesCount);
    map.insert(QStringLiteral("flashStrategy"), artifact.flashStrategy);
    map.insert(QStringLiteral("flashParameters"), artifact.flashParameters);
    map.insert(QStringLiteral("allowedFromFirmwareIds"), artifact.allowedFromFirmwareIds);
    return map;
}

MainWindow::MainWindow(ServiceContainer* services, QWidget* parent) :
    QMainWindow(parent),
    mServices(services)
{
    qRegisterMetaType<DeviceIdentity>("DeviceIdentity");
    buildUi();

    connect(&mServices->udpDiscovery(), &IDiscoveryStrategy::deviceFound, this, &MainWindow::onDeviceFound);
    connect(&mServices->udpDiscovery(), &IDiscoveryStrategy::logMessage, this, &MainWindow::appendLog);
    connect(&mServices->udpDiscovery(), &IDiscoveryStrategy::finished, this, &MainWindow::onDiscoveryFinished);

    connect(&mServices->rs485Discovery(), &IDiscoveryStrategy::deviceFound, this, &MainWindow::onDeviceFound);
    connect(&mServices->rs485Discovery(), &IDiscoveryStrategy::logMessage, this, &MainWindow::appendLog);
    connect(&mServices->rs485Discovery(), &IDiscoveryStrategy::finished, this, &MainWindow::onDiscoveryFinished);

    connect(&mServices->workflow(), &WorkflowRunner::logMessage, this, &MainWindow::appendLog);
    connect(&mServices->workflow(), &WorkflowRunner::transportLogMessage, this, &MainWindow::appendTransportLog);
    connect(&mServices->workflow(), &WorkflowRunner::progressChanged, this, &MainWindow::onWorkflowProgress);

    appendLog(QStringLiteral("Приложение запущено. Файл журнала: %1").arg(logFilePath()));
    ExecutionJournal executionJournal;
    QString recoveryError;
    const QVector<QJsonObject> interrupted = executionJournal.recoverInterrupted(&recoveryError);
    if (!recoveryError.isEmpty())
        appendLog(QStringLiteral("Журнал заданий недоступен: %1").arg(recoveryError));
    for (const QJsonObject& event : interrupted)
    {
        const QString deviceId = event.value(QStringLiteral("uuid")).toString().isEmpty()
            ? event.value(QStringLiteral("endpoint")).toString()
            : event.value(QStringLiteral("uuid")).toString();
        const bool flashMayHaveStarted = event.value(QStringLiteral("flashMayHaveStarted")).toBool();
        const QString completed = event.value(QStringLiteral("completedOperationId")).toString();
        const QString inFlight = event.value(QStringLiteral("inFlightOperationId")).toString();
        appendLog(QStringLiteral("Прерванное задание %1, устройство %2, последний завершённый этап %3, незавершённый этап %4. %5")
            .arg(event.value(QStringLiteral("jobId")).toString(), deviceId,
                completed.isEmpty() ? QStringLiteral("нет") : completed,
                inFlight.isEmpty() ? QStringLiteral("нет") : inFlight,
                flashMayHaveStarted
                    ? QStringLiteral("Запись flash могла начаться; проверьте устройство в загрузчике перед повтором.")
                    : QStringLiteral("Запись flash не начиналась; проверьте состояние устройства перед повтором.")));
    }
}

MainWindow::~MainWindow()
{
    if (mWorkflowThread && mWorkflowThread->isRunning())
    {
        mWorkflowThread->quit();
        mWorkflowThread->wait(5000);
    }

    const QSet<QThread*> deviceDataThreads = mDeviceDataThreads;
    for (QThread* thread : deviceDataThreads)
    {
        if (!thread || !thread->isRunning())
            continue;
        thread->quit();
        thread->wait(5000);
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (mActionBusy)
    {
        event->ignore();
        QMessageBox::information(this,
            QStringLiteral("Операция выполняется"),
            QStringLiteral("Дождитесь завершения текущей операции с устройствами."));
        return;
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::buildUi()
{
    resize(1280, 760);
    setWindowTitle(AppEdition::displayName());

    QWidget* root = new QWidget(this);
    QVBoxLayout* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->addWidget(buildDiscoveryPage());
    setCentralWidget(root);

    setStyleSheet(QStringLiteral(R"(
        QWidget#page { background: #f4f6f8; }
        QLabel#h1 { color: #17212b; font-size: 22px; font-weight: 700; }
        QLabel#subtitle { color: #667584; font-size: 13px; }
        QPushButton { min-height: 34px; border: 1px solid #d8e0e5; border-radius: 7px; padding: 0 12px; background: white; color: #25313f; }
        QPushButton#primary { background: #2563eb; border-color: #2563eb; color: white; }
        QPushButton#primary:disabled { background: #e5e9ed; border-color: #c8d0d8; color: #8a98a6; }
        QToolButton { min-height: 34px; border: 1px solid #d8e0e5; border-radius: 7px; padding: 0 10px; background: white; color: #25313f; }
        QToolButton:disabled { background: #e5e9ed; color: #8a98a6; }
        QFrame#band { background: white; border: 1px solid #d8e0e5; border-radius: 8px; }
        QComboBox, QLineEdit { min-height: 34px; border: 1px solid #d8e0e5; border-radius: 7px; padding: 0 8px; background: white; }
        QTableWidget { background: white; border: 0; gridline-color: #d8e0e5; selection-background-color: #edf5ff; selection-color: #17212b; alternate-background-color: #fafcff; }
        QHeaderView::section { background: #fbfcfd; color: #667584; border: 0; border-bottom: 1px solid #d8e0e5; padding: 8px; font-weight: 700; }
        QPlainTextEdit { background: #101820; color: #e5edf4; border-radius: 7px; padding: 8px; font-family: Consolas, monospace; }
        QPlainTextEdit#transportLog { background: #0d1320; color: #d1e7ff; border: 1px solid #20314f; }
    )"));

    updateLineMode();
}

QWidget* MainWindow::buildDiscoveryPage()
{
    QWidget* page = new QWidget;
    page->setObjectName(QStringLiteral("page"));
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(22, 16, 22, 18);
    layout->setSpacing(14);

    QLabel* h1 = new QLabel(QStringLiteral("Обнаружение устройств"));
    h1->setObjectName(QStringLiteral("h1"));
    QLabel* subtitle = new QLabel(QStringLiteral("Задайте параметры линии связи и выполните поиск."));
    subtitle->setObjectName(QStringLiteral("subtitle"));
    layout->addWidget(h1);
    layout->addWidget(subtitle);
    layout->addWidget(buildDiscoveryPanel());
    layout->addWidget(buildWorkflowProgressPanel());
    QSplitter* content = new QSplitter(Qt::Vertical);
    content->addWidget(buildDiscoveryTablePanel());
    content->addWidget(buildLogsPanel());
    content->setStretchFactor(0, 3);
    content->setStretchFactor(1, 2);
    content->setSizes({420, 220});
    layout->addWidget(content, 1);
    return page;
}

QWidget* MainWindow::buildLogsPanel()
{
    QSplitter* logs = new QSplitter(Qt::Horizontal);
    QWidget* operationPane = new QWidget;
    QVBoxLayout* operationLayout = new QVBoxLayout(operationPane);
    operationLayout->setContentsMargins(0, 0, 0, 0);
    QLabel* operationLabel = new QLabel(QStringLiteral("Журнал операций"));
    operationLabel->setObjectName(QStringLiteral("subtitle"));
    operationLayout->addWidget(operationLabel);
    QPlainTextEdit* operationLog = new QPlainTextEdit;
    operationLog->setReadOnly(true);
    operationLog->setPlaceholderText(QStringLiteral("Операции обнаружения и прошивки"));
    operationLog->setObjectName(QStringLiteral("operationLog"));
    operationLayout->addWidget(operationLog);
    mOperationLogs.append(operationLog);
    logs->addWidget(operationPane);

    QWidget* transportPane = new QWidget;
    QVBoxLayout* transportLayout = new QVBoxLayout(transportPane);
    transportLayout->setContentsMargins(0, 0, 0, 0);
    QLabel* transportLabel = new QLabel(QStringLiteral("Сырой транспорт"));
    transportLabel->setObjectName(QStringLiteral("subtitle"));
    transportLayout->addWidget(transportLabel);
    QPlainTextEdit* transportLog = new QPlainTextEdit;
    transportLog->setReadOnly(true);
    transportLog->setPlaceholderText(QStringLiteral("TX/RX ASCII packets"));
    transportLog->setObjectName(QStringLiteral("transportLog"));
    transportLayout->addWidget(transportLog);
    mTransportLogs.append(transportLog);
    logs->addWidget(transportPane);
    logs->setSizes({620, 420});
    return logs;
}

QWidget* MainWindow::buildWorkflowProgressPanel()
{
    QWidget* panel = new QWidget;
    QHBoxLayout* layout = new QHBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    QLabel* stage = new QLabel(QStringLiteral("Подготовка"));
    stage->setObjectName(QStringLiteral("subtitle"));
    stage->setMinimumWidth(260);
    stage->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QProgressBar* progress = new QProgressBar;
    progress->setRange(0, 100);
    progress->setValue(0);
    progress->setFixedWidth(210);
    progress->setFormat(QStringLiteral("%p%"));
    layout->addStretch();
    layout->addWidget(stage);
    layout->addWidget(progress);
    QPushButton* cancel = new QPushButton(QStringLiteral("Отмена"));
    cancel->setEnabled(false);
    connect(cancel, &QPushButton::clicked, this, [this]() {
        if (!mWorkflowCancelToken || mWorkflowCancelToken->exchange(true))
            return;
        appendLog(QStringLiteral("Запрошена отмена операции; выполнение остановится на безопасной границе."));
        for (QPushButton* button : mWorkflowCancelButtons)
            button->setEnabled(false);
    });
    layout->addWidget(cancel);

    panel->setVisible(false);
    mWorkflowProgressPanels.append(panel);
    mWorkflowStageLabels.append(stage);
    mWorkflowProgressBars.append(progress);
    mWorkflowCancelButtons.append(cancel);
    return panel;
}

QWidget* MainWindow::buildDiscoveryPanel()
{
    QFrame* frame = new QFrame;
    frame->setObjectName(QStringLiteral("band"));
    QVBoxLayout* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(8);

    const auto addFieldRow = [](QVBoxLayout* parent, const QString& text, QWidget* field) {
        QHBoxLayout* row = new QHBoxLayout;
        QLabel* label = new QLabel(text);
        label->setFixedWidth(190);
        field->setFixedWidth(420);
        row->addWidget(label);
        row->addWidget(field);
        row->addStretch();
        parent->addLayout(row);
    };

    mLineMode = new QComboBox;
    mLineMode->addItem(QStringLiteral("UDP broadcast"), QStringLiteral("udp"));
    mLineMode->addItem(QStringLiteral("RS-485"), QStringLiteral("rs485"));
    configureCombo(mLineMode);
    addFieldRow(layout, QStringLiteral("Линия связи"), mLineMode);

    mUdpPanel = new QWidget;
    QVBoxLayout* udp = new QVBoxLayout(mUdpPanel);
    udp->setContentsMargins(0, 0, 0, 0);
    udp->setSpacing(8);
    mNetworkInterface = new QComboBox;
    mNetworkInterface->addItems(availableNetworkInterfaces());
    configureCombo(mNetworkInterface);
    mUdpProtocol = new QComboBox;
    mUdpProtocol->addItem(QStringLiteral("Unicorn ASCII · FINE / 0xFF"), QStringLiteral("unicorn-ascii"));
    mUdpProtocol->addItem(QStringLiteral("Modbus RTU · RS-485"), QStringLiteral("modbus-rtu"));
    configureCombo(mUdpProtocol);
    addFieldRow(udp, QStringLiteral("Сетевой интерфейс"), mNetworkInterface);
    addFieldRow(udp, QStringLiteral("Протокол"), mUdpProtocol);
    layout->addWidget(mUdpPanel);

    mRs485Panel = new QWidget;
    QVBoxLayout* rs = new QVBoxLayout(mRs485Panel);
    rs->setContentsMargins(0, 0, 0, 0);
    rs->setSpacing(8);
    mSerialPort = new QComboBox;
    mSerialPort->addItems(availableSerialPorts());
    configureCombo(mSerialPort);
    mRs485Protocol = new QComboBox;
    mRs485Protocol->addItem(QStringLiteral("Unicorn ASCII · identity 0xFF"), QStringLiteral("unicorn-ascii"));
    mRs485Protocol->addItem(QStringLiteral("Modbus RTU · read identity"), QStringLiteral("modbus-rtu"));
    configureCombo(mRs485Protocol);
    mAddressStart = new QLineEdit(QStringLiteral("1"));
    mAddressEnd = new QLineEdit(QStringLiteral("64"));
    addFieldRow(rs, QStringLiteral("Порт"), mSerialPort);
    addFieldRow(rs, QStringLiteral("Протокол"), mRs485Protocol);
    addFieldRow(rs, QStringLiteral("Адрес начала"), mAddressStart);
    addFieldRow(rs, QStringLiteral("Адрес конца"), mAddressEnd);
    layout->addWidget(mRs485Panel);

    mSearchButton = new QPushButton(QStringLiteral("Broadcast поиск"));
    mSearchButton->setObjectName(QStringLiteral("primary"));
    QHBoxLayout* buttonRow = new QHBoxLayout;
    QLabel* buttonSpacer = new QLabel;
    buttonSpacer->setFixedWidth(190);
    buttonRow->addWidget(buttonSpacer);
    buttonRow->addWidget(mSearchButton, 0, Qt::AlignLeft);
    buttonRow->addStretch();
    layout->addLayout(buttonRow);

    connect(mLineMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::updateLineMode);
    connect(mSearchButton, &QPushButton::clicked, this, &MainWindow::startDiscovery);
    return frame;
}

QWidget* MainWindow::buildDiscoveryTablePanel()
{
    QFrame* frame = new QFrame;
    frame->setObjectName(QStringLiteral("band"));
    QVBoxLayout* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QWidget* filter = new QWidget;
    QHBoxLayout* filterLayout = new QHBoxLayout(filter);
    filterLayout->setContentsMargins(14, 12, 14, 12);
    filterLayout->addWidget(new QLabel(QStringLiteral("<b>Устройства текущего поиска</b>")));
    mBulkActionsButton = new QToolButton;
    mBulkActionsButton->setObjectName(QStringLiteral("bulkActions"));
    mBulkActionsButton->setText(QStringLiteral("Действия с выбранными"));
    mBulkActionsButton->setPopupMode(QToolButton::InstantPopup);
    mBulkActionsButton->setMenu(new QMenu(mBulkActionsButton));
    mBulkActionsButton->setEnabled(false);
    filterLayout->addWidget(mBulkActionsButton);
    filterLayout->addStretch();
    mDeviceDataProgressLabel = new QLabel(QStringLiteral("Получение данных"));
    mDeviceDataProgressLabel->setObjectName(QStringLiteral("subtitle"));
    mDeviceDataProgressLabel->setMinimumWidth(260);
    mDeviceDataProgressLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    mDeviceDataProgressBar = new QProgressBar;
    mDeviceDataProgressBar->setRange(0, 100);
    mDeviceDataProgressBar->setValue(0);
    mDeviceDataProgressBar->setFixedWidth(210);
    mDeviceDataProgressBar->setFormat(QStringLiteral("%p%"));
    mDeviceDataProgressLabel->setVisible(false);
    mDeviceDataProgressBar->setVisible(false);
    filterLayout->addWidget(mDeviceDataProgressLabel);
    filterLayout->addWidget(mDeviceDataProgressBar);
    layout->addWidget(filter);

    mDiscoveryTable = new QTableWidget(0, DiscoveryColumnCount);
    mDiscoveryTable->setObjectName(QStringLiteral("deviceTable"));
    mDiscoveryTable->setHorizontalHeaderLabels({
        QString(), QStringLiteral("Устройство"), QStringLiteral("Номер"),
        QStringLiteral("Адрес"), QStringLiteral("Подключение"),
        QStringLiteral("Прошивка"), QStringLiteral("Состояние"), QStringLiteral("Действия")
    });
    mDiscoveryTable->verticalHeader()->setVisible(false);
    mDiscoveryTable->verticalHeader()->setMinimumSectionSize(54);
    mDiscoveryTable->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    mDiscoveryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    mDiscoveryTable->setSelectionMode(QAbstractItemView::NoSelection);
    mDiscoveryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mDiscoveryTable->setAlternatingRowColors(true);
    mDiscoveryTable->setWordWrap(true);
    QHeaderView* header = mDiscoveryTable->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setStretchLastSection(false);
    header->setMinimumSectionSize(48);
    mDiscoveryTable->setColumnWidth(DiscoveryCheck, 42);
    header->setSectionResizeMode(DiscoveryDevice, QHeaderView::Stretch);
    mDiscoveryTable->setColumnWidth(DiscoveryNumber, 95);
    mDiscoveryTable->setColumnWidth(DiscoveryAddress, 78);
    mDiscoveryTable->setColumnWidth(DiscoveryChannel, 180);
    mDiscoveryTable->setColumnWidth(DiscoveryFirmware, 180);
    mDiscoveryTable->setColumnWidth(DiscoveryState, 120);
    mDiscoveryTable->setColumnWidth(DiscoveryActions, 140);
    mDiscoveryTable->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    connect(header, &QHeaderView::sectionResized, mDiscoveryTable, [this]() {
        mDiscoveryTable->resizeRowsToContents();
    });
    connect(mDiscoveryTable, &QTableWidget::itemChanged, this, &MainWindow::updateBulkMenu);
    layout->addWidget(mDiscoveryTable, 1);
    return frame;
}

void MainWindow::startDiscovery()
{
    ++mDiscoveryGeneration;
    mDeviceDataEndpoints.clear();
    mDeviceDataTotal = 0;
    mDeviceDataCompleted = 0;
    mDevices.clear();
    mDiscoveryTable->setRowCount(0);
    updateBulkMenu();
    setBusy(true);
    if (mDeviceDataProgressLabel)
    {
        mDeviceDataProgressLabel->setText(QStringLiteral("Поиск устройств"));
        mDeviceDataProgressLabel->setVisible(true);
    }
    if (mDeviceDataProgressBar)
    {
        mDeviceDataProgressBar->setValue(0);
        mDeviceDataProgressBar->setVisible(true);
    }

    DiscoverySettings settings;
    settings.lineMode = mLineMode->currentData().toString();
    settings.timeoutMs = 2000;

    if (settings.lineMode == QStringLiteral("udp"))
    {
        settings.interfaceName = mNetworkInterface->currentText();
        settings.protocolId = mUdpProtocol->currentData().toString();
        if (settings.protocolId != QStringLiteral("unicorn-ascii"))
        {
            appendLog(QStringLiteral("UDP discovery supports Unicorn ASCII only; selected protocol is %1").arg(settings.protocolId));
            setBusy(false);
            return;
        }
        mServices->udpDiscovery().start(settings);
    }
    else
    {
        settings.serialPortName = mSerialPort->currentText();
        settings.protocolId = mRs485Protocol->currentData().toString();
        settings.addressStart = mAddressStart->text().toInt();
        settings.addressEnd = mAddressEnd->text().toInt();
        mServices->rs485Discovery().start(settings);
    }
}

void MainWindow::onDeviceFound(DeviceIdentity device)
{
    CatalogMatch matched = mServices->catalog().match(std::move(device));
    device = std::move(matched.identity);
    std::shared_ptr<DeviceBase> deviceObject = mDeviceFactory.create(
        device, std::move(matched.profile));
    if (!deviceObject)
        return;

    // Show the discovery result immediately. Full description, JSON metadata
    // and UUID are populated by the per-device background worker below.
    mergeDiscoveredDevice(deviceObject);

    const QString endpointKey = QStringLiteral("%1|%2")
        .arg(mDiscoveryGeneration)
        .arg(device.endpoint);
    if (mDeviceDataEndpoints.contains(endpointKey))
        return;

    const quint64 requestId = mNextDeviceDataRequestId++;
    mDeviceDataEndpoints.insert(endpointKey);
    mPendingDeviceDataReads.insert(requestId,
        {deviceObject, mDiscoveryGeneration, endpointKey, 0, QStringLiteral("Подготовка")});
    ++mDeviceDataTotal;
    updateDeviceDataProgress();

    QThread* thread = new QThread;
    thread->setObjectName(QStringLiteral("device-data-%1").arg(requestId));
    DeviceDataWorker* worker = new DeviceDataWorker(requestId, deviceObject);
    worker->moveToThread(thread);
    mDeviceDataThreads.insert(thread);

    connect(thread, &QThread::started, worker, &DeviceDataWorker::run);
    connect(worker, &DeviceDataWorker::progressChanged, this, &MainWindow::onDeviceDataProgress);
    connect(worker, &DeviceDataWorker::finished, this, &MainWindow::onDeviceDataFinished);
    connect(worker, &DeviceDataWorker::finished, thread, &QThread::quit);
    connect(worker, &DeviceDataWorker::finished, worker, &DeviceDataWorker::deleteLater);
    connect(thread, &QThread::finished, this, [this, thread]() {
        mDeviceDataThreads.remove(thread);
    });
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
}

void MainWindow::onDeviceDataProgress(quint64 requestId, int percent, const QString& stage)
{
    auto pendingIt = mPendingDeviceDataReads.find(requestId);
    if (pendingIt == mPendingDeviceDataReads.end()
        || pendingIt->discoveryGeneration != mDiscoveryGeneration)
        return;
    pendingIt->progress = qBound(0, percent, 100);
    pendingIt->stage = stage;
    updateDeviceDataProgress();
}

void MainWindow::onDeviceDataFinished(quint64 requestId,
    DeviceIdentity identity,
    const QStringList& warnings,
    const QString& rawResponse)
{
    const auto pendingIt = mPendingDeviceDataReads.find(requestId);
    if (pendingIt == mPendingDeviceDataReads.end())
        return;

    const PendingDeviceDataRead pending = pendingIt.value();
    mPendingDeviceDataReads.erase(pendingIt);
    if (!pending.device || pending.discoveryGeneration != mDiscoveryGeneration)
        return;

    CatalogMatch matched = mServices->catalog().match(std::move(identity));
    identity = std::move(matched.identity);
    if (!identity.uuid.isEmpty())
        identity.id = identity.uuid;
    pending.device->updateIdentity(identity);
    pending.device->updateProfile(std::move(matched.profile));
    for (const QString& warning : warnings)
        appendLog(QStringLiteral("[%1] %2").arg(identity.typeHex(), warning));
    if (!rawResponse.isEmpty())
        appendTransportLog(QStringLiteral("[%1] %2").arg(identity.typeHex(), rawResponse));

    mergeDiscoveredDevice(pending.device);
    ++mDeviceDataCompleted;
    updateDeviceDataProgress();
    if (mDeviceDataCompleted >= mDeviceDataTotal)
        appendLog(QStringLiteral("Found %1 device(s); device data received").arg(mDevices.size()));
}

void MainWindow::mergeDiscoveredDevice(const std::shared_ptr<DeviceBase>& deviceObject)
{
    if (!deviceObject)
        return;

    const DeviceIdentity& device = deviceObject->identity();

    for (int i = 0; i < mDevices.size(); ++i)
    {
        const DeviceIdentity& existing = mDevices.at(i)->identity();
        const bool sameUuid = !device.uuid.isEmpty() && !existing.uuid.isEmpty()
            && device.uuid.compare(existing.uuid, Qt::CaseInsensitive) == 0;
        const bool sameEndpoint = existing.endpoint == device.endpoint;
        if (sameUuid || sameEndpoint)
        {
            mDevices.at(i)->updateIdentity(device);
            mDevices.at(i)->updateProfile(deviceObject->profile());
            updateDeviceRow(i, mDevices.at(i));
            updateBulkMenu();
            return;
        }
    }
    mDevices.append(deviceObject);
    addDeviceRow(deviceObject);
    updateBulkMenu();
}

void MainWindow::onDiscoveryFinished()
{
    setBusy(false);
    int pendingCount = 0;
    for (auto it = mPendingDeviceDataReads.cbegin(); it != mPendingDeviceDataReads.cend(); ++it)
    {
        if (it.value().discoveryGeneration == mDiscoveryGeneration)
            ++pendingCount;
    }
    appendLog(pendingCount > 0
        ? QStringLiteral("Discovery finished; receiving data for %1 device(s)").arg(pendingCount)
        : QStringLiteral("Found %1 device(s)").arg(mDevices.size()));
    updateDeviceDataProgress();
}

void MainWindow::updateDeviceDataProgress()
{
    if (!mDeviceDataProgressBar || !mDeviceDataProgressLabel)
        return;
    int progressSum = mDeviceDataCompleted * 100;
    QString currentStage;
    for (auto it = mPendingDeviceDataReads.cbegin(); it != mPendingDeviceDataReads.cend(); ++it)
    {
        if (it.value().discoveryGeneration != mDiscoveryGeneration)
            continue;
        progressSum += it.value().progress;
        if (currentStage.isEmpty() && !it.value().stage.isEmpty())
            currentStage = it.value().stage;
    }
    const int percent = mDeviceDataTotal > 0
        ? qBound(0, progressSum / mDeviceDataTotal, 100)
        : (mDiscoveryBusy ? 0 : 100);
    mDeviceDataProgressBar->setVisible(true);
    mDeviceDataProgressBar->setValue(percent);
    mDeviceDataProgressLabel->setVisible(true);
    if (mDeviceDataTotal == 0)
        mDeviceDataProgressLabel->setText(mDiscoveryBusy
            ? QStringLiteral("Поиск устройств")
            : QStringLiteral("Устройства не найдены"));
    else if (mDeviceDataCompleted >= mDeviceDataTotal)
        mDeviceDataProgressLabel->setText(
            QStringLiteral("Данные получены: %1/%1").arg(mDeviceDataTotal));
    else
        mDeviceDataProgressLabel->setText(QStringLiteral("%1 · %2/%3")
            .arg(currentStage.isEmpty() ? QStringLiteral("Получение данных") : currentStage)
            .arg(mDeviceDataCompleted)
            .arg(mDeviceDataTotal));
}

void MainWindow::updateLineMode()
{
    const bool rs485 = mLineMode->currentData().toString() == QStringLiteral("rs485");
    mUdpPanel->setVisible(!rs485);
    mRs485Panel->setVisible(rs485);
    mSearchButton->setText(rs485 ? QStringLiteral("Поиск RS-485") : QStringLiteral("Broadcast поиск"));
}

void MainWindow::updateBulkMenu()
{
    rebuildBulkMenu();
}

bool MainWindow::actionHasArtifact(const ActionSpec& action,
    const QVector<std::shared_ptr<DeviceBase>>& devices) const
{
    return action.target.isEmpty() || !artifactsForTarget(devices, action.target).isEmpty();
}

void MainWindow::runActionForRow(int row, const QString& actionId)
{
    if (row < 0 || row >= mDevices.size())
        return;
    executeAction(actionById(actionId), {mDevices.at(row)});
}

void MainWindow::executeAction(const ActionSpec& action, const QVector<std::shared_ptr<DeviceBase>>& devices)
{
    if (action.id.isEmpty() || devices.isEmpty())
        return;

    for (const std::shared_ptr<DeviceBase>& device : devices)
    {
        if (isDeviceBusy(device))
        {
            appendLog(QStringLiteral("Operation %1 ignored: device is busy").arg(action.id));
            return;
        }
    }

    if (action.id == QStringLiteral("device.ping"))
    {
        if (devices.size() == 1)
            showPingDialog(devices.first());
        return;
    }

    QVariantMap parameters;
    if (!prepareActionInvocation(action, devices, &parameters))
        return;

    startWorkflowAction(action, devices, parameters);
}

void MainWindow::startWorkflowAction(const ActionSpec& action, const QVector<std::shared_ptr<DeviceBase>>& devices, const QVariantMap& parameters)
{
    if (mWorkflowThread)
    {
        appendLog(QStringLiteral("Another workflow is already running"));
        return;
    }

    QThread* thread = new QThread;
    mWorkflowCancelToken = std::make_shared<std::atomic_bool>(false);
    WorkflowWorker* worker = new WorkflowWorker(&mServices->workflows(), action, devices,
        parameters, mWorkflowCancelToken);
    worker->moveToThread(thread);

    struct WorkflowResult
    {
        bool received = false;
        bool successful = false;
        QString operation;
        QString stage;
        QHash<int, QString> deviceErrors;
    };
    const auto result = std::make_shared<WorkflowResult>();

    connect(thread, &QThread::started, worker, &WorkflowWorker::run);
    connect(worker, &WorkflowWorker::logMessage, this, &MainWindow::appendLog);
    connect(worker, &WorkflowWorker::transportLogMessage, this, &MainWindow::appendTransportLog);
    connect(worker, &WorkflowWorker::progressChanged, this, &MainWindow::onWorkflowProgress);
    connect(worker, &WorkflowWorker::stageChanged, this, &MainWindow::onWorkflowStageChanged);
    connect(worker, &WorkflowWorker::deviceStageChanged, this,
        [this, devices, result](int deviceIndex, const QString& operation, const QString& stage) {
            if (result->received || deviceIndex < 0 || deviceIndex >= devices.size())
                return;
            for (int row = 0; row < mDevices.size(); ++row)
            {
                if (mDevices.at(row) == devices.at(deviceIndex))
                {
                    if (QTableWidgetItem* item = mDiscoveryTable->item(row, DiscoveryState))
                        item->setText(workflowStageText(operation, stage));
                    break;
                }
            }
        });
    connect(worker, &WorkflowWorker::deviceResult, this,
        [this, devices, result](int deviceIndex, bool successful,
            const QString& operation, const QString& stage) {
            if (successful || deviceIndex < 0 || deviceIndex >= devices.size())
                return;
            result->deviceErrors.insert(deviceIndex,
                stage.isEmpty() ? QStringLiteral("Подробности в журнале операций")
                    : workflowStageText(operation, stage));
        });
    connect(worker, &WorkflowWorker::identityRefreshed, this,
        [this, devices](int deviceIndex, DeviceIdentity identity) {
            if (deviceIndex < 0 || deviceIndex >= devices.size() || !devices.at(deviceIndex))
                return;
            if (!identity.uuid.isEmpty())
                identity.id = identity.uuid;
            CatalogMatch matched = mServices->catalog().match(std::move(identity));
            identity = std::move(matched.identity);
            devices.at(deviceIndex)->updateIdentity(identity);
            devices.at(deviceIndex)->updateProfile(std::move(matched.profile));
            for (int row = 0; row < mDevices.size(); ++row)
            {
                if (mDevices.at(row) == devices.at(deviceIndex))
                {
                    updateDeviceRow(row, devices.at(deviceIndex));
                    break;
                }
            }
        });
    connect(worker, &WorkflowWorker::finished, this,
        [this, devices, result](bool successful, const QString& operation, const QString& stage) {
        result->received = true;
        result->successful = successful;
        result->operation = operation;
        result->stage = stage;
        for (int deviceIndex = 0; deviceIndex < devices.size(); ++deviceIndex)
        {
            const std::shared_ptr<DeviceBase>& device = devices.at(deviceIndex);
            for (int row = 0; row < mDevices.size(); ++row)
            {
                if (mDevices.at(row) == device)
                {
                    updateDeviceRow(row, device);
                    break;
                }
            }
        }
        updateBulkMenu();
    });
    connect(worker, &WorkflowWorker::finished, thread, &QThread::quit);
    connect(worker, &WorkflowWorker::finished, worker, &WorkflowWorker::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    connect(thread, &QThread::finished, this, [this, thread, devices, action, result]() {
        if (mWorkflowThread == thread)
            mWorkflowThread = nullptr;
        setDevicesBusy(devices, false);
        setActionBusy(false);
        const bool cancelled = mWorkflowCancelToken && mWorkflowCancelToken->load();
        mWorkflowCancelToken.reset();
        for (QPushButton* button : mWorkflowCancelButtons)
            button->setEnabled(false);
        for (auto it = result->deviceErrors.constBegin(); it != result->deviceErrors.constEnd(); ++it)
        {
            if (it.key() < 0 || it.key() >= devices.size())
                continue;
            for (int row = 0; row < mDevices.size(); ++row)
            {
                if (mDevices.at(row) == devices.at(it.key()))
                {
                    if (QTableWidgetItem* item = mDiscoveryTable->item(row, DiscoveryState))
                    {
                        item->setText(QStringLiteral("Ошибка операции"));
                        item->setToolTip(it.value());
                    }
                    break;
                }
            }
        }

        const bool successful = result->received && result->successful;
        const bool rediscoverAfterWorkflow = isFlashAction(action.id);
        const QString stage = workflowStageText(result->operation, result->stage);
        const QString title = action.title.isEmpty() ? action.id : action.title;
        const QString refreshMessage = rediscoverAfterWorkflow
            ? QStringLiteral("\nПосле закрытия сообщения будет выполнен повторный поиск устройств.")
            : QString();
        QMessageBox notification(successful ? QMessageBox::Information : QMessageBox::Warning,
            title,
            successful
                ? QStringLiteral("Операция завершена успешно.\nЭтап: %1%2").arg(stage, refreshMessage)
                : cancelled
                    ? QStringLiteral("Операция прервана по запросу.\nЭтап: %1\n"
                        "Проверьте состояние устройства перед повтором.%2")
                        .arg(stage, refreshMessage)
                    : QStringLiteral("Операция завершилась с ошибкой.\nЭтап: %1\n"
                        "Подробности записаны в журнал операций.%2").arg(stage, refreshMessage),
            QMessageBox::Ok,
            this);
        notification.show();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        if (successful)
        {
            for (QProgressBar* progress : mWorkflowProgressBars)
                progress->setValue(100);
            for (QLabel* label : mWorkflowStageLabels)
                label->setText(QStringLiteral("Завершено"));
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
        notification.exec();
        for (QWidget* panel : mWorkflowProgressPanels)
            panel->setVisible(false);
        for (QProgressBar* progress : mWorkflowProgressBars)
            progress->setValue(0);
        if (rediscoverAfterWorkflow)
        {
            appendLog(QStringLiteral("Повторный поиск устройств после прошивки"));
            QTimer::singleShot(0, this, &MainWindow::startDiscovery);
        }
    });

    mWorkflowThread = thread;
    for (QWidget* panel : mWorkflowProgressPanels)
        panel->setVisible(true);
    for (QProgressBar* progress : mWorkflowProgressBars)
        progress->setValue(0);
    for (QPushButton* button : mWorkflowCancelButtons)
        button->setEnabled(true);
    onWorkflowStageChanged(QStringLiteral("workflow.start"), QStringLiteral("start"));
    setDevicesBusy(devices, true);
    setActionBusy(true);
    thread->start();
}

void MainWindow::appendLog(const QString& message)
{
    appendFileLog(QStringLiteral("OPERATION"), message);
    const QString line = QStringLiteral("[%1] %2")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), message);
    for (QPlainTextEdit* log : mOperationLogs)
    {
        if (log)
            log->appendPlainText(line);
    }
}

void MainWindow::appendTransportLog(const QString& message)
{
    appendFileLog(QStringLiteral("TRANSPORT"), message);
    const QString line = QStringLiteral("[%1] %2")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), message);
    for (QPlainTextEdit* log : mTransportLogs)
    {
        if (log)
            log->appendPlainText(line);
    }
}

QString MainWindow::logFilePath() const
{
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dataPath.isEmpty())
        dataPath = QCoreApplication::applicationDirPath();
    const QString directoryPath = QDir(dataPath).filePath(QStringLiteral("logs"));
    QDir().mkpath(directoryPath);
    return QDir(directoryPath).filePath(QStringLiteral("device-workbench-%1.log")
        .arg(QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"))));
}

void MainWindow::appendFileLog(const QString& category, const QString& message) const
{
    QFile file(logFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;

    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    stream << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
           << QStringLiteral(" [") << category << QStringLiteral("] ")
           << message << QLatin1Char('\n');
}

void MainWindow::onWorkflowProgress(int percent)
{
    // 100% is reserved for a confirmed successful result and is set immediately
    // before the result notification is shown.
    const int boundedPercent = qBound(0, percent, 99);
    for (QWidget* panel : mWorkflowProgressPanels)
        panel->setVisible(true);
    for (QProgressBar* progress : mWorkflowProgressBars)
        progress->setValue(boundedPercent);
}

void MainWindow::onWorkflowStageChanged(const QString& operation, const QString& stage)
{
    const QString text = workflowStageText(operation, stage);
    for (QWidget* panel : mWorkflowProgressPanels)
        panel->setVisible(true);
    for (QLabel* label : mWorkflowStageLabels)
    {
        label->setText(text);
        label->setToolTip(text);
    }
}

QString MainWindow::workflowStageText(const QString& operation, const QString& stage) const
{
    static const QHash<QString, QString> operationTitles = {
        {QStringLiteral("workflow.start"), QStringLiteral("Подготовка операции")},
        {QStringLiteral("workflow.definition"), QStringLiteral("Загрузка сценария")},
        {QStringLiteral("workflow.cancelled"), QStringLiteral("Операция отменена")},
        {QStringLiteral("workflow.complete"), QStringLiteral("Завершено")},
        {QStringLiteral("workflow"), QStringLiteral("Выполнение сценария")},
        {QStringLiteral("context.productionDate"), QStringLiteral("Проверка даты")},
        {QStringLiteral("context.serialNumber"), QStringLiteral("Проверка номера")},
        {QStringLiteral("device.ensureUuid"), QStringLiteral("Проверка UUID")},
        {QStringLiteral("device.reset"), QStringLiteral("Перезагрузка устройства")},
        {QStringLiteral("sleep"), QStringLiteral("Ожидание устройства")},
        {QStringLiteral("device.connect"), QStringLiteral("Подключение к устройству")},
        {QStringLiteral("device.enterBootloader"), QStringLiteral("Загрузка bootloader")},
        {QStringLiteral("device.disableApplicationLoad"), QStringLiteral("Запрет загрузки приложения")},
        {QStringLiteral("device.disableLoadApplication"), QStringLiteral("Запрет загрузки приложения")},
        {QStringLiteral("device.captureServiceData"), QStringLiteral("Сохранение параметров устройства")},
        {QStringLiteral("device.restoreServiceData"), QStringLiteral("Восстановление параметров устройства")},
        {QStringLiteral("device.writeProductionDate"), QStringLiteral("Запись даты производства")},
        {QStringLiteral("device.writeSerialNumber"), QStringLiteral("Запись номера устройства")},
        {QStringLiteral("device.loadApplication"), QStringLiteral("Запуск приложения")},
        {QStringLiteral("device.loadApplicationNoReply"), QStringLiteral("Запуск приложения")},
        {QStringLiteral("device.waitForApplication"), QStringLiteral("Ожидание приложения")},
        {QStringLiteral("device.refreshIdentity"), QStringLiteral("Обновление данных устройства")},
        {QStringLiteral("firmware.validateTransition"), QStringLiteral("Проверка перехода прошивки")},
        {QStringLiteral("firmware.validateArtifact"), QStringLiteral("Проверка файла прошивки")},
        {QStringLiteral("flash.validateArtifact"), QStringLiteral("Проверка файла прошивки")},
        {QStringLiteral("flash.prepare"), QStringLiteral("Подготовка прошивки")},
        {QStringLiteral("flash.buildPagePlan"), QStringLiteral("Проверка размещения в flash")},
        {QStringLiteral("flash.preflight"), QStringLiteral("Подготовка flash-памяти")},
        {QStringLiteral("firmware.flash"), QStringLiteral("Запись прошивки")},
        {QStringLiteral("firmware.verify"), QStringLiteral("Проверка записанной прошивки")},
        {QStringLiteral("firmware.verifyInstalledVersion"), QStringLiteral("Проверка версии прошивки")},
        {QStringLiteral("firmware.complete"), QStringLiteral("Завершение прошивки")},
        {QStringLiteral("flash.complete"), QStringLiteral("Завершение прошивки")},
        {QStringLiteral("workflow.finish"), QStringLiteral("Завершение операции")},
        {QStringLiteral("log"), QStringLiteral("Завершение операции")}
    };
    if (operationTitles.contains(operation))
        return operationTitles.value(operation);
    static const QHash<QString, QString> stageTitles = {
        {QStringLiteral("queued"), QStringLiteral("Операция поставлена в очередь")},
        {QStringLiteral("validating firmware artifact"), QStringLiteral("Проверка файла прошивки")},
        {QStringLiteral("disable application loading"), QStringLiteral("Запрет загрузки приложения")},
        {QStringLiteral("refresh identity"), QStringLiteral("Обновление данных устройства")},
        {QStringLiteral("done"), QStringLiteral("Завершено")}
    };
    const QString normalizedStage = stage.trimmed().toLower();
    if (stageTitles.contains(normalizedStage))
        return stageTitles.value(normalizedStage);
    if (normalizedStage.startsWith(QStringLiteral("read identity")))
        return QStringLiteral("Чтение данных устройства");
    if (normalizedStage.startsWith(QStringLiteral("prepare ")))
        return QStringLiteral("Подготовка flash-памяти");
    if (normalizedStage.startsWith(QStringLiteral("verify ")))
        return QStringLiteral("Проверка записанных данных");
    if (!stage.trimmed().isEmpty())
        return stage.trimmed();
    return QStringLiteral("Неизвестный этап");
}

bool MainWindow::prepareActionInvocation(const ActionSpec& action, const QVector<std::shared_ptr<DeviceBase>>& devices, QVariantMap* parameters)
{
    if (!parameters)
        return false;

    parameters->clear();

    if (action.id == QStringLiteral("device.productionDate.update"))
    {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Смена даты производства"));

        QVBoxLayout* layout = new QVBoxLayout(&dialog);
        QLineEdit* dateEdit = new QLineEdit;
        dateEdit->setInputMask(QStringLiteral("00.00.0000;_"));
        dateEdit->setText(QDate::currentDate().toString(QStringLiteral("dd.MM.yyyy")));
        dateEdit->setAccessibleName(QStringLiteral("Дата производства"));
        dateEdit->setToolTip(QStringLiteral("Дата в формате ДД.ММ.ГГГГ"));
        dateEdit->setMinimumWidth(240);
        layout->addWidget(dateEdit);

        QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        QPushButton* apply = buttons->button(QDialogButtonBox::Ok);
        apply->setText(QStringLiteral("Применить"));
        buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Отмена"));
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);

        const auto validateDate = [dateEdit, apply]() {
            const QDate date = QDate::fromString(dateEdit->text(), QStringLiteral("dd.MM.yyyy"));
            const bool valid = date.isValid()
                && date >= QDate(1970, 1, 1)
                && date <= QDate(2099, 12, 31);
            apply->setEnabled(valid);
            dateEdit->setStyleSheet(valid ? QString() : QStringLiteral("border: 1px solid #dc2626;"));
            dateEdit->setToolTip(valid
                ? QStringLiteral("Дата в формате ДД.ММ.ГГГГ")
                : QStringLiteral("Введите корректную дату от 01.01.1970 до 31.12.2099"));
        };
        connect(dateEdit, &QLineEdit::textChanged, &dialog, validateDate);
        validateDate();
        dateEdit->setFocus();
        dateEdit->selectAll();

        if (dialog.exec() != QDialog::Accepted)
            return false;

        const QDate productionDate = QDate::fromString(dateEdit->text(), QStringLiteral("dd.MM.yyyy"));
        if (!productionDate.isValid()
            || productionDate < QDate(1970, 1, 1)
            || productionDate > QDate(2099, 12, 31))
            return false;
        parameters->insert(QStringLiteral("productionDate"), productionDate);
        return true;
    }

    if (action.id == QStringLiteral("device.serialNumber.update"))
    {
        if (devices.size() != 1 || !devices.first())
            return false;

        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Смена номера устройства"));

        QVBoxLayout* layout = new QVBoxLayout(&dialog);
        QLineEdit* serialEdit = new QLineEdit;
        serialEdit->setMaxLength(6);
        serialEdit->setValidator(new QRegularExpressionValidator(
            QRegularExpression(QStringLiteral("^[0-9]{1,6}$")), serialEdit));
        serialEdit->setPlaceholderText(QStringLiteral("Номер от 0 до 999999"));
        serialEdit->setAccessibleName(QStringLiteral("Номер устройства"));
        serialEdit->setToolTip(QStringLiteral("Число от 0 до 999999"));
        serialEdit->setMinimumWidth(240);
        const QString currentSerial = devices.first()->identity().serialNumber;
        if (!currentSerial.isEmpty())
            serialEdit->setText(currentSerial);
        layout->addWidget(serialEdit);

        QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        QPushButton* apply = buttons->button(QDialogButtonBox::Ok);
        apply->setText(QStringLiteral("Применить"));
        buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Отмена"));
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);

        const auto validateSerial = [serialEdit, apply]() {
            bool numberOk = false;
            const int number = serialEdit->text().toInt(&numberOk);
            const bool valid = serialEdit->hasAcceptableInput()
                && numberOk && number >= 0 && number <= 999999;
            apply->setEnabled(valid);
            serialEdit->setStyleSheet(valid ? QString() : QStringLiteral("border: 1px solid #dc2626;"));
            serialEdit->setToolTip(valid
                ? QStringLiteral("Число от 0 до 999999")
                : QStringLiteral("Введите от одной до шести цифр"));
        };
        connect(serialEdit, &QLineEdit::textChanged, &dialog, validateSerial);
        validateSerial();
        serialEdit->setFocus();
        serialEdit->selectAll();

        if (dialog.exec() != QDialog::Accepted)
            return false;

        bool numberOk = false;
        const int serialNumber = serialEdit->text().toInt(&numberOk);
        if (!numberOk || serialNumber < 0 || serialNumber > 999999)
            return false;
        parameters->insert(QStringLiteral("serialNumber"), serialNumber);
        return true;
    }

    if (isFlashAction(action.id))
    {
        const QString title = action.title.isEmpty() ? action.id : action.title;
        const QVector<FirmwareArtifact> artifacts = artifactsForTarget(devices, action.target);
        const bool graphControlled = action.target == QStringLiteral("application")
            && devices.first() && !devices.first()->firmwareVersions().isEmpty();
        const bool bootloaderFlash = action.target == QStringLiteral("bootloader");
        const bool allowsCustomFirmware = AppEdition::allowsCustomFirmware()
            && !graphControlled && !bootloaderFlash;
        bool hasUnknownCurrentFirmware = false;
        bool hasBlockedUnknownCurrentFirmware = false;
        bool hasRestrictedExternalBocV6 = false;
        for (const std::shared_ptr<DeviceBase>& device : devices)
        {
            if (device && device->identity().known
                && device->identity().currentFirmwareId.isEmpty())
            {
                hasUnknownCurrentFirmware = true;
                hasBlockedUnknownCurrentFirmware = hasBlockedUnknownCurrentFirmware
                    || !device->allowUnknownCurrentFirmware();
            }
            if (device && FirmwareAccessPolicy::isRestrictedExternalBocV6(*device))
                hasRestrictedExternalBocV6 = true;
        }

        QDialog dialog(this);
        dialog.setWindowTitle(title);

        QVBoxLayout* layout = new QVBoxLayout(&dialog);
        QString introText;
        if (graphControlled && hasRestrictedExternalBocV6)
        {
            introText = hasUnknownCurrentFirmware
                ? QStringLiteral("Версия текущей прошивки БОЦ-В-6 не определена. "
                                 "Во внешней версии прошивка запрещена.")
                : QStringLiteral("Во внешней версии обновление до "
                                 "BOCv6_ADCVibr_Digital20260831_1007 доступно только с версии "
                                 "BOCv6_ADCVibr_Digital20260721_1228.");
        }
        else if (graphControlled && hasBlockedUnknownCurrentFirmware)
        {
            introText = QStringLiteral("Версия текущей прошивки не определена. "
                "Обновление для этого устройства запрещено.");
        }
        else if (graphControlled && devices.size() == 1)
        {
            introText = hasUnknownCurrentFirmware
                ? QStringLiteral("Версия текущей прошивки не определена. "
                                 "Доступны все прошивки для этого устройства.")
                : QStringLiteral("Текущая прошивка: %1. Выберите разрешённый переход.")
                    .arg(devices.first()->identity().currentFirmwareId);
        }
        else if (graphControlled)
        {
            introText = hasUnknownCurrentFirmware
                ? QStringLiteral("Для устройств с неизвестной версией доступны все прошивки; "
                                 "для остальных учтены разрешённые переходы.")
                : QStringLiteral("Выберите переход, разрешённый для всех %1 устройств.").arg(devices.size());
        }
        else if (devices.size() == 1)
        {
            introText = allowsCustomFirmware
                ? QStringLiteral("Выберите прошивку для 1 устройства.")
                : QStringLiteral("Выберите встроенную прошивку для 1 устройства.");
        }
        else
        {
            introText = allowsCustomFirmware
                ? QStringLiteral("Выберите прошивку для %1 устройств.").arg(devices.size())
                : QStringLiteral("Выберите встроенную прошивку для %1 устройств.").arg(devices.size());
        }
        QLabel* intro = new QLabel(introText);
        intro->setWordWrap(true);
        layout->addWidget(intro);

        QFormLayout* form = new QFormLayout;
        QComboBox* artifactCombo = new QComboBox;
        configureCombo(artifactCombo);
        int defaultIndex = 0;
        for (int i = 0; i < artifacts.size(); ++i)
        {
            artifactCombo->addItem(artifactLabel(artifacts.at(i)), i);
            if (artifacts.at(i).isDefault)
                defaultIndex = i;
        }
        if (artifactCombo->count() == 0)
            artifactCombo->addItem(graphControlled
                ? (hasUnknownCurrentFirmware
                    ? QStringLiteral("Нет общей подходящей прошивки")
                    : QStringLiteral("Нет разрешённых переходов"))
                : QStringLiteral("Нет прошивки в каталоге"), -1);
        artifactCombo->setCurrentIndex(defaultIndex);
        form->addRow(QStringLiteral("Прошивка"), artifactCombo);

        QLineEdit* customFile = nullptr;
        if (allowsCustomFirmware)
        {
            customFile = new QLineEdit;
            customFile->setReadOnly(true);
            QPushButton* browse = new QPushButton(QStringLiteral("Выбрать файл..."));
            QHBoxLayout* fileRow = new QHBoxLayout;
            fileRow->addWidget(customFile, 1);
            fileRow->addWidget(browse);
            form->addRow(QStringLiteral("Другой файл"), fileRow);
            connect(browse, &QPushButton::clicked, &dialog, [customFile]() {
                const QString fileName = QFileDialog::getOpenFileName(nullptr,
                    QStringLiteral("Выбрать прошивку"),
                    QString(),
                    QStringLiteral("Firmware (*.bin *.hex *.ldr);;All files (*.*)"));
                if (!fileName.isEmpty())
                    customFile->setText(fileName);
            });
        }

        QCheckBox* verify = new QCheckBox(QStringLiteral("Проверить после записи"));
        verify->setChecked(true);
        verify->setVisible(!graphControlled && !bootloaderFlash);
        form->addRow(QString(), verify);
        layout->addLayout(form);

        QLabel* warning = new QLabel(QStringLiteral("Запись application flash выполняется через bootloader block flash: pages are written with command 0x43, verify reads back with 0x44."));
        if (graphControlled)
            warning->setText(QStringLiteral("Порядок reset, flash, verify, restart и ожидания application "
                "задаётся выбранной прошивкой."));
        else if (bootloaderFlash)
            warning->setText(QStringLiteral("Bootloader будет записан напрямую из основного приложения "
                "и обязательно проверен чтением записанных страниц."));
        warning->setWordWrap(true);
        layout->addWidget(warning);

        QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Прошить"));
        buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Отмена"));
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!artifacts.isEmpty() || allowsCustomFirmware);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);

        if (dialog.exec() != QDialog::Accepted)
            return false;

        FirmwareArtifact selected;
        const int selectedIndex = artifactCombo->currentData().toInt();
        if (selectedIndex >= 0 && selectedIndex < artifacts.size())
            selected = artifacts.at(selectedIndex);
        if (allowsCustomFirmware && customFile && !customFile->text().isEmpty())
        {
            selected.relativePath = customFile->text();
            selected.title = QFileInfo(customFile->text()).fileName();
            selected.version = QStringLiteral("custom");
            selected.sha256.clear();
            selected.isDefault = false;
            selected.target = action.target;
        }
        if (selected.relativePath.isEmpty())
        {
            QMessageBox::warning(this, title, QStringLiteral("Не выбрана прошивка."));
            return false;
        }

        parameters->insert(QStringLiteral("artifact"), artifactToMap(selected));
        if (!selected.firmwareId.isEmpty())
            parameters->insert(QStringLiteral("targetFirmwareId"), selected.firmwareId);
        if (!graphControlled)
            parameters->insert(QStringLiteral("verifyAfterWrite"),
                bootloaderFlash || verify->isChecked());
        return true;
    }

    const QString title = action.title.isEmpty() ? action.id : action.title;
    const QString message = devices.size() == 1
        ? QStringLiteral("Выполнить действие \"%1\" для 1 устройства?").arg(title)
        : QStringLiteral("Выполнить действие \"%1\" для %2 устройств?").arg(title).arg(devices.size());
    return QMessageBox::question(this, title, message, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

void MainWindow::addDeviceRow(const std::shared_ptr<DeviceBase>& device)
{
    const int row = mDevices.size() - 1;
    mDiscoveryTable->insertRow(row);
    updateDeviceRow(row, device);
}

void MainWindow::updateDeviceRow(int row, const std::shared_ptr<DeviceBase>& device)
{
    updateDiscoveryDeviceRow(row, device);
}

bool MainWindow::isDeviceBusy(const std::shared_ptr<DeviceBase>& device) const
{
    return device && mBusyDevices.contains(device.get());
}

void MainWindow::setDevicesBusy(const QVector<std::shared_ptr<DeviceBase>>& devices, bool busy)
{
    for (const std::shared_ptr<DeviceBase>& device : devices)
    {
        if (!device)
            continue;
        if (busy)
            mBusyDevices.insert(device.get());
        else
            mBusyDevices.remove(device.get());
    }

    for (int row = 0; row < mDevices.size(); ++row)
        updateDeviceRow(row, mDevices.at(row));
    updateBulkMenu();
}

void MainWindow::updateDiscoveryDeviceRow(int row, const std::shared_ptr<DeviceBase>& device)
{
    if (!device || row < 0 || row >= mDiscoveryTable->rowCount())
        return;

    const QSignalBlocker blocker(mDiscoveryTable);
    const DeviceIdentity& identity = device->identity();
    const Qt::CheckState previousCheckState = mDiscoveryTable->item(row, DiscoveryCheck)
        ? mDiscoveryTable->item(row, DiscoveryCheck)->checkState() : Qt::Unchecked;
    QTableWidgetItem* check = new QTableWidgetItem;
    check->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
    check->setCheckState(previousCheckState);
    check->setToolTip(QStringLiteral("Выбрать устройство для группового действия"));
    mDiscoveryTable->setItem(row, DiscoveryCheck, check);
    const QString modelName = device->profile()
        ? device->profile()->name : QStringLiteral("Unknown device");
    const QString deviceText = identity.description.isEmpty()
        ? modelName
        : QStringLiteral("%1\n%2").arg(modelName, identity.description);
    QTableWidgetItem* deviceItem = new QTableWidgetItem(deviceText);
    deviceItem->setToolTip(QStringLiteral("%1 %2\nUUID: %3")
        .arg(identity.typeHex(), identity.versionHex(), identity.uuid.isEmpty() ? QStringLiteral("—") : identity.uuid));
    mDiscoveryTable->setItem(row, DiscoveryDevice, deviceItem);

    QTableWidgetItem* number = new QTableWidgetItem(
        identity.serialNumber.isEmpty() ? QStringLiteral("—") : identity.serialNumber);
    if (identity.serialNumber.isEmpty())
        number->setToolTip(QStringLiteral("Номер устройства не получен\nID: %1").arg(identity.id));
    mDiscoveryTable->setItem(row, DiscoveryNumber, number);
    mDiscoveryTable->setItem(row, DiscoveryAddress,
        new QTableWidgetItem(identity.modbusAddress > 0 ? QString::number(identity.modbusAddress) : QString()));
    mDiscoveryTable->setItem(row, DiscoveryChannel,
        new QTableWidgetItem(QStringLiteral("%1 %2").arg(identity.channel, identity.endpoint)));
    QTableWidgetItem* firmware = new QTableWidgetItem(identity.currentFirmwareId.isEmpty()
        ? QStringLiteral("—") : identity.currentFirmwareId);
    firmware->setToolTip(firmware->text());
    mDiscoveryTable->setItem(row, DiscoveryFirmware, firmware);
    const QString stateText = identity.state == QStringLiteral("bootloader")
        ? QStringLiteral("Загрузчик")
        : identity.state == QStringLiteral("application")
            ? QStringLiteral("Приложение") : identity.state;
    QTableWidgetItem* state = new QTableWidgetItem(stateText);
    state->setToolTip(identity.state);
    state->setForeground(identity.isBootloader() ? QColor(QStringLiteral("#a15c07")) : QColor(QStringLiteral("#2563eb")));
    mDiscoveryTable->setItem(row, DiscoveryState, state);

    const QVector<ActionSpec> specs = mServices->actions().actionsForDevice(*device);
    QToolButton* actions = new QToolButton;
    actions->setObjectName(QStringLiteral("rowActions"));
    actions->setText(QStringLiteral("Действия"));
    actions->setPopupMode(QToolButton::InstantPopup);
    QMenu* menu = new QMenu(actions);
    for (const ActionSpec& spec : specs)
    {
        if (!actionHasArtifact(spec, {device}))
            continue;
        QAction* item = menu->addAction(spec.title.isEmpty() ? spec.id : spec.title);
        connect(item, &QAction::triggered, this, [this, row, id = spec.id]() {
            runActionForRow(row, id);
        });
    }
    actions->setMenu(menu);
    actions->setEnabled(!menu->isEmpty() && !isDeviceBusy(device) && !mActionBusy);
    actions->setToolTip(menu->isEmpty()
        ? QStringLiteral("Для устройства нет доступных действий")
        : QStringLiteral("Выбрать действие для устройства"));
    mDiscoveryTable->setCellWidget(row, DiscoveryActions, actions);
    mDiscoveryTable->resizeRowToContents(row);
}

QVector<std::shared_ptr<DeviceBase>> MainWindow::selectedDevices() const
{
    QVector<std::shared_ptr<DeviceBase>> selected;
    for (int row = 0; row < mDiscoveryTable->rowCount() && row < mDevices.size(); ++row)
    {
        const QTableWidgetItem* item = mDiscoveryTable->item(row, DiscoveryCheck);
        if (item && item->checkState() == Qt::Checked)
            selected.append(mDevices.at(row));
    }
    return selected;
}

ActionSpec MainWindow::actionById(const QString& actionId) const
{
    for (const ActionSpec& action : mServices->actions().allActions())
    {
        if (action.id == actionId)
            return action;
    }
    return {};
}

void MainWindow::showPingDialog(const std::shared_ptr<DeviceBase>& device)
{
    if (!device)
        return;

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Ping int[0]"));

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    const DeviceIdentity& identity = device->identity();
    QLabel* title = new QLabel(QStringLiteral("%1 %2, адрес %3")
        .arg(identity.typeHex(), identity.endpoint)
        .arg(identity.modbusAddress > 0 ? QString::number(identity.modbusAddress) : QStringLiteral("-")));
    title->setWordWrap(true);
    layout->addWidget(title);

    QPlainTextEdit* output = new QPlainTextEdit;
    output->setReadOnly(true);
    output->setMinimumHeight(180);
    layout->addWidget(output);

    QDialogButtonBox* buttons = new QDialogButtonBox;
    QPushButton* start = buttons->addButton(QStringLiteral("Начать"), QDialogButtonBox::ActionRole);
    QPushButton* cancel = buttons->addButton(QStringLiteral("Отмена"), QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);

    const auto appendPingLine = [&output](const QString& message) {
        output->appendPlainText(QStringLiteral("[%1] %2")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), message));
    };

    struct PingSession
    {
        QThread* thread = nullptr;
        PingWorker* worker = nullptr;
    };
    auto pingSession = std::make_shared<PingSession>();

    connect(start, &QPushButton::clicked, &dialog, [this, &dialog, device, pingSession, start, appendPingLine]() {
        if (pingSession->thread)
            return;
        if (isDeviceBusy(device))
        {
            appendPingLine(QStringLiteral("Устройство занято другой операцией"));
            return;
        }

        start->setEnabled(false);
        appendPingLine(QStringLiteral("Опрос запущен"));
        setDevicesBusy({device}, true);
        pingSession->thread = new QThread;
        pingSession->worker = new PingWorker(device, 500);
        pingSession->worker->moveToThread(pingSession->thread);

        connect(pingSession->thread, &QThread::started, pingSession->worker, &PingWorker::start);
        connect(pingSession->worker, &PingWorker::pingLine, &dialog, [appendPingLine](const QString& message) {
            appendPingLine(message);
        });
        connect(pingSession->worker, &PingWorker::transportLogMessage, this, &MainWindow::appendTransportLog);
        connect(pingSession->worker, &PingWorker::finished, this, [this, device]() {
            setDevicesBusy({device}, false);
        });
        connect(pingSession->worker, &PingWorker::finished, pingSession->thread, &QThread::quit);
        connect(pingSession->worker, &PingWorker::finished, pingSession->worker, &PingWorker::deleteLater);
        connect(pingSession->thread, &QThread::finished, pingSession->thread, &QThread::deleteLater);
        connect(pingSession->thread, &QThread::finished, &dialog, [pingSession]() {
            pingSession->thread = nullptr;
            pingSession->worker = nullptr;
        });

        pingSession->thread->start();
    });
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(&dialog, &QDialog::finished, &dialog, [pingSession]() {
        if (pingSession->worker)
            QMetaObject::invokeMethod(pingSession->worker, "stop", Qt::QueuedConnection);
        else if (pingSession->thread)
            pingSession->thread->quit();
    });

    dialog.exec();
}

void MainWindow::rebuildBulkMenu()
{
    if (!mBulkActionsButton)
        return;

    QMenu* menu = mBulkActionsButton->menu();
    menu->clear();
    const QVector<std::shared_ptr<DeviceBase>> selected = selectedDevices();
    bool hasBusyDevice = false;
    for (const std::shared_ptr<DeviceBase>& device : selected)
        hasBusyDevice = hasBusyDevice || isDeviceBusy(device);

    const QVector<ActionSpec> common = mServices->actions().commonActions(selected);
    for (const ActionSpec& action : common)
    {
        if (!actionHasArtifact(action, selected))
            continue;
        QAction* item = menu->addAction(action.title.isEmpty() ? action.id : action.title);
        connect(item, &QAction::triggered, this, [this, action, selected]() {
            executeAction(action, selected);
        });
    }
    mBulkActionsButton->setText(selected.isEmpty()
        ? QStringLiteral("Действия с выбранными")
        : QStringLiteral("Действия с выбранными (%1)").arg(selected.size()));
    mBulkActionsButton->setEnabled(!menu->isEmpty() && !hasBusyDevice
        && !mActionBusy && !mWorkflowThread);
    mBulkActionsButton->setToolTip(selected.isEmpty()
        ? QStringLiteral("Отметьте устройства в первом столбце")
        : (menu->isEmpty()
            ? QStringLiteral("Для выбранных устройств нет общего доступного действия")
            : QStringLiteral("Выбрать общее действие для отмеченных устройств")));
}

void MainWindow::setActionBusy(bool busy)
{
    mActionBusy = busy;
    if (mBulkActionsButton)
        mBulkActionsButton->setEnabled(false);

    const QVector<QWidget*> interactionWidgets = {
        mLineMode,
        mNetworkInterface,
        mUdpProtocol,
        mSerialPort,
        mRs485Protocol,
        mAddressStart,
        mAddressEnd,
        mDiscoveryTable
    };
    for (QWidget* widget : interactionWidgets)
    {
        if (widget)
            widget->setEnabled(!busy);
    }

    for (int row = 0; row < mDevices.size(); ++row)
        updateDeviceRow(row, mDevices.at(row));
    if (!busy)
        updateBulkMenu();
    if (mSearchButton)
        mSearchButton->setEnabled(!busy && !mDiscoveryBusy);
}

void MainWindow::setBusy(bool busy)
{
    mDiscoveryBusy = busy;
    mSearchButton->setEnabled(!busy && !mActionBusy);
    mSearchButton->setText(busy ? QStringLiteral("Идет поиск...") :
        (mLineMode->currentData().toString() == QStringLiteral("rs485") ? QStringLiteral("Поиск RS-485") : QStringLiteral("Broadcast поиск")));
}
