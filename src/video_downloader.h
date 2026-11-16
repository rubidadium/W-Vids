#pragma once

#include <QObject>
#include <QUrl>
#include <memory>

class GithubTool;
class QProcess;
class QTimer;
class QTemporaryDir;

class VideoDownloader : public QObject
{
    Q_OBJECT
public:
    explicit VideoDownloader(const QString &downloadDirectory, const QString &toolsDirectory,
                             QObject *parent = nullptr);
    ~VideoDownloader() override;
    void start(const QUrl &url, const QString &ffmpeg);
    void cancel();
    bool isBusy() const;
signals:
    void statusChanged(const QString &text);
    void progressChanged(int percent); // -1 means the server hasn't provided a total.
    void completed(const QString &path);
    void failed(const QString &message);
private:
    enum class Step { Direct, Extractor, Verify };
    enum class Tool { Ytdlp, Deno };
    void setupExtractor();
    void setupRuntime();
    void downloadMedia();
    void verifyMedia(const QString &path);
    void run(const QString &program, const QStringList &arguments, Step step);
    void readOutput();
    void finish(int exitCode);
    void fail(const QString &message);
    QString downloadDirectory_, toolsDirectory_, ffmpeg_, ytdlp_, runtime_, candidate_;
    QUrl url_;
    GithubTool *tool_;
    QProcess *process_;
    QTimer *timer_;
    std::unique_ptr<QTemporaryDir> staging_;
    QByteArray errors_, output_, parseBuffer_;
    Tool awaitingTool_ = Tool::Ytdlp;
    Step step_ = Step::Direct;
    bool busy_ = false;
    void *job_ = nullptr;
};
