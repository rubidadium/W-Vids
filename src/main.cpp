#include <QApplication>
#include <QDir>
#include <QTimer>

#include "ffmpeg_installer.h"
#include "ffmpeg_setup_dialog.h"
#include "player.h"
#include "window.h"

int main(int argc, char *argv[])
{
    Player::configureBackend();
    QApplication app(argc, argv);
    Window window;
    window.centralWidget()->setEnabled(false);
    FfmpegInstaller ffmpeg(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("ffmpeg")));
    FfmpegSetupDialog setup(&ffmpeg, &window);
    QObject::connect(&setup, &FfmpegSetupDialog::exitRequested, &app, &QCoreApplication::quit);
    QObject::connect(&ffmpeg, &FfmpegInstaller::ready, &window, [&window](const QString &) {
        window.centralWidget()->setEnabled(true);
    });
    window.show();
    QTimer::singleShot(0, &ffmpeg, &FfmpegInstaller::start);
    return app.exec();
}
