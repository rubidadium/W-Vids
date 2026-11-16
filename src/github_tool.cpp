#include "github_tool.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTimer>

GithubTool::GithubTool(const QString &directory, QObject *parent)
    : QObject(parent), directory_(directory), process_(new QProcess(this)), timer_(new QTimer(this))
{
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, [this] { fail(QStringLiteral("Download tool setup timed out.")); });
    connect(process_, &QProcess::readyReadStandardError, this, [this] {
        errors_ = (errors_ + process_->readAllStandardError()).right(8192);
    });
    connect(process_, &QProcess::readyReadStandardOutput, this, [this] {
        output_ = (output_ + process_->readAllStandardOutput()).right(8192);
    });
    connect(process_, &QProcess::finished, this, &GithubTool::finish);
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (busy_ && error == QProcess::FailedToStart)
            fail(QStringLiteral("Cannot run the download setup tool: ") + process_->errorString());
    });
}

GithubTool::~GithubTool() { cancel(); }

void GithubTool::ensure(const QString &repository, const QString &asset, const QString &executable)
{
    if (busy_) return;
    repository_ = repository;
    asset_ = asset;
    executable_ = executable;
    destination_ = QDir(directory_).filePath(executable);
    busy_ = true;
    if (!QDir().mkpath(directory_)) {
        fail(QStringLiteral("Cannot create the download tools folder."));
        return;
    }
    if (QFileInfo(destination_).isFile()) {
        candidate_ = destination_;
        run(candidate_, {"--version"}, Step::CheckExisting);
        return;
    }
    lock_ = std::make_unique<QLockFile>(QDir(directory_).filePath(".setup.lock"));
    if (!lock_->tryLock(0)) {
        fail(QStringLiteral("Another download is setting up these tools. Please retry shortly."));
        return;
    }
    staging_ = std::make_unique<QTemporaryDir>(QDir(directory_).filePath(".setup-XXXXXX"));
    if (!staging_->isValid()) {
        fail(QStringLiteral("Cannot write to the download tools folder."));
        return;
    }
    emit statusChanged(QStringLiteral("Setting up %1…").arg(executable));
    fetch(QUrl("https://api.github.com/repos/" + repository + "/releases/latest"),
          staging_->filePath("release.json"), Step::Metadata);
}

void GithubTool::run(const QString &program, const QStringList &arguments, Step step)
{
    step_ = step;
    errors_.clear();
    output_.clear();
    timer_->start(step == Step::Download ? 3600000 : 120000);
    process_->start(program, arguments);
}

void GithubTool::fetch(const QUrl &url, const QString &path, Step step)
{
    run(QDir(qEnvironmentVariable("SystemRoot", "C:/Windows")).filePath("System32/curl.exe"),
        {"--disable", "--fail", "--location", "--silent", "--show-error",
         "--connect-timeout", "20", "--max-time", "3600", "--max-filesize", "268435456",
         "--proto", "=https", "--proto-redir", "=https", "--user-agent", "W-Vids",
         "--output", path, "--url", url.toString()}, step);
}

void GithubTool::finish(int exitCode)
{
    timer_->stop();
    if (!busy_) return;
    errors_ = (errors_ + process_->readAllStandardError()).right(8192);
    output_ += process_->readAllStandardOutput();
    if (exitCode != 0 || process_->exitStatus() != QProcess::NormalExit) {
        fail(QStringLiteral("Download tool setup failed: ") + QString::fromUtf8(errors_).trimmed());
        return;
    }
    if (step_ == Step::CheckExisting) {
        const QString path = destination_;
        cancel();
        emit ready(path);
    } else if (step_ == Step::Metadata) {
        QFile metadata(staging_->filePath("release.json"));
        if (!metadata.open(QIODevice::ReadOnly) || metadata.size() > 4 * 1024 * 1024) {
            fail(QStringLiteral("Cannot read download tool release information."));
            return;
        }
        const auto assets = QJsonDocument::fromJson(metadata.readAll()).object()["assets"].toArray();
        digest_.clear();
        for (const auto &value : assets) {
            const auto item = value.toObject();
            if (item["name"].toString() != asset_) continue;
            const QString digest = item["digest"].toString();
            if (!QRegularExpression("^sha256:[a-fA-F0-9]{64}$").match(digest).hasMatch()) break;
            digest_ = digest.mid(7).toLatin1().toLower();
            assetUrl_ = QUrl(item["browser_download_url"].toString());
            break;
        }
        if (digest_.isEmpty() || assetUrl_.scheme() != "https") {
            fail(QStringLiteral("The download tool release lacks a verified Windows asset."));
            return;
        }
        emit statusChanged(QStringLiteral("Downloading %1…").arg(executable_));
        fetch(assetUrl_, staging_->filePath(asset_), Step::Download);
    } else if (step_ == Step::Download) {
        file_ = std::make_unique<QFile>(staging_->filePath(asset_));
        hash_ = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
        if (!file_->open(QIODevice::ReadOnly)) {
            fail(QStringLiteral("Cannot verify the downloaded tool."));
            return;
        }
        verifyChunk();
    } else if (step_ == Step::Extract) {
        QDirIterator files(staging_->filePath("extracted"), {executable_}, QDir::Files, QDirIterator::Subdirectories);
        if (!files.hasNext()) {
            fail(QStringLiteral("The tool archive is missing its executable."));
            return;
        }
        candidate_ = files.next();
        run(candidate_, {"--version"}, Step::CheckNew);
    } else {
        QFile input(candidate_);
        QSaveFile target(destination_);
        if (output_.trimmed().isEmpty() || !input.open(QIODevice::ReadOnly) || !target.open(QIODevice::WriteOnly)) {
            fail(QStringLiteral("Cannot install the downloaded tool."));
            return;
        }
        while (!input.atEnd()) {
            const auto data = input.read(1024 * 1024);
            if (data.isEmpty() || target.write(data) != data.size()) {
                fail(QStringLiteral("Cannot save the downloaded tool."));
                return;
            }
        }
        if (!target.commit()) {
            fail(QStringLiteral("Cannot finish installing the downloaded tool."));
            return;
        }
        input.close();
        const QString path = destination_;
        cancel();
        emit ready(path);
    }
}

void GithubTool::verifyChunk()
{
    if (!busy_ || !file_) return;
    hash_->addData(file_->read(1024 * 1024));
    if (file_->error() != QFile::NoError) {
        fail(QStringLiteral("Cannot read the downloaded tool."));
        return;
    }
    if (!file_->atEnd()) {
        QTimer::singleShot(0, this, &GithubTool::verifyChunk);
        return;
    }
    file_->close();
    if (hash_->result().toHex() != digest_) {
        fail(QStringLiteral("Download tool checksum verification failed."));
        return;
    }
    if (asset_.endsWith(".zip")) {
        const QString extracted = staging_->filePath("extracted");
        QDir().mkpath(extracted);
        run(QDir(qEnvironmentVariable("SystemRoot", "C:/Windows")).filePath("System32/tar.exe"),
            {"-xf", staging_->filePath(asset_), "-C", extracted}, Step::Extract);
    } else {
        candidate_ = staging_->filePath(asset_);
        run(candidate_, {"--version"}, Step::CheckNew);
    }
}

void GithubTool::fail(const QString &message) { cancel(); emit failed(message); }

void GithubTool::cancel()
{
    busy_ = false;
    timer_->stop();
    if (process_->state() != QProcess::NotRunning) {
        process_->kill();
        process_->waitForFinished(2000);
    }
    file_.reset();
    hash_.reset();
    staging_.reset();
    lock_.reset();
}
