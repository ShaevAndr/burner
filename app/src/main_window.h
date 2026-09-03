#ifndef DEVICE_WORKBENCH_MAIN_WINDOW_H
#define DEVICE_WORKBENCH_MAIN_WINDOW_H

#include "device.h"
#include "service_container.h"

#include <QComboBox>
#include <QHash>
#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QVariantMap>
#include <memory>

class QThread;
class QStackedWidget;
class QLabel;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(ServiceContainer* services, QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void startDiscovery();
    void onDeviceFound(DeviceIdentity device);
    void onDeviceDataProgress(quint64 requestId, int percent, const QString& stage);
    void onDeviceDataFinished(quint64 requestId, DeviceIdentity identity,
        const QStringList& warnings, const QString& rawResponse);
    void onDiscoveryFinished();
    void updateLineMode();
    void updateBulkMenu();
    void runProductionDateUpdate();
    void runBootloaderUpdate();
    void runActionForRow(int row, const QString& actionId);
    void executeAction(const ActionSpec& action, const QVector<std::shared_ptr<DeviceBase>>& devices);
    void appendLog(const QString& message);
    void appendTransportLog(const QString& message);
    void onWorkflowProgress(int percent);
    void onWorkflowStageChanged(const QString& operation, const QString& stage);

private:
    void buildUi();
    QWidget* buildSidebar();
    QWidget* buildDiscoveryPage();
    QWidget* buildFirmwarePage();
    QWidget* buildBootloaderPage();
    QWidget* buildProductionDatePage();
    QWidget* buildSerialNumberPage();
    QWidget* buildDiscoveryPanel();
    QWidget* buildDiscoveryTablePanel();
    QWidget* buildFirmwareTablePanel();
    QWidget* buildBootloaderTablePanel();
    QWidget* buildProductionDateTablePanel();
    QWidget* buildSerialNumberTablePanel();
    QWidget* buildWorkflowProgressPanel();
    void addDeviceRow(const std::shared_ptr<DeviceBase>& device);
    void updateDeviceRow(int row, const std::shared_ptr<DeviceBase>& device);
    void updateDiscoveryDeviceRow(int row, const std::shared_ptr<DeviceBase>& device);
    void updateFirmwareDeviceRow(int row, const std::shared_ptr<DeviceBase>& device);
    void updateBootloaderDeviceRow(int row, const std::shared_ptr<DeviceBase>& device);
    void updateProductionDateDeviceRow(int row, const std::shared_ptr<DeviceBase>& device);
    void updateSerialNumberDeviceRow(int row, const std::shared_ptr<DeviceBase>& device);
    void updateDeviceActionRow(QTableWidget* table,
        int row,
        const std::shared_ptr<DeviceBase>& device,
        const QString& actionId,
        bool checkable);
    void mergeDiscoveredDevice(const std::shared_ptr<DeviceBase>& device);
    QVector<std::shared_ptr<DeviceBase>> selectedDevices() const;
    QVector<std::shared_ptr<DeviceBase>> selectedBootloaderDevices() const;
    QVector<std::shared_ptr<DeviceBase>> selectedProductionDateDevices() const;
    QVector<std::shared_ptr<DeviceBase>> devicesForAction(const QString& actionId, bool includeBusy = true) const;
    void updateNavigationActions();
    void showPage(int pageIndex);
    ActionSpec actionById(const QString& actionId) const;
    bool prepareActionInvocation(const ActionSpec& action, const QVector<std::shared_ptr<DeviceBase>>& devices, QVariantMap* parameters);
    void startWorkflowAction(const ActionSpec& action, const QVector<std::shared_ptr<DeviceBase>>& devices, const QVariantMap& parameters);
    void showPingDialog(const std::shared_ptr<DeviceBase>& device);
    void rebuildBulkMenu();
    void rebuildBootloaderBulkAction();
    void rebuildProductionDateBulkAction();
    bool isDeviceBusy(const std::shared_ptr<DeviceBase>& device) const;
    void setDevicesBusy(const QVector<std::shared_ptr<DeviceBase>>& devices, bool busy);
    void setBusy(bool busy);
    void setActionBusy(bool busy);
    void updateDeviceDataProgress();
    QString workflowStageText(const QString& operation, const QString& stage) const;
    QString logFilePath() const;
    void appendFileLog(const QString& category, const QString& message) const;

    ServiceContainer* mServices = nullptr;
    DeviceFactory mDeviceFactory;

    struct PendingDeviceDataRead
    {
        std::shared_ptr<DeviceBase> device;
        quint64 discoveryGeneration = 0;
        QString endpointKey;
        int progress = 0;
        QString stage;
    };

    QVector<std::shared_ptr<DeviceBase>> mDevices;
    QHash<quint64, PendingDeviceDataRead> mPendingDeviceDataReads;
    QSet<QString> mDeviceDataEndpoints;
    QSet<QThread*> mDeviceDataThreads;
    QSet<const DeviceBase*> mBusyDevices;
    quint64 mNextDeviceDataRequestId = 1;
    quint64 mDiscoveryGeneration = 0;
    int mDeviceDataTotal = 0;
    int mDeviceDataCompleted = 0;
    QThread* mWorkflowThread = nullptr;

    QComboBox* mLineMode = nullptr;
    QComboBox* mNetworkInterface = nullptr;
    QComboBox* mUdpProtocol = nullptr;
    QWidget* mUdpPanel = nullptr;
    QComboBox* mSerialPort = nullptr;
    QComboBox* mRs485Protocol = nullptr;
    QLineEdit* mAddressStart = nullptr;
    QLineEdit* mAddressEnd = nullptr;
    QWidget* mRs485Panel = nullptr;
    QPushButton* mSearchButton = nullptr;
    QPushButton* mDiscoveryTabButton = nullptr;
    QPushButton* mFirmwareTabButton = nullptr;
    QPushButton* mBootloaderTabButton = nullptr;
    QPushButton* mProductionDateButton = nullptr;
    QPushButton* mSerialNumberButton = nullptr;
    QStackedWidget* mPages = nullptr;
    QPushButton* mBulkFlashButton = nullptr;
    QPushButton* mBulkBootloaderButton = nullptr;
    QPushButton* mBulkProductionDateButton = nullptr;
    QVector<QWidget*> mWorkflowProgressPanels;
    QVector<QLabel*> mWorkflowStageLabels;
    QVector<QProgressBar*> mWorkflowProgressBars;
    QTableWidget* mDiscoveryTable = nullptr;
    QLabel* mDeviceDataProgressLabel = nullptr;
    QProgressBar* mDeviceDataProgressBar = nullptr;
    QTableWidget* mFirmwareTable = nullptr;
    QTableWidget* mBootloaderTable = nullptr;
    QTableWidget* mProductionDateTable = nullptr;
    QTableWidget* mSerialNumberTable = nullptr;
    QPlainTextEdit* mLog = nullptr;
    QPlainTextEdit* mTransportLog = nullptr;
    bool mDiscoveryBusy = false;
};

#endif // DEVICE_WORKBENCH_MAIN_WINDOW_H
