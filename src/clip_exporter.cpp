#include "clip_exporter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QUuid>

ClipExporter::ClipExporter(QObject *parent) : QObject(parent), process_(new QProcess(this))
{
    connect(process_, &QProcess::readyReadStandardError, this, [this] {
        errors_ = (errors_ + process_->readAllStandardError()).right(8192);
    });
    connect(process_, &QProcess::readyReadStandardOutput, this, &ClipExporter::readProgress);
    connect(process_, &QProcess::finished, this, &ClipExporter::finish);
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (busy_ && error == QProcess::FailedToStart)
            fail(QStringLiteral("Cannot start FFmpeg: ") + process_->errorString());
    });
}

void ClipExporter::readProgress()
{
    progressBuffer_ += process_->readAllStandardOutput();
    int newline = -1;
    while ((newline = progressBuffer_.indexOf('\n')) >= 0) {
        const QByteArray line = progressBuffer_.left(newline).trimmed();
        progressBuffer_.remove(0, newline + 1);
        if (line.startsWith("frame="))
            framesWritten_ = line.mid(6).toLongLong();
        if (line.startsWith("out_time_us=") && durationMs_ > 0) {
            bool ok = false;
            const qint64 time = line.mid(12).toLongLong(&ok);
            if (ok)
                emit progressChanged(int(qBound(qint64(0), time / 10 / durationMs_, qint64(99))));
        }
    }
}

ClipExporter::~ClipExporter()
{
    cancel();
}

bool ClipExporter::isBusy() const
{
    return busy_;
}

void ClipExporter::start(const QString &ffmpeg, const QString &source, const QString &clipsDirectory,
                         qint64 startMs, qint64 endMs)
{
    if (busy_)
        return;
    if (startMs < 0 || endMs <= startMs || !QFileInfo(source).isFile() || !QFileInfo(ffmpeg).isFile()) {
        emit failed(QStringLiteral("Select a valid clip range and make sure FFmpeg and the source file are available."));
        return;
    }
    if (!QDir().mkpath(clipsDirectory)) {
        emit failed(QStringLiteral("Cannot create the clips folder: ") + clipsDirectory);
        return;
    }
    staging_ = std::make_unique<QTemporaryDir>(QDir(clipsDirectory).filePath(QStringLiteral(".export-XXXXXX")));
    if (!staging_->isValid()) {
        fail(QStringLiteral("Cannot write to the clips folder."));
        return;
    }
    QString name = QFileInfo(source).completeBaseName().left(70);
    name.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}_-]")), QStringLiteral("_"));
    destination_ = QDir(clipsDirectory).filePath(QStringLiteral("%1_%2-%3_%4.mp4")
        .arg(name).arg(startMs).arg(endMs).arg(QUuid::createUuid().toString(QUuid::Id128)));
    durationMs_ = endMs - startMs;
    framesWritten_ = 0;
    progressBuffer_.clear();
    errors_.clear();
    busy_ = true;
    emit progressChanged(0);
    // Re-encoding supports accurate cuts between keyframes and consistent MP4 compatibility.
    // Select the first video/audio streams; subtitles and attachments aren't part of the clip.
    process_->start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"),
        QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-n"),
        QStringLiteral("-ss"), QString::number(startMs / 1000.0, 'f', 3),
        QStringLiteral("-i"), QFileInfo(source).absoluteFilePath(),
        QStringLiteral("-t"), QString::number(durationMs_ / 1000.0, 'f', 3),
        QStringLiteral("-map"), QStringLiteral("0:v:0"), QStringLiteral("-map"), QStringLiteral("0:a:0?"),
        QStringLiteral("-sn"), QStringLiteral("-dn"),
        QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-preset"), QStringLiteral("fast"),
        QStringLiteral("-crf"), QStringLiteral("18"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
        QStringLiteral("-vf"), QStringLiteral("pad=ceil(iw/2)*2:ceil(ih/2)*2"),
        QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-b:a"), QStringLiteral("192k"),
        QStringLiteral("-movflags"), QStringLiteral("+faststart"),
        QStringLiteral("-progress"), QStringLiteral("pipe:1"), QStringLiteral("-nostats"),
        staging_->filePath(QStringLiteral("clip.mp4"))});
}

void ClipExporter::finish(int exitCode)
{
    if (!busy_)
        return;
    readProgress();
    errors_ = (errors_ + process_->readAllStandardError()).right(8192);
    if (process_->exitStatus() != QProcess::NormalExit || exitCode != 0) {
        fail(QStringLiteral("Clip export failed: ") + QString::fromLocal8Bit(errors_).trimmed());
        return;
    }
    const QString rendered = staging_->filePath(QStringLiteral("clip.mp4"));
    if (framesWritten_ <= 0) {
        fail(QStringLiteral("No video frames were exported. Check that the selected range is within the video."));
        return;
    }
    if (QFileInfo(rendered).size() <= 0 || !QFile::rename(rendered, destination_)) {
        fail(QStringLiteral("Could not save the finished clip. Check available disk space and folder permissions."));
        return;
    }
    busy_ = false;
    staging_.reset();
    emit progressChanged(100);
    emit completed(destination_);
}

void ClipExporter::fail(const QString &message)
{
    cancel();
    emit failed(message);
}

void ClipExporter::cancel()
{
    busy_ = false;
    if (process_->state() != QProcess::NotRunning) {
        process_->kill();
        process_->waitForFinished(3000);
    }
    staging_.reset();
}
