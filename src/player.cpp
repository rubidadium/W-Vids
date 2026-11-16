#include "player.h"
#include "seek_slider.h"

#include <QAudioOutput>
#include <QByteArray>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMediaMetaData>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoFrame>
#include <QVideoSink>
#include <QVideoWidget>
#include <algorithm>
#include <cmath>
#include <limits>

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

    playButton_ = new QPushButton(this);
    playButton_->setObjectName(QStringLiteral("play"));
    playButton_->setFixedSize(44, 44);
    playButton_->setIconSize(QSize(24, 24));
    playButton_->setCursor(Qt::PointingHandCursor);
    playButton_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #08a4bf; border: none; border-radius: 22px; }"
        "QPushButton:hover { background: #13b8d4; }"
        "QPushButton:pressed { background: #07869c; }"
        "QPushButton:focus { border: 2px solid #eeeeee; }"
        "QPushButton:disabled { background: #555555; }"));
    auto *trimStart = new QPushButton(QStringLiteral("[ Set start"), this);
    trimStart->setObjectName(QStringLiteral("trimStart"));
    trimStart->setAccessibleName(QStringLiteral("Set trim start"));
    trimStart->setToolTip(QStringLiteral("Set trim start"));
    auto *trimEnd = new QPushButton(QStringLiteral("Set end ]"), this);
    trimEnd->setObjectName(QStringLiteral("trimEnd"));
    trimEnd->setAccessibleName(QStringLiteral("Set trim end"));
    trimEnd->setToolTip(QStringLiteral("Set trim end"));
    // Trim controls are placeholders until trim selection is implemented.
    for (auto *button : {trimStart, trimEnd}) {
        button->setFixedSize(110, 32);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(QStringLiteral(
            "QPushButton { color: #f2eeeb; background: #69564c; border: 1px solid #826d61;"
            " border-radius: 5px; padding: 4px 10px; }"
            "QPushButton:hover { background: #7b6558; }"
            "QPushButton:pressed { background: #58483f; }"
            "QPushButton:focus { border-color: #eeeeee; }"));
    }
    seek_ = new SeekSlider(this);
    seek_->setObjectName(QStringLiteral("timeline"));
    seek_->setRange(0, 0);
    positionEdit_ = new QLineEdit(QStringLiteral("00:00:00.000"), this);
    positionEdit_->setObjectName(QStringLiteral("position"));
    positionEdit_->setAccessibleName(QStringLiteral("Seek time"));
    positionEdit_->setToolTip(QStringLiteral("Enter HH:MM:SS.mmm and press Enter to seek and pause."));
    positionEdit_->setMaximumWidth(150);
    positionEdit_->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[0-9]{2,}:[0-5][0-9]:[0-5][0-9]\\.[0-9]{3}")), positionEdit_));
    time_ = new QLabel(QStringLiteral("/ 00:00:00.000"), this);
    previousFrame_ = new QPushButton(QStringLiteral("Previous frame"), this);
    previousFrame_->setObjectName(QStringLiteral("previousFrame"));
    nextFrame_ = new QPushButton(QStringLiteral("Next frame"), this);
    nextFrame_->setObjectName(QStringLiteral("nextFrame"));
    const int frameButtonWidth = std::max(previousFrame_->sizeHint().width(), nextFrame_->sizeHint().width());
    previousFrame_->setFixedWidth(frameButtonWidth);
    nextFrame_->setFixedWidth(frameButtonWidth);
    for (auto *button : {previousFrame_, nextFrame_})
        button->setToolTip(QStringLiteral("Pause and step using the video's reported frame rate. Variable-rate footage may differ."));
    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->hide();

    previewTimer_ = new QTimer(this);
    previewTimer_->setSingleShot(true);
    previewTimer_->setInterval(75);
    settleTimer_ = new QTimer(this);
    settleTimer_->setSingleShot(true);
    settleTimer_->setInterval(2000);

    auto *timeControls = new QHBoxLayout;
    timeControls->addStretch();
    timeControls->addWidget(positionEdit_);
    timeControls->addWidget(time_);
    timeControls->addStretch();
    auto *controls = new QHBoxLayout;
    controls->setSpacing(10);
    controls->addStretch();
    controls->addWidget(previousFrame_);
    controls->addWidget(trimStart);
    controls->addWidget(playButton_);
    controls->addWidget(trimEnd);
    controls->addWidget(nextFrame_);
    controls->addStretch();
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 8);
    layout->addWidget(video, 1);
    layout->addWidget(seek_);
    layout->addLayout(timeControls);
    layout->addLayout(controls);
    layout->addWidget(status_);

    connect(playButton_, &QPushButton::clicked, this, &Player::togglePlayback);
    connect(previousFrame_, &QPushButton::clicked, this, [this] { stepFrame(-1); });
    connect(nextFrame_, &QPushButton::clicked, this, [this] { stepFrame(1); });
    connect(positionEdit_, &QLineEdit::returnPressed, this, &Player::seekFromTime);
    connect(media_, &QMediaPlayer::playbackStateChanged, this, &Player::updatePlaybackControls);
    connect(media_, &QMediaPlayer::mediaStatusChanged, this, &Player::updatePlaybackControls);
    connect(media_, &QMediaPlayer::metaDataChanged, this, &Player::updatePlaybackControls);
    connect(media_, &QMediaPlayer::positionChanged, this, &Player::updateProgress);
    connect(media_, &QMediaPlayer::durationChanged, this, &Player::updateDuration);
    connect(media_, &QMediaPlayer::seekableChanged, this, &Player::updatePlaybackControls);
    connect(seek_, &QSlider::sliderPressed, this, &Player::beginScrub);
    connect(seek_, &QSlider::sliderReleased, this, &Player::endScrub);
    connect(seek_, &QSlider::valueChanged, this, &Player::seekToPosition);
    connect(previewTimer_, &QTimer::timeout, this, &Player::commitSeek);
    connect(settleTimer_, &QTimer::timeout, this, &Player::finishSeek);
    connect(video->videoSink(), &QVideoSink::videoFrameChanged, this, &Player::framePresented);
    connect(media_, &QMediaPlayer::errorOccurred, this, &Player::showPlaybackError);
    updatePlaybackControls();
}

QString Player::formatTime(qint64 milliseconds)
{
    const qint64 seconds = milliseconds / 1000;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'))
        .arg(milliseconds % 1000, 3, 10, QLatin1Char('0'));
}

void Player::togglePlayback()
{
    if (pendingSeek_) {
        resumeAfterSeek_ = !resumeAfterSeek_;
        updatePlaybackControls();
    } else if (media_->playbackState() == QMediaPlayer::PlayingState) {
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
    const bool valid = state != QMediaPlayer::InvalidMedia && state != QMediaPlayer::NoMedia
                       && media_->error() == QMediaPlayer::NoError;
    const bool seekable = valid && media_->isSeekable() && media_->duration() > 0;
    playButton_->setEnabled(valid);
    seek_->setEnabled(seekable);
    positionEdit_->setEnabled(seekable);
    const double fps = media_->metaData().value(QMediaMetaData::VideoFrameRate).toDouble();
    previousFrame_->setEnabled(seekable && std::isfinite(fps) && fps > 0);
    nextFrame_->setEnabled(previousFrame_->isEnabled());
    const bool playing = pendingSeek_ ? resumeAfterSeek_
                                     : media_->playbackState() == QMediaPlayer::PlayingState;
    const QString action = playing ? QStringLiteral("Pause")
        : state == QMediaPlayer::EndOfMedia ? QStringLiteral("Replay") : QStringLiteral("Play");
    playButton_->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    playButton_->setToolTip(action);
    playButton_->setAccessibleName(action);
}

qint64 Player::sliderTime(int value) const
{
    return seek_->maximum() > 0
        ? qRound64(double(value) * media_->duration() / seek_->maximum()) : 0;
}

void Player::updateDuration()
{
    const QSignalBlocker block(seek_);
    seek_->setRange(0, int(std::min<qint64>(media_->duration(), std::numeric_limits<int>::max())));
    seek_->setPageStep(std::max(1, seek_->maximum() / 100));
    updatePlaybackControls();
    updateProgress();
}

void Player::updateProgress()
{
    const qint64 position = pendingSeek_ || scrubbing_ ? desiredPosition_ : media_->position();
    if (!scrubbing_) {
        const QSignalBlocker block(seek_);
        seek_->setValue(media_->duration() > 0
            ? qRound(double(position) * seek_->maximum() / media_->duration()) : 0);
    }
    if (!positionEdit_->hasFocus())
        positionEdit_->setText(formatTime(position));
    time_->setText(QStringLiteral("/ ") + formatTime(media_->duration()));
}

void Player::beginScrub()
{
    resumeAfterSeek_ = pendingSeek_ ? resumeAfterSeek_
        : media_->playbackState() == QMediaPlayer::PlayingState;
    desiredPosition_ = pendingSeek_ ? desiredPosition_ : media_->position();
    scrubbing_ = true;
    media_->pause();
}

void Player::endScrub()
{
    scrubbing_ = false;
    previewTimer_->stop();
    // Always commit the release position, including a click without movement.
    desiredPosition_ = sliderTime(seek_->value());
    commitSeek();
}

void Player::seekToPosition(int value)
{
    requestSeek(sliderTime(value), false);
}

void Player::requestSeek(qint64 position, bool pause)
{
    if (!media_->isSeekable() || media_->duration() <= 0)
        return;
    if (!pendingSeek_ && !scrubbing_)
        resumeAfterSeek_ = media_->playbackState() == QMediaPlayer::PlayingState;
    if (pause)
        resumeAfterSeek_ = false;
    desiredPosition_ = std::clamp(position, qint64(0), media_->duration());
    pendingSeek_ = true;
    media_->pause();
    updateProgress();
    updatePlaybackControls();
    if (scrubbing_) {
        if (!previewTimer_->isActive())
            previewTimer_->start();
    } else {
        previewTimer_->stop();
        commitSeek();
    }
}

void Player::commitSeek()
{
    if (!media_->isSeekable())
        return;
    pendingSeek_ = true;
    issuedPosition_ = desiredPosition_;
    // Set state before setPosition: it can emit positionChanged synchronously.
    settleTimer_->start();
    media_->setPosition(issuedPosition_);
    if (!scrubbing_ && targetFramePresented())
        finishSeek();
}

bool Player::targetFramePresented() const
{
    // Frame timestamps are in microseconds, unlike QMediaPlayer's milliseconds.
    const qint64 target = issuedPosition_ * 1000;
    return frameStart_ >= 0 && frameStart_ <= target + 1000
        && frameEnd_ > target;
}

void Player::framePresented(const QVideoFrame &frame)
{
    if (!frame.isValid())
        return;
    frameStart_ = frame.startTime();
    frameEnd_ = frame.endTime();
    if (pendingSeek_ && !scrubbing_ && issuedPosition_ == desiredPosition_
        && targetFramePresented())
        finishSeek();
}

void Player::finishSeek()
{
    if (scrubbing_)
        return;
    settleTimer_->stop();
    pendingSeek_ = false;
    const bool resume = resumeAfterSeek_;
    resumeAfterSeek_ = false;
    if (resume)
        media_->play();
    updateProgress();
    updatePlaybackControls();
}

void Player::stepFrame(int direction)
{
    const double fps = media_->metaData().value(QMediaMetaData::VideoFrameRate).toDouble();
    if (!std::isfinite(fps) || fps <= 0)
        return;
    const double base = pendingSeek_ ? double(desiredPosition_)
        : frameStart_ >= 0 ? frameStart_ / 1000.0 : double(media_->position());
    // Round from a frame index rather than accumulating rounded frame durations.
    const qint64 frame = qRound64(base * fps / 1000.0) + direction;
    requestSeek(qRound64(frame * 1000.0 / fps), true);
}

void Player::seekFromTime()
{
    const auto parts = positionEdit_->text().split(QRegularExpression(QStringLiteral("[:.]")));
    if (parts.size() != 4 || !positionEdit_->hasAcceptableInput())
        return;
    bool ok = false;
    const qint64 hours = parts[0].toLongLong(&ok);
    if (!ok || hours > media_->duration() / 3600000) {
        positionEdit_->setText(formatTime(media_->duration()));
        requestSeek(media_->duration(), true);
        return;
    }
    const qint64 position = hours * 3600000 + parts[1].toInt() * 60000
                           + parts[2].toInt() * 1000 + parts[3].toInt();
    const qint64 target = std::min(position, media_->duration());
    positionEdit_->setText(formatTime(target));
    requestSeek(target, true);
}

void Player::showPlaybackError(QMediaPlayer::Error, const QString &message)
{
    previewTimer_->stop();
    settleTimer_->stop();
    pendingSeek_ = scrubbing_ = resumeAfterSeek_ = false;
    status_->setText(QStringLiteral("Unable to play video: ") + message);
    status_->show();
    updatePlaybackControls();
}

void Player::openFile(const QString &videoPath)
{
    previewTimer_->stop();
    settleTimer_->stop();
    pendingSeek_ = scrubbing_ = resumeAfterSeek_ = false;
    frameStart_ = frameEnd_ = -1;
    desiredPosition_ = issuedPosition_ = 0;
    media_->stop();
    media_->setSource(QUrl());
    status_->hide();
    updateDuration();
    if (!QFileInfo(videoPath).isFile()) {
        status_->setText(QStringLiteral("Video not found: ") + videoPath);
        status_->show();
        return;
    }
    media_->setSource(QUrl::fromLocalFile(videoPath));
    media_->play();
}

