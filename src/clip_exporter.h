#pragma once

#include <QObject>
#include <QString>
#include <memory>

class QProcess;
class QTemporaryDir;

class ClipExporter : public QObject
{
    Q_OBJECT
public:
    explicit ClipExporter(QObject *parent = nullptr);
    ~ClipExporter() override;
    bool isBusy() const;
    void start(const QString &ffmpeg, const QString &source, const QString &clipsDirectory,
               qint64 startMs, qint64 endMs);
    void cancel();

signals:
    void progressChanged(int percent);
    void completed(const QString &path);
    void failed(const QString &message);

private:
    void finish(int exitCode);
    void readProgress();
    void fail(const QString &message);
    QProcess *process_;
    std::unique_ptr<QTemporaryDir> staging_;
    QString destination_;
    QByteArray progressBuffer_;
    QByteArray errors_;
    qint64 durationMs_ = 0;
    qint64 framesWritten_ = 0;
    bool busy_ = false;
};
