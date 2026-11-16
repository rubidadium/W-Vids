#include "video_downloader.h"
#include "github_tool.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

VideoDownloader::VideoDownloader(const QString &downloadDirectory, const QString &toolsDirectory, QObject *parent)
    : QObject(parent), downloadDirectory_(downloadDirectory), toolsDirectory_(toolsDirectory),
      tool_(new GithubTool(toolsDirectory, this)), process_(new QProcess(this)), timer_(new QTimer(this))
{
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, [this] { fail(QStringLiteral("The video download timed out. Please retry.")); });
    connect(tool_, &GithubTool::statusChanged, this, &VideoDownloader::statusChanged);
    connect(tool_, &GithubTool::failed, this, &VideoDownloader::fail);
    connect(tool_, &GithubTool::ready, this, [this](const QString &path) {
        if (!busy_) return;
        if (awaitingTool_ == Tool::Ytdlp) {
            ytdlp_ = path;
            setupRuntime();
        } else {
            runtime_ = "deno:" + path;
            downloadMedia();
        }
    });
    connect(process_, &QProcess::started, this, [this] {
#ifdef Q_OS_WIN
        HANDLE child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(process_->processId()));
        const bool assigned = child && AssignProcessToJobObject(static_cast<HANDLE>(job_), child);
        if (child) CloseHandle(child);
        if (!assigned)
            fail(QStringLiteral("Cannot manage the download process safely. Please restart the application."));
#endif
    });
    connect(process_, &QProcess::readyReadStandardOutput, this, &VideoDownloader::readOutput);
    connect(process_, &QProcess::readyReadStandardError, this, &VideoDownloader::readOutput);
    connect(process_, &QProcess::finished, this, &VideoDownloader::finish);
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (busy_ && error == QProcess::FailedToStart)
            fail(QStringLiteral("Cannot start the download tool: ") + process_->errorString());
    });
}

VideoDownloader::~VideoDownloader() { cancel(); }
bool VideoDownloader::isBusy() const { return busy_; }

void VideoDownloader::start(const QUrl &url, const QString &ffmpeg)
{
    if (busy_) return;
    if (!url.isValid() || (url.scheme() != "https" && url.scheme() != "http") || url.host().isEmpty()) {
        emit failed(QStringLiteral("Enter a valid http:// or https:// video link."));
        return;
    }
    if (!QFileInfo(ffmpeg).isFile()) {
        emit failed(QStringLiteral("FFmpeg must be installed before loading a link."));
        return;
    }
    if (!QDir().mkpath(downloadDirectory_)) {
        emit failed(QStringLiteral("Cannot create the download folder."));
        return;
    }
    staging_ = std::make_unique<QTemporaryDir>(QDir(downloadDirectory_).filePath(".download-XXXXXX"));
    if (!staging_->isValid()) {
        fail(QStringLiteral("Cannot write to the download folder."));
        return;
    }
    busy_ = true;
    url_ = url;
    ffmpeg_ = ffmpeg;
    ytdlp_.clear();
    runtime_.clear();
    emit progressChanged(-1);
    const QString suffix = QFileInfo(url.path()).suffix().toLower();
    const QStringList direct = {"mp4", "mkv", "mov", "avi", "webm", "m4v", "mpg", "mpeg", "wmv", "flv", "ts", "mts", "m2ts", "ogv"};
    if (direct.contains(suffix))
        downloadMedia();
    else
        setupExtractor();
}

void VideoDownloader::setupExtractor()
{
    awaitingTool_ = Tool::Ytdlp;
    tool_->ensure("yt-dlp/yt-dlp", "yt-dlp.exe", "yt-dlp.exe");
}

void VideoDownloader::setupRuntime()
{
    const QString host = url_.host().toLower();
    const bool youtube = host == "youtu.be" || host == "youtube.com" || host.endsWith(".youtube.com")
                         || host == "youtube-nocookie.com" || host.endsWith(".youtube-nocookie.com");
    QString deno = QStandardPaths::findExecutable("deno");
    if (deno.isEmpty() && QFileInfo(QDir(toolsDirectory_).filePath("deno.exe")).isFile())
        deno = QDir(toolsDirectory_).filePath("deno.exe");
    const QString node = QStandardPaths::findExecutable("node");
    if (!deno.isEmpty()) runtime_ = "deno:" + deno;
    else if (!node.isEmpty()) runtime_ = "node:" + node;
    if (youtube && runtime_.isEmpty()) {
        awaitingTool_ = Tool::Deno;
        tool_->ensure("denoland/deno", "deno-x86_64-pc-windows-msvc.zip", "deno.exe");
    } else {
        downloadMedia();
    }
}

void VideoDownloader::downloadMedia()
{
    const QString curl = QDir(qEnvironmentVariable("SystemRoot", "C:/Windows")).filePath("System32/curl.exe");
    emit statusChanged(QStringLiteral("Downloading video…"));
    if (ytdlp_.isEmpty()) {
        candidate_ = staging_->filePath("video." + QFileInfo(url_.path()).suffix().toLower());
        run(curl, {"--disable", "--fail", "--location", "--max-redirs", "10", "--progress-bar",
            "--connect-timeout", "20", "--max-time", "7200", "--speed-limit", "1024", "--speed-time", "60",
            "--proto", "=http,https", "--proto-redir", "=http,https",
            "--output", candidate_, "--url", url_.toString(QUrl::FullyEncoded)}, Step::Direct);
    } else {
        QStringList args = {"--ignore-config", "--no-plugin-dirs", "--no-playlist", "--playlist-items", "1",
            "--match-filter", "!is_live", "--no-simulate", "--newline", "--progress", "--no-colors",
            "--encoding", "utf-8", "--socket-timeout", "30", "--retries", "3", "--fragment-retries", "3",
            "--ffmpeg-location", ffmpeg_, "--merge-output-format", "mkv",
            "--downloader", "http,https:" + curl, "--downloader", "dash,m3u8:native",
            "--downloader-args", "curl:--no-silent --progress-bar",
            "--progress-template", "download:WVIDS_PROGRESS:%(progress._percent_str)s",
            "--windows-filenames", "--trim-filenames", "100",
            "--output", staging_->filePath("%(title).80B [%(id)s].%(ext)s"),
            "--print-to-file", "after_move:%(filepath)s", staging_->filePath("result.txt")};
        if (!runtime_.isEmpty()) args << "--js-runtimes" << runtime_;
        args << "--" << url_.toString(QUrl::FullyEncoded);
        run(ytdlp_, args, Step::Extractor);
    }
}

void VideoDownloader::run(const QString &program, const QStringList &arguments, Step step)
{
    step_ = step;
    errors_.clear();
    output_.clear();
    parseBuffer_.clear();
#ifdef Q_OS_WIN
    if (job_) CloseHandle(static_cast<HANDLE>(job_));
    job_ = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job_ || !SetInformationJobObject(static_cast<HANDLE>(job_), JobObjectExtendedLimitInformation,
                                         &limits, sizeof(limits))) {
        fail(QStringLiteral("Cannot create the download process group."));
        return;
    }
#endif
    timer_->start(step == Step::Verify ? 60000 : 2 * 60 * 60 * 1000);
    process_->start(program, arguments);
}

void VideoDownloader::readOutput()
{
    const auto out = process_->readAllStandardOutput();
    const auto err = process_->readAllStandardError();
    output_ = (output_ + out).right(16384);
    errors_ = (errors_ + err).right(8192);
    if (!busy_ || step_ == Step::Verify) return;
    parseBuffer_ += out + err;
    const QRegularExpression percent(QStringLiteral("([0-9]{1,3}(?:\\.[0-9]+)?)%"));
    auto matches = percent.globalMatch(QString::fromUtf8(parseBuffer_));
    while (matches.hasNext())
        emit progressChanged(qBound(0, int(matches.next().captured(1).toDouble()), 100));
    const auto boundary = qMax(parseBuffer_.lastIndexOf('\r'), parseBuffer_.lastIndexOf('\n'));
    if (boundary >= 0) parseBuffer_.remove(0, boundary + 1);
    parseBuffer_ = parseBuffer_.right(4096);
}

void VideoDownloader::verifyMedia(const QString &path)
{
    const QString canonical = QFileInfo(path).canonicalFilePath();
    const QString stage = QDir(staging_->path()).canonicalPath() + QDir::separator();
    if (canonical.isEmpty() || !QDir::toNativeSeparators(canonical).startsWith(
            QDir::toNativeSeparators(stage), Qt::CaseInsensitive) || QFileInfo(canonical).size() <= 0) {
        fail(QStringLiteral("The download did not produce a valid video file."));
        return;
    }
    candidate_ = canonical;
    emit statusChanged(QStringLiteral("Checking downloaded video…"));
    emit progressChanged(-1);
    run(ffmpeg_, {"-v", "error", "-nostdin", "-i", candidate_,
        "-map", "0:v:0", "-frames:v", "1", "-f", "null", "-"}, Step::Verify);
}

void VideoDownloader::finish(int exitCode)
{
    timer_->stop();
    if (!busy_) return;
    readOutput();
#ifdef Q_OS_WIN
    if (job_) { CloseHandle(static_cast<HANDLE>(job_)); job_ = nullptr; }
#endif
    if (exitCode != 0 || process_->exitStatus() != QProcess::NormalExit) {
        fail(QStringLiteral("Could not load this video link. ") + QString::fromUtf8(errors_).trimmed());
        return;
    }
    if (step_ == Step::Direct) {
        verifyMedia(candidate_);
    } else if (step_ == Step::Extractor) {
        QFile result(staging_->filePath("result.txt"));
        if (!result.open(QIODevice::ReadOnly) || result.size() > 32768) {
            fail(QStringLiteral("The link did not produce a downloadable video."));
            return;
        }
        verifyMedia(QString::fromUtf8(result.readAll()).trimmed());
    } else {
        QString name = ytdlp_.isEmpty() ? QFileInfo(url_.path()).completeBaseName()
                                      : QFileInfo(candidate_).completeBaseName();
        name = name.left(80);
        name.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}_-]")), "_");
        if (name.isEmpty()) name = "video";
        const QString destination = QDir(downloadDirectory_).filePath(name + "_"
            + QUuid::createUuid().toString(QUuid::Id128) + "." + QFileInfo(candidate_).suffix());
        if (!QFile::rename(candidate_, destination)) {
            fail(QStringLiteral("Cannot save the completed video in the download folder."));
            return;
        }
        cancel();
        emit progressChanged(100);
        emit completed(destination);
    }
}

void VideoDownloader::fail(const QString &message) { cancel(); emit failed(message); }

void VideoDownloader::cancel()
{
    busy_ = false;
    timer_->stop();
    tool_->cancel();
#ifdef Q_OS_WIN
    if (job_) { CloseHandle(static_cast<HANDLE>(job_)); job_ = nullptr; }
#endif
    if (process_->state() != QProcess::NotRunning) {
        process_->kill();
        process_->waitForFinished(3000);
    }
    staging_.reset();
}
