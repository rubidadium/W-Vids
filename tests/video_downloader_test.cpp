#include "video_downloader.h"
#include "github_tool.h"
#include "window.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QMediaPlayer>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <cstdio>
#include <cstdlib>

static void require(bool ok, const char *message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() == 3 && args[1] == "--setup") {
        GithubTool tools(args[2]);
        bool deno = false;
        QObject::connect(&tools, &GithubTool::statusChanged, [](const QString &s) {
            std::printf("%s\n", s.toUtf8().constData()); std::fflush(stdout);
        });
        QObject::connect(&tools, &GithubTool::failed, &app, [&](const QString &s) {
            std::fprintf(stderr, "%s\n", s.toUtf8().constData()); app.exit(1);
        });
        QObject::connect(&tools, &GithubTool::ready, &app, [&](const QString &s) {
            std::printf("Ready: %s\n", s.toUtf8().constData()); std::fflush(stdout);
            if (!deno) {
                deno = true;
                tools.ensure("denoland/deno", "deno-x86_64-pc-windows-msvc.zip", "deno.exe");
            } else app.exit(0);
        });
        QTimer::singleShot(0, &tools, [&] { tools.ensure("yt-dlp/yt-dlp", "yt-dlp.exe", "yt-dlp.exe"); });
        return app.exec();
    }
    // fake_yt_dlp is used for deterministic extractor tests; optionally pass a real tools folder as argument 4.
    require(args.size() == 3 || args.size() == 4, "Pass ffmpeg.exe, fake_yt_dlp.exe, and optionally real tools directory");
    const QString ffmpeg = QFileInfo(args[1]).absoluteFilePath();
    QTemporaryDir temporary;
    const QDir root(temporary.path());
    const QString mediaPath = root.filePath("fixture.mp4");
    QProcess generator;
    generator.start(ffmpeg, {"-v", "error", "-f", "lavfi", "-i", "testsrc2=size=160x90:rate=25",
        "-t", "1", "-c:v", "libx264", "-an", mediaPath});
    require(generator.waitForFinished(15000) && generator.exitCode() == 0, "Generate video fixture");
    QFile fixture(mediaPath);
    require(fixture.open(QIODevice::ReadOnly), "Read fixture");
    const QByteArray video = fixture.readAll();
    const QString toolsPath = args.size() == 4 ? QFileInfo(args[3]).absoluteFilePath() : root.filePath("tools");
    if (args.size() == 3) {
        QDir().mkpath(toolsPath);
        require(QFile::copy(args[2], QDir(toolsPath).filePath("yt-dlp.exe")), "Install fake extractor");
    }
    qputenv("WVIDS_TEST_VIDEO", mediaPath.toUtf8());
    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost), "Start local video server");
    bool hold = false;
    bool holdVideo = false;
    int requests = 0;
    QObject::connect(&server, &QTcpServer::newConnection, [&] {
        while (server.hasPendingConnections()) {
            auto *socket = server.nextPendingConnection();
            auto buffer = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, buffer] {
                *buffer += socket->readAll();
                if (!buffer->contains("\r\n\r\n")) return;
                ++requests;
                if (hold) return;
                if (holdVideo && buffer->contains("/fixture.mp4 ")) return;
                const bool page = buffer->startsWith("GET /page ");
                const bool invalid = buffer->startsWith("GET /invalid.mp4 ");
                const QByteArray body = page ? QByteArray("<html><title>Fixture video</title><video src=\"/fixture.mp4\" controls></video></html>")
                    : invalid ? QByteArray("<html>This is not a video</html>") : video;
                const QByteArray type = page || invalid ? "text/html" : "video/mp4";
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: " + type + "\r\nContent-Length: "
                              + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    const QUrl base(QStringLiteral("http://127.0.0.1:%1/").arg(server.serverPort()));
    const QString downloads = root.filePath("download");
    VideoDownloader downloader(downloads, toolsPath);
    QSignalSpy done(&downloader, &VideoDownloader::completed);
    QSignalSpy failure(&downloader, &VideoDownloader::failed);
    downloader.start(QUrl("file:///C:/not-a-url.mp4"), ffmpeg);
    require(failure.size() == 1 && requests == 0, "Reject non-HTTP URLs before starting");
    failure.clear();
    downloader.start(base.resolved(QUrl("fixture.mp4?token=a%26b")), ffmpeg);
    require(QTest::qWaitFor([&] { return !done.isEmpty() || !failure.isEmpty(); }, 15000), "Direct curl download finishes");
    if (!failure.isEmpty()) std::fprintf(stderr, "%s\n", failure[0][0].toString().toUtf8().constData());
    require(failure.isEmpty() && QFileInfo(done[0][0].toString()).isFile(), "Direct video is saved");
    QFile downloaded(done[0][0].toString());
    require(downloaded.open(QIODevice::ReadOnly) && downloaded.readAll() == video, "Downloaded bytes match source");
    const QString first = done[0][0].toString();
    done.clear();
    downloader.start(base.resolved(QUrl("page")), ffmpeg);
    require(QTest::qWaitFor([&] { return !done.isEmpty() || !failure.isEmpty(); }, 30000), "yt-dlp page download finishes");
    if (!failure.isEmpty()) std::fprintf(stderr, "%s\n", failure[0][0].toString().toUtf8().constData());
    require(failure.isEmpty() && QFileInfo(done[0][0].toString()).isFile(), "Extractor video is saved");
    require(done[0][0].toString() != first, "Downloads use unique filenames");
    done.clear();
    downloader.start(base.resolved(QUrl("invalid.mp4")), ffmpeg);
    require(QTest::qWaitFor([&] { return !failure.isEmpty(); }, 10000), "HTML disguised as video is rejected");
    require(done.isEmpty(), "Invalid download never opens");
    failure.clear();
    hold = true;
    const int before = requests;
    downloader.start(base.resolved(QUrl("slow.mp4")), ffmpeg);
    require(QTest::qWaitFor([&] { return requests > before; }, 5000), "Download in flight");
    downloader.cancel();
    require(!downloader.isBusy() && QDir(downloads).entryList({".download-*"}, QDir::Dirs | QDir::Hidden).isEmpty(),
            "Cancel removes partial download");
    hold = false;
    downloader.start(base.resolved(QUrl("fixture.mp4")), ffmpeg);
    require(QTest::qWaitFor([&] { return !done.isEmpty() || !failure.isEmpty(); }, 15000), "Retry succeeds after cancellation");
    require(failure.isEmpty(), "Retry has no errors");
    if (args.size() == 4) {
        done.clear();
        holdVideo = true;
        const int beforeExtractor = requests;
        downloader.start(base.resolved(QUrl("page")), ffmpeg);
        require(QTest::qWaitFor([&] { return requests >= beforeExtractor + 2 || !failure.isEmpty(); }, 15000),
                "Real yt-dlp starts its media transfer");
        require(failure.isEmpty(), "Extractor has no error before cancellation");
        downloader.cancel();
        require(!downloader.isBusy() && QDir(downloads).entryList({".download-*"}, QDir::Dirs | QDir::Hidden).isEmpty(),
                "Cancel stops yt-dlp and its curl child and removes partial files");
        holdVideo = false;
    }

    Window window;
    window.setFfmpegPath(ffmpeg);
    window.show();
    auto *field = window.findChild<QLineEdit *>("videoLink");
    require(field && field->placeholderText() == "Or load from a link", "Welcome field is available");
    if (qEnvironmentVariableIsSet("WVIDS_LINK_SCREENSHOT"))
        window.grab().save(qEnvironmentVariable("WVIDS_LINK_SCREENSHOT"));
    field->setText(base.resolved(QUrl("fixture.mp4")).toString());
    QTest::keyClick(field, Qt::Key_Return);
    require(QTest::qWaitFor([&] { return window.findChild<QMediaPlayer *>() != nullptr; }, 15000),
            "Submitting a link opens downloaded video in the player");
    std::puts("PASS: URL validation, direct curl, yt-dlp page, unique saves, media validation, cancellation, retry, welcome field, player opening");
    return 0;
}
