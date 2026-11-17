#include <QApplication>
#include <QTimer>
#include <QIcon>

#include "ffmpeg_installer.h"
#include "ffmpeg_setup_dialog.h"
#include "player.h"
#include "window.h"
#include "app_paths.h"

int main(int argc, char *argv[])
{
    Player::configureBackend();
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("wvids"));
    app.setWindowIcon(QIcon(":/asset/icon.ico"));
    const auto paths = AppPaths::forCurrentUser();
    Window window(nullptr, paths);
    window.centralWidget()->setEnabled(false);
    FfmpegInstaller ffmpeg(paths.ffmpeg);
    FfmpegSetupDialog setup(&ffmpeg, &window);
    QObject::connect(&setup, &FfmpegSetupDialog::exitRequested, &app, &QCoreApplication::quit);
    QObject::connect(&ffmpeg, &FfmpegInstaller::ready, &window, [&window](const QString &path) {
        window.setFfmpegPath(path);
        window.centralWidget()->setEnabled(true);
    });
    window.show();
    QTimer::singleShot(0, &ffmpeg, &FfmpegInstaller::start);
    return app.exec();
}
