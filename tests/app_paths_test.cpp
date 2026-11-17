#include "app_paths.h"

#include <QCoreApplication>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("wvids"));
    QStandardPaths::setTestModeEnabled(true);
    const auto paths = AppPaths::forCurrentUser();
    const QDir videos(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));
    const QDir data(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    if (paths.download != videos.filePath("wvids/download")
        || paths.clip != videos.filePath("wvids/clip")
        || paths.tools != data.filePath("tools")
        || paths.ffmpeg != data.filePath("tools/ffmpeg")
        || paths.tools.startsWith(QCoreApplication::applicationDirPath() + '/')) {
        std::fprintf(stderr, "FAIL: per-user storage paths\n");
        return 1;
    }
    std::puts("PASS: configured Videos folder, singular clip folder, persistent user tools, independent of executable location");
    return 0;
}
