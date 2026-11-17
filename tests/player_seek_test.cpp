// Run player_seek_test <path to a seekable video at least 10 seconds long>.
#include "player.h"
#include "seek_slider.h"

#include <QApplication>
#include <QFileInfo>
#include <QLineEdit>
#include <QMediaPlayer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QVideoFrame>
#include <QVideoSink>
#include <QVideoWidget>
#include <QWheelEvent>
#include <cstdio>
#include <cstdlib>

static void require(bool passed, const char *message)
{
    if (!passed) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

int main(int argc, char **argv)
{
    Player::configureBackend();
    QApplication app(argc, argv);
    require(argc == 2 && QFileInfo(QString::fromLocal8Bit(argv[1])).isFile(),
            "Pass a real video fixture");
    Player widget;
    widget.resize(960, 600);
    widget.show();
    widget.activateWindow();
    require(QTest::qWaitForWindowActive(&widget), "Player window is active for keyboard shortcut tests");
    widget.openFile(QString::fromLocal8Bit(argv[1]));
    auto *media = widget.findChild<QMediaPlayer *>();
    auto *slider = widget.findChild<QSlider *>(QStringLiteral("timeline"));
    auto *editor = widget.findChild<QLineEdit *>(QStringLiteral("position"));
    auto *play = widget.findChild<QPushButton *>(QStringLiteral("play"));
    require(QTest::qWaitFor([&] { return media->duration() >= 10000 && slider->isEnabled()
                                      && media->videoSink()->videoFrame().isValid(); }, 10000),
            "Fixture loaded and decoded");
    require(slider->maximum() == media->duration(), "Timeline has millisecond resolution");
    media->pause();
    require(QTest::qWaitFor([&] { return media->playbackState() == QMediaPlayer::PausedState; }),
            "Pause before precision checks");

    const auto enterTime = [&](const QString &text, qint64 target) {
        editor->setFocus();
        editor->setText(text);
        QTest::keyClick(editor, Qt::Key_Return);
        require(QTest::qWaitFor([&] { return qAbs(media->position() - target) < 2; }),
                "Exact time entry seeks to millisecond target");
        slider->setFocus();
        QTest::qWait(250);
        require(qAbs(slider->value() - target) < 2, "Target stays stable after seek");
    };
    enterTime(QStringLiteral("00:00:03.217"), 3217);
    require(media->playbackState() == QMediaPlayer::PausedState, "Time entry stays paused");
    // Verify a decoded preview, rather than only QMediaPlayer's optimistic position.
    require(QTest::qWaitFor([&] {
        const auto frame = media->videoSink()->videoFrame();
        return frame.startTime() <= 3218000 && frame.endTime() > 3217000;
    }, 3000), "Decoded frame covers entered time");

    QTest::keyClick(slider, Qt::Key_Right);
    require(QTest::qWaitFor([&] { return media->position() == 3218; }),
            "Right arrow seeks forward one millisecond");
    QTest::keyClick(slider, Qt::Key_Left);
    require(QTest::qWaitFor([&] { return media->position() == 3217; }),
            "Left arrow seeks backward one millisecond");

    play->setFocus();
    QTest::keyClick(play, Qt::Key_Right);
    require(QTest::qWaitFor([&] { return media->position() == 3218; }),
            "Right arrow scrubs while play button has focus");
    auto *trimStart = widget.findChild<QPushButton *>("trimStart");
    trimStart->setFocus();
    QTest::keyClick(trimStart, Qt::Key_Left);
    require(QTest::qWaitFor([&] { return media->position() == 3217; }),
            "Left arrow scrubs while trim button has focus");
    auto *video = widget.findChild<QVideoWidget *>();
    video->setFocus();
    QTest::keyClick(video, Qt::Key_Right);
    require(QTest::qWaitFor([&] { return media->position() == 3218; }),
            "Right arrow scrubs while video has focus");
    editor->setFocus();
    editor->setCursorPosition(8);
    const qint64 beforeTextEdit = media->position();
    QTest::keyClick(editor, Qt::Key_Left);
    require(editor->cursorPosition() == 7 && media->position() == beforeTextEdit,
            "Arrow keys still move the cursor in the time editor");
    QTest::keyClick(editor, Qt::Key_Right);
    require(editor->cursorPosition() == 8 && media->position() == beforeTextEdit,
            "Time editor does not trigger a seek");
    slider->setFocus();
    QTest::keyClick(slider, Qt::Key_Space);
    require(QTest::qWaitFor([&] { return media->playbackState() == QMediaPlayer::PlayingState; }, 3000),
            "Space starts playback from the timeline");
    trimStart->setFocus();
    QTest::keyClick(trimStart, Qt::Key_Space);
    require(QTest::qWaitFor([&] { return media->playbackState() == QMediaPlayer::PausedState; }),
            "Space pauses playback while a trim button has focus");
    editor->setFocus();
    QTest::keyClick(editor, Qt::Key_Space);
    require(media->playbackState() == QMediaPlayer::PausedState,
            "Typing in the time field does not toggle playback");
    slider->setFocus();

    QTest::mouseClick(slider, Qt::LeftButton, Qt::NoModifier,
                      QPoint(slider->width() * 3 / 4, slider->height() / 2));
    const int clickedTarget = slider->value();
    require(clickedTarget > media->duration() / 2, "Track click jumps toward pointer");
    require(QTest::qWaitFor([&] { return qAbs(media->position() - clickedTarget) < 2; }),
            "Track click commits seek");
    QTest::qWait(300);
    require(qAbs(slider->value() - clickedTarget) < 2, "Track click does not snap back");

    QWheelEvent wheel(QPointF(slider->rect().center()),
        QPointF(slider->mapToGlobal(slider->rect().center())), QPoint(), QPoint(0, 120),
        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(slider, &wheel);
    require(slider->value() != clickedTarget && media->position() == slider->value(),
            "Wheel input commits a seek");

    enterTime(QStringLiteral("00:00:05.000"), 5000);
    QTest::mouseClick(widget.findChild<QPushButton *>(QStringLiteral("nextFrame")), Qt::LeftButton);
    const qint64 nextFrame = media->position();
    require(nextFrame > 5000 && nextFrame < 5100, "Next frame advances nominal frame");
    QTest::qWait(250);
    QTest::mouseClick(widget.findChild<QPushButton *>(QStringLiteral("previousFrame")), Qt::LeftButton);
    require(media->position() < nextFrame, "Previous frame moves backward");
    require(media->playbackState() == QMediaPlayer::PausedState, "Frame stepping stays paused");

    QTest::mouseClick(play, Qt::LeftButton);
    require(QTest::qWaitFor([&] { return media->playbackState() == QMediaPlayer::PlayingState; }, 3000),
            "Play before dragging");
    slider->setSliderDown(true);
    require(media->playbackState() == QMediaPlayer::PausedState, "Dragging pauses playback");
    QSignalSpy positions(media, &QMediaPlayer::positionChanged);
    for (int i = 0; i < 100; ++i)
        slider->setValue(6000 + i * 10);
    QTest::qWait(100);
    require(positions.size() < 10, "Rapid drag coalesces preview seeks");
    require(slider->value() == 6990, "Backend updates cannot overwrite active drag");
    slider->setValue(7123);
    slider->setSliderDown(false);
    require(QTest::qWaitFor([&] { return media->playbackState() == QMediaPlayer::PlayingState; }, 3000),
            "Playback resumes after final seek settles");
    require(media->position() >= 7123 && media->position() < 7800, "Final drag target committed");

    media->pause();
    QTest::qWait(100);
    const int start = slider->value();
    const QPoint from(slider->width() / 2, slider->height() / 2);
    const QPoint to = from + QPoint(50, 0);
    QTest::mousePress(slider, Qt::LeftButton, Qt::ShiftModifier, from);
    require(slider->value() == start, "Shift press does not jump");
    QMouseEvent fineMove(QEvent::MouseMove, to, slider->mapToGlobal(to),
                         Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
    QApplication::sendEvent(slider, &fineMove);
    require(slider->value() > start && slider->value() - start < media->duration() / 100,
            "Shift dragging uses fine relative movement");
    QTest::mouseRelease(slider, Qt::LeftButton, Qt::ShiftModifier, to);
    QTest::qWait(300);
    require(media->playbackState() == QMediaPlayer::PausedState, "Paused drag stays paused");

    std::fprintf(stdout, "PASS: precise entry, decoded preview, keyboard, track, wheel, frame steps, throttling, release, fine drag\n");
    return 0;
}
