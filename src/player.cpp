#include "player.h"

#include <QAudioOutput>
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoWidget>

void Player::configureBackend()
{
    qputenv("QT_MEDIA_BACKEND", "ffmpeg");
}

Player::Player(QWidget *parent) : QWidget(parent)
{
    auto *video = new QVideoWidget(this);
    video->setAspectRatioMode(Qt::KeepAspectRatio);
    media_ = new QMediaPlayer(this);
    auto *audio = new QAudioOutput(this);
    audio->setVolume(1.0);
    media_->setAudioOutput(audio);
    media_->setVideoOutput(video);

    playButton_ = new QPushButton(QStringLiteral("Play"), this);
    playButton_->setEnabled(false);
    seek_ = new QSlider(Qt::Horizontal, this);
    seek_->setRange(0, 1000);
    seek_->setEnabled(false);
    time_ = new QLabel(QStringLiteral("00:00:00 / 00:00:00"), this);
    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->hide();

    auto *controls = new QHBoxLayout;
    controls->addWidget(playButton_);
    controls->addWidget(seek_, 1);
    controls->addWidget(time_);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 8);
    layout->addWidget(video, 1);
    layout->addLayout(controls);
    layout->addWidget(status_);

    connect(playButton_, &QPushButton::clicked, this, &Player::togglePlayback);
    connect(media_, &QMediaPlayer::playbackStateChanged,
            this, &Player::updatePlaybackControls);
    connect(media_, &QMediaPlayer::mediaStatusChanged,
            this, &Player::updatePlaybackControls);
    connect(media_, &QMediaPlayer::positionChanged, this, &Player::updateProgress);
    connect(media_, &QMediaPlayer::durationChanged, this, &Player::updateProgress);
    connect(media_, &QMediaPlayer::seekableChanged, seek_, &QWidget::setEnabled);
    connect(seek_, &QSlider::sliderMoved, this, &Player::seekToPosition);
    connect(media_, &QMediaPlayer::errorOccurred, this, &Player::showPlaybackError);

    loadDefaultMedia();
}

QString Player::formatTime(qint64 milliseconds)
{
    const qint64 seconds = milliseconds / 1000;
    return QStringLiteral("%1:%2:%3")
        .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

void Player::togglePlayback()
{
    if (media_->playbackState() == QMediaPlayer::PlayingState) {
        media_->pause();
    } else {
        if (media_->mediaStatus() == QMediaPlayer::EndOfMedia)
            media_->setPosition(0);
        media_->play();
    }
}

void Player::updatePlaybackControls()
{
    const auto state = media_->mediaStatus();
    playButton_->setEnabled(state != QMediaPlayer::InvalidMedia
                            && state != QMediaPlayer::NoMedia
                            && media_->error() == QMediaPlayer::NoError);
    if (state == QMediaPlayer::EndOfMedia)
        playButton_->setText(QStringLiteral("Replay"));
    else
        playButton_->setText(media_->playbackState() == QMediaPlayer::PlayingState
                                 ? QStringLiteral("Pause") : QStringLiteral("Play"));
}

void Player::updateProgress()
{
    const qint64 duration = media_->duration();
    if (!seek_->isSliderDown())
        seek_->setValue(duration > 0 ? int(media_->position() * 1000 / duration) : 0);
    time_->setText(formatTime(media_->position()) + QStringLiteral(" / ")
                   + formatTime(duration));
}

void Player::seekToPosition(int value)
{
    media_->setPosition(media_->duration() * value / 1000);
}

void Player::showPlaybackError(QMediaPlayer::Error, const QString &message)
{
    status_->setText(QStringLiteral("Unable to play video: ") + message);
    status_->show();
    playButton_->setEnabled(false);
    seek_->setEnabled(false);
}

void Player::loadDefaultMedia()
{
    const QString videoPath = QDir(QCoreApplication::applicationDirPath())
                                  .filePath(QStringLiteral("asset/test.mp4"));
    if (!QFileInfo(videoPath).isFile()) {
        status_->setText(QStringLiteral("Video not found: ") + videoPath);
        status_->show();
        return;
    }
    media_->setSource(QUrl::fromLocalFile(videoPath));
    media_->play();
}

