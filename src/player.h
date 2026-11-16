#pragma once

#include <QMediaPlayer>
#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;
class QSlider;

class Player : public QWidget
{
public:
    // Call before creating QApplication or any multimedia objects.
    static void configureBackend();
    explicit Player(QWidget *parent = nullptr);

private:
    static QString formatTime(qint64 milliseconds);
    void togglePlayback();
    void updatePlaybackControls();
    void updateProgress();
    void seekToPosition(int value);
    void showPlaybackError(QMediaPlayer::Error error, const QString &message);
    void loadDefaultMedia();

    // Qt parent ownership releases these objects when the Player is destroyed.
    QMediaPlayer *media_ = nullptr;
    QPushButton *playButton_ = nullptr;
    QSlider *seek_ = nullptr;
    QLabel *time_ = nullptr;
    QLabel *status_ = nullptr;
};

