#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <memory>

class QFile;
class QTemporaryDir;
class QLockFile;
class QCryptographicHash;
class QProcess;
class QTimer;

class FfmpegInstaller : public QObject
{
    Q_OBJECT

public:
    explicit FfmpegInstaller(const QString &directory, QObject *parent = nullptr,
        const QUrl &archiveUrl = QUrl(QStringLiteral(
            "https://github.com/FFmpeg-Builds/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip")));
    ~FfmpegInstaller() override;
    void start();
    void confirmDownload(bool accepted);
    QString executablePath() const;

signals:
    void statusChanged(const QString &message);
    void ready(const QString &executable);
    void failed(const QString &message);
    void downloadRequired();
    void outputReceived(const QString &text);
    void progressChanged(int percent);

private:
    enum class ProcessStep { CheckExisting, DownloadChecksum, DownloadArchive, Extract, CheckDownloaded };
    void checkExecutable(const QString &path, ProcessStep step);
    void requestDownload();
    void beginDownload();
    void fetchChecksum();
    void fetchArchive();
    void download(const QUrl &url, const QString &destination, ProcessStep step);
    void runProcess(const QString &program, const QStringList &arguments, ProcessStep step, int timeout);
    void readProcessOutput();
    void verifyArchiveChunk();
    void extractArchive();
    void processFinished(int exitCode);
    void install();
    void fail(const QString &message);
    void cleanup();

    QString directory_;
    QUrl archiveUrl_;
    QString executablePath_;
    QString downloadedExecutable_;
    QByteArray expectedHash_;
    QProcess *process_;
    QTimer *processTimer_;
    std::unique_ptr<QTemporaryDir> staging_;
    std::unique_ptr<QLockFile> lock_;
    std::unique_ptr<QFile> archive_;
    std::unique_ptr<QCryptographicHash> hash_;
    ProcessStep processStep_ = ProcessStep::CheckExisting;
    bool busy_ = false;
    bool awaitingConfirmation_ = false;
    qint64 lastProgress_ = -1;
    QByteArray stdout_;
    QByteArray stderr_;
    QByteArray progressBuffer_;
};
