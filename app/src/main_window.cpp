#include "main_window.h"
#include "workers.h"

#include <QAction>
#include <QCheckBox>
#include <QDateEdit>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QProgressBar>
#include <QSplitter>
#include <QThread>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

enum Columns
{
    ColCheck = 0,
    ColDevice,
    ColNumber,
    ColAddress,
    ColChannel,
    ColType,
    ColVersion,
    ColState,
    ColDescription,
    ColStatus,
    ColActions,
    ColCount
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

    QWidget* main = new QWidget(root);
    main->setObjectName(QStringLiteral("main"));
    QVBoxLayout* mainLayout = new QVBoxLayout(main);
    mainLayout->setContentsMargins(22, 16, 22, 18);
    mainLayout->setSpacing(14);

    QHBoxLayout* header = new QHBoxLayout;
    QVBoxLayout* title = new QVBoxLayout;
    QLabel* h1 = new QLabel(QStringLiteral("Обнаружение и обслуживание устройств"));
    h1->setObjectName(QStringLiteral("h1"));
    QLabel* subtitle = new QLabel(QStringLiteral("Выбор линии связи, протокола и последовательный поиск устройств только в текущем контексте."));
    subtitle->setObjectName(QStringLiteral("subtitle"));
    title->addWidget(h1);
    title->addWidget(subtitle);
    header->addLayout(title);
    header->addStretch();
    QPushButton* refresh = new QPushButton(QStringLiteral("Обновить сценарии"));
    connect(refresh, &QPushButton::clicked, this, [this]() {
        QString error;
        if (!mServices->reloadWorkflows(&error))
        {
            appendLog(QStringLiteral("Workflow reload failed: %1").arg(error));
            QMessageBox::warning(this, QStringLiteral("Сценарии"), error);
            return;
        }
        appendLog(QStringLiteral("Workflow scenarios reloaded"));
    });
    header->addWidget(refresh);
    mFlashProgress = new QProgressBar;
    mFlashProgress->setRange(0, 100);
    mFlashProgress->setValue(0);
    mFlashProgress->setFixedWidth(180);
    mFlashProgress->setVisible(false);
    mFlashProgress->setFormat(QStringLiteral("Flash %p%"));
    header->addWidget(mFlashProgress);
    mainLayout->addLayout(header);

    mainLayout->addWidget(buildDiscoveryPanel());
    mainLayout->addWidget(buildTablePanel(), 1);

    mLog = new QPlainTextEdit;
    mLog->setReadOnly(true);
    mLog->setMaximumHeight(130);
    mLog->setPlaceholderText(QStringLiteral("Журнал операций"));
    mainLayout->addWidget(mLog);

    QLabel* transportLabel = new QLabel(QStringLiteral("Сырой транспорт"));
    transportLabel->setObjectName(QStringLiteral("subtitle"));
    mainLayout->addWidget(transportLabel);
    mTransportLog = new QPlainTextEdit;
    mTransportLog->setReadOnly(true);
    mTransportLog->setMaximumHeight(110);
    mTransportLog->setPlaceholderText(QStringLiteral("TX/RX ASCII packets"));
    mTransportLog->setObjectName(QStringLiteral("transportLog"));
    mainLayout->addWidget(mTransportLog);

    rootLayout->addWidget(main, 1);
    setCentralWidget(root);

    setStyleSheet(QStringLiteral(R"(
        QWidget#sidebar { background: #17212b; color: #d9e1e8; }
        QWidget#main { background: #f4f6f8; }
        QLabel#brandTitle { color: white; font-weight: 700; font-size: 15px; }
        QLabel#brandSub { color: #9fb0bf; font-size: 12px; }
        QLabel#h1 { color: #17212b; font-size: 22px; font-weight: 700; }
        QLabel#subtitle { color: #667584; font-size: 13px; }
        QPushButton { min-height: 34px; border: 1px solid #d8e0e5; border-radius: 7px; padding: 0 12px; background: white; color: #25313f; }
        QPushButton#primary { background: #2563eb; border-color: #2563eb; color: white; }
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

    QPushButton* discovery = new QPushButton(QStringLiteral("Обнаружение"));
    discovery->setObjectName(QStringLiteral("primary"));
    QPushButton* firmware = new QPushButton(QStringLiteral("Прошивки"));
    layout->addWidget(discovery);
    layout->addWidget(firmware);
    layout->addStretch();
    return sidebar;
}

QWidget* MainWindow::buildDiscoveryPanel()
{
    QFrame* frame = new QFrame;
    frame->setObjectName(QStringLiteral("band"));
    QVBoxLayout* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(12);

    QHBoxLayout* modeRow = new QHBoxLayout;
    mLineMode = new QComboBox;
    mLineMode->addItem(QStringLiteral("UDP broadcast"), QStringLiteral("udp"));
    mLineMode->addItem(QStringLiteral("RS-485"), QStringLiteral("rs485"));
    configureCombo(mLineMode);
    modeRow->addWidget(new QLabel(QStringLiteral("Линия связи")));
    modeRow->addWidget(mLineMode);
    modeRow->addStretch();
    layout->addLayout(modeRow);

    mUdpPanel = new QWidget;
    QHBoxLayout* udp = new QHBoxLayout(mUdpPanel);
    udp->setContentsMargins(0, 0, 0, 0);
    mNetworkInterface = new QComboBox;
    mNetworkInterface->addItems(availableNetworkInterfaces());
    configureCombo(mNetworkInterface);
    mUdpProtocol = new QComboBox;
    mUdpProtocol->addItem(QStringLiteral("Unicorn ASCII · FINE / 0xFF"), QStringLiteral("unicorn-ascii"));
    mUdpProtocol->addItem(QStringLiteral("Modbus RTU · RS-485"), QStringLiteral("modbus-rtu"));
    configureCombo(mUdpProtocol);
    udp->addWidget(new QLabel(QStringLiteral("Сетевой интерфейс")));
    udp->addWidget(mNetworkInterface, 1);
    udp->addWidget(new QLabel(QStringLiteral("Протокол")));
    udp->addWidget(mUdpProtocol, 1);
    layout->addWidget(mUdpPanel);

    mRs485Panel = new QWidget;
    QHBoxLayout* rs = new QHBoxLayout(mRs485Panel);
    rs->setContentsMargins(0, 0, 0, 0);
    mSerialPort = new QComboBox;
    mSerialPort->addItems(availableSerialPorts());
    configureCombo(mSerialPort);
    mRs485Protocol = new QComboBox;
    mRs485Protocol->addItem(QStringLiteral("Unicorn ASCII · identity 0xFF"), QStringLiteral("unicorn-ascii"));
    mRs485Protocol->addItem(QStringLiteral("Modbus RTU · read identity"), QStringLiteral("modbus-rtu"));
    configureCombo(mRs485Protocol);
    mAddressStart = new QLineEdit(QStringLiteral("1"));
    mAddressEnd = new QLineEdit(QStringLiteral("64"));
    rs->addWidget(new QLabel(QStringLiteral("Порт")));
    rs->addWidget(mSerialPort);
    rs->addWidget(new QLabel(QStringLiteral("Протокол")));
    rs->addWidget(mRs485Protocol);
    rs->addWidget(new QLabel(QStringLiteral("Адрес старт")));
    rs->addWidget(mAddressStart);
    rs->addWidget(new QLabel(QStringLiteral("Адрес конец")));
    rs->addWidget(mAddressEnd);
    layout->addWidget(mRs485Panel);

    mSearchButton = new QPushButton(QStringLiteral("Broadcast поиск"));
    mSearchButton->setObjectName(QStringLiteral("primary"));
    layout->addWidget(mSearchButton, 0, Qt::AlignRight);

    connect(mLineMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::updateLineMode);
    connect(mSearchButton, &QPushButton::clicked, this, &MainWindow::startDiscovery);
    return frame;
}

QWidget* MainWindow::buildTablePanel()
{
    QFrame* frame = new QFrame;
    frame->setObjectName(QStringLiteral("band"));
    QVBoxLayout* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QWidget* filter = new QWidget;
    QHBoxLayout* filterLayout = new QHBoxLayout(filter);
    filterLayout->setContentsMargins(14, 12, 14, 12);
    QLabel* title = new QLabel(QStringLiteral("<b>Устройства текущего поиска</b><br><span style='color:#667584'>Текущая линия и протокол</span>"));
    mSearch = new QLineEdit(QStringLiteral("Unicorn ASCII"));
    mSearch->setMaximumWidth(260);
    mBulkButton = new QPushButton(QStringLiteral("Действия выбранных"));
    mBulkButton->setObjectName(QStringLiteral("primary"));
    mBulkMenu = new QMenu(mBulkButton);
    mBulkButton->setMenu(mBulkMenu);
    filterLayout->addWidget(title);
    filterLayout->addStretch();
    filterLayout->addWidget(mSearch);
    filterLayout->addWidget(mBulkButton);
    layout->addWidget(filter);

    mTable = new QTableWidget(0, ColCount);
    mTable->setHorizontalHeaderLabels({
        QString(), QStringLiteral("Устройство"), QStringLiteral("Номер"), QStringLiteral("Канал"),
        QStringLiteral("Тип"), QStringLiteral("Версия"), QStringLiteral("Короткое описание"),
        QStringLiteral("Статус"), QStringLiteral("Действия")
    });
    mTable->verticalHeader()->setVisible(false);
    mTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    mTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mTable->setAlternatingRowColors(true);
    QHeaderView* header = mTable->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setStretchLastSection(false);
    header->setMinimumSectionSize(48);
    mTable->setColumnWidth(ColCheck, 42);
    mTable->setColumnWidth(ColDevice, 220);
    mTable->setColumnWidth(ColNumber, 96);
    mTable->setColumnWidth(ColAddress, 88);
    mTable->setColumnWidth(ColChannel, 210);
    mTable->setColumnWidth(ColType, 88);
    mTable->setColumnWidth(ColVersion, 88);
    mTable->setColumnWidth(ColState, 108);
    mTable->setColumnWidth(ColDescription, 260);
    mTable->setColumnWidth(ColStatus, 136);
    mTable->setColumnWidth(ColActions, 180);
    mTable->setHorizontalHeaderItem(ColAddress, new QTableWidgetItem(QStringLiteral("Адрес")));
    mTable->setHorizontalHeaderItem(ColDevice, new QTableWidgetItem(QStringLiteral("Device")));
    mTable->setHorizontalHeaderItem(ColNumber, new QTableWidgetItem(QStringLiteral("Number")));
    mTable->setHorizontalHeaderItem(ColAddress, new QTableWidgetItem(QStringLiteral("Address")));
    mTable->setHorizontalHeaderItem(ColChannel, new QTableWidgetItem(QStringLiteral("Channel")));
    mTable->setHorizontalHeaderItem(ColType, new QTableWidgetItem(QStringLiteral("Type")));
    mTable->setHorizontalHeaderItem(ColVersion, new QTableWidgetItem(QStringLiteral("Version")));
    mTable->setHorizontalHeaderItem(ColState, new QTableWidgetItem(QStringLiteral("State")));
    mTable->setHorizontalHeaderItem(ColDescription, new QTableWidgetItem(QStringLiteral("Description")));
    mTable->setHorizontalHeaderItem(ColStatus, new QTableWidgetItem(QStringLiteral("Status")));
    mTable->setHorizontalHeaderItem(ColActions, new QTableWidgetItem(QStringLiteral("Actions")));
    layout->addWidget(mTable, 1);

    connect(mTable, &QTableWidget::itemChanged, this, &MainWindow::updateBulkMenu);
    return frame;
}

void MainWindow::startDiscovery()
{
    mDevices.clear();
    mTable->setRowCount(0);
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

    for (int i = 0; i < mDevices.size(); ++i)
    {
        if (mDevices.at(i)->identity().id == device.id)
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
    appendLog(QStringLiteral("Found %1 device(s)").arg(mDevices.size()));
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
    connect(worker, &WorkflowWorker::finished, thread, &QThread::quit);
    connect(worker, &WorkflowWorker::finished, worker, &WorkflowWorker::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (mWorkflowThread == thread)
            mWorkflowThread = nullptr;
        setActionBusy(false);
    });

    mWorkflowThread = thread;
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
        const QString message = devices.size() == 1
            ? QStringLiteral("%1 будет выполнено для 1 устройства.\nПродолжить?").arg(title)
            : QStringLiteral("%1 будет выполнено для %2 устройств.\nПродолжить?")
                .arg(title)
                .arg(devices.size());
        return QMessageBox::question(this, title, message, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
    }

    const QString title = action.title.isEmpty() ? action.id : action.title;
    const QString message = devices.size() == 1
        ? QStringLiteral("Выполнить действие \"%1\" для 1 устройства?").arg(title)
        : QStringLiteral("Выполнить действие \"%1\" для %2 устройств?").arg(title).arg(devices.size());
    return QMessageBox::question(this, title, message, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

void MainWindow::addDeviceRow(const std::shared_ptr<DeviceBase>& device)
{
    const int row = mTable->rowCount();
    mTable->insertRow(row);
    updateDeviceRow(row, device);
}

void MainWindow::updateDeviceRow(int row, const std::shared_ptr<DeviceBase>& device)
{
    if (!device)
        return;

    const DeviceIdentity& identity = device->identity();
    const Qt::CheckState checkState = mTable->item(row, ColCheck) ? mTable->item(row, ColCheck)->checkState() : Qt::Unchecked;
    QTableWidgetItem* check = new QTableWidgetItem;
    check->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    check->setCheckState(checkState);
    mTable->setItem(row, ColCheck, check);

    mTable->setItem(row, ColDevice, new QTableWidgetItem(identity.name));
    const QString numberText = !identity.serialNumber.isEmpty() ? identity.serialNumber : identity.id;
    QTableWidgetItem* number = new QTableWidgetItem(numberText);
    if (identity.serialNumber.isEmpty())
        number->setToolTip(QStringLiteral("ID %1").arg(identity.id));
    mTable->setItem(row, ColNumber, number);
    QTableWidgetItem* address = new QTableWidgetItem(identity.modbusAddress > 0 ? QString::number(identity.modbusAddress) : QString());
    address->setToolTip(QStringLiteral("Modbus address"));
    mTable->setItem(row, ColAddress, address);
    mTable->setItem(row, ColChannel, new QTableWidgetItem(QStringLiteral("%1 %2").arg(identity.channel, identity.endpoint)));
    mTable->setItem(row, ColType, new QTableWidgetItem(identity.typeHex()));
    mTable->setItem(row, ColVersion, new QTableWidgetItem(identity.versionHex()));
    QTableWidgetItem* state = new QTableWidgetItem(identity.state);
    state->setForeground(identity.isBootloader() ? QColor(QStringLiteral("#a15c07")) : QColor(QStringLiteral("#2563eb")));
    mTable->setItem(row, ColState, state);
    const QString displayDescription = QStringLiteral("%1 (%2 %3)")
        .arg(identity.description, identity.typeHex(), identity.versionHex());
    QTableWidgetItem* desc = new QTableWidgetItem(displayDescription);
    if (identity.descriptionMismatch)
        desc->setToolTip(QStringLiteral("JSON ожидает: %1").arg(identity.expectedDescription));
    mTable->setItem(row, ColDescription, desc);
    QTableWidgetItem* status = new QTableWidgetItem(identity.status);
    status->setForeground(identity.descriptionMismatch ? QColor(QStringLiteral("#a15c07")) : QColor(QStringLiteral("#15803d")));
    mTable->setItem(row, ColStatus, status);

    QWidget* actions = new QWidget;
    QHBoxLayout* layout = new QHBoxLayout(actions);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    const QVector<ActionSpec> specs = mServices->actions().actionsForDevice(identity);
    for (const ActionSpec& spec : specs)
    {
        QToolButton* button = new QToolButton;
        button->setText(spec.id == QStringLiteral("device.productionDate.update")
            ? QStringLiteral("Дата")
            : (spec.id == QStringLiteral("device.ping")
                ? QStringLiteral("Ping")
                : (spec.target == QStringLiteral("bootloader") ? QStringLiteral("B") : QStringLiteral("A"))));
        if (spec.id == QStringLiteral("device.serialNumber.update"))
            button->setText(QStringLiteral("Number"));
        button->setToolTip(spec.title.isEmpty() ? spec.id : spec.title);
        button->setAutoRaise(false);
        connect(button, &QToolButton::clicked, this, [this, row, id = spec.id]() {
            runActionForRow(row, id);
        });
        layout->addWidget(button);
    }
    layout->addStretch();
    mTable->setCellWidget(row, ColActions, actions);
}

QVector<std::shared_ptr<DeviceBase>> MainWindow::selectedDevices() const
{
    QVector<std::shared_ptr<DeviceBase>> selected;
    for (int row = 0; row < mTable->rowCount() && row < mDevices.size(); ++row)
    {
        const QTableWidgetItem* item = mTable->item(row, ColCheck);
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

        start->setEnabled(false);
        appendPingLine(QStringLiteral("Опрос запущен"));
        pingSession->thread = new QThread;
        pingSession->worker = new PingWorker(device, 500);
        pingSession->worker->moveToThread(pingSession->thread);

        connect(pingSession->thread, &QThread::started, pingSession->worker, &PingWorker::start);
        connect(pingSession->worker, &PingWorker::pingLine, &dialog, [appendPingLine](const QString& message) {
            appendPingLine(message);
        });
        connect(pingSession->worker, &PingWorker::transportLogMessage, this, &MainWindow::appendTransportLog);
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
    mBulkMenu->clear();
    const QVector<std::shared_ptr<DeviceBase>> selected = selectedDevices();
    const QVector<ActionSpec> actions = mServices->actions().commonActions(selected);
    mBulkButton->setEnabled(!actions.isEmpty());

    for (const ActionSpec& action : actions)
    {
        QAction* menuAction = mBulkMenu->addAction(action.title.isEmpty() ? action.id : action.title);
        connect(menuAction, &QAction::triggered, this, [this, action, selected]() {
            executeAction(action, selected);
        });
    }
}

void MainWindow::setActionBusy(bool busy)
{
    if (mTable)
        mTable->setEnabled(!busy);
    if (mBulkButton)
        mBulkButton->setEnabled(false);
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
