// Run clip_export_test <ffmpeg.exe> <ffprobe.exe>.
#include "player.h"
#include "clip_exporter.h"

#include <QApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <cstdio>
#include <cstdlib>

static void require(bool passed, const char *message)
{
    if (!passed) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static QByteArray run(const QString &program, const QStringList &args)
{
    QProcess process;
    process.start(program, args);
    require(process.waitForFinished(30000), "Tool finishes");
    if (process.exitCode() != 0)
        std::fprintf(stderr, "%s\n", process.readAllStandardError().constData());
    require(process.exitCode() == 0, "Tool succeeds");
    return process.readAllStandardOutput();
}

static QByteArray hashFile(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Read source");
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
}

int main(int argc, char **argv)
{
    Player::configureBackend();
    QApplication app(argc, argv);
    require(argc == 3, "Pass FFmpeg and ffprobe");
    const QString ffmpeg = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
    const QString ffprobe = QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath();
    QTemporaryDir temporary;
    require(temporary.isValid(), "Temporary test workspace");
    const QDir root(temporary.path());
    const QString source = root.filePath(QString::fromUtf8("source \xc3\xbc.mkv"));
    const QString clips = root.filePath("clips");
    run(ffmpeg, {"-v", "error", "-f", "lavfi", "-i", "testsrc=size=161x91:rate=25",
        "-f", "lavfi", "-i", "sine=frequency=1000:sample_rate=48000", "-t", "6",
        "-c:v", "ffv1", "-c:a", "pcm_s16le", source});
    const QByteArray originalHash = hashFile(source);
    QString saved;
    {
        Player player;
        player.resize(960, 600);
        player.show();
        player.setExportOptions(ffmpeg, clips);
        player.openFile(source);
        auto *media = player.findChild<QMediaPlayer *>();
        auto *copy = player.findChild<QPushButton *>("copyClip");
        auto *editor = player.findChild<QLineEdit *>("position");
        auto *start = player.findChild<QPushButton *>("trimStart");
        auto *end = player.findChild<QPushButton *>("trimEnd");
        auto *range = player.findChild<QLabel *>("trimRange");
        require(QTest::qWaitFor([&] { return start->isEnabled() && media->duration() > 5000; }, 10000),
                "MKV loaded");
        media->pause();
        const auto seek = [&](const QString &text) {
            editor->setText(text);
            QTest::keyClick(editor, Qt::Key_Return);
        };
        seek("00:00:01.213");
        QTest::mouseClick(start, Qt::LeftButton);
        seek("00:00:03.987");
        QTest::mouseClick(end, Qt::LeftButton);
        require(range->text().contains("00:00:01.213") && range->text().contains("00:00:03.987"),
                "Markers use precise requested positions");
        const QString validRange = range->text();
        seek("00:00:00.000");
        QTest::mouseClick(end, Qt::LeftButton);
        require(range->text() == validRange, "Crossed markers are rejected");
        auto *exporter = player.findChild<ClipExporter *>();
        QSignalSpy completed(exporter, &ClipExporter::completed);
        QSignalSpy failed(exporter, &ClipExporter::failed);
        QTest::mouseClick(copy, Qt::LeftButton);
        require(exporter->isBusy() && !copy->isEnabled(), "Export runs asynchronously and prevents duplicate clicks");
        require(QTest::qWaitFor([&] { return !completed.isEmpty() || !failed.isEmpty(); }, 30000),
                "Export completes");
        if (!failed.isEmpty()) std::fprintf(stderr, "%s\n", failed.first()[0].toString().toUtf8().constData());
        require(failed.isEmpty(), "Export succeeds");
        saved = completed.first()[0].toString();
        require(QFileInfo(saved).dir().absolutePath() == QDir(clips).absolutePath()
                && QFileInfo(saved).suffix() == "mp4", "Saved MP4 is in clips folder");
        require(QApplication::clipboard()->mimeData()->urls() == QList<QUrl>{QUrl::fromLocalFile(saved)},
                "Clipboard contains a file URL, not just text");
#ifdef Q_OS_WIN
        require(IsClipboardFormatAvailable(CF_HDROP), "Windows clipboard exposes a pasteable file");
#endif
        QTest::mouseClick(copy, Qt::LeftButton);
        require(QTest::qWaitFor([&] { return completed.size() == 2 || !failed.isEmpty(); }, 30000),
                "Second copy completes");
        require(completed.size() == 2 && completed[1][0].toString() != saved,
                "Repeated copies create unique files");
        if (qEnvironmentVariableIsSet("WVIDS_CLIP_SCREENSHOT"))
            player.grab().save(qEnvironmentVariable("WVIDS_CLIP_SCREENSHOT"));
    }
    require(QFileInfo(saved).size() > 0, "Clip persists after closing player");
    require(hashFile(source) == originalHash, "Source remains unchanged");
    const auto info = QJsonDocument::fromJson(run(ffprobe,
        {"-v", "error", "-show_streams", "-show_format", "-of", "json", saved})).object();
    const auto streams = info["streams"].toArray();
    require(streams.size() == 2, "Clip contains video and audio");
    require(streams[0].toObject()["codec_name"] == "h264"
            && streams[0].toObject()["pix_fmt"] == "yuv420p"
            && streams[0].toObject()["width"] == 162 && streams[0].toObject()["height"] == 92,
            "Video uses compatible H.264 with padded even dimensions");
    require(streams[1].toObject()["codec_name"] == "aac", "Audio converted to AAC");
    const auto format = info["format"].toObject();
    require(qAbs(format["duration"].toString().toDouble() - 2.774) < 0.08, "Trim duration is frame accurate");
    require(qAbs(format["start_time"].toString().toDouble()) < 0.05, "Output timestamps start at zero");

    ClipExporter exporter;
    QSignalSpy done(&exporter, &ClipExporter::completed);
    QSignalSpy failure(&exporter, &ClipExporter::failed);
    const QString silent = root.filePath("long-gop.mkv");
    run(ffmpeg, {"-v", "error", "-f", "lavfi", "-i", "testsrc2=size=160x90:rate=25",
        "-t", "6", "-c:v", "libx264", "-g", "250", "-an", silent});
    exporter.start(ffmpeg, silent, clips, 1213, 3987);
    require(QTest::qWaitFor([&] { return !done.isEmpty() || !failure.isEmpty(); }, 30000),
            "Silent long-GOP export completes");
    require(failure.isEmpty(), "No-audio video exports successfully");
    const QString silentClip = done.last()[0].toString();
    const auto silentInfo = QJsonDocument::fromJson(run(ffprobe,
        {"-v", "error", "-show_streams", "-show_format", "-of", "json", silentClip})).object();
    require(silentInfo["streams"].toArray().size() == 1
        && qAbs(silentInfo["format"].toObject()["duration"].toString().toDouble() - 2.774) < 0.08,
        "Cut between keyframes does not include preceding GOP");
    // Compare the first output frame to a reference seek, allowing normal H.264 loss.
    const QByteArray reference = run(ffmpeg, {"-v", "error", "-ss", "1.213", "-i", silent,
        "-frames:v", "1", "-pix_fmt", "rgb24", "-f", "rawvideo", "pipe:1"});
    const QByteArray actual = run(ffmpeg, {"-v", "error", "-i", silentClip,
        "-frames:v", "1", "-pix_fmt", "rgb24", "-f", "rawvideo", "pipe:1"});
    require(reference.size() == actual.size() && !reference.isEmpty(), "Reference frames decoded");
    qint64 difference = 0;
    for (int i = 0; i < actual.size(); ++i)
        difference += qAbs(int(static_cast<unsigned char>(actual[i])) - int(static_cast<unsigned char>(reference[i])));
    require(double(difference) / actual.size() < 12, "First frame matches selected start, not previous keyframe");
    const int existing = QDir(clips).entryList({"*.mp4"}, QDir::Files).size();
    exporter.start(ffmpeg, source, clips, 4000, 2000);
    require(!failure.isEmpty(), "Invalid range rejected");
    failure.clear();
    exporter.start(ffmpeg, source, clips, 100000, 102000);
    require(QTest::qWaitFor([&] { return !failure.isEmpty(); }, 10000), "Empty-frame export rejected");
    require(QDir(clips).entryList({"*.mp4"}, QDir::Files).size() == existing, "Failed exports leave no final clip");
    exporter.start(ffmpeg, source, clips, 0, 5000);
    exporter.cancel();
    require(!exporter.isBusy() && QDir(clips).entryList({".export-*"}, QDir::Dirs | QDir::Hidden).isEmpty(),
            "Cancel removes unfinished output");
    QApplication::clipboard()->clear();
    std::puts("PASS: trim markers, MP4 compatibility, real file clipboard, unique names, persistence, precise cuts, silent input, failures, cancellation");
    return 0;
}
