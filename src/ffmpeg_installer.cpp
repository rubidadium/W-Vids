#include "ffmpeg_installer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTimer>

FfmpegInstaller::FfmpegInstaller(const QString &directory, QObject *parent, const QUrl &archiveUrl)
    : QObject(parent), directory_(QDir(directory).absolutePath()), archiveUrl_(archiveUrl),
      process_(new QProcess(this)), processTimer_(new QTimer(this))
{
    processTimer_->setSingleShot(true);
    connect(processTimer_, &QTimer::timeout, this, [this] {
        fail(QStringLiteral("FFmpeg setup timed out while running a local tool."));
    });
    connect(process_, &QProcess::finished, this, &FfmpegInstaller::processFinished);
    connect(process_, &QProcess::readyReadStandardOutput, this, &FfmpegInstaller::readProcessOutput);
    connect(process_, &QProcess::readyReadStandardError, this, &FfmpegInstaller::readProcessOutput);
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!busy_ || error == QProcess::Crashed)
            return; // Crashes are handled by finished().
        if (processStep_ == ProcessStep::CheckExisting) {
            processTimer_->stop();
            requestDownload();
        } else {
            fail(QStringLiteral("Unable to run FFmpeg setup: ") + process_->errorString());
        }
    });
}

FfmpegInstaller::~FfmpegInstaller()
{
    cleanup();
}

QString FfmpegInstaller::executablePath() const
{
    return executablePath_;
}

void FfmpegInstaller::start()
{
    if (busy_)
        return;
    busy_ = true;
    executablePath_.clear();
    emit statusChanged(QStringLiteral("Checking FFmpeg…"));
    if (!QDir().mkpath(directory_)) {
        fail(QStringLiteral("Cannot create the FFmpeg folder: ") + directory_);
        return;
    }
#ifndef Q_OS_WIN
    fail(QStringLiteral("Automatic FFmpeg installation currently supports Windows x64 only."));
    return;
#else
    if (sizeof(void *) != 8) {
        fail(QStringLiteral("Automatic FFmpeg installation requires a 64-bit Windows application."));
        return;
    }
    for (const QString &relative : {QStringLiteral("ffmpeg.exe"), QStringLiteral("bin/ffmpeg.exe")}) {
        const QString candidate = QDir(directory_).filePath(relative);
        if (QFileInfo(candidate).isFile()) {
            checkExecutable(candidate, ProcessStep::CheckExisting);
            return;
        }
    }
    requestDownload();
#endif
}

void FfmpegInstaller::checkExecutable(const QString &path, ProcessStep step)
{
    downloadedExecutable_ = path;
    runProcess(path, {QStringLiteral("-version")}, step, 15000);
}

void FfmpegInstaller::requestDownload()
{
    awaitingConfirmation_ = true;
    emit statusChanged(QStringLiteral("FFmpeg not detected. Waiting for download confirmation."));
    emit downloadRequired();
}

void FfmpegInstaller::confirmDownload(bool accepted)
{
    if (!busy_ || !awaitingConfirmation_)
        return;
    awaitingConfirmation_ = false;
    if (accepted) {
        beginDownload();
    } else {
        cleanup();
        emit statusChanged(QStringLiteral("FFmpeg download skipped"));
    }
}

void FfmpegInstaller::beginDownload()
{
    lock_ = std::make_unique<QLockFile>(QDir(directory_).filePath(QStringLiteral(".install.lock")));
    if (!lock_->tryLock(0)) {
        fail(QStringLiteral("Another application instance is installing FFmpeg. Retry when it finishes."));
        return;
    }
    staging_ = std::make_unique<QTemporaryDir>(
        QDir(directory_).filePath(QStringLiteral(".download-XXXXXX")));
    if (!staging_->isValid()) {
        fail(QStringLiteral("Cannot write to the FFmpeg folder: ") + directory_);
        return;
    }
    fetchChecksum();
}

void FfmpegInstaller::runProcess(const QString &program, const QStringList &arguments,
                                 ProcessStep step, int timeout)
{
    processStep_ = step;
    stdout_.clear();
    stderr_.clear();
    progressBuffer_.clear();
    QStringList displayed;
    for (QString argument : arguments) {
        argument.replace(QLatin1Char('"'), QStringLiteral("\\\""));
        displayed << QLatin1Char('"') + argument + QLatin1Char('"');
    }
    emit outputReceived(QStringLiteral("\n$ \"%1\" %2\n")
        .arg(QDir::toNativeSeparators(program), displayed.join(QLatin1Char(' '))));
    processTimer_->start(timeout);
    process_->start(program, arguments);
}

void FfmpegInstaller::readProcessOutput()
{
    if (!busy_) {
        process_->readAllStandardOutput();
        process_->readAllStandardError();
        return;
    }
    const QByteArray out = process_->readAllStandardOutput();
    const QByteArray err = process_->readAllStandardError();
    stdout_ = (stdout_ + out).right(65536);
    stderr_ = (stderr_ + err).right(8192);
    if (!out.isEmpty())
        emit outputReceived(QString::fromLocal8Bit(out));
    if (!err.isEmpty())
        emit outputReceived(QString::fromLocal8Bit(err));
    if (processStep_ == ProcessStep::DownloadArchive) {
        progressBuffer_ += err;
        const QRegularExpression percent(QStringLiteral("([0-9]{1,3}\\.[0-9]+)%"));
        auto matches = percent.globalMatch(QString::fromLatin1(progressBuffer_));
        while (matches.hasNext()) {
            const int value = qBound(0, int(matches.next().captured(1).toDouble()), 100);
            if (value != lastProgress_) {
                lastProgress_ = value;
                emit progressChanged(value);
            }
        }
        // Keep incomplete lines only, so split reads don't lose percentages.
        const auto boundary = qMax(progressBuffer_.lastIndexOf('\r'), progressBuffer_.lastIndexOf('\n'));
        if (boundary >= 0)
            progressBuffer_.remove(0, boundary + 1);
        progressBuffer_ = progressBuffer_.right(4096);
    }
}

void FfmpegInstaller::download(const QUrl &url, const QString &destination, ProcessStep step)
{
    const QString curl = QDir(qEnvironmentVariable("SystemRoot", "C:/Windows"))
                             .filePath(QStringLiteral("System32/curl.exe"));
    const QString protocol = url.scheme() == QStringLiteral("https")
        ? QStringLiteral("=https") : QStringLiteral("=http");
    runProcess(curl, {QStringLiteral("--disable"), QStringLiteral("--fail"),
        QStringLiteral("--location"), QStringLiteral("--max-redirs"), QStringLiteral("5"),
        QStringLiteral("--progress-bar"), QStringLiteral("--connect-timeout"), QStringLiteral("20"),
        QStringLiteral("--speed-limit"), QStringLiteral("1024"), QStringLiteral("--speed-time"), QStringLiteral("60"),
        QStringLiteral("--max-time"), QStringLiteral("3600"),
        QStringLiteral("--max-filesize"), QStringLiteral("536870912"),
        QStringLiteral("--proto"), protocol, QStringLiteral("--proto-redir"), protocol,
        QStringLiteral("--output"), destination, QStringLiteral("--url"), url.toString()}, step, 3660000);
}

void FfmpegInstaller::fetchChecksum()
{
    emit statusChanged(QStringLiteral("Fetching release information…"));
    emit progressChanged(-1);
    download(archiveUrl_.resolved(QUrl(QStringLiteral("checksums.sha256"))),
             staging_->filePath(QStringLiteral("checksum.txt")), ProcessStep::DownloadChecksum);
}

void FfmpegInstaller::fetchArchive()
{
    emit statusChanged(QStringLiteral("Downloading FFmpeg…"));
    lastProgress_ = -1;
    emit progressChanged(0);
    download(archiveUrl_, staging_->filePath(QStringLiteral("ffmpeg.zip")), ProcessStep::DownloadArchive);
}

void FfmpegInstaller::verifyArchiveChunk()
{
    if (!busy_ || !archive_)
        return;
    const QByteArray chunk = archive_->read(1024 * 1024);
    if (archive_->error() != QFile::NoError) {
        fail(QStringLiteral("Cannot read the downloaded FFmpeg archive."));
        return;
    }
    hash_->addData(chunk);
    if (!archive_->atEnd()) {
        QTimer::singleShot(0, this, &FfmpegInstaller::verifyArchiveChunk);
        return;
    }
    archive_->close();
    if (hash_->result().toHex() != expectedHash_) {
        fail(QStringLiteral("FFmpeg checksum verification failed. Please retry."));
        return;
    }
    emit outputReceived(QStringLiteral("\nSHA-256 checksum verified.\n"));
    extractArchive();
}

void FfmpegInstaller::extractArchive()
{
    emit statusChanged(QStringLiteral("Extracting and checking FFmpeg…"));
    const QString destination = staging_->filePath(QStringLiteral("extracted"));
    if (!QDir().mkpath(destination)) {
        fail(QStringLiteral("Cannot create the FFmpeg extraction folder."));
        return;
    }
    const QString tar = QDir(qEnvironmentVariable("SystemRoot", "C:/Windows"))
                            .filePath(QStringLiteral("System32/tar.exe"));
    runProcess(tar, {QStringLiteral("-xf"), archive_->fileName(),
                         QStringLiteral("-C"), destination}, ProcessStep::Extract, 120000);
}

void FfmpegInstaller::processFinished(int exitCode)
{
    processTimer_->stop();
    if (!busy_)
        return;
    readProcessOutput();
    emit outputReceived(QStringLiteral("\nProcess exited with code %1.\n").arg(exitCode));
    const bool success = process_->exitStatus() == QProcess::NormalExit && exitCode == 0;
    if (processStep_ == ProcessStep::DownloadChecksum || processStep_ == ProcessStep::DownloadArchive) {
        if (!success) {
            fail(QStringLiteral("FFmpeg download failed (curl exit %1). Show Details for command output.").arg(exitCode));
            return;
        }
        if (processStep_ == ProcessStep::DownloadChecksum) {
            QFile checksum(staging_->filePath(QStringLiteral("checksum.txt")));
            if (!checksum.open(QIODevice::ReadOnly) || checksum.size() > 1024 * 1024) {
                fail(QStringLiteral("Cannot read FFmpeg checksum information."));
                return;
            }
            const QRegularExpression entry(QStringLiteral("^([a-fA-F0-9]{64})\\s+\\*?(.+)$"));
            expectedHash_.clear();
            for (const QString &line : QString::fromUtf8(checksum.readAll()).split(QLatin1Char('\n'))) {
                const auto match = entry.match(line.trimmed());
                if (match.hasMatch() && match.captured(2) == archiveUrl_.fileName()) {
                    if (!expectedHash_.isEmpty()) {
                        fail(QStringLiteral("The provider returned duplicate checksums for the FFmpeg archive."));
                        return;
                    }
                    expectedHash_ = match.captured(1).toLatin1().toLower();
                }
            }
            if (expectedHash_.isEmpty()) {
                fail(QStringLiteral("The provider's checksums do not include the selected FFmpeg archive."));
                return;
            }
            fetchArchive();
        } else {
            emit progressChanged(100);
            emit statusChanged(QStringLiteral("Verifying download…"));
            archive_ = std::make_unique<QFile>(staging_->filePath(QStringLiteral("ffmpeg.zip")));
            if (!archive_->open(QIODevice::ReadOnly)) {
                fail(QStringLiteral("Cannot open the downloaded FFmpeg archive."));
                return;
            }
            hash_ = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
            verifyArchiveChunk();
        }
        return;
    }
    if (processStep_ == ProcessStep::Extract) {
        if (!success) {
            fail(QStringLiteral("Cannot extract FFmpeg: ")
                 + QString::fromLocal8Bit(stderr_).left(1000));
            return;
        }
        QDirIterator files(staging_->filePath(QStringLiteral("extracted")),
                           {QStringLiteral("ffmpeg.exe")}, QDir::Files, QDirIterator::Subdirectories);
        if (!files.hasNext()) {
            fail(QStringLiteral("The downloaded archive does not contain ffmpeg.exe."));
            return;
        }
        checkExecutable(files.next(), ProcessStep::CheckDownloaded);
        return;
    }
    const QByteArray output = stdout_;
    if (!success || !output.startsWith("ffmpeg version ")) {
        if (processStep_ == ProcessStep::CheckExisting)
            requestDownload();
        else
            fail(QStringLiteral("The downloaded FFmpeg executable failed its version check."));
        return;
    }
    if (processStep_ == ProcessStep::CheckDownloaded) {
        install();
    } else {
        executablePath_ = downloadedExecutable_;
        cleanup();
        emit ready(executablePath_);
    }
}

void FfmpegInstaller::install()
{
    // QSaveFile publishes complete files atomically; a failed download never becomes ffmpeg.exe.
    const QDir source = QFileInfo(downloadedExecutable_).dir();
    for (const QString &name : {QStringLiteral("LICENSE"), QStringLiteral("README.txt"),
                                QStringLiteral("ffprobe.exe"), QStringLiteral("ffmpeg.exe")}) {
        const bool tool = name.endsWith(QStringLiteral(".exe"));
        QFile input(tool ? source.filePath(name) : source.filePath(QStringLiteral("../") + name));
        if (!input.exists() && name != QStringLiteral("ffmpeg.exe"))
            continue;
        QSaveFile output(QDir(directory_).filePath(name));
        if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {
            fail(QStringLiteral("Cannot install ") + name + QStringLiteral(" in ") + directory_);
            return;
        }
        while (!input.atEnd()) {
            const QByteArray chunk = input.read(1024 * 1024);
            if (chunk.isEmpty() || output.write(chunk) != chunk.size()) {
                fail(QStringLiteral("Failed to copy ") + name + QStringLiteral(" into the FFmpeg folder."));
                return;
            }
        }
        if (!output.commit()) {
            fail(QStringLiteral("Cannot finish installing ") + name + QStringLiteral(": ") + output.errorString());
            return;
        }
    }
    executablePath_ = QDir(directory_).filePath(QStringLiteral("ffmpeg.exe"));
    cleanup();
    emit ready(executablePath_);
}

void FfmpegInstaller::fail(const QString &message)
{
    cleanup();
    emit failed(message);
}

void FfmpegInstaller::cleanup()
{
    busy_ = false;
    awaitingConfirmation_ = false;
    processTimer_->stop();
    if (process_->state() != QProcess::NotRunning) {
        process_->kill();
        process_->waitForFinished(1000);
    }
    archive_.reset();
    hash_.reset();
    staging_.reset();
    lock_.reset();
}
