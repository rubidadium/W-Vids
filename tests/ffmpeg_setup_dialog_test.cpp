#include "ffmpeg_installer.h"
#include "ffmpeg_setup_dialog.h"

#include <QApplication>
#include <QDir>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
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
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost), "Local fixture server");
    QWidget window;
    window.resize(960, 600);
    window.setStyleSheet(QStringLiteral("background: #161616;"));
    window.show();
    FfmpegInstaller installer(temporary.path(), nullptr,
        QUrl(QStringLiteral("http://127.0.0.1:%1/release.zip").arg(server.serverPort())));
    FfmpegSetupDialog dialog(&installer, &window);
    installer.start();
    require(QTest::qWaitFor([&] { return dialog.isVisible(); }), "Prompt opens");
    QTest::qWait(220);
    require(app.activeModalWidget() == &dialog, "Setup blocks interaction with main window");
    auto *dimmer = window.findChild<QWidget *>("setupDimmer");
    require(dimmer && dimmer->isVisible() && dimmer->geometry() == window.rect(), "Dimmer covers main window");
    auto *opacity = qobject_cast<QGraphicsOpacityEffect *>(dimmer->graphicsEffect());
    require(opacity && opacity->opacity() > 0.99, "Background fade completes");
    auto *primary = dialog.findChild<QPushButton *>("setupPrimary");
    auto *details = dialog.findChild<QPushButton *>("setupDetails");
    auto *progress = dialog.findChild<QProgressBar *>("setupProgress");
    auto *terminal = dialog.findChild<QPlainTextEdit *>("setupTerminal");
    require(primary->text() == "Download" && details->isHidden() && progress->isHidden(),
            "Prompt shows confirmation before download");
    if (argc > 1)
        dialog.grab().save(QDir(QString::fromLocal8Bit(argv[1])).filePath("setup-prompt.png"));
    QTest::mouseClick(primary, Qt::LeftButton);
    require(!primary->isEnabled() && progress->isVisible() && details->isVisible(),
            "Download reveals progress and details button");
    require(QTest::qWaitFor([&] { return server.hasPendingConnections(); }, 5000), "curl starts");
    require(terminal->toPlainText().contains("curl.exe"), "Terminal logs actual curl command");
    const int collapsed = dialog.height();
    QTest::mouseClick(details, Qt::LeftButton);
    require(terminal->isVisible() && dialog.height() > collapsed && details->text() == "Hide Details",
            "Details expands popup to show terminal");
    installer.progressChanged(37);
    require(progress->maximum() == 100 && progress->value() == 37, "Progress bar shows real numeric percentage");
    if (argc > 1)
        dialog.grab().save(QDir(QString::fromLocal8Bit(argv[1])).filePath("setup-details.png"));
    QTest::mouseClick(details, Qt::LeftButton);
    require(terminal->isHidden() && dialog.height() == collapsed, "Details collapses popup");
    window.resize(800, 500);
    app.processEvents();
    require(dimmer->geometry() == window.rect(), "Dimming tracks parent resize");
    // Finish the held request with an HTTP failure to exercise the same popup's retry state.
    auto *socket = server.nextPendingConnection();
    socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    socket->disconnectFromHost();
    require(QTest::qWaitFor([&] { return primary->isEnabled() && primary->text() == "Retry"; }, 5000),
            "Failure offers retry in the popup");
    require(dialog.isVisible() && terminal->toPlainText().contains("ERROR:"), "Error details retained");
    QSignalSpy exit(&dialog, &FfmpegSetupDialog::exitRequested);
    QTest::keyClick(&dialog, Qt::Key_Escape);
    require(exit.size() == 1, "Escape requests clean application shutdown");
    dialog.hide();
    require(!dimmer->isVisible(), "Closing setup removes dimming");
    std::puts("PASS: modal popup, fade, confirmation, CLI details, expansion, percentage, retry, exit");
    return 0;
}
