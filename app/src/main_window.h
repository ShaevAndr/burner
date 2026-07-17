#ifndef DEVICE_WORKBENCH_MAIN_WINDOW_H
#define DEVICE_WORKBENCH_MAIN_WINDOW_H

#include "device.h"
#include "service_container.h"

#include <QComboBox>
#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QVariantMap>
#include <memory>

class QThread;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(ServiceContainer* services, QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void startDiscovery();
    void onDeviceFound(DeviceIdentity device);
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
    QWidget* buildDiscoveryPanel();
    QWidget* buildTablePanel();
    void addDeviceRow(const std::shared_ptr<DeviceBase>& device);
    void updateDeviceRow(int row, const std::shared_ptr<DeviceBase>& device);
    QVector<std::shared_ptr<DeviceBase>> selectedDevices() const;
    ActionSpec actionById(const QString& actionId) const;
    bool prepareActionInvocation(const ActionSpec& action, const QVector<std::shared_ptr<DeviceBase>>& devices, QVariantMap* parameters);
    void startWorkflowAction(const ActionSpec& action, const QVector<std::shared_ptr<DeviceBase>>& devices, const QVariantMap& parameters);
    void showPingDialog(const std::shared_ptr<DeviceBase>& device);
    void rebuildBulkMenu();
    void setBusy(bool busy);
    void setActionBusy(bool busy);

    ServiceContainer* mServices = nullptr;
    DeviceFactory mDeviceFactory;
    QVector<std::shared_ptr<DeviceBase>> mDevices;
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
    QLineEdit* mSearch = nullptr;
    QPushButton* mBulkButton = nullptr;
    QMenu* mBulkMenu = nullptr;
    QProgressBar* mFlashProgress = nullptr;
    QTableWidget* mTable = nullptr;
    QPlainTextEdit* mLog = nullptr;
    QPlainTextEdit* mTransportLog = nullptr;
};

#endif // DEVICE_WORKBENCH_MAIN_WINDOW_H
