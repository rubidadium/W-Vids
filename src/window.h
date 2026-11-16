#pragma once

#include <QMainWindow>

class Player;
class QLabel;
class QPushButton;

class Window : public QMainWindow
{
public:
    explicit Window(QWidget *parent = nullptr);
    void setFfmpegPath(const QString &path);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void selectVideo();
    void updateWelcomeText(const QSize &size);

    Player *player_ = nullptr;
    QPushButton *selectButton_ = nullptr;
    QLabel *welcomeTitle_ = nullptr;
    QLabel *welcomeFormats_ = nullptr;
    bool selectingVideo_ = false;
    QString ffmpegPath_;
};
