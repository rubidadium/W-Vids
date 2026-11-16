#pragma once

#include <QMediaPlayer>
#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;
class SeekSlider;
class QLineEdit;
class QTimer;
class QVideoFrame;

class Player : public QWidget
{
public:
    // Call before creating QApplication or any multimedia objects.
    static void configureBackend();
    explicit Player(QWidget *parent = nullptr);
    void openFile(const QString &videoPath);

private:
    static QString formatTime(qint64 milliseconds);
    void togglePlayback();
    void updatePlaybackControls();
    void updateProgress();
    void updateDuration();
    void seekToPosition(int value);
    void requestSeek(qint64 position, bool pause);
    void commitSeek();
    void finishSeek();
    void beginScrub();
    void endScrub();
    void stepFrame(int direction);
    void seekFromTime();
    void framePresented(const QVideoFrame &frame);
    bool targetFramePresented() const;
    qint64 sliderTime(int value) const;
    void showPlaybackError(QMediaPlayer::Error error, const QString &message);

    // Qt parent ownership releases these objects when the Player is destroyed.
    QMediaPlayer *media_ = nullptr;
    QPushButton *playButton_ = nullptr;
    SeekSlider *seek_ = nullptr;
    QLineEdit *positionEdit_ = nullptr;
    QPushButton *previousFrame_ = nullptr;
    QPushButton *nextFrame_ = nullptr;
    QTimer *previewTimer_ = nullptr;
    QTimer *settleTimer_ = nullptr;
    QLabel *time_ = nullptr;
    QLabel *status_ = nullptr;
    qint64 desiredPosition_ = 0;
    qint64 issuedPosition_ = 0;
    qint64 frameStart_ = -1;
    qint64 frameEnd_ = -1;
    bool scrubbing_ = false;
    bool pendingSeek_ = false;
    bool resumeAfterSeek_ = false;
};

