#include "unicorn_ascii_transport.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QTcpSocket>
#include <QVector>

#include <cctype>

namespace
{
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
    while (!packetBuffer.isEmpty())
    {
        QByteArray packet;
        if (!takeAsciiPacket(packetBuffer, &packet, error))
        {
            if (packetBuffer.indexOf('\r') >= 0)
                continue;
            return false;
        }

        if (acceptPacket(packet, device, expectedCommands, responseBody, error))
            return true;
    }

    if (error)
        *error = QStringLiteral("No valid ASCII packet received");
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
};
} // namespace

std::shared_ptr<IDeviceTransport> createUnicornAsciiTransport()
{
    return std::make_shared<UnicornAsciiTransport>();
}
