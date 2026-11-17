#pragma once

#include <QDir>
#include <QStandardPaths>
#include <QString>

struct AppPaths
{
    QString download;
    QString clip;
    QString tools;
    QString ffmpeg;

    static AppPaths forCurrentUser()
    {
        const QDir videos(QDir(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation))
                              .filePath(QStringLiteral("wvids")));
        const QDir data(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
        const QString tools = data.filePath(QStringLiteral("tools"));
        return {videos.filePath(QStringLiteral("download")), videos.filePath(QStringLiteral("clip")),
                tools, QDir(tools).filePath(QStringLiteral("ffmpeg"))};
    }
};
