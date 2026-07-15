#include "discovery.h"

#include <QDateTime>
#include <QNetworkDatagram>
#include <QTextCodec>
#include <QtEndian>

static const QHostAddress kSearchAddress(QStringLiteral("239.1.2.1"));
static const quint16 kSearchPort = 5001;
static const quint32 kSearchMagic = 0x34125643;
static const int kDescriptionOffset = 16;
static const int kDescriptionSize = 100;
static const int kSerialOffset = 116;
static const int kSerialSize = 40;

static QString zeroTerminatedCp1251(const QByteArray& bytes)
{
    const int zero = bytes.indexOf('\0');
    const QByteArray clean = zero >= 0 ? bytes.left(zero) : bytes;
    QTextCodec* codec = QTextCodec::codecForName("Windows-1251");
    return codec ? codec->toUnicode(clean).trimmed() : QString::fromLocal8Bit(clean).trimmed();
}

UdpBroadcastDiscovery::UdpBroadcastDiscovery(QObject* parent) :
    IDiscoveryStrategy(parent)
{
    connect(&mSocket, &QUdpSocket::readyRead, this, &UdpBroadcastDiscovery::readPendingDatagrams);
    connect(&mFinishTimer, &QTimer::timeout, this, [this]() {
        mFinishTimer.stop();
        emit logMessage(QStringLiteral("UDP discovery finished"));
        emit finished();
    });
}

void UdpBroadcastDiscovery::start(const DiscoverySettings& settings)
{
    if (mSocket.state() != QAbstractSocket::BoundState)
    {
        if (!mSocket.bind(QHostAddress::AnyIPv4, 0, QAbstractSocket::DefaultForPlatform | QAbstractSocket::ReuseAddressHint))
        {
            emit logMessage(QStringLiteral("UDP bind failed: %1").arg(mSocket.errorString()));
            emit finished();
            return;
        }
    }

    emit logMessage(QStringLiteral("Sending FINE discovery to %1:%2").arg(kSearchAddress.toString()).arg(kSearchPort));
    sendSearchDatagram(settings);
    mFinishTimer.start(settings.timeoutMs);
}

void UdpBroadcastDiscovery::cancel()
{
    mFinishTimer.stop();
    emit logMessage(QStringLiteral("UDP discovery cancelled"));
    emit finished();
}

void UdpBroadcastDiscovery::sendSearchDatagram(const DiscoverySettings& settings)
{
    const QByteArray data("FINE", 4);
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    bool sent = false;

    for (const QNetworkInterface& iface : interfaces)
    {
        if (!iface.isValid())
            continue;
        if (!settings.interfaceName.isEmpty() && settings.interfaceName != QStringLiteral("all") && iface.name() != settings.interfaceName)
            continue;
        if (!iface.flags().testFlag(QNetworkInterface::IsRunning))
            continue;
        if (!iface.flags().testFlag(QNetworkInterface::CanMulticast))
            continue;

        for (const QNetworkAddressEntry& address : iface.addressEntries())
        {
            if (address.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;

            QNetworkDatagram datagram(data, kSearchAddress, kSearchPort);
            datagram.setInterfaceIndex(uint(iface.index()));
            datagram.setSender(address.ip());
            const qint64 written = mSocket.writeDatagram(datagram);
            if (written >= 0)
            {
                sent = true;
                emit logMessage(QStringLiteral("Discovery sent via %1 (%2)").arg(iface.name(), address.ip().toString()));
            }
            else
            {
                emit logMessage(QStringLiteral("Discovery send failed via %1: %2").arg(iface.name(), mSocket.errorString()));
            }
        }
    }

    if (!sent)
    {
        const qint64 written = mSocket.writeDatagram(data, kSearchAddress, kSearchPort);
        emit logMessage(written >= 0
            ? QStringLiteral("Discovery sent without explicit interface")
            : QStringLiteral("Discovery send failed: %1").arg(mSocket.errorString()));
    }
}

void UdpBroadcastDiscovery::readPendingDatagrams()
{
    while (mSocket.hasPendingDatagrams())
    {
        QHostAddress sender;
        quint16 senderPort = 0;
        QByteArray datagram;
        datagram.resize(int(mSocket.pendingDatagramSize()));
        const qint64 read = mSocket.readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
        if (read < 0)
        {
            emit logMessage(QStringLiteral("UDP read failed: %1").arg(mSocket.errorString()));
            return;
        }
        parseDatagram(datagram, sender);
    }
}

void UdpBroadcastDiscovery::parseDatagram(const QByteArray& datagram, const QHostAddress& sender)
{
    if (datagram.size() < kSerialOffset)
    {
        emit logMessage(QStringLiteral("Ignored short datagram from %1").arg(sender.toString()));
        return;
    }

    const uchar* raw = reinterpret_cast<const uchar*>(datagram.constData());
    const quint32 magic = qFromBigEndian<quint32>(raw);
    if (magic != kSearchMagic)
    {
        emit logMessage(QStringLiteral("Ignored datagram with wrong magic from %1").arg(sender.toString()));
        return;
    }

    const quint16 dataVersion = qFromBigEndian<quint16>(raw + 4);
    if (dataVersion != 1)
    {
        emit logMessage(QStringLiteral("Ignored datagram with unsupported data version %1").arg(dataVersion));
        return;
    }

    DeviceIdentity device;
    device.protocol = QStringLiteral("unicorn-ascii");
    device.channel = QStringLiteral("UDP");
    device.endpoint = QStringLiteral("%1:%2").arg(sender.toString()).arg(qFromBigEndian<quint16>(raw + 6));
    device.type = qFromBigEndian<quint16>(raw + 12);
    device.version = qFromBigEndian<quint16>(raw + 14);
    device.description = zeroTerminatedCp1251(datagram.mid(kDescriptionOffset, kDescriptionSize));
    device.serialNumber = zeroTerminatedCp1251(datagram.mid(kSerialOffset, qMin(kSerialSize, datagram.size() - kSerialOffset)));
    device.id = !device.serialNumber.isEmpty()
        ? device.serialNumber
        : QStringLiteral("%1-%2-%3").arg(sender.toString()).arg(device.typeHex()).arg(device.versionHex());

    emit logMessage(QStringLiteral("Found %1 %2 %3 at %4")
        .arg(device.typeHex(), device.versionHex(), device.description, device.endpoint));
    emit deviceFound(device);
}

Rs485Discovery::Rs485Discovery(QObject* parent) :
    IDiscoveryStrategy(parent)
{
}

void Rs485Discovery::start(const DiscoverySettings& settings)
{
    emit logMessage(QStringLiteral("RS-485 discovery requested on %1, addresses %2-%3, protocol %4")
        .arg(settings.serialPortName)
        .arg(settings.addressStart)
        .arg(settings.addressEnd)
        .arg(settings.protocolId));
    emit logMessage(QStringLiteral("RS-485 packet strategy is registered but hardware transaction is not enabled in this MVP build"));
    emit finished();
}

void Rs485Discovery::cancel()
{
    emit logMessage(QStringLiteral("RS-485 discovery cancelled"));
    emit finished();
}

QStringList availableSerialPorts()
{
    QStringList ports;
    for (const QSerialPortInfo& port : QSerialPortInfo::availablePorts())
        ports.append(port.portName());
    if (ports.isEmpty())
        ports.append(QStringLiteral("COM3"));
    return ports;
}

QStringList availableNetworkInterfaces()
{
    QStringList names;
    names.append(QStringLiteral("all"));
    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces())
    {
        if (!iface.isValid())
            continue;
        if (!iface.flags().testFlag(QNetworkInterface::IsRunning))
            continue;
        for (const QNetworkAddressEntry& address : iface.addressEntries())
        {
            if (address.ip().protocol() == QAbstractSocket::IPv4Protocol)
            {
                names.append(iface.name());
                break;
            }
        }
    }
    names.removeDuplicates();
    return names;
}
