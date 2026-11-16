// Local fixture tests: ffmpeg_installer_test <fake_ffmpeg.exe>
// Live provider check: ffmpeg_installer_test --live <installation directory>
#include "ffmpeg_installer.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <cstdio>
#include <cstdlib>

static void require(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--live")) {
        FfmpegInstaller installer(QString::fromLocal8Bit(argv[2]));
        QObject::connect(&installer, &FfmpegInstaller::downloadRequired, &installer,
                         [&] { installer.confirmDownload(true); });
        QObject::connect(&installer, &FfmpegInstaller::statusChanged, [](const QString &message) {
            std::fprintf(stdout, "%s\n", message.toUtf8().constData());
            std::fflush(stdout);
        });
        QObject::connect(&installer, &FfmpegInstaller::ready, &app, [&](const QString &path) {
            std::fprintf(stdout, "READY: %s\n", path.toUtf8().constData());
            app.exit(0);
        });
        QObject::connect(&installer, &FfmpegInstaller::failed, &app, [&](const QString &error) {
            std::fprintf(stderr, "FAILED: %s\n", error.toUtf8().constData());
            app.exit(1);
        });
        QTimer::singleShot(0, &installer, &FfmpegInstaller::start);
        QTimer::singleShot(12 * 60 * 1000, &app, [&] { app.exit(2); });
        return app.exec();
    }
    require(argc == 2, "Pass the fake ffmpeg executable");
    QTemporaryDir fixtures;
    require(fixtures.isValid(), "Temporary fixture directory");
    const QDir root(fixtures.path());
    require(root.mkpath(QStringLiteral("source/bin")), "Archive source directory");
    require(QFile::copy(QString::fromLocal8Bit(argv[1]), root.filePath("source/bin/ffmpeg.exe")),
            "Copy test executable into fixture");
    require(QFile::copy(QString::fromLocal8Bit(argv[1]), root.filePath("source/bin/ffprobe.exe")),
            "Copy probe into fixture");
    QProcess tar;
    tar.start(QDir(qEnvironmentVariable("SystemRoot", "C:/Windows")).filePath("System32/tar.exe"),
        {"-a", "-cf", root.filePath("release.zip"), "-C", root.filePath("source"), "bin"});
    require(tar.waitForFinished(15000) && tar.exitCode() == 0, "Build ZIP fixture");
    QFile archive(root.filePath("release.zip"));
    require(archive.open(QIODevice::ReadOnly), "Read archive fixture");
    const QByteArray zip = archive.readAll();
    const QByteArray checksum = QCryptographicHash::hash(zip, QCryptographicHash::Sha256).toHex();
    int requests = 0;
    bool wrongChecksum = false;
    bool unavailable = false;
    bool holdResponse = false;
    bool corruptArchive = false;
    bool slowArchive = true;
    bool missingChecksum = false;
    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost), "Local download fixture server");
    QObject::connect(&server, &QTcpServer::newConnection, [&] {
        while (server.hasPendingConnections()) {
            auto *socket = server.nextPendingConnection();
            auto request = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, request] {
                *request += socket->readAll();
                if (!request->contains("\r\n\r\n"))
                    return;
                ++requests;
                if (holdResponse)
                    return;
                const bool sha = request->startsWith("GET /checksums.sha256 ");
                const QByteArray content = corruptArchive ? QByteArray("not an archive") : zip;
                const QByteArray digest = corruptArchive
                    ? QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex() : checksum;
                QByteArray manifest;
                for (int i = 0; i < 60; ++i)
                    manifest += QByteArray(64, 'a') + "  other-platform-" + QByteArray::number(i) + ".zip\n";
                if (!missingChecksum)
                    manifest += (wrongChecksum ? QByteArray(64, '0') : digest) + " *release.zip\r\n";
                const QByteArray body = sha ? manifest : content;
                const QByteArray status = unavailable ? "404 Not Found" : "200 OK";
                socket->write("HTTP/1.1 " + status + "\r\nContent-Length: "
                    + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n");
                if (!sha && slowArchive) {
                    socket->write(body.left(body.size() / 2));
                    const QByteArray remainder = body.mid(body.size() / 2);
                    QTimer::singleShot(1200, socket, [socket, remainder] {
                        socket->write(remainder);
                        socket->disconnectFromHost();
                    });
                } else {
                    socket->write(body);
                    socket->disconnectFromHost();
                }
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    const QUrl url(QStringLiteral("http://127.0.0.1:%1/release.zip").arg(server.serverPort()));
    const QString installation = root.filePath("installed");
    FfmpegInstaller installer(installation, nullptr, url);
    QSignalSpy ready(&installer, &FfmpegInstaller::ready);
    QSignalSpy failed(&installer, &FfmpegInstaller::failed);
    QSignalSpy confirmation(&installer, &FfmpegInstaller::downloadRequired);
    QSignalSpy output(&installer, &FfmpegInstaller::outputReceived);
    QSignalSpy progress(&installer, &FfmpegInstaller::progressChanged);
    installer.start();
    installer.start(); // Repeated startup requests must not open duplicate prompts.
    QTest::qWait(100);
    require(confirmation.size() == 1 && requests == 0, "No network access before confirmation");
    installer.confirmDownload(false);
    QTest::qWait(100);
    require(requests == 0 && failed.isEmpty(), "Declining leaves downloads stopped without error");
    installer.confirmDownload(true);
    require(requests == 0, "Late confirmation after cancellation is ignored");
    installer.start();
    require(confirmation.size() == 2, "A later attempt can request confirmation again");
    installer.confirmDownload(true);
    require(QTest::qWaitFor([&] { return !ready.isEmpty() || !failed.isEmpty(); }, 20000),
            "Fresh installation completes");
    require(failed.isEmpty() && ready.size() == 1 && requests == 2, "One verified download");
    QString commandOutput;
    for (const auto &entry : output)
        commandOutput += entry[0].toString();
    require(commandOutput.contains("curl.exe") && commandOutput.contains("--output")
            && commandOutput.contains(url.toString()) && commandOutput.contains("ffmpeg version installer-test"),
            "Details contain actual CLI commands and process output");
    require(!progress.isEmpty() && progress.last()[0].toInt() == 100, "Completed download reports 100 percent");
    bool intermediate = false;
    for (const auto &entry : progress)
        intermediate |= entry[0].toInt() > 0 && entry[0].toInt() < 100;
    require(intermediate, "Intermediate percentage parsed from real curl progress output");
    slowArchive = false;
    require(QFile::exists(QDir(installation).filePath("ffmpeg.exe"))
            && QFile::exists(QDir(installation).filePath("ffprobe.exe")), "Tools installed");
    require(QDir(installation).entryList({".download-*"}, QDir::Dirs | QDir::Hidden).isEmpty(),
            "Staging cleaned after success");
    ready.clear();
    installer.start();
    require(QTest::qWaitFor([&] { return !ready.isEmpty(); }, 5000), "Reuse installed executable");
    require(requests == 2, "Existing FFmpeg does not access network");
    require(confirmation.size() == 2, "Existing FFmpeg does not prompt for downloading");

    const QString binInstall = root.filePath("existing-bin");
    require(QDir().mkpath(QDir(binInstall).filePath("bin")), "Create alternate layout");
    require(QFile::copy(QString::fromLocal8Bit(argv[1]), QDir(binInstall).filePath("bin/ffmpeg.exe")),
            "Populate alternate layout");
    FfmpegInstaller binInstaller(binInstall, nullptr, url);
    QSignalSpy binReady(&binInstaller, &FfmpegInstaller::ready);
    binInstaller.start();
    require(QTest::qWaitFor([&] { return !binReady.isEmpty(); }, 5000) && requests == 2,
            "Reuse ffmpeg/bin layout without downloading");

    wrongChecksum = true;
    const QString badInstall = root.filePath("bad-hash");
    FfmpegInstaller retryInstaller(badInstall, nullptr, url);
    QObject::connect(&retryInstaller, &FfmpegInstaller::downloadRequired, &retryInstaller,
                     [&] { retryInstaller.confirmDownload(true); });
    QSignalSpy bad(&retryInstaller, &FfmpegInstaller::failed);
    QSignalSpy retryReady(&retryInstaller, &FfmpegInstaller::ready);
    retryInstaller.start();
    require(QTest::qWaitFor([&] { return !bad.isEmpty(); }, 5000), "Bad checksum rejected");
    require(!QFile::exists(QDir(badInstall).filePath("ffmpeg.exe")), "Bad hash never installed");
    require(QDir(badInstall).entryList({".download-*"}, QDir::Dirs | QDir::Hidden).isEmpty(),
            "Failure cleans staging");
    wrongChecksum = false;
    retryInstaller.start();
    require(QTest::qWaitFor([&] { return !retryReady.isEmpty(); }, 20000), "Retry succeeds");

    unavailable = true;
    FfmpegInstaller missing(root.filePath("missing"), nullptr, url);
    QObject::connect(&missing, &FfmpegInstaller::downloadRequired, &missing,
                     [&] { missing.confirmDownload(true); });
    QSignalSpy missingFailure(&missing, &FfmpegInstaller::failed);
    missing.start();
    require(QTest::qWaitFor([&] { return !missingFailure.isEmpty(); }, 5000), "HTTP failure reported");
    unavailable = false;
    missingChecksum = true;
    FfmpegInstaller absent(root.filePath("absent-checksum"), nullptr, url);
    QObject::connect(&absent, &FfmpegInstaller::downloadRequired, &absent,
                     [&] { absent.confirmDownload(true); });
    QSignalSpy absentFailure(&absent, &FfmpegInstaller::failed);
    const int beforeMissing = requests;
    absent.start();
    require(QTest::qWaitFor([&] { return !absentFailure.isEmpty(); }, 5000),
            "Manifest without exact archive filename is rejected");
    require(requests == beforeMissing + 1, "Missing checksum stops before downloading archive");
    missingChecksum = false;
    corruptArchive = true;
    FfmpegInstaller corrupt(root.filePath("corrupt"), nullptr, url);
    QObject::connect(&corrupt, &FfmpegInstaller::downloadRequired, &corrupt,
                     [&] { corrupt.confirmDownload(true); });
    QSignalSpy corruptFailure(&corrupt, &FfmpegInstaller::failed);
    corrupt.start();
    require(QTest::qWaitFor([&] { return !corruptFailure.isEmpty(); }, 10000),
            "Invalid archive rejected even when its checksum matches");
    corruptArchive = false;
    holdResponse = true;
    const QString interruptedPath = root.filePath("interrupted");
    auto interrupted = std::make_unique<FfmpegInstaller>(interruptedPath, nullptr, url);
    QObject::connect(interrupted.get(), &FfmpegInstaller::downloadRequired, interrupted.get(),
                     [&] { interrupted->confirmDownload(true); });
    const int previousRequests = requests;
    interrupted->start();
    require(QTest::qWaitFor([&] { return requests > previousRequests; }, 5000), "Download in flight");
    interrupted.reset();
    require(QDir(interruptedPath).entryList({".download-*"}, QDir::Dirs | QDir::Hidden).isEmpty(),
            "Shutdown removes incomplete downloads");
    QLockFile released(QDir(interruptedPath).filePath(".install.lock"));
    require(released.tryLock(0), "Shutdown releases installation lock");
    std::puts("PASS: confirmation, decline, duplicate prompts, download, verification, existing layouts, failure cleanup, retry, shutdown");
    return 0;
}
