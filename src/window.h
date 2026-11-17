#pragma once

#include <QMainWindow>
#include "app_paths.h"

class Player;
class QLabel;
class QPushButton;
class QLineEdit;
class QProgressBar;
class VideoDownloader;

class Window : public QMainWindow
{
public:
    explicit Window(QWidget *parent = nullptr, const AppPaths &paths = AppPaths::forCurrentUser());
    void setFfmpegPath(const QString &path);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void selectVideo();
    void openVideo(const QString &path);
    void loadLink();
    void updateDownloadControls();
    void updateWelcomeText(const QSize &size);

    Player *player_ = nullptr;
    QPushButton *selectButton_ = nullptr;
    QLabel *welcomeTitle_ = nullptr;
    QLabel *welcomeFormats_ = nullptr;
    bool selectingVideo_ = false;
    QString ffmpegPath_;
    QString clipsDirectory_;
    QLineEdit *link_ = nullptr;
    QPushButton *loadLinkButton_ = nullptr;
    QPushButton *cancelDownloadButton_ = nullptr;
    QLabel *downloadStatus_ = nullptr;
    QProgressBar *downloadProgress_ = nullptr;
    VideoDownloader *downloader_ = nullptr;
};
