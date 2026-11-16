#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.contains("--version")) {
        std::puts("2026.11.16-test");
        return 0;
    }
    const int resultIndex = args.indexOf("--print-to-file");
    if (resultIndex < 0 || resultIndex + 2 >= args.size()
        || !args.contains("--ignore-config") || !args.contains("--no-playlist")
        || !args.contains("--downloader") || !args.contains("--ffmpeg-location")
        || !args.contains("--")) return 2;
    const QString result = args[resultIndex + 2];
    const QString video = QFileInfo(result).dir().filePath("fixture.mp4");
    if (!QFile::copy(qEnvironmentVariable("WVIDS_TEST_VIDEO"), video)) return 3;
    QFile output(result);
    if (!output.open(QIODevice::WriteOnly)) return 4;
    output.write(video.toUtf8() + "\n");
    std::puts("WVIDS_PROGRESS: 50.0%");
    std::puts("WVIDS_PROGRESS: 100.0%");
    return 0;
}
