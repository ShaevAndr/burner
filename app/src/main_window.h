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
#include <atomic>
#include <memory>

class QThread;
class QLabel;
class QCloseEvent;
class QToolButton;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(ServiceContainer* services, QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void startDiscovery();
    void onDeviceFound(DeviceIdentity device);
    void onDeviceDataProgress(quint64 requestId, int percent, const QString& stage);
    void onDeviceDataFinished(quint64 requestId, DeviceIdentity identity,
        const QStringList& warnings, const QString& rawResponse);
    void onDiscoveryFinished();
    void updateLineMode();
    void updateBulkMenu();
    void runActionForRow(int row, const QString& actionId);
    void executeAction(const ActionSpec& action, const QVector<std::shared_ptr<DeviceBase>>& devices);
    void appendLog(const QString& message);
    void appendTransportLog(const QString& message);
    void onWorkflowProgress(int percent);
    void onWorkflowStageChanged(const QString& operation, const QString& stage);

private:
    void buildUi();
    QWidget* buildDiscoveryPage();
    QWidget* buildLogsPanel();
    QWidget* buildDiscoveryPanel();
    QWidget* buildDiscoveryTablePanel();
    QWidget* buildWorkflowProgressPanel();
    void addDeviceRow(const std::shared_ptr<DeviceBase>& device);
    void updateDeviceRow(int row, const std::shared_ptr<DeviceBase>& device);
    void updateDiscoveryDeviceRow(int row, const std::shared_ptr<DeviceBase>& device);
    void mergeDiscoveredDevice(const std::shared_ptr<DeviceBase>& device);
    QVector<std::shared_ptr<DeviceBase>> selectedDevices() const;
    bool actionHasArtifact(const ActionSpec& action, const QVector<std::shared_ptr<DeviceBase>>& devices) const;
    ActionSpec actionById(const QString& actionId) const;
    bool prepareActionInvocation(const ActionSpec& action, const QVector<std::shared_ptr<DeviceBase>>& devices, QVariantMap* parameters);
    void startWorkflowAction(const ActionSpec& action, const QVector<std::shared_ptr<DeviceBase>>& devices, const QVariantMap& parameters);
    void showPingDialog(const std::shared_ptr<DeviceBase>& device);
    void rebuildBulkMenu();
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
    QToolButton* mBulkActionsButton = nullptr;
    QVector<QWidget*> mWorkflowProgressPanels;
    QVector<QLabel*> mWorkflowStageLabels;
    QVector<QProgressBar*> mWorkflowProgressBars;
    QVector<QPushButton*> mWorkflowCancelButtons;
    std::shared_ptr<std::atomic_bool> mWorkflowCancelToken;
    QTableWidget* mDiscoveryTable = nullptr;
    QLabel* mDeviceDataProgressLabel = nullptr;
    QProgressBar* mDeviceDataProgressBar = nullptr;
    QVector<QPlainTextEdit*> mOperationLogs;
    QVector<QPlainTextEdit*> mTransportLogs;
    bool mDiscoveryBusy = false;
    bool mActionBusy = false;
};

#endif // DEVICE_WORKBENCH_MAIN_WINDOW_H
