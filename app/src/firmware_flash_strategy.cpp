#include "firmware_flash_strategy.h"

#include <QMap>
#include <QThread>
#include <algorithm>
#include <utility>

namespace
{
struct IntelHexImage
{
    QMap<int, QByteArray> pages;
    int dataBytes = 0;
    int ignoredBytes = 0;
};

bool parseIntelHex(const QByteArray& fileData,
    quint32 addressBase,
    int flashSize,
    int flashOffset,
    int pageSize,
    IntelHexImage* image,
    QString* error,
    bool syntaxOnly = false)
{
    if (!image || (!syntaxOnly && (flashSize <= 0 || flashOffset < 0
        || flashOffset >= flashSize || pageSize <= 0)))
    {
        if (error)
            *error = QStringLiteral("Invalid Intel HEX target range");
        return false;
    }

    image->pages.clear();
    image->dataBytes = 0;
    image->ignoredBytes = 0;
    quint32 upperAddress = 0;
    bool eofSeen = false;
    const QList<QByteArray> lines = fileData.split('\n');
    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
    {
        const QByteArray line = lines.at(lineIndex).trimmed();
        if (line.isEmpty())
            continue;
        if (!line.startsWith(':') || ((line.size() - 1) % 2) != 0)
        {
            if (error)
                *error = QStringLiteral("Bad Intel HEX record at line %1").arg(lineIndex + 1);
            return false;
        }

        const QByteArray encoded = line.mid(1);
        for (char character : encoded)
        {
            const bool isHex = (character >= '0' && character <= '9')
                || (character >= 'a' && character <= 'f')
                || (character >= 'A' && character <= 'F');
            if (!isHex)
            {
                if (error)
                    *error = QStringLiteral("Bad Intel HEX character at line %1").arg(lineIndex + 1);
                return false;
            }
        }

        const QByteArray record = QByteArray::fromHex(encoded);
        if (record.size() < 5 || record.size() != quint8(record.at(0)) + 5)
        {
            if (error)
                *error = QStringLiteral("Bad Intel HEX length at line %1").arg(lineIndex + 1);
            return false;
        }
        quint8 checksum = 0;
        for (char byte : record)
            checksum = quint8(checksum + quint8(byte));
        if (checksum != 0)
        {
            if (error)
                *error = QStringLiteral("Bad Intel HEX checksum at line %1").arg(lineIndex + 1);
            return false;
        }

        const int byteCount = quint8(record.at(0));
        const quint16 offset = quint16((quint16(quint8(record.at(1))) << 8) | quint8(record.at(2)));
        const quint8 type = quint8(record.at(3));
        if (type == 0x00)
        {
            if (syntaxOnly)
            {
                image->dataBytes += byteCount;
                continue;
            }
            const quint64 absoluteAddress = quint64(upperAddress) + offset;
            const quint64 rangeBegin = addressBase;
            const quint64 rangeEnd = rangeBegin + quint64(flashSize - flashOffset);
            const quint64 recordEnd = absoluteAddress + quint64(byteCount);
            if (absoluteAddress >= rangeBegin && recordEnd <= rangeEnd)
            {
                int targetAddress = flashOffset + int(absoluteAddress - rangeBegin);
                int sourceOffset = 0;
                while (sourceOffset < byteCount)
                {
                    const int pageNum = targetAddress / pageSize;
                    const int pageOffset = targetAddress % pageSize;
                    const int chunkSize = qMin(byteCount - sourceOffset, pageSize - pageOffset);
                    auto pageIt = image->pages.find(pageNum);
                    if (pageIt == image->pages.end())
                        pageIt = image->pages.insert(pageNum, QByteArray(pageSize, char(0xFF)));
                    std::copy(record.constBegin() + 4 + sourceOffset,
                        record.constBegin() + 4 + sourceOffset + chunkSize,
                        pageIt.value().begin() + pageOffset);
                    sourceOffset += chunkSize;
                    targetAddress += chunkSize;
                }
                image->dataBytes += byteCount;
            }
            else if (recordEnd <= rangeBegin || absoluteAddress >= rangeEnd)
            {
                image->ignoredBytes += byteCount;
            }
            else
            {
                if (error)
                    *error = QStringLiteral("Intel HEX record crosses flash boundary at line %1")
                        .arg(lineIndex + 1);
                return false;
            }
        }
        else if (type == 0x01)
        {
            eofSeen = true;
            break;
        }
        else if (type == 0x04 && byteCount == 2)
        {
            upperAddress = quint32((quint32(quint8(record.at(4))) << 8)
                | quint8(record.at(5))) << 16;
        }
        else if (type != 0x05)
        {
            if (error)
                *error = QStringLiteral("Unsupported Intel HEX record type %1 at line %2")
                    .arg(type)
                    .arg(lineIndex + 1);
            return false;
        }
    }

    if (!eofSeen || (syntaxOnly ? image->dataBytes == 0 : image->pages.isEmpty()))
    {
        if (error)
            *error = !eofSeen
                ? QStringLiteral("Intel HEX end-of-file record is missing")
                : QStringLiteral("Intel HEX has no data in the configured flash range");
        return false;
    }
    return true;
}

bool isIntelHex(const FirmwareFlashPlan& plan)
{
    return plan.artifact.format.compare(QStringLiteral("intelHex"), Qt::CaseInsensitive) == 0
        || plan.fileName.endsWith(QStringLiteral(".hex"), Qt::CaseInsensitive)
        || plan.fileName.endsWith(QStringLiteral(".ldr"), Qt::CaseInsensitive);
}

void call(const std::function<void(const QString&)>& callback, const QString& message)
{
    if (callback)
        callback(message);
}

class PageFlashStrategy final : public FirmwareFlashStrategy
{
public:
    QString id() const override { return QStringLiteral("page-flash"); }

    bool prepare(DeviceBase& device,
        FirmwareFlashPlan& plan,
        const FirmwareFlashCallbacks& callbacks) const override
    {
        plan.writePlan.reset();
        const DeviceIdentity& identity = device.identity();
        QVector<FlashMemoryParams> params;
        QString error;
        QString raw;
        bool paramsLoaded = false;
        const int paramsAttempts = qMax(1, plan.strategyParameters.value(QStringLiteral("paramsRetry"), 4).toInt());
        for (int attempt = 1; attempt <= paramsAttempts; ++attempt)
        {
            error.clear();
            raw.clear();
            if (device.flashGetParams(&params, &error, &raw))
            {
                paramsLoaded = true;
                break;
            }
            if (!raw.isEmpty())
                call(callbacks.transportLog, QStringLiteral("[%1] %2").arg(identity.typeHex(), raw));
            if (attempt < paramsAttempts)
            {
                call(callbacks.log, QStringLiteral("[%1] flashGetParams retry %2/%3 after: %4")
                    .arg(identity.typeHex()).arg(attempt + 1).arg(paramsAttempts).arg(error));
                QThread::msleep(500);
            }
        }
        if (!paramsLoaded)
        {
            call(callbacks.log, QStringLiteral("[%1] flashGetParams failed after %2 attempt(s): %3")
                .arg(identity.typeHex()).arg(paramsAttempts).arg(error));
            return false;
        }
        if (!raw.isEmpty())
            call(callbacks.transportLog, QStringLiteral("[%1] %2").arg(identity.typeHex(), raw));

        const int flashNum = plan.flashNum;
        if (flashNum < 0 || flashNum >= params.size() || !params.at(flashNum).isValid())
        {
            call(callbacks.log, QStringLiteral("[%1] flash #%2 is not available").arg(identity.typeHex()).arg(flashNum));
            return false;
        }

        plan.pageSize = params.at(flashNum).pageSize;
        const int pagesCount = params.at(flashNum).pagesCount;
        const bool intelHex = isIntelHex(plan);
        FirmwareWritePlan prepared;
        prepared.flashNum = flashNum;
        prepared.pageSize = plan.pageSize;
        if (intelHex)
        {
            const int flashSize = pagesCount * plan.pageSize;
            IntelHexImage image;
            if (!parseIntelHex(plan.data, plan.artifact.addressBase, flashSize,
                    plan.offset, plan.pageSize, &image, &error))
            {
                call(callbacks.log, QStringLiteral("[%1] Intel HEX parse failed: %2").arg(identity.typeHex(), error));
                return false;
            }
            for (auto pageIt = image.pages.cbegin(); pageIt != image.pages.cend(); ++pageIt)
            {
                prepared.pageNumbers.append(pageIt.key());
                prepared.pages.append(pageIt.value());
            }
            plan.data.clear();
            call(callbacks.log, QStringLiteral("[%1] Intel HEX loaded %2 data bytes into %3 flash pages from base 0x%4; ignored %5 out-of-range bytes")
                .arg(identity.typeHex()).arg(image.dataBytes).arg(image.pages.size())
                .arg(plan.artifact.addressBase, 8, 16, QLatin1Char('0')).arg(image.ignoredBytes));
        }
        else
        {
            const int firstPage = qMax(0, plan.offset / plan.pageSize);
            const int firstPageOffset = qMax(0, plan.offset % plan.pageSize);
            const int pagesToWrite = (firstPageOffset + plan.data.size() + plan.pageSize - 1) / plan.pageSize;
            if (pagesToWrite <= 0 || firstPage + pagesToWrite > pagesCount)
            {
                call(callbacks.log, QStringLiteral("[%1] firmware does not fit flash #%2: size=%3 pageSize=%4 pages=%5")
                    .arg(identity.typeHex()).arg(flashNum).arg(plan.data.size()).arg(plan.pageSize).arg(pagesCount));
                return false;
            }

            for (int pageIndex = 0; pageIndex < pagesToWrite; ++pageIndex)
            {
                QByteArray page(plan.pageSize, char(0xFF));
                const int targetOffset = pageIndex == 0 ? firstPageOffset : 0;
                const int sourceOffset = qMax(0, pageIndex * plan.pageSize - firstPageOffset);
                const int chunkSize = qMin(plan.pageSize - targetOffset, plan.data.size() - sourceOffset);
                std::copy(plan.data.constBegin() + sourceOffset,
                    plan.data.constBegin() + sourceOffset + chunkSize,
                    page.begin() + targetOffset);
                prepared.pageNumbers.append(firstPage + pageIndex);
                prepared.pages.append(page);
            }
        }

        if (prepared.pages.isEmpty()
            || prepared.pageNumbers.size() != prepared.pages.size())
        {
            call(callbacks.log, QStringLiteral("[%1] firmware contains no writable flash pages")
                .arg(identity.typeHex()));
            return false;
        }

        plan.writePlan = std::make_shared<const FirmwareWritePlan>(std::move(prepared));
        call(callbacks.log, QStringLiteral("[%1] prepared %2 populated pages for flash #%3")
            .arg(identity.typeHex()).arg(plan.writePlan->pages.size()).arg(flashNum));
        return true;
    }

    bool flash(DeviceBase& device,
        const FirmwareFlashPlan& plan,
        const FirmwareFlashCallbacks& callbacks) const override
    {
        const DeviceIdentity& identity = device.identity();
        if (!plan.writePlan || plan.writePlan->pages.isEmpty())
        {
            call(callbacks.log, QStringLiteral("[%1] flash write requested without a prepared page plan")
                .arg(identity.typeHex()));
            return false;
        }

        const FirmwareWritePlan& prepared = *plan.writePlan;
        const int pagesToWrite = prepared.pages.size();
        const int flashNum = prepared.flashNum;
        QString error;
        QString raw;
        call(callbacks.log, QStringLiteral("[%1] writing %2 populated pages to flash #%3, page numbers %4-%5")
            .arg(identity.typeHex()).arg(pagesToWrite).arg(flashNum)
            .arg(prepared.pageNumbers.first()).arg(prepared.pageNumbers.last()));

        for (int pageIndex = 0; pageIndex < pagesToWrite; ++pageIndex)
        {
            if (callbacks.shouldCancel && callbacks.shouldCancel())
            {
                call(callbacks.log, QStringLiteral("[%1] flash interrupted before page %2")
                    .arg(identity.typeHex()).arg(prepared.pageNumbers.at(pageIndex)));
                return false;
            }
            const int pageNum = prepared.pageNumbers.at(pageIndex);
            const QByteArray& page = prepared.pages.at(pageIndex);

            raw.clear();
            if (!device.flashWritePage(flashNum, pageNum, page, &error, &raw))
            {
                if (!raw.isEmpty())
                    call(callbacks.transportLog, QStringLiteral("[%1] %2").arg(identity.typeHex(), raw));
                call(callbacks.log, QStringLiteral("[%1] flash page %2 write failed: %3")
                    .arg(identity.typeHex()).arg(pageNum).arg(error));
                return false;
            }
            if (!raw.isEmpty())
                call(callbacks.transportLog, QStringLiteral("[%1] %2").arg(identity.typeHex(), raw));
            if (callbacks.progress)
                callbacks.progress(((pageIndex + 1) * 80) / pagesToWrite);
            call(callbacks.log, QStringLiteral("[%1] flash #%2 wrote page %3 of %4")
                .arg(identity.typeHex()).arg(flashNum).arg(pageIndex + 1).arg(pagesToWrite));
            if (callbacks.processEvents)
                callbacks.processEvents();
        }
        return true;
    }
};

class TestNoWriteStrategy final : public FirmwareFlashStrategy
{
public:
    QString id() const override { return QStringLiteral("test-no-write"); }

    bool prepare(DeviceBase&, FirmwareFlashPlan& plan,
        const FirmwareFlashCallbacks&) const override
    {
        plan.writePlan = std::make_shared<const FirmwareWritePlan>();
        return true;
    }

    bool flash(DeviceBase& device,
        const FirmwareFlashPlan&,
        const FirmwareFlashCallbacks& callbacks) const override
    {
        call(callbacks.log, QStringLiteral("[%1] writeFlash is intentionally not sent to hardware")
            .arg(device.identity().typeHex()));
        if (callbacks.progress)
            callbacks.progress(100);
        return true;
    }
};
}

bool validateFirmwareImage(const FirmwareFlashPlan& plan, QString* error)
{
    if (plan.data.isEmpty())
    {
        if (error)
            *error = QStringLiteral("Firmware file is empty");
        return false;
    }
    if (!isIntelHex(plan))
        return true;
    IntelHexImage image;
    return parseIntelHex(plan.data, 0, 0, 0, 0, &image, error, true);
}

const FirmwareFlashStrategy* FirmwareFlashStrategyRegistry::find(const QString& strategyId)
{
    static const PageFlashStrategy pageFlash;
    static const TestNoWriteStrategy testNoWrite;
    if (strategyId == pageFlash.id())
        return &pageFlash;
    if (strategyId == testNoWrite.id())
        return &testNoWrite;
    return nullptr;
}
