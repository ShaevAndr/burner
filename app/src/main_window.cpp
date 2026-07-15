#include "main_window.h"

#include <QAction>
#include <QCheckBox>
#include <QDateTime>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>

enum Columns
{
    ColCheck = 0,
    ColDevice,
    ColChannel,
    ColType,
    ColVersion,
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
    QPushButton* refresh = new QPushButton(QStringLiteral("Обновить"));
    header->addWidget(refresh);
    mainLayout->addLayout(header);

    mainLayout->addWidget(buildDiscoveryPanel());
    mainLayout->addWidget(buildTablePanel(), 1);

    mLog = new QPlainTextEdit;
    mLog->setReadOnly(true);
    mLog->setMaximumHeight(130);
    mLog->setPlaceholderText(QStringLiteral("Журнал операций"));
    mainLayout->addWidget(mLog);

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
        QTableWidget { background: white; border: 0; gridline-color: #d8e0e5; selection-background-color: #edf5ff; }
        QHeaderView::section { background: #fbfcfd; color: #667584; border: 0; border-bottom: 1px solid #d8e0e5; padding: 8px; font-weight: 700; }
        QPlainTextEdit { background: #101820; color: #e5edf4; border-radius: 7px; padding: 8px; font-family: Consolas, monospace; }
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
        QString(), QStringLiteral("Устройство"), QStringLiteral("Канал"),
        QStringLiteral("Тип"), QStringLiteral("Версия"), QStringLiteral("Короткое описание"),
        QStringLiteral("Статус"), QStringLiteral("Действия")
    });
    mTable->verticalHeader()->setVisible(false);
    mTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    mTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mTable->horizontalHeader()->setSectionResizeMode(ColDevice, QHeaderView::Stretch);
    mTable->horizontalHeader()->setSectionResizeMode(ColDescription, QHeaderView::Stretch);
    mTable->setColumnWidth(ColCheck, 42);
    mTable->setColumnWidth(ColChannel, 210);
    mTable->setColumnWidth(ColType, 88);
    mTable->setColumnWidth(ColVersion, 88);
    mTable->setColumnWidth(ColStatus, 136);
    mTable->setColumnWidth(ColActions, 150);
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
    for (int i = 0; i < mDevices.size(); ++i)
    {
        if (mDevices.at(i).id == device.id)
        {
            mDevices[i] = device;
            return;
        }
    }
    mDevices.append(device);
    addDeviceRow(device);
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
    mServices->workflow().run(actionById(actionId), {mDevices.at(row)});
}

void MainWindow::appendLog(const QString& message)
{
    mLog->appendPlainText(QStringLiteral("[%1] %2")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), message));
}

void MainWindow::addDeviceRow(const DeviceIdentity& device)
{
    const int row = mTable->rowCount();
    mTable->insertRow(row);

    QTableWidgetItem* check = new QTableWidgetItem;
    check->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    check->setCheckState(Qt::Unchecked);
    mTable->setItem(row, ColCheck, check);

    mTable->setItem(row, ColDevice, new QTableWidgetItem(QStringLiteral("%1\n%2").arg(device.name, device.serialNumber)));
    mTable->setItem(row, ColChannel, new QTableWidgetItem(QStringLiteral("%1 %2").arg(device.channel, device.endpoint)));
    mTable->setItem(row, ColType, new QTableWidgetItem(device.typeHex()));
    mTable->setItem(row, ColVersion, new QTableWidgetItem(device.versionHex()));
    QTableWidgetItem* desc = new QTableWidgetItem(device.description);
    if (device.descriptionMismatch)
        desc->setToolTip(QStringLiteral("JSON ожидает: %1").arg(device.expectedDescription));
    mTable->setItem(row, ColDescription, desc);
    QTableWidgetItem* status = new QTableWidgetItem(device.status);
    status->setForeground(device.descriptionMismatch ? QColor(QStringLiteral("#a15c07")) : QColor(QStringLiteral("#15803d")));
    mTable->setItem(row, ColStatus, status);

    QWidget* actions = new QWidget;
    QHBoxLayout* layout = new QHBoxLayout(actions);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    const QVector<ActionSpec> specs = mServices->actions().actionsForDevice(device);
    for (const ActionSpec& spec : specs)
    {
        QToolButton* button = new QToolButton;
        button->setText(spec.target == QStringLiteral("bootloader") ? QStringLiteral("B") : QStringLiteral("A"));
        button->setToolTip(spec.id);
        button->setAutoRaise(false);
        connect(button, &QToolButton::clicked, this, [this, row, id = spec.id]() { runActionForRow(row, id); });
        layout->addWidget(button);
    }
    layout->addStretch();
    mTable->setCellWidget(row, ColActions, actions);
}

QVector<DeviceIdentity> MainWindow::selectedDevices() const
{
    QVector<DeviceIdentity> selected;
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

void MainWindow::rebuildBulkMenu()
{
    mBulkMenu->clear();
    const QVector<DeviceIdentity> selected = selectedDevices();
    const QVector<ActionSpec> actions = mServices->actions().commonActions(selected);
    mBulkButton->setEnabled(!actions.isEmpty());

    for (const ActionSpec& action : actions)
    {
        QAction* menuAction = mBulkMenu->addAction(action.id);
        connect(menuAction, &QAction::triggered, this, [this, action, selected]() {
            mServices->workflow().run(action, selected);
        });
    }
}

void MainWindow::setBusy(bool busy)
{
    mSearchButton->setEnabled(!busy);
    mSearchButton->setText(busy ? QStringLiteral("Идет поиск...") :
        (mLineMode->currentData().toString() == QStringLiteral("rs485") ? QStringLiteral("Поиск RS-485") : QStringLiteral("Broadcast поиск")));
}
