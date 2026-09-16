#ifndef DEVICE_WORKBENCH_EXECUTION_JOURNAL_H
#define DEVICE_WORKBENCH_EXECUTION_JOURNAL_H

#include <QJsonObject>
#include <QString>
#include <QVector>

// Append-only operational history. Events contain identifiers and stages only;
// firmware data, transport packets and factory settings must never be stored.
class ExecutionJournal
{
public:
    explicit ExecutionJournal(QString filePath = {});

    QString filePath() const { return mFilePath; }
    bool append(QJsonObject event, QString* error = nullptr) const;
    QVector<QJsonObject> recoverInterrupted(QString* error = nullptr) const;

private:
    QString mFilePath;
};

#endif // DEVICE_WORKBENCH_EXECUTION_JOURNAL_H
