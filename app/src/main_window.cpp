#include "main_window.h"
#include "workers.h"

#include <QCheckBox>
#include <QDateEdit>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QMessageBox>
#include <QMetaObject>
#include <QProgressBar>
#include <QSplitter>
#include <QThread>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>

enum DiscoveryColumns
{
    DiscoveryDevice = 0,
    DiscoveryNumber,
    DiscoveryAddress,
    DiscoveryChannel,
    DiscoveryState,
    DiscoveryPing,
    DiscoveryColumnCount
};

enum FirmwareColumns
{
    FirmwareCheck = 0,
    FirmwareActions,
    FirmwareDevice,
    FirmwareNumber,
    FirmwareAddress,
    FirmwareChannel,
    FirmwareCurrent,
    FirmwareState,
    FirmwareColumnCount
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

static QWidget* tableButtonCell(QPushButton* first, QPushButton* second = nullptr)
{
    QWidget* cell = new QWidget;
    QHBoxLayout* layout = new QHBoxLayout(cell);
    layout->setContentsMargins(5, 4, 5, 4);
    layout->setSpacing(5);
    layout->addWidget(first);
    if (second)
        layout->addWidget(second);
    layout->addStretch();
    return cell;
}

static QVector<FirmwareArtifact> artifactsForTarget(const QVector<std::shared_ptr<DeviceBase>>& devices, const QString& target)
{
    QVector<FirmwareArtifact> artifacts;
    if (devices.isEmpty() || !devices.first())
        return artifacts;

    const DeviceIdentity& firstIdentity = devices.first()->identity();
    if (target == QStringLiteral("application") && !firstIdentity.firmwareVersions.isEmpty())
    {
        for (const FirmwareTransitionSpec& transition : firstIdentity.firmwareTransitions)
        {
            if (!transition.enabled || transition.from != firstIdentity.currentFirmwareId)
                continue;
            const FirmwareVersionSpec* targetFirmware = firstIdentity.firmwareVersionById(transition.to);
            if (!targetFirmware || targetFirmware->artifact.target != target)
                continue;

            bool availableForAll = true;
            for (const std::shared_ptr<DeviceBase>& device : devices)
            {
                if (!device)
                {
                    availableForAll = false;
                    break;
                }
                const FirmwareTransitionSpec* deviceTransition = device->identity().transitionTo(transition.to);
                if (!deviceTransition || !deviceTransition->enabled)
                {
                    availableForAll = false;
                    break;
                }
            }
            if (availableForAll)
                artifacts.append(targetFirmware->artifact);
        }
        return artifacts;
    }

    for (const FirmwareArtifact& artifact : firstIdentity.firmwareArtifacts)
    {
        if (artifact.target == target)
            artifacts.append(artifact);
    }

    for (int deviceIndex = 1; deviceIndex < devices.size() && !artifacts.isEmpty(); ++deviceIndex)
    {
        if (!devices.at(deviceIndex))
        {
            artifacts.clear();
            break;
        }

        const QVector<FirmwareArtifact>& otherArtifacts = devices.at(deviceIndex)->identity().firmwareArtifacts;
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
                if (other.target == target && (sameFirmwareId || samePath || sameHash))
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
    return map;
}

MainWindow::MainWindow(ServiceContainer* services, QWidget* parent) :
    QMainWindow(parent),
    mServices(services)
{
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
}

MainWindow::~MainWindow()
{
    if (mWorkflowThread && mWorkflowThread->isRunning())
    {
        mWorkflowThread->quit();
        mWorkflowThread->wait(5000);
    }

    const QSet<QThread*> uuidThreads = mUuidThreads;
    for (QThread* thread : uuidThreads)
    {
        if (!thread || !thread->isRunning())
            continue;
        thread->quit();
        thread->wait(5000);
    }
}

void MainWindow::buildUi()
{
    resize(1280, 760);
    setWindowTitle(QStringLiteral("Device Workbench"));

    QWidget* root = new QWidget(this);
    QHBoxLayout* rootLayout = new QHBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    rootLayout->addWidget(buildSidebar());

    mPages = new QStackedWidget(root);
    mPages->setObjectName(QStringLiteral("main"));
    mPages->addWidget(buildDiscoveryPage());
    mPages->addWidget(buildFirmwarePage());
    rootLayout->addWidget(mPages, 1);

    connect(mDiscoveryTabButton, &QPushButton::clicked, this, [this]() {
        mPages->setCurrentIndex(0);
        mDiscoveryTabButton->setChecked(true);
        mFirmwareTabButton->setChecked(false);
    });
    connect(mFirmwareTabButton, &QPushButton::clicked, this, [this]() {
        mPages->setCurrentIndex(1);
        mDiscoveryTabButton->setChecked(false);
        mFirmwareTabButton->setChecked(true);
    });
    mDiscoveryTabButton->setChecked(true);
    setCentralWidget(root);

    setStyleSheet(QStringLiteral(R"(
        QWidget#sidebar { background: #17212b; color: #d9e1e8; }
        QStackedWidget#main, QWidget#page { background: #f4f6f8; }
        QLabel#brandTitle { color: white; font-weight: 700; font-size: 15px; }
        QLabel#brandSub { color: #9fb0bf; font-size: 12px; }
        QLabel#h1 { color: #17212b; font-size: 22px; font-weight: 700; }
        QLabel#subtitle { color: #667584; font-size: 13px; }
        QPushButton { min-height: 34px; border: 1px solid #d8e0e5; border-radius: 7px; padding: 0 12px; background: white; color: #25313f; }
        QPushButton#primary, QPushButton#nav:checked { background: #2563eb; border-color: #2563eb; color: white; }
        QPushButton#nav { background: #17212b; border-color: #334150; color: #d9e1e8; text-align: left; }
        QPushButton#tablePing { min-width: 32px; max-width: 32px; min-height: 32px; max-height: 32px; padding: 0; background: #eff6ff; border: 1px solid #2563eb; color: #1d4ed8; }
        QPushButton#tablePing:hover { background: #dbeafe; }
        QPushButton#tableFlash { min-width: 32px; max-width: 32px; min-height: 32px; max-height: 32px; padding: 0; background: #2563eb; border: 1px solid #2563eb; color: white; }
        QPushButton#tableFlash:hover { background: #1d4ed8; border-color: #1d4ed8; }
        QPushButton#tablePing:disabled, QPushButton#tableFlash:disabled { background: #eef1f4; border-color: #c8d0d8; color: #8a98a6; }
        QFrame#band { background: white; border: 1px solid #d8e0e5; border-radius: 8px; }
        QComboBox, QLineEdit { min-height: 34px; border: 1px solid #d8e0e5; border-radius: 7px; padding: 0 8px; background: white; }
        QTableWidget { background: white; border: 0; gridline-color: #d8e0e5; selection-background-color: #edf5ff; selection-color: #17212b; alternate-background-color: #fafcff; }
        QHeaderView::section { background: #fbfcfd; color: #667584; border: 0; border-bottom: 1px solid #d8e0e5; padding: 8px; font-weight: 700; }
        QPlainTextEdit { background: #101820; color: #e5edf4; border-radius: 7px; padding: 8px; font-family: Consolas, monospace; }
        QPlainTextEdit#transportLog { background: #0d1320; color: #d1e7ff; border: 1px solid #20314f; }
    )"));

    updateLineMode();
}

QWidget* MainWindow::buildSidebar()
{
    QWidget* sidebar = new QWidget;
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(248);
    QVBoxLayout* layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(16, 20, 16, 20);
    layout->setSpacing(18);

    QLabel* brandTitle = new QLabel(QStringLiteral("Device Workbench"));
    brandTitle->setObjectName(QStringLiteral("brandTitle"));
    QLabel* brandSub = new QLabel(QStringLiteral("UDP / RS-485"));
    brandSub->setObjectName(QStringLiteral("brandSub"));
    layout->addWidget(brandTitle);
    layout->addWidget(brandSub);

    mDiscoveryTabButton = new QPushButton(QStringLiteral("Обнаружение"));
    mDiscoveryTabButton->setObjectName(QStringLiteral("nav"));
    mDiscoveryTabButton->setCheckable(true);
    mFirmwareTabButton = new QPushButton(QStringLiteral("Прошивки"));
    mFirmwareTabButton->setObjectName(QStringLiteral("nav"));
    mFirmwareTabButton->setCheckable(true);
    layout->addWidget(mDiscoveryTabButton);
    layout->addWidget(mFirmwareTabButton);
    layout->addStretch();
    return sidebar;
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
    layout->addWidget(buildDiscoveryTablePanel(), 1);
    return page;
}

QWidget* MainWindow::buildFirmwarePage()
{
    QWidget* page = new QWidget;
    page->setObjectName(QStringLiteral("page"));
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(22, 16, 22, 18);
    layout->setSpacing(14);

    QHBoxLayout* header = new QHBoxLayout;
    QVBoxLayout* title = new QVBoxLayout;
    QLabel* h1 = new QLabel(QStringLiteral("Прошивки"));
    h1->setObjectName(QStringLiteral("h1"));
    QLabel* subtitle = new QLabel(QStringLiteral("Прошивка отдельных устройств или группы по общим доступным переходам."));
    subtitle->setObjectName(QStringLiteral("subtitle"));
    title->addWidget(h1);
    title->addWidget(subtitle);
    header->addLayout(title);
    header->addStretch();

    mFlashProgress = new QProgressBar;
    mFlashProgress->setRange(0, 100);
    mFlashProgress->setValue(0);
    mFlashProgress->setFixedWidth(180);
    mFlashProgress->setVisible(false);
    mFlashProgress->setFormat(QStringLiteral("Flash %p%"));
    header->addWidget(mFlashProgress);
    layout->addLayout(header);
    layout->addWidget(buildFirmwareTablePanel(), 3);

    QSplitter* logs = new QSplitter(Qt::Horizontal);
    QWidget* operationPane = new QWidget;
    QVBoxLayout* operationLayout = new QVBoxLayout(operationPane);
    operationLayout->setContentsMargins(0, 0, 0, 0);
    QLabel* operationLabel = new QLabel(QStringLiteral("Журнал операций"));
    operationLabel->setObjectName(QStringLiteral("subtitle"));
    operationLayout->addWidget(operationLabel);
    mLog = new QPlainTextEdit;
    mLog->setReadOnly(true);
    mLog->setPlaceholderText(QStringLiteral("Операции обнаружения и прошивки"));
    operationLayout->addWidget(mLog);
    logs->addWidget(operationPane);

    QWidget* transportPane = new QWidget;
    QVBoxLayout* transportLayout = new QVBoxLayout(transportPane);
    transportLayout->setContentsMargins(0, 0, 0, 0);
    QLabel* transportLabel = new QLabel(QStringLiteral("Сырой транспорт"));
    transportLabel->setObjectName(QStringLiteral("subtitle"));
    transportLayout->addWidget(transportLabel);
    mTransportLog = new QPlainTextEdit;
    mTransportLog->setReadOnly(true);
    mTransportLog->setPlaceholderText(QStringLiteral("TX/RX ASCII packets"));
    mTransportLog->setObjectName(QStringLiteral("transportLog"));
    transportLayout->addWidget(mTransportLog);
    logs->addWidget(transportPane);
    logs->setSizes({620, 420});
    layout->addWidget(logs, 2);
    return page;
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
        label->setFixedWidth(160);
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
    buttonSpacer->setFixedWidth(160);
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
    filterLayout->addStretch();
    layout->addWidget(filter);

    mDiscoveryTable = new QTableWidget(0, DiscoveryColumnCount);
    mDiscoveryTable->setHorizontalHeaderLabels({
        QStringLiteral("Device"), QStringLiteral("Number"), QStringLiteral("Address"),
        QStringLiteral("Channel"), QStringLiteral("State"), QStringLiteral("Ping")
    });
    mDiscoveryTable->verticalHeader()->setVisible(false);
    mDiscoveryTable->verticalHeader()->setMinimumSectionSize(54);
    mDiscoveryTable->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    mDiscoveryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    mDiscoveryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mDiscoveryTable->setAlternatingRowColors(true);
    mDiscoveryTable->setWordWrap(true);
    QHeaderView* header = mDiscoveryTable->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setStretchLastSection(false);
    header->setMinimumSectionSize(48);
    mDiscoveryTable->setColumnWidth(DiscoveryDevice, 440);
    mDiscoveryTable->setColumnWidth(DiscoveryNumber, 105);
    mDiscoveryTable->setColumnWidth(DiscoveryAddress, 85);
    mDiscoveryTable->setColumnWidth(DiscoveryChannel, 220);
    mDiscoveryTable->setColumnWidth(DiscoveryState, 105);
    mDiscoveryTable->setColumnWidth(DiscoveryPing, 90);
    mDiscoveryTable->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    connect(header, &QHeaderView::sectionResized, mDiscoveryTable, [this]() {
        mDiscoveryTable->resizeRowsToContents();
    });
    layout->addWidget(mDiscoveryTable, 1);
    return frame;
}

QWidget* MainWindow::buildFirmwareTablePanel()
{
    QFrame* frame = new QFrame;
    frame->setObjectName(QStringLiteral("band"));
    QVBoxLayout* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QWidget* toolbar = new QWidget;
    QHBoxLayout* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(14, 12, 14, 12);
    toolbarLayout->addWidget(new QLabel(QStringLiteral("<b>Все обнаруженные устройства</b>")));
    toolbarLayout->addStretch();
    mBulkFlashButton = new QPushButton(QStringLiteral("Прошить выбранные"));
    mBulkFlashButton->setObjectName(QStringLiteral("primary"));
    mBulkFlashButton->setAttribute(Qt::WA_AlwaysShowToolTips);
    toolbarLayout->addWidget(mBulkFlashButton);
    layout->addWidget(toolbar);

    mFirmwareTable = new QTableWidget(0, FirmwareColumnCount);
    mFirmwareTable->setHorizontalHeaderLabels({
        QString(), QString(), QStringLiteral("Device"), QStringLiteral("Number"),
        QStringLiteral("Address"), QStringLiteral("Channel"), QStringLiteral("Firmware"),
        QStringLiteral("State")
    });
    mFirmwareTable->verticalHeader()->setVisible(false);
    mFirmwareTable->verticalHeader()->setMinimumSectionSize(54);
    mFirmwareTable->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    mFirmwareTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    mFirmwareTable->setSelectionMode(QAbstractItemView::NoSelection);
    mFirmwareTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mFirmwareTable->setAlternatingRowColors(true);
    mFirmwareTable->setWordWrap(true);
    QHeaderView* header = mFirmwareTable->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setStretchLastSection(false);
    header->setMinimumSectionSize(42);
    mFirmwareTable->setColumnWidth(FirmwareCheck, 42);
    mFirmwareTable->setColumnWidth(FirmwareActions, 90);
    mFirmwareTable->setColumnWidth(FirmwareDevice, 420);
    mFirmwareTable->setColumnWidth(FirmwareNumber, 95);
    mFirmwareTable->setColumnWidth(FirmwareAddress, 78);
    mFirmwareTable->setColumnWidth(FirmwareChannel, 180);
    mFirmwareTable->setColumnWidth(FirmwareCurrent, 205);
    mFirmwareTable->setColumnWidth(FirmwareState, 95);
    mFirmwareTable->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    layout->addWidget(mFirmwareTable, 1);

    connect(mFirmwareTable, &QTableWidget::itemChanged, this, &MainWindow::updateBulkMenu);
    connect(header, &QHeaderView::sectionResized, mFirmwareTable, [this]() {
        mFirmwareTable->resizeRowsToContents();
    });
    connect(mBulkFlashButton, &QPushButton::clicked, this, [this]() {
        const QVector<std::shared_ptr<DeviceBase>> selected = selectedDevices();
        if (!selected.isEmpty())
            executeAction(actionById(QStringLiteral("flash.application.write")), selected);
    });
    return frame;
}

void MainWindow::startDiscovery()
{
    ++mDiscoveryGeneration;
    mDevices.clear();
    mDiscoveryTable->setRowCount(0);
    mFirmwareTable->setRowCount(0);
    updateBulkMenu();
    setBusy(true);

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
    device = mServices->catalog().enrich(device);
    std::shared_ptr<DeviceBase> deviceObject = mDeviceFactory.create(device);
    if (!deviceObject)
        return;

    const QString endpointKey = QStringLiteral("%1|%2")
        .arg(mDiscoveryGeneration)
        .arg(device.endpoint);
    if (mPendingUuidEndpoints.contains(endpointKey))
        return;

    const quint64 requestId = mNextUuidRequestId++;
    mPendingUuidEndpoints.insert(endpointKey);
    mPendingUuidReads.insert(requestId, {deviceObject, mDiscoveryGeneration, endpointKey});

    QThread* thread = new QThread;
    thread->setObjectName(QStringLiteral("uuid-%1").arg(requestId));
    UuidWorker* worker = new UuidWorker(requestId, deviceObject);
    worker->moveToThread(thread);
    mUuidThreads.insert(thread);

    connect(thread, &QThread::started, worker, &UuidWorker::run);
    connect(worker, &UuidWorker::finished, this, &MainWindow::onUuidReadFinished);
    connect(worker, &UuidWorker::finished, thread, &QThread::quit);
    connect(worker, &UuidWorker::finished, worker, &UuidWorker::deleteLater);
    connect(thread, &QThread::finished, this, [this, thread]() {
        mUuidThreads.remove(thread);
    });
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
}

void MainWindow::onUuidReadFinished(quint64 requestId,
    const QString& uuid,
    const QString& error,
    const QString& rawResponse)
{
    const auto pendingIt = mPendingUuidReads.find(requestId);
    if (pendingIt == mPendingUuidReads.end())
        return;

    const PendingUuidRead pending = pendingIt.value();
    mPendingUuidReads.erase(pendingIt);
    mPendingUuidEndpoints.remove(pending.endpointKey);
    if (!pending.device || pending.discoveryGeneration != mDiscoveryGeneration)
        return;

    DeviceIdentity device = pending.device->identity();
    if (!uuid.isEmpty())
    {
        device.uuid = uuid;
        device.id = uuid;
        pending.device->updateIdentity(device);
        appendLog(QStringLiteral("[%1] UUID %2").arg(device.typeHex(), device.uuid));
    }
    else
    {
        appendLog(QStringLiteral("[%1] UUID read failed at %2: %3")
            .arg(device.typeHex(), device.endpoint, error));
    }
    if (!rawResponse.isEmpty())
        appendTransportLog(QStringLiteral("[%1] %2").arg(device.typeHex(), rawResponse));

    mergeDiscoveredDevice(pending.device);

    bool hasPendingCurrentDiscovery = false;
    for (auto it = mPendingUuidReads.cbegin(); it != mPendingUuidReads.cend(); ++it)
    {
        if (it.value().discoveryGeneration == mDiscoveryGeneration)
        {
            hasPendingCurrentDiscovery = true;
            break;
        }
    }
    if (!hasPendingCurrentDiscovery)
        appendLog(QStringLiteral("Found %1 device(s); UUID identification finished").arg(mDevices.size()));
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
            if (mDevices.at(i)->className() == deviceObject->className())
                mDevices.at(i)->updateIdentity(device);
            else
                mDevices[i] = deviceObject;
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
    for (auto it = mPendingUuidReads.cbegin(); it != mPendingUuidReads.cend(); ++it)
    {
        if (it.value().discoveryGeneration == mDiscoveryGeneration)
            ++pendingCount;
    }
    appendLog(pendingCount > 0
        ? QStringLiteral("Discovery finished; identifying UUID for %1 device(s)").arg(pendingCount)
        : QStringLiteral("Found %1 device(s)").arg(mDevices.size()));
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
    WorkflowWorker* worker = new WorkflowWorker(&mServices->workflows(), action, devices, parameters);
    worker->moveToThread(thread);

    connect(thread, &QThread::started, worker, &WorkflowWorker::run);
    connect(worker, &WorkflowWorker::logMessage, this, &MainWindow::appendLog);
    connect(worker, &WorkflowWorker::transportLogMessage, this, &MainWindow::appendTransportLog);
    connect(worker, &WorkflowWorker::progressChanged, this, &MainWindow::onWorkflowProgress);
    connect(worker, &WorkflowWorker::finished, this, [this, devices](bool) {
        for (const std::shared_ptr<DeviceBase>& device : devices)
        {
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
    connect(thread, &QThread::finished, this, [this, thread, devices]() {
        if (mWorkflowThread == thread)
            mWorkflowThread = nullptr;
        setDevicesBusy(devices, false);
        setActionBusy(false);
    });

    mWorkflowThread = thread;
    setDevicesBusy(devices, true);
    setActionBusy(true);
    thread->start();
}

void MainWindow::appendLog(const QString& message)
{
    mLog->appendPlainText(QStringLiteral("[%1] %2")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), message));
}

void MainWindow::appendTransportLog(const QString& message)
{
    if (!mTransportLog)
        return;

    mTransportLog->appendPlainText(QStringLiteral("[%1] %2")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), message));
}

void MainWindow::onWorkflowProgress(int percent)
{
    if (!mFlashProgress)
        return;

    mFlashProgress->setVisible(true);
    mFlashProgress->setValue(qBound(0, percent, 100));
}

bool MainWindow::prepareActionInvocation(const ActionSpec& action, const QVector<std::shared_ptr<DeviceBase>>& devices, QVariantMap* parameters)
{
    if (!parameters)
        return false;

    parameters->clear();

    if (action.id == QStringLiteral("device.productionDate.update"))
    {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Обновить дату производства"));

        QVBoxLayout* layout = new QVBoxLayout(&dialog);
        QLabel* intro = new QLabel(QStringLiteral("Будет выполнен вход в bootloader, затем записан timestamp в регистр даты, который задаётся устройством."));
        intro->setWordWrap(true);
        layout->addWidget(intro);

        QLabel* countLabel = new QLabel(QStringLiteral("Устройств для обработки: %1").arg(devices.size()));
        layout->addWidget(countLabel);

        QGridLayout* grid = new QGridLayout;
        QLabel* dateLabel = new QLabel(QStringLiteral("Дата производства"));
        QDateEdit* dateEdit = new QDateEdit(QDate::currentDate());
        dateEdit->setCalendarPopup(true);
        dateEdit->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
        dateEdit->setMinimumDate(QDate(1970, 1, 1));
        dateEdit->setMaximumDate(QDate(2099, 12, 31));
        grid->addWidget(dateLabel, 0, 0);
        grid->addWidget(dateEdit, 0, 1);
        layout->addLayout(grid);

        QLabel* details = new QLabel(QStringLiteral("Схема: reset -> wait for (Boot) -> int[0] = 0 -> int[dateIndex] = timestamp -> pause 1s -> int[0] = 1."));
        details->setWordWrap(true);
        layout->addWidget(details);

        QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Продолжить"));
        buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Отмена"));
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);

        if (dialog.exec() != QDialog::Accepted)
            return false;

        parameters->insert(QStringLiteral("productionDate"), dateEdit->date());
        return true;
    }

    if (action.id == QStringLiteral("device.serialNumber.update"))
    {
        if (devices.size() != 1 || !devices.first())
            return false;

        const DeviceIdentity& identity = devices.first()->identity();

        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Change device number"));

        QVBoxLayout* layout = new QVBoxLayout(&dialog);
        QLabel* intro = new QLabel(QStringLiteral("The number will be written in bootloader mode. BOC-V-12 uses int[10]."));
        intro->setWordWrap(true);
        layout->addWidget(intro);

        QLabel* deviceLabel = new QLabel(QStringLiteral("%1 %2, %3")
            .arg(identity.typeHex(), identity.versionHex(), identity.endpoint));
        deviceLabel->setWordWrap(true);
        layout->addWidget(deviceLabel);

        QFormLayout* form = new QFormLayout;
        QSpinBox* serialEdit = new QSpinBox;
        serialEdit->setRange(0, 999999);
        serialEdit->setValue(identity.serialNumber.toInt());
        form->addRow(QStringLiteral("New number"), serialEdit);
        layout->addLayout(form);

        QLabel* details = new QLabel(QStringLiteral("Flow: if needed reset -> wait for (Boot) -> int[0] = 0 -> int[serialIndex] = number -> pause 1s -> int[0] = 1."));
        details->setWordWrap(true);
        layout->addWidget(details);

        QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Continue"));
        buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Cancel"));
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);

        if (dialog.exec() != QDialog::Accepted)
            return false;

        parameters->insert(QStringLiteral("serialNumber"), serialEdit->value());
        return true;
    }

    if (isFlashAction(action.id))
    {
        const QString title = action.title.isEmpty() ? action.id : action.title;
        const QVector<FirmwareArtifact> artifacts = artifactsForTarget(devices, action.target);
        const bool graphControlled = action.target == QStringLiteral("application")
            && devices.first() && !devices.first()->identity().firmwareVersions.isEmpty();

        QDialog dialog(this);
        dialog.setWindowTitle(title);

        QVBoxLayout* layout = new QVBoxLayout(&dialog);
        QLabel* intro = new QLabel(graphControlled
            ? (devices.size() == 1
                ? QStringLiteral("Текущая прошивка: %1. Выберите разрешённый переход.")
                    .arg(devices.first()->identity().currentFirmwareId.isEmpty()
                        ? QStringLiteral("не определена")
                        : devices.first()->identity().currentFirmwareId)
                : QStringLiteral("Выберите переход, разрешённый для всех %1 устройств.").arg(devices.size()))
            : (devices.size() == 1
                ? QStringLiteral("Выберите файл прошивки для 1 устройства.")
                : QStringLiteral("Выберите файл прошивки для %1 устройств.").arg(devices.size())));
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
                ? QStringLiteral("Нет разрешённых переходов")
                : QStringLiteral("Нет прошивки в каталоге"), -1);
        artifactCombo->setCurrentIndex(defaultIndex);
        form->addRow(QStringLiteral("Прошивка"), artifactCombo);

        QLineEdit* customFile = new QLineEdit;
        customFile->setReadOnly(true);
        QPushButton* browse = new QPushButton(QStringLiteral("Выбрать файл..."));
        customFile->setEnabled(!graphControlled);
        browse->setEnabled(!graphControlled);
        QHBoxLayout* fileRow = new QHBoxLayout;
        fileRow->addWidget(customFile, 1);
        fileRow->addWidget(browse);
        form->addRow(QStringLiteral("Другой файл"), fileRow);

        QCheckBox* verify = new QCheckBox(QStringLiteral("Проверить после записи"));
        verify->setChecked(true);
        verify->setVisible(!graphControlled);
        form->addRow(QString(), verify);
        layout->addLayout(form);

        QLabel* warning = new QLabel(QStringLiteral("Запись application flash выполняется через bootloader block flash: pages are written with command 0x43, verify reads back with 0x44."));
        if (graphControlled)
            warning->setText(QStringLiteral("Порядок reset, flash, verify, restart и ожидания application задаётся выбранным переходом в графе прошивок."));
        warning->setWordWrap(true);
        layout->addWidget(warning);

        connect(browse, &QPushButton::clicked, &dialog, [customFile]() {
            const QString fileName = QFileDialog::getOpenFileName(nullptr,
                QStringLiteral("Выбрать прошивку"),
                QString(),
                QStringLiteral("Firmware (*.bin *.hex *.ldr);;All files (*.*)"));
            if (!fileName.isEmpty())
                customFile->setText(fileName);
        });

        QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Прошить"));
        buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Отмена"));
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!artifacts.isEmpty() || !graphControlled);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);

        if (dialog.exec() != QDialog::Accepted)
            return false;

        FirmwareArtifact selected;
        const int selectedIndex = artifactCombo->currentData().toInt();
        if (selectedIndex >= 0 && selectedIndex < artifacts.size())
            selected = artifacts.at(selectedIndex);
        if (!graphControlled && !customFile->text().isEmpty())
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
            parameters->insert(QStringLiteral("verifyAfterWrite"), verify->isChecked());
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
    mFirmwareTable->insertRow(row);
    updateDeviceRow(row, device);
}

void MainWindow::updateDeviceRow(int row, const std::shared_ptr<DeviceBase>& device)
{
    updateDiscoveryDeviceRow(row, device);
    updateFirmwareDeviceRow(row, device);
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

    const DeviceIdentity& identity = device->identity();
    const QString deviceText = identity.description.isEmpty()
        ? identity.name
        : QStringLiteral("%1\n%2").arg(identity.name, identity.description);
    QTableWidgetItem* deviceItem = new QTableWidgetItem(deviceText);
    deviceItem->setToolTip(QStringLiteral("%1 %2\nUUID: %3")
        .arg(identity.typeHex(), identity.versionHex(), identity.uuid.isEmpty() ? QStringLiteral("—") : identity.uuid));
    mDiscoveryTable->setItem(row, DiscoveryDevice, deviceItem);

    const QString numberText = !identity.serialNumber.isEmpty() ? identity.serialNumber : identity.id;
    mDiscoveryTable->setItem(row, DiscoveryNumber, new QTableWidgetItem(numberText));
    mDiscoveryTable->setItem(row, DiscoveryAddress,
        new QTableWidgetItem(identity.modbusAddress > 0 ? QString::number(identity.modbusAddress) : QString()));
    mDiscoveryTable->setItem(row, DiscoveryChannel,
        new QTableWidgetItem(QStringLiteral("%1 %2").arg(identity.channel, identity.endpoint)));
    QTableWidgetItem* state = new QTableWidgetItem(identity.state);
    state->setForeground(identity.isBootloader() ? QColor(QStringLiteral("#a15c07")) : QColor(QStringLiteral("#2563eb")));
    mDiscoveryTable->setItem(row, DiscoveryState, state);

    const QVector<ActionSpec> specs = mServices->actions().actionsForDevice(identity);
    bool canPing = false;
    for (const ActionSpec& spec : specs)
        canPing = canPing || spec.id == QStringLiteral("device.ping");
    const bool deviceBusy = isDeviceBusy(device);
    QPushButton* ping = new QPushButton;
    ping->setObjectName(QStringLiteral("tablePing"));
    ping->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    ping->setIconSize(QSize(18, 18));
    ping->setAccessibleName(QStringLiteral("Ping"));
    ping->setToolTip(deviceBusy
        ? QStringLiteral("Дождитесь завершения текущей операции с устройством")
        : QStringLiteral("Проверить связь с устройством"));
    ping->setEnabled(canPing && !deviceBusy);
    connect(ping, &QPushButton::clicked, this, [this, row]() {
        runActionForRow(row, QStringLiteral("device.ping"));
    });
    mDiscoveryTable->setCellWidget(row, DiscoveryPing, tableButtonCell(ping));
    mDiscoveryTable->resizeRowToContents(row);
}

void MainWindow::updateFirmwareDeviceRow(int row, const std::shared_ptr<DeviceBase>& device)
{
    if (!device || row < 0 || row >= mFirmwareTable->rowCount())
        return;

    const DeviceIdentity& identity = device->identity();
    const Qt::CheckState checkState = mFirmwareTable->item(row, FirmwareCheck)
        ? mFirmwareTable->item(row, FirmwareCheck)->checkState()
        : Qt::Unchecked;
    QTableWidgetItem* check = new QTableWidgetItem;
    check->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
    check->setTextAlignment(Qt::AlignCenter);
    check->setToolTip(QStringLiteral("Выбрать устройство для массовой прошивки"));
    check->setCheckState(checkState);
    mFirmwareTable->setItem(row, FirmwareCheck, check);

    const QString deviceText = identity.description.isEmpty()
        ? identity.name
        : QStringLiteral("%1\n%2").arg(identity.name, identity.description);
    QTableWidgetItem* deviceItem = new QTableWidgetItem(deviceText);
    deviceItem->setToolTip(QStringLiteral("%1 %2\nUUID: %3")
        .arg(identity.typeHex(), identity.versionHex(), identity.uuid.isEmpty() ? QStringLiteral("—") : identity.uuid));
    mFirmwareTable->setItem(row, FirmwareDevice, deviceItem);
    const QString numberText = !identity.serialNumber.isEmpty() ? identity.serialNumber : identity.id;
    QTableWidgetItem* number = new QTableWidgetItem(numberText);
    if (identity.serialNumber.isEmpty())
        number->setToolTip(QStringLiteral("ID %1").arg(identity.id));
    mFirmwareTable->setItem(row, FirmwareNumber, number);
    QTableWidgetItem* address = new QTableWidgetItem(identity.modbusAddress > 0 ? QString::number(identity.modbusAddress) : QString());
    address->setToolTip(QStringLiteral("Modbus address"));
    mFirmwareTable->setItem(row, FirmwareAddress, address);
    mFirmwareTable->setItem(row, FirmwareChannel,
        new QTableWidgetItem(QStringLiteral("%1 %2").arg(identity.channel, identity.endpoint)));
    mFirmwareTable->setItem(row, FirmwareCurrent,
        new QTableWidgetItem(identity.currentFirmwareId.isEmpty() ? QStringLiteral("—") : identity.currentFirmwareId));
    QTableWidgetItem* state = new QTableWidgetItem(identity.state);
    state->setForeground(identity.isBootloader() ? QColor(QStringLiteral("#a15c07")) : QColor(QStringLiteral("#2563eb")));
    mFirmwareTable->setItem(row, FirmwareState, state);

    const QVector<ActionSpec> specs = mServices->actions().actionsForDevice(identity);
    bool canPing = false;
    bool supportsFlash = false;
    for (const ActionSpec& spec : specs)
    {
        canPing = canPing || spec.id == QStringLiteral("device.ping");
        supportsFlash = supportsFlash || spec.id == QStringLiteral("flash.application.write");
    }
    const bool hasFirmware = !artifactsForTarget({device}, QStringLiteral("application")).isEmpty();
    const bool deviceBusy = isDeviceBusy(device);
    const bool canFlash = supportsFlash && hasFirmware && !deviceBusy && !mWorkflowThread;

    QPushButton* ping = new QPushButton;
    ping->setObjectName(QStringLiteral("tablePing"));
    ping->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    ping->setIconSize(QSize(18, 18));
    ping->setAccessibleName(QStringLiteral("Ping"));
    ping->setToolTip(deviceBusy
        ? QStringLiteral("Дождитесь завершения текущей операции с устройством")
        : QStringLiteral("Проверить связь с устройством"));
    ping->setEnabled(canPing && !deviceBusy);
    connect(ping, &QPushButton::clicked, this, [this, row]() {
        runActionForRow(row, QStringLiteral("device.ping"));
    });
    QPushButton* flash = new QPushButton;
    flash->setObjectName(QStringLiteral("tableFlash"));
    flash->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
    flash->setIconSize(QSize(18, 18));
    flash->setAccessibleName(QStringLiteral("Прошить"));
    flash->setAttribute(Qt::WA_AlwaysShowToolTips);
    if (deviceBusy)
        flash->setToolTip(QStringLiteral("Дождитесь завершения текущей операции с устройством"));
    else if (mWorkflowThread)
        flash->setToolTip(QStringLiteral("Дождитесь завершения текущей операции прошивки"));
    else if (!supportsFlash)
        flash->setToolTip(QStringLiteral("Прошивка недоступна для типа или текущего состояния устройства"));
    else if (!hasFirmware)
        flash->setToolTip(QStringLiteral("Нет подходящих прошивок для текущей версии устройства"));
    else
        flash->setToolTip(QStringLiteral("Выбрать доступную прошивку и записать её в устройство"));
    flash->setEnabled(canFlash);
    connect(flash, &QPushButton::clicked, this, [this, row]() {
        runActionForRow(row, QStringLiteral("flash.application.write"));
    });
    mFirmwareTable->setCellWidget(row, FirmwareActions, tableButtonCell(ping, flash));
    mFirmwareTable->resizeRowToContents(row);
}

QVector<std::shared_ptr<DeviceBase>> MainWindow::selectedDevices() const
{
    QVector<std::shared_ptr<DeviceBase>> selected;
    for (int row = 0; row < mFirmwareTable->rowCount() && row < mDevices.size(); ++row)
    {
        const QTableWidgetItem* item = mFirmwareTable->item(row, FirmwareCheck);
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
    if (!mBulkFlashButton)
        return;

    const QVector<std::shared_ptr<DeviceBase>> selected = selectedDevices();
    const QVector<ActionSpec> actions = mServices->actions().commonActions(selected);
    bool hasApplicationFlash = false;
    for (const ActionSpec& action : actions)
        hasApplicationFlash = hasApplicationFlash || action.id == QStringLiteral("flash.application.write");
    bool hasBusyDevice = false;
    for (const std::shared_ptr<DeviceBase>& device : selected)
        hasBusyDevice = hasBusyDevice || isDeviceBusy(device);

    const bool hasCommonFirmware = hasApplicationFlash
        && !artifactsForTarget(selected, QStringLiteral("application")).isEmpty();
    mBulkFlashButton->setText(selected.isEmpty()
        ? QStringLiteral("Прошить выбранные")
        : QStringLiteral("Прошить выбранные (%1)").arg(selected.size()));
    if (selected.isEmpty())
        mBulkFlashButton->setToolTip(QStringLiteral("Выберите устройства флажками в первом столбце"));
    else if (hasBusyDevice)
        mBulkFlashButton->setToolTip(QStringLiteral("Дождитесь завершения операции с выбранным устройством"));
    else if (!hasApplicationFlash)
        mBulkFlashButton->setToolTip(QStringLiteral("Прошивка недоступна для одного или нескольких выбранных устройств"));
    else if (!hasCommonFirmware)
        mBulkFlashButton->setToolTip(QStringLiteral("Для выбранных устройств нет общей подходящей прошивки"));
    else
        mBulkFlashButton->setToolTip(QStringLiteral("Выбрать общую прошивку для отмеченных устройств"));
    mBulkFlashButton->setEnabled(hasCommonFirmware && !hasBusyDevice && !mWorkflowThread);
}

void MainWindow::setActionBusy(bool busy)
{
    if (mBulkFlashButton)
        mBulkFlashButton->setEnabled(false);
    for (int row = 0; row < mDevices.size(); ++row)
        updateDeviceRow(row, mDevices.at(row));
    if (!busy)
        rebuildBulkMenu();
    if (mSearchButton)
        mSearchButton->setEnabled(!busy);
    if (mFlashProgress && !busy && mFlashProgress->value() >= 100)
        mFlashProgress->setVisible(false);
}

void MainWindow::setBusy(bool busy)
{
    mSearchButton->setEnabled(!busy);
    mSearchButton->setText(busy ? QStringLiteral("Идет поиск...") :
        (mLineMode->currentData().toString() == QStringLiteral("rs485") ? QStringLiteral("Поиск RS-485") : QStringLiteral("Broadcast поиск")));
}
