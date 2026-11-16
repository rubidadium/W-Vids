#pragma once

#include <QObject>
#include <QUrl>
#include <memory>

class QProcess;
class QTimer;
class QFile;
class QCryptographicHash;
class QTemporaryDir;
class QLockFile;

// Installs a single executable from a GitHub release asset, verified against its SHA-256 digest.
class GithubTool : public QObject
{
    Q_OBJECT
public:
    explicit GithubTool(const QString &directory, QObject *parent = nullptr);
    ~GithubTool() override;
    void ensure(const QString &repository, const QString &asset, const QString &executable);
    void cancel();
signals:
    void statusChanged(const QString &text);
    void ready(const QString &path);
    void failed(const QString &message);
private:
    enum class Step { Metadata, Download, Extract, CheckExisting, CheckNew };
    void run(const QString &program, const QStringList &arguments, Step step);
    void fetch(const QUrl &url, const QString &path, Step step);
    void finish(int exitCode);
    void verifyChunk();
    void fail(const QString &message);
    QString directory_, repository_, asset_, executable_, destination_, candidate_;
    QUrl assetUrl_;
    QByteArray digest_, errors_, output_;
    QProcess *process_;
    QTimer *timer_;
    std::unique_ptr<QTemporaryDir> staging_;
    std::unique_ptr<QLockFile> lock_;
    std::unique_ptr<QFile> file_;
    std::unique_ptr<QCryptographicHash> hash_;
    Step step_ = Step::Metadata;
    bool busy_ = false;
};
