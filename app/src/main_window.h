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

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(ServiceContainer* services, QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void startDiscovery();
    void onDeviceFound(DeviceIdentity device);
    void onUuidReadFinished(quint64 requestId, const QString& uuid, const QString& error, const QString& rawResponse);
    void onDiscoveryFinished();
    void updateLineMode();
    void updateBulkMenu();
    void runActionForRow(int row, const QString& actionId);
    void executeAction(const ActionSpec& action, const QVector<std::shared_ptr<DeviceBase>>& devices);
    void appendLog(const QString& message);
    void appendTransportLog(const QString& message);
    void onWorkflowProgress(int percent);

private:
    void buildUi();
    QWidget* buildSidebar();
    QWidget* buildDiscoveryPage();
    QWidget* buildFirmwarePage();
    QWidget* buildDiscoveryPanel();
    QWidget* buildDiscoveryTablePanel();
    QWidget* buildFirmwareTablePanel();
    void addDeviceRow(const std::shared_ptr<DeviceBase>& device);
    void updateDeviceRow(int row, const std::shared_ptr<DeviceBase>& device);
    void updateDiscoveryDeviceRow(int row, const std::shared_ptr<DeviceBase>& device);
    void updateFirmwareDeviceRow(int row, const std::shared_ptr<DeviceBase>& device);
    void mergeDiscoveredDevice(const std::shared_ptr<DeviceBase>& device);
    QVector<std::shared_ptr<DeviceBase>> selectedDevices() const;
    ActionSpec actionById(const QString& actionId) const;
    bool prepareActionInvocation(const ActionSpec& action, const QVector<std::shared_ptr<DeviceBase>>& devices, QVariantMap* parameters);
    void startWorkflowAction(const ActionSpec& action, const QVector<std::shared_ptr<DeviceBase>>& devices, const QVariantMap& parameters);
    void showPingDialog(const std::shared_ptr<DeviceBase>& device);
    void rebuildBulkMenu();
    bool isDeviceBusy(const std::shared_ptr<DeviceBase>& device) const;
    void setDevicesBusy(const QVector<std::shared_ptr<DeviceBase>>& devices, bool busy);
    void setBusy(bool busy);
    void setActionBusy(bool busy);

    ServiceContainer* mServices = nullptr;
    DeviceFactory mDeviceFactory;

    struct PendingUuidRead
    {
        std::shared_ptr<DeviceBase> device;
        quint64 discoveryGeneration = 0;
        QString endpointKey;
    };

    QVector<std::shared_ptr<DeviceBase>> mDevices;
    QHash<quint64, PendingUuidRead> mPendingUuidReads;
    QSet<QString> mPendingUuidEndpoints;
    QSet<QThread*> mUuidThreads;
    QSet<const DeviceBase*> mBusyDevices;
    quint64 mNextUuidRequestId = 1;
    quint64 mDiscoveryGeneration = 0;
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
    QStackedWidget* mPages = nullptr;
    QPushButton* mBulkFlashButton = nullptr;
    QProgressBar* mFlashProgress = nullptr;
    QTableWidget* mDiscoveryTable = nullptr;
    QTableWidget* mFirmwareTable = nullptr;
    QPlainTextEdit* mLog = nullptr;
    QPlainTextEdit* mTransportLog = nullptr;
};

#endif // DEVICE_WORKBENCH_MAIN_WINDOW_H
