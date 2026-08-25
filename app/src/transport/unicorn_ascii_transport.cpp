#include "unicorn_ascii_transport.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTextCodec>
#include <QUdpSocket>
#include <QVector>
#include <QtEndian>

#include <cctype>

namespace
{
static const QHostAddress kDiscoveryAddress(QStringLiteral("239.1.2.1"));
static const quint16 kDiscoveryPort = 5001;
static const quint32 kDiscoveryMagic = 0x34125643;

bool isValidUtf8(const QByteArray& bytes)
{
    for (int i = 0; i < bytes.size(); )
    {
        const quint8 c = quint8(bytes.at(i));
        if (c < 0x80)
        {
            ++i;
            continue;
        }
        int length = 0;
        if ((c & 0xE0) == 0xC0)
            length = 2;
        else if ((c & 0xF0) == 0xE0)
            length = 3;
        else if ((c & 0xF8) == 0xF0)
            length = 4;
        else
            return false;
        if (i + length > bytes.size())
            return false;
        for (int j = 1; j < length; ++j)
        {
            if ((quint8(bytes.at(i + j)) & 0xC0) != 0x80)
                return false;
        }
        i += length;
    }
    return true;
}

QString decodeDiscoveryText(const QByteArray& bytes)
{
    const int zero = bytes.indexOf('\0');
    const QByteArray clean = zero >= 0 ? bytes.left(zero) : bytes;
    if (clean.isEmpty())
        return {};
    if (isValidUtf8(clean))
        return QString::fromUtf8(clean).trimmed();
    QTextCodec* codec = QTextCodec::codecForName("Windows-1251");
    return codec ? codec->toUnicode(clean).trimmed() : QString::fromLocal8Bit(clean).trimmed();
}

bool descriptionContainsBoot(const QString& description)
{
    static const QRegularExpression bootPattern(
        QStringLiteral("\\(\\s*Boot\\s*\\)"),
        QRegularExpression::CaseInsensitiveOption);
    return bootPattern.match(description).hasMatch();
}

QString formatUuid(const QByteArray& bytes)
{
    if (bytes.size() != 16)
        return {};
    const QString hex = QString::fromLatin1(bytes.toHex().toUpper());
    return QStringLiteral("%1-%2-%3-%4-%5")
        .arg(hex.mid(0, 8), hex.mid(8, 4), hex.mid(12, 4), hex.mid(16, 4), hex.mid(20, 12));
}

bool parseDiscoveryDatagram(const QByteArray& datagram, const QHostAddress& sender, DeviceIdentity* device)
{
    static const int kDescriptionOffset = 16;
    static const int kDescriptionSize = 100;
    static const int kSerialOffset = 116;
    static const int kSerialSize = 40;
    if (!device || datagram.size() < kSerialOffset)
        return false;

    const uchar* raw = reinterpret_cast<const uchar*>(datagram.constData());
    if (qFromBigEndian<quint32>(raw) != kDiscoveryMagic || qFromBigEndian<quint16>(raw + 4) != 1)
        return false;

    DeviceIdentity found;
    found.protocol = QStringLiteral("unicorn-ascii");
    found.channel = QStringLiteral("UDP");
    found.endpoint = QStringLiteral("%1:%2").arg(sender.toString()).arg(qFromBigEndian<quint16>(raw + 6));
    found.modbusAddress = qFromBigEndian<qint32>(raw + 8);
    found.type = qFromBigEndian<quint16>(raw + 12);
    found.version = qFromBigEndian<quint16>(raw + 14);
    found.description = decodeDiscoveryText(datagram.mid(kDescriptionOffset, kDescriptionSize));
    found.serialNumber = decodeDiscoveryText(datagram.mid(kSerialOffset, qMin(kSerialSize, datagram.size() - kSerialOffset)));
    found.id = found.endpoint;
    *device = found;
    return true;
}

bool sendDiscovery(QUdpSocket* socket, const QHostAddress& directAddress = QHostAddress())
{
    bool sent = false;
    const QByteArray payload("FINE", 4);
    // Try both the known address and multicast. Some devices only answer the
    // multicast FINE request even when their current IP address is known.
    if (!directAddress.isNull() && directAddress.protocol() == QAbstractSocket::IPv4Protocol)
        sent = socket->writeDatagram(payload, directAddress, kDiscoveryPort) >= 0;

    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces())
    {
        if (!iface.isValid() || !iface.flags().testFlag(QNetworkInterface::IsRunning)
            || !iface.flags().testFlag(QNetworkInterface::CanMulticast))
            continue;
        for (const QNetworkAddressEntry& address : iface.addressEntries())
        {
            if (address.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            QNetworkDatagram datagram(payload, kDiscoveryAddress, kDiscoveryPort);
            datagram.setInterfaceIndex(uint(iface.index()));
            datagram.setSender(address.ip());
            sent = socket->writeDatagram(datagram) >= 0 || sent;
        }
    }
    if (!sent)
        sent = socket->writeDatagram(payload, kDiscoveryAddress, kDiscoveryPort) >= 0;
    return sent;
}

bool parseEndpoint(const QString& endpoint, QString* host, quint16* port, QString* error)
{
    const int split = endpoint.lastIndexOf(QLatin1Char(':'));
    if (split <= 0 || split >= endpoint.size() - 1)
    {
        if (error)
            *error = QStringLiteral("Bad endpoint \"%1\"").arg(endpoint);
        return false;
    }

    const QString hostPart = endpoint.left(split);
    bool ok = false;
    const uint portValue = endpoint.mid(split + 1).toUInt(&ok);
    if (!ok || portValue == 0 || portValue > 65535)
    {
        if (error)
            *error = QStringLiteral("Bad endpoint port in \"%1\"").arg(endpoint);
        return false;
    }

    if (host)
        *host = hostPart;
    if (port)
        *port = quint16(portValue);
    return true;
}

QByteArray buildAsciiPacket(quint8 address, quint8 command, quint16 checksumSalt, const QByteArray& body)
{
    QByteArray packet;
    packet.reserve(4 + body.size() * 2 + 3);
    quint16 checksum = checksumSalt;
    packet.append(':');
    checksum += quint8(':');
    static const char kHex[] = "0123456789ABCDEF";
    auto appendAsciiByte = [&](quint8 value) {
        const char hi = kHex[(value >> 4) & 0x0F];
        const char lo = kHex[value & 0x0F];
        packet.append(hi);
        packet.append(lo);
        checksum = quint16(checksum + quint8(hi) + quint8(lo));
    };

    appendAsciiByte(address);
    appendAsciiByte(command);
    for (uchar byte : body)
        appendAsciiByte(byte);
    appendAsciiByte(quint8(checksum & 0xFF));
    packet.append('\r');
    return packet;
}

void appendInt32(QByteArray* body, qint32 value)
{
    const quint32 raw = quint32(value);
    body->append(char((raw >> 24) & 0xFF));
    body->append(char((raw >> 16) & 0xFF));
    body->append(char((raw >> 8) & 0xFF));
    body->append(char(raw & 0xFF));
}

void appendUInt8(QByteArray* body, quint8 value)
{
    body->append(char(value));
}

void appendUInt32(QByteArray* body, quint32 value)
{
    body->append(char((value >> 24) & 0xFF));
    body->append(char((value >> 16) & 0xFF));
    body->append(char((value >> 8) & 0xFF));
    body->append(char(value & 0xFF));
}

bool readInt32(const QByteArray& body, int offset, qint32* value)
{
    if (offset < 0 || body.size() < offset + 4)
        return false;
    const quint32 raw = (quint32(quint8(body.at(offset))) << 24)
        | (quint32(quint8(body.at(offset + 1))) << 16)
        | (quint32(quint8(body.at(offset + 2))) << 8)
        | quint32(quint8(body.at(offset + 3)));
    if (value)
        *value = qint32(raw);
    return true;
}

quint8 flashChecksum(const QByteArray& data, int beginPos, int endPos)
{
    quint16 checksum = 0;
    for (int i = beginPos; i <= endPos && i < data.size(); ++i)
    {
        checksum = quint16(checksum + quint8(data.at(i)));
        while (checksum > 0x00FF)
        {
            checksum &= 0x00FF;
            checksum++;
        }
    }
    return quint8((checksum ^ 0x00FF) & 0x00FF);
}

bool decodeAsciiPair(char hi, char lo, quint8* value)
{
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        c = char(std::toupper(static_cast<unsigned char>(c)));
        if (c >= 'A' && c <= 'F')
            return 10 + (c - 'A');
        return -1;
    };

    const int hiValue = hex(hi);
    const int loValue = hex(lo);
    if (hiValue < 0 || loValue < 0)
        return false;
    if (value)
        *value = quint8((hiValue << 4) | loValue);
    return true;
}

bool parseAsciiPacket(const QByteArray& packet, char* header, quint8* address, quint8* command, QByteArray* body, QString* error)
{
    if (packet.size() < 8)
    {
        if (error)
            *error = QStringLiteral("Bad ASCII packet framing");
        return false;
    }

    const char first = packet.front();
    const char last = packet.back();
    if ((first != ':') && (first != '!') && (first != '?'))
    {
        if (error)
            *error = QStringLiteral("Bad ASCII packet framing");
        return false;
    }
    if (last != '\r')
    {
        if (error)
            *error = QStringLiteral("Bad ASCII packet framing");
        return false;
    }

    const QByteArray payload = packet.mid(1, packet.size() - 2);
    if (payload.size() < 6 || (payload.size() % 2) != 0)
    {
        if (error)
            *error = QStringLiteral("Bad ASCII packet length");
        return false;
    }

    QByteArray bytes;
    bytes.reserve(payload.size() / 2);
    for (int i = 0; i < payload.size(); i += 2)
    {
        quint8 value = 0;
        if (!decodeAsciiPair(payload.at(i), payload.at(i + 1), &value))
        {
            if (error)
                *error = QStringLiteral("Bad ASCII packet hex");
            return false;
        }
        bytes.append(char(value));
    }

    if (bytes.size() < 3)
    {
        if (error)
            *error = QStringLiteral("ASCII packet too short");
        return false;
    }

    if (header)
        *header = packet.front();
    if (address)
        *address = quint8(bytes.at(0));
    if (command)
        *command = quint8(bytes.at(1));
    if (body)
        *body = bytes.mid(2, bytes.size() - 3);
    return true;
}

bool takeAsciiPacket(QByteArray& buffer, QByteArray* packet, QString* error)
{
    const int start = buffer.indexOf(':');
    const int answerStart = buffer.indexOf('!');
    const int errorStart = buffer.indexOf('?');
    int firstStart = -1;
    for (int candidate : {start, answerStart, errorStart})
    {
        if (candidate < 0)
            continue;
        if (firstStart < 0 || candidate < firstStart)
            firstStart = candidate;
    }

    if (firstStart < 0)
        return false;

    const int end = buffer.indexOf('\r', firstStart);
    if (end < 0)
    {
        if (error)
            *error = QStringLiteral("Incomplete ASCII packet");
        return false;
    }

    if (packet)
        *packet = buffer.mid(firstStart, end - firstStart + 1);
    buffer.remove(0, end + 1);
    return true;
}

QString escapedPacketText(const QByteArray& packet)
{
    return QString::fromLatin1(packet.constData(), packet.size())
        .replace(QLatin1Char('\r'), QStringLiteral("\\r"))
        .replace(QLatin1Char('\n'), QStringLiteral("\\n"));
}

bool hasCompleteDeviceResponse(const QByteArray& buffer)
{
    for (int i = 0; i < buffer.size(); ++i)
    {
        const char marker = buffer.at(i);
        if (marker != '!' && marker != '?')
            continue;
        if (buffer.indexOf('\r', i) >= 0)
            return true;
    }
    return false;
}

bool readAsciiResponse(QTcpSocket& socket, QByteArray* response)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 2500)
    {
        if (!socket.waitForReadyRead(100))
            continue;
        response->append(socket.readAll());
        if (hasCompleteDeviceResponse(*response))
            return true;
    }
    return response && !response->isEmpty();
}

bool openAndSend(QTcpSocket& socket, const QString& host, quint16 port, const QByteArray& request, QString* error)
{
    socket.connectToHost(host, port);
    if (!socket.waitForConnected(2000))
    {
        if (error)
            *error = socket.errorString();
        return false;
    }

    if (socket.write(request) != request.size() || !socket.waitForBytesWritten(2000))
    {
        if (error)
            *error = socket.errorString();
        return false;
    }
    return true;
}

bool acceptPacket(const QByteArray& packet, const DeviceIdentity& device, const QVector<quint8>& expectedCommands, QByteArray* responseBody, QString* error)
{
    char rxHeader = '\0';
    quint8 rxAddress = 0;
    quint8 rxCommand = 0;
    QByteArray rxBody;
    if (!parseAsciiPacket(packet, &rxHeader, &rxAddress, &rxCommand, &rxBody, error))
        return false;

    if (rxAddress != quint8(device.modbusAddress))
    {
        if (error)
            *error = QStringLiteral("ASCII response address mismatch");
        return false;
    }

    if (rxHeader == ':')
        return false;

    if (rxHeader == '?')
    {
        if (!rxBody.isEmpty())
        {
            if (error)
                *error = QStringLiteral("Device returned ASCII error 0x%1")
                    .arg(int(quint8(rxBody.at(0))), 2, 16, QLatin1Char('0'));
        }
        else if (error)
        {
            *error = QStringLiteral("Device returned ASCII error packet");
        }
        return false;
    }

    if (!expectedCommands.contains(rxCommand))
    {
        if (error)
            *error = QStringLiteral("Unexpected ASCII response command 0x%1")
                .arg(int(rxCommand), 2, 16, QLatin1Char('0'));
        return false;
    }

    if (responseBody)
        *responseBody = rxBody;
    return true;
}

bool sendRequest(const DeviceIdentity& device, const QByteArray& request, const QVector<quint8>& expectedCommands, QByteArray* responseBody, QString* error, QString* rawResponse)
{
    if (device.modbusAddress <= 0 || device.modbusAddress > 254)
    {
        if (error)
            *error = QStringLiteral("Device address is not available");
        return false;
    }

    QString host;
    quint16 port = 0;
    if (!parseEndpoint(device.endpoint, &host, &port, error))
        return false;

    if (rawResponse)
        *rawResponse = QStringLiteral("TX %1").arg(escapedPacketText(request));

    QTcpSocket socket;
    if (!openAndSend(socket, host, port, request, error))
        return false;

    QByteArray response;
    if (!readAsciiResponse(socket, &response))
    {
        if (rawResponse)
            *rawResponse = QStringLiteral("%1\nRX <timeout>").arg(*rawResponse);
        if (error)
            *error = QStringLiteral("Timed out waiting for ASCII response");
        return false;
    }

    if (rawResponse)
        *rawResponse = QStringLiteral("%1\nRX %2").arg(*rawResponse, escapedPacketText(response));

    QByteArray packetBuffer = response;
    QString packetError;
    while (!packetBuffer.isEmpty())
    {
        QByteArray packet;
        if (!takeAsciiPacket(packetBuffer, &packet, &packetError))
        {
            if (packetBuffer.indexOf('\r') >= 0)
                continue;
            if (error)
                *error = packetError;
            return false;
        }

        if (acceptPacket(packet, device, expectedCommands, responseBody, &packetError))
            return true;
    }

    if (error)
        *error = packetError.isEmpty()
            ? QStringLiteral("No valid ASCII packet received")
            : packetError;
    return false;
}

bool sendRequestNoReply(const DeviceIdentity& device, const QByteArray& request, QString* error, QString* rawResponse)
{
    if (device.modbusAddress <= 0 || device.modbusAddress > 254)
    {
        if (error)
            *error = QStringLiteral("Device address is not available");
        return false;
    }

    QString host;
    quint16 port = 0;
    if (!parseEndpoint(device.endpoint, &host, &port, error))
        return false;

    if (rawResponse)
        *rawResponse = QStringLiteral("TX %1\nRX <not expected>").arg(escapedPacketText(request));

    QTcpSocket socket;
    return openAndSend(socket, host, port, request, error);
}

bool readTypeVersionDescription(const DeviceIdentity& device,
    quint16* type,
    quint16* version,
    QString* description,
    QString* error,
    QString* rawResponse)
{
    QByteArray responseBody;
    const QByteArray request = buildAsciiPacket(quint8(device.modbusAddress), 0xFF, 0, QByteArray());
    if (!sendRequest(device, request, {0xFF}, &responseBody, error, rawResponse))
        return false;
    if (responseBody.size() < 4)
    {
        if (error)
            *error = QStringLiteral("Type/version response is too short");
        return false;
    }

    const quint16 foundType = (quint16(quint8(responseBody.at(0))) << 8)
        | quint16(quint8(responseBody.at(1)));
    const quint16 foundVersion = (quint16(quint8(responseBody.at(2))) << 8)
        | quint16(quint8(responseBody.at(3)));
    const QString foundDescription = decodeDiscoveryText(responseBody.mid(4));
    if (foundDescription.isEmpty())
    {
        if (error)
            *error = QStringLiteral("Type/version response has no device description");
        return false;
    }
    if (type)
        *type = foundType;
    if (version)
        *version = foundVersion;
    if (description)
        *description = foundDescription;
    return true;
}

class UnicornAsciiTransport final : public IDeviceTransport
{
public:
    bool writeRegister(const DeviceIdentity& device, quint16 index, qint32 value, QString* error, QString* rawResponse = nullptr) override
    {
        QByteArray body;
        body.reserve(8);
        appendInt32(&body, qint32(index));
        appendInt32(&body, value);

        const QByteArray request = buildAsciiPacket(quint8(device.modbusAddress), 0xE2, 0, body);
        return sendRequest(device, request, {0xE2, 0xFA, 0xFF}, nullptr, error, rawResponse);
    }

    bool writeRegisterNoReply(const DeviceIdentity& device, quint16 index, qint32 value, QString* error, QString* rawResponse = nullptr) override
    {
        QByteArray body;
        body.reserve(8);
        appendInt32(&body, qint32(index));
        appendInt32(&body, value);

        const QByteArray request = buildAsciiPacket(quint8(device.modbusAddress), 0xE2, 0, body);
        return sendRequestNoReply(device, request, error, rawResponse);
    }

    bool readRegister(const DeviceIdentity& device, quint16 index, qint32* value, QString* error, QString* rawResponse = nullptr) override
    {
        QByteArray body;
        body.reserve(4);
        appendInt32(&body, qint32(index));

        QByteArray responseBody;
        const QByteArray request = buildAsciiPacket(quint8(device.modbusAddress), 0xEB, 0, body);
        if (!sendRequest(device, request, {0xEB, 0xFF}, &responseBody, error, rawResponse))
            return false;

        if (responseBody.size() < 8)
        {
            if (error)
                *error = QStringLiteral("ReadInt response is too short");
            return false;
        }

        qint32 responseIndex = -1;
        qint32 responseValue = 0;
        if (!readInt32(responseBody, 0, &responseIndex) || !readInt32(responseBody, 4, &responseValue))
        {
            if (error)
                *error = QStringLiteral("Bad ReadInt response body");
            return false;
        }

        if (responseIndex != qint32(index))
        {
            if (error)
                *error = QStringLiteral("ReadInt response index mismatch");
            return false;
        }

        if (value)
            *value = responseValue;
        return true;
    }

    bool readUuid(const DeviceIdentity& device, QString* uuid, QString* error, QString* rawResponse = nullptr) override
    {
        QByteArray responseBody;
        const QByteArray request = buildAsciiPacket(quint8(device.modbusAddress), 0x07, 0, QByteArray());
        if (!sendRequest(device, request, {0x07}, &responseBody, error, rawResponse))
            return false;

        if (responseBody.size() != 16)
        {
            if (error)
                *error = QStringLiteral("UUID response must contain 4 Integer values (16 bytes), got %1")
                    .arg(responseBody.size());
            return false;
        }

        const QString value = formatUuid(responseBody);
        if (value.isEmpty() || responseBody == QByteArray(16, char(0)))
        {
            if (error)
                *error = QStringLiteral("Device returned an invalid UUID");
            return false;
        }
        if (uuid)
            *uuid = value;
        return true;
    }

    bool readIdentityDescription(const DeviceIdentity& device,
        quint16* type,
        quint16* version,
        QString* description,
        QString* error,
        QString* rawResponse = nullptr) override
    {
        return readTypeVersionDescription(device, type, version, description, error, rawResponse);
    }

    bool readExtendedDescription(const DeviceIdentity& device,
        QByteArray* description,
        const std::function<void(int)>& progress,
        QString* error,
        QString* rawResponse = nullptr) override
    {
        constexpr int kFragmentSize = 512;
        constexpr int kMaximumDescriptionSize = 4 * 1024 * 1024;
        QByteArray result;
        int offset = 0;
        int totalSize = -1;
        int fragments = 0;
        if (progress)
            progress(0);

        do
        {
            QByteArray requestBody;
            requestBody.reserve(8);
            appendInt32(&requestBody, offset);
            appendInt32(&requestBody, kFragmentSize);

            QByteArray responseBody;
            QString fragmentError;
            const QByteArray request = buildAsciiPacket(
                quint8(device.modbusAddress), 0xEE, 0, requestBody);
            if (!sendRequest(device, request, {0xEE}, &responseBody,
                    &fragmentError, nullptr))
            {
                if (error)
                    *error = fragmentError;
                return false;
            }
            if (responseBody.size() < 16)
            {
                if (error)
                    *error = QStringLiteral("Extended description response is too short");
                return false;
            }

            qint32 responseOffset = -1;
            qint32 segmentSize = -1;
            qint32 responseTotalSize = -1;
            if (!readInt32(responseBody, 0, &responseOffset)
                || !readInt32(responseBody, 4, &segmentSize)
                || !readInt32(responseBody, 8, &responseTotalSize))
            {
                if (error)
                    *error = QStringLiteral("Bad extended description response header");
                return false;
            }
            if (responseOffset != offset || segmentSize < 0
                || responseTotalSize < 0 || responseTotalSize > kMaximumDescriptionSize
                || responseBody.size() < 12 + segmentSize + 4
                || (segmentSize == 0 && responseOffset < responseTotalSize))
            {
                if (error)
                    *error = QStringLiteral("Invalid extended description fragment at offset %1")
                        .arg(offset);
                return false;
            }
            if (totalSize >= 0 && totalSize != responseTotalSize)
            {
                if (error)
                    *error = QStringLiteral("Extended description size changed while reading");
                return false;
            }

            totalSize = responseTotalSize;
            result.append(responseBody.mid(12, segmentSize));
            offset += segmentSize;
            ++fragments;
            if (offset > totalSize)
            {
                if (error)
                    *error = QStringLiteral("Extended description exceeds its declared size");
                return false;
            }
            if (progress)
                progress(totalSize > 0 ? qMin(100, (offset * 100) / totalSize) : 100);
        }
        while (offset < totalSize);

        if (description)
            *description = result;
        if (rawResponse)
            *rawResponse = QStringLiteral("0xEE extended description: %1 byte(s), %2 fragment(s)")
                .arg(result.size())
                .arg(fragments);
        return true;
    }

    bool resetDevice(const DeviceIdentity& device, QString* error, QString* rawResponse = nullptr) override
    {
        QByteArray body;
        body.reserve(4);
        const quint32 magic = 0x55AA1234;
        body.append(char((magic >> 24) & 0xFF));
        body.append(char((magic >> 16) & 0xFF));
        body.append(char((magic >> 8) & 0xFF));
        body.append(char(magic & 0xFF));

        const QByteArray request = buildAsciiPacket(quint8(device.modbusAddress), 0xFA, 0, body);
        return sendRequest(device, request, {0xFA, 0xFF}, nullptr, error, rawResponse);
    }

    bool flashGetParams(const DeviceIdentity& device, QVector<FlashMemoryParams>* params, QString* error, QString* rawResponse = nullptr) override
    {
        QByteArray responseBody;
        const QByteArray request = buildAsciiPacket(quint8(device.modbusAddress), 0x45, 0, QByteArray());
        if (!sendRequest(device, request, {0x45, 0xFF}, &responseBody, error, rawResponse))
            return false;

        if ((responseBody.size() % 8) != 0)
        {
            if (error)
                *error = QStringLiteral("Flash params response has bad size");
            return false;
        }

        if (params)
        {
            params->clear();
            for (int offset = 0; offset < responseBody.size(); offset += 8)
            {
                qint32 pagesCount = 0;
                qint32 pageSize = 0;
                if (!readInt32(responseBody, offset, &pagesCount) || !readInt32(responseBody, offset + 4, &pageSize))
                {
                    if (error)
                        *error = QStringLiteral("Bad flash params response body");
                    return false;
                }
                params->append(FlashMemoryParams{pagesCount, pageSize});
            }
        }
        return true;
    }

    bool flashWritePage(const DeviceIdentity& device, int flashNum, int pageNum, const QByteArray& page, QString* error, QString* rawResponse = nullptr) override
    {
        if (flashNum < 0 || flashNum > 255 || pageNum < 0)
        {
            if (error)
                *error = QStringLiteral("Bad flash or page number");
            return false;
        }

        QByteArray body;
        body.reserve(1 + 4 + 4 + page.size() + 1);
        appendUInt8(&body, quint8(flashNum));
        appendInt32(&body, qint32(pageNum));
        appendUInt32(&body, 0xEB1C5A3F);
        body.append(page);
        body.append(char(flashChecksum(body, 0, body.size() - 1)));

        QByteArray responseBody;
        const QByteArray request = buildAsciiPacket(quint8(device.modbusAddress), 0x43, 0, body);
        if (!sendRequest(device, request, {0x43, 0xFF}, &responseBody, error, rawResponse))
            return false;

        if (responseBody.size() < 5)
        {
            if (error)
                *error = QStringLiteral("Flash write response is too short");
            return false;
        }

        qint32 responsePage = -1;
        if (quint8(responseBody.at(0)) != quint8(flashNum) || !readInt32(responseBody, 1, &responsePage) || responsePage != pageNum)
        {
            if (error)
                *error = QStringLiteral("Flash write response target mismatch");
            return false;
        }
        return true;
    }

    bool flashReadPage(const DeviceIdentity& device, int flashNum, int pageNum, QByteArray* page, QString* error, QString* rawResponse = nullptr) override
    {
        if (flashNum < 0 || flashNum > 255 || pageNum < 0)
        {
            if (error)
                *error = QStringLiteral("Bad flash or page number");
            return false;
        }

        QByteArray body;
        body.reserve(5);
        appendUInt8(&body, quint8(flashNum));
        appendInt32(&body, qint32(pageNum));

        QByteArray responseBody;
        const QByteArray request = buildAsciiPacket(quint8(device.modbusAddress), 0x44, 0, body);
        if (!sendRequest(device, request, {0x44, 0xFF}, &responseBody, error, rawResponse))
            return false;

        if (responseBody.size() < 6)
        {
            if (error)
                *error = QStringLiteral("Flash read response is too short");
            return false;
        }

        qint32 responsePage = -1;
        if (quint8(responseBody.at(0)) != quint8(flashNum) || !readInt32(responseBody, 1, &responsePage) || responsePage != pageNum)
        {
            if (error)
                *error = QStringLiteral("Flash read response target mismatch");
            return false;
        }

        const quint8 rxChecksum = quint8(responseBody.at(responseBody.size() - 1));
        const quint8 calculated = flashChecksum(responseBody, 0, responseBody.size() - 2);
        if (rxChecksum != calculated)
        {
            if (error)
                *error = QStringLiteral("Flash read checksum mismatch");
            return false;
        }

        if (page)
            *page = responseBody.mid(5, responseBody.size() - 6);
        return true;
    }

    bool waitForDeviceIdentity(const DeviceIdentity& expected,
        int timeoutMs,
        int pollIntervalMs,
        DeviceIdentity* identity,
        QString* error,
        QString* rawResponse = nullptr) override
    {
        QString expectedHost;
        if (!parseEndpoint(expected.endpoint, &expectedHost, nullptr, error))
            return false;
        const QHostAddress expectedAddress(expectedHost);

        QUdpSocket socket;
        if (!socket.bind(QHostAddress::AnyIPv4, 0, QAbstractSocket::DefaultForPlatform | QAbstractSocket::ReuseAddressHint))
        {
            if (error)
                *error = QStringLiteral("UDP discovery bind failed: %1").arg(socket.errorString());
            return false;
        }

        QElapsedTimer elapsed;
        elapsed.start();
        const int effectiveTimeout = qMax(1, timeoutMs);
        const int effectivePoll = qMax(50, pollIntervalMs);
        qint64 nextSendAt = 0;
        int receivedDatagrams = 0;
        QString lastCandidate;
        QString lastRejection;
        const bool identifyByUuid = !expected.uuid.trimmed().isEmpty();
        auto acceptCandidate = [&](DeviceIdentity found, const QString& candidateRaw) {
            lastCandidate = QStringLiteral("%1 %2 '%3' serial '%4'")
                .arg(found.typeHex(), found.versionHex(), found.description, found.serialNumber);
            if (identifyByUuid && found.uuid.compare(expected.uuid, Qt::CaseInsensitive) != 0)
            {
                lastRejection = QStringLiteral("UUID %1 does not match expected %2")
                    .arg(found.uuid, expected.uuid);
                return false;
            }

            // UUID is the stable device identity across resets, firmware changes
            // and service-data updates. Do not reject that same device because
            // its serial field changed while it was offline. Serial remains a
            // fallback discriminator only for devices without UUID support.
            if (!identifyByUuid
                && !expected.serialNumber.isEmpty() && !found.serialNumber.isEmpty()
                && expected.serialNumber != found.serialNumber)
            {
                lastRejection = QStringLiteral("serial '%1' does not match '%2'")
                    .arg(found.serialNumber, expected.serialNumber);
                return false;
            }
            if (expected.type != 0
                && (found.type != expected.type || found.version != expected.version))
            {
                lastRejection = QStringLiteral("type/version does not match");
                return false;
            }
            const bool bootDescription = descriptionContainsBoot(found.description);
            if (expected.state == QStringLiteral("bootloader") && !bootDescription)
            {
                lastRejection = QStringLiteral("description has no (Boot) marker");
                return false;
            }
            if (expected.state == QStringLiteral("application") && bootDescription)
            {
                lastRejection = QStringLiteral("description still has (Boot) marker");
                return false;
            }

            found.state = bootDescription
                ? QStringLiteral("bootloader")
                : QStringLiteral("application");
            if (rawResponse)
                *rawResponse = QStringLiteral("discovered %1 %2 %3 at %4 UUID=%5%6")
                    .arg(found.typeHex(), found.versionHex(), found.description,
                        found.endpoint, found.uuid,
                        candidateRaw.isEmpty() ? QString() : QStringLiteral("\n%1").arg(candidateRaw));
            if (identity)
                *identity = found;
            return true;
        };
        while (elapsed.elapsed() < effectiveTimeout)
        {
            if (elapsed.elapsed() >= nextSendAt)
            {
                if (!sendDiscovery(&socket, expectedAddress) && rawResponse)
                    *rawResponse = QStringLiteral("UDP discovery send failed: %1").arg(socket.errorString());
                nextSendAt = elapsed.elapsed() + effectivePoll;

                // UDP discovery can be lost while the network stack is starting.
                // Once UUID is known, probe the last endpoint directly as well;
                // UUID validation prevents accepting a different device that took
                // over the old address.
                if (identifyByUuid)
                {
                    DeviceIdentity direct = expected;
                    quint16 directType = 0;
                    quint16 directVersion = 0;
                    QString directDescription;
                    QString directError;
                    QString directRaw;
                    if (readTypeVersionDescription(direct, &directType, &directVersion,
                            &directDescription, &directError, &directRaw))
                    {
                        direct.type = directType;
                        direct.version = directVersion;
                        direct.description = directDescription;
                        direct.serialNumber.clear();
                        QString directUuid;
                        QString uuidError;
                        QString uuidRaw;
                        if (readUuid(direct, &directUuid, &uuidError, &uuidRaw))
                        {
                            direct.uuid = directUuid;
                            if (acceptCandidate(direct,
                                    QStringLiteral("direct TCP identity\n%1\n%2")
                                        .arg(directRaw, uuidRaw)))
                                return true;
                        }
                        else
                        {
                            lastRejection = QStringLiteral("direct UUID read failed: %1").arg(uuidError);
                        }
                    }
                    else
                    {
                        lastRejection = QStringLiteral("direct identity read failed: %1").arg(directError);
                    }
                }
            }

            const int waitMs = qMin(effectivePoll, effectiveTimeout - int(elapsed.elapsed()));
            if (!socket.waitForReadyRead(waitMs))
                continue;

            while (socket.hasPendingDatagrams())
            {
                QHostAddress sender;
                quint16 senderPort = 0;
                QByteArray datagram(int(socket.pendingDatagramSize()), Qt::Uninitialized);
                if (socket.readDatagram(datagram.data(), datagram.size(), &sender, &senderPort) < 0)
                    continue;
                ++receivedDatagrams;
                if (!identifyByUuid && !expectedAddress.isNull() && !sender.isEqual(expectedAddress))
                {
                    lastRejection = QStringLiteral("sender %1 does not match %2").arg(sender.toString(), expectedHost);
                    continue;
                }

                DeviceIdentity found;
                if (!parseDiscoveryDatagram(datagram, sender, &found))
                {
                    lastRejection = QStringLiteral("invalid discovery datagram from %1 (%2 bytes)")
                        .arg(sender.toString()).arg(datagram.size());
                    continue;
                }
                quint16 fullType = 0;
                quint16 fullVersion = 0;
                QString fullDescription;
                QString descriptionError;
                if (readTypeVersionDescription(found, &fullType, &fullVersion,
                        &fullDescription, &descriptionError, nullptr))
                {
                    found.type = fullType;
                    found.version = fullVersion;
                    found.description = fullDescription;
                }

                QString foundUuid;
                QString uuidError;
                QString uuidRaw;
                if (!readUuid(found, &foundUuid, &uuidError, &uuidRaw))
                {
                    lastRejection = QStringLiteral("UUID read failed: %1").arg(uuidError);
                    continue;
                }
                found.uuid = foundUuid;
                if (acceptCandidate(found, uuidRaw))
                    return true;
            }
        }

        if (rawResponse && !lastCandidate.isEmpty())
            *rawResponse = QStringLiteral("last discovery candidate: %1; rejected: %2")
                .arg(lastCandidate, lastRejection);
        if (error)
            *error = QStringLiteral("Device did not reappear within %1 ms; received %2 datagram(s)%3")
                .arg(effectiveTimeout)
                .arg(receivedDatagrams)
                .arg(lastRejection.isEmpty() ? QString() : QStringLiteral("; last rejection: %1").arg(lastRejection));
        return false;
    }
};
} // namespace

std::shared_ptr<IDeviceTransport> createUnicornAsciiTransport()
{
    return std::make_shared<UnicornAsciiTransport>();
}
