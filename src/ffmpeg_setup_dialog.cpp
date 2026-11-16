#include "ffmpeg_setup_dialog.h"
#include "ffmpeg_installer.h"

#include <QFontDatabase>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QShowEvent>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>

FfmpegSetupDialog::FfmpegSetupDialog(FfmpegInstaller *installer, QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint), installer_(installer)
{
    setObjectName(QStringLiteral("ffmpegSetup"));
    setWindowTitle(QStringLiteral("FFmpeg setup"));
    setWindowModality(Qt::WindowModal);
    setStyleSheet(QStringLiteral(
        "QDialog#ffmpegSetup { background: #222426; border: 1px solid #52565b; border-radius: 12px; }"
        "QLabel { color: #c1c5ca; background: transparent; }"
        "QLabel#setupTitle { color: #f3f4f5; font-size: 23px; font-weight: 600; }"
        "QPushButton { color: #e9edef; background: #35383c; border: 1px solid #50555a;"
        " border-radius: 6px; padding: 9px 18px; }"
        "QPushButton:hover { background: #42474c; }"
        "QPushButton:focus { border-color: #48bdcf; }"
        "QPushButton#setupPrimary { background: #079bb3; border-color: #09aac4; color: white; }"
        "QPushButton#setupPrimary:hover { background: #0aafc8; }"
        "QPushButton:disabled { color: #777e84; background: #303438; border-color: #3c4145; }"
        "QProgressBar { color: white; background: #151719; border: 1px solid #41464a;"
        " border-radius: 5px; text-align: center; min-height: 22px; }"
        "QProgressBar::chunk { background: #079bb3; border-radius: 4px; }"
        "QPlainTextEdit { color: #cbd5d9; background: #101214; border: 1px solid #41464a;"
        " border-radius: 6px; padding: 10px; }"));
    title_ = new QLabel(QStringLiteral("FFmpeg is required"), this);
    title_->setObjectName(QStringLiteral("setupTitle"));
    message_ = new QLabel(this);
    message_->setWordWrap(true);
    progress_ = new QProgressBar(this);
    progress_->setObjectName(QStringLiteral("setupProgress"));
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->hide();
    detailsButton_ = new QPushButton(QStringLiteral("Show Details"), this);
    detailsButton_->setObjectName(QStringLiteral("setupDetails"));
    detailsButton_->hide();
    primary_ = new QPushButton(QStringLiteral("Download"), this);
    primary_->setObjectName(QStringLiteral("setupPrimary"));
    primary_->setDefault(true);
    auto *exit = new QPushButton(QStringLiteral("Exit"), this);
    terminal_ = new QPlainTextEdit(this);
    terminal_->setObjectName(QStringLiteral("setupTerminal"));
    terminal_->setReadOnly(true);
    terminal_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    terminal_->setLineWrapMode(QPlainTextEdit::NoWrap);
    terminal_->document()->setMaximumBlockCount(1500);
    terminal_->setFixedHeight(230);
    terminal_->hide();
    auto *actions = new QHBoxLayout;
    actions->addWidget(detailsButton_);
    actions->addStretch();
    actions->addWidget(exit);
    actions->addWidget(primary_);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 28, 28, 28);
    layout->setSpacing(18);
    layout->addWidget(title_);
    layout->addWidget(message_);
    layout->addWidget(progress_);
    layout->addLayout(actions);
    layout->addWidget(terminal_);
    setFixedSize(540, 260);

    dimmer_ = new QWidget(parent);
    dimmer_->setObjectName(QStringLiteral("setupDimmer"));
    dimmer_->setStyleSheet(QStringLiteral("background-color: rgba(0, 0, 0, 175);"));
    dimmer_->hide();
    auto *opacity = new QGraphicsOpacityEffect(dimmer_);
    dimmer_->setGraphicsEffect(opacity);
    fade_ = new QPropertyAnimation(opacity, "opacity", this);
    fade_->setDuration(180);
    fade_->setStartValue(0.0);
    fade_->setEndValue(1.0);
    parent->installEventFilter(this);

    connect(exit, &QPushButton::clicked, this, &FfmpegSetupDialog::reject);
    connect(detailsButton_, &QPushButton::clicked, this, &FfmpegSetupDialog::toggleDetails);
    connect(primary_, &QPushButton::clicked, this, [this] {
        if (mode_ == Mode::Retry) {
            primary_->setEnabled(false);
            installer_->start();
        } else if (mode_ == Mode::Prompt) {
            beginDownload();
        }
    });
    connect(installer_, &FfmpegInstaller::downloadRequired, this, [this] {
        if (mode_ == Mode::Retry)
            beginDownload();
        else
            showPrompt();
    });
    connect(installer_, &FfmpegInstaller::statusChanged, message_, &QLabel::setText);
    connect(installer_, &FfmpegInstaller::outputReceived, this, &FfmpegSetupDialog::appendOutput);
    connect(installer_, &FfmpegInstaller::progressChanged, this, [this](int percent) {
        progress_->setRange(0, percent < 0 ? 0 : 100);
        if (percent >= 0)
            progress_->setValue(percent);
    });
    connect(installer_, &FfmpegInstaller::failed, this, &FfmpegSetupDialog::showFailure);
    connect(installer_, &FfmpegInstaller::ready, this, &QDialog::accept);
}

FfmpegSetupDialog::~FfmpegSetupDialog()
{
    delete dimmer_;
}

void FfmpegSetupDialog::showPrompt()
{
    mode_ = Mode::Prompt;
    title_->setText(QStringLiteral("FFmpeg is required"));
    message_->setText(QStringLiteral("FFmpeg was not detected. Download it to enable video trimming.\n"
                                    "It will be saved in the ffmpeg folder beside this application."));
    primary_->setText(QStringLiteral("Download"));
    primary_->setEnabled(true);
    show();
    primary_->setFocus();
}

void FfmpegSetupDialog::beginDownload()
{
    mode_ = Mode::Downloading;
    title_->setText(QStringLiteral("Setting up FFmpeg"));
    primary_->setText(QStringLiteral("Downloading…"));
    primary_->setEnabled(false);
    progress_->show();
    detailsButton_->show();
    installer_->confirmDownload(true);
}

void FfmpegSetupDialog::showFailure(const QString &message)
{
    mode_ = Mode::Retry;
    title_->setText(QStringLiteral("FFmpeg setup failed"));
    message_->setText(message);
    progress_->setRange(0, 100);
    primary_->setText(QStringLiteral("Retry"));
    primary_->setEnabled(true);
    detailsButton_->show();
    appendOutput(QStringLiteral("\nERROR: ") + message + QLatin1Char('\n'));
    show();
}

void FfmpegSetupDialog::toggleDetails()
{
    const bool expanded = terminal_->isHidden();
    terminal_->setVisible(expanded);
    detailsButton_->setText(expanded ? QStringLiteral("Hide Details") : QStringLiteral("Show Details"));
    setFixedSize(expanded ? 760 : 540, expanded ? 540 : 260);
    centerOnParent();
}

void FfmpegSetupDialog::appendOutput(const QString &text)
{
    auto cursor = terminal_->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(QString(text).replace(QStringLiteral("\r\n"), QStringLiteral("\n"))
                                  .replace(QLatin1Char('\r'), QLatin1Char('\n')));
    terminal_->setTextCursor(cursor);
    terminal_->ensureCursorVisible();
}

void FfmpegSetupDialog::reject()
{
    emit exitRequested();
}

void FfmpegSetupDialog::centerOnParent()
{
    const QPoint center = parentWidget()->mapToGlobal(parentWidget()->rect().center());
    QRect area = screen()->availableGeometry();
    const QPoint desired = center - rect().center();
    move(qBound(area.left(), desired.x(), qMax(area.left(), area.right() - width() + 1)),
         qBound(area.top(), desired.y(), qMax(area.top(), area.bottom() - height() + 1)));
}

void FfmpegSetupDialog::showEvent(QShowEvent *event)
{
    dimmer_->setGeometry(parentWidget()->rect());
    dimmer_->show();
    dimmer_->raise();
    centerOnParent();
    fade_->start();
    QDialog::showEvent(event);
}

void FfmpegSetupDialog::hideEvent(QHideEvent *event)
{
    fade_->stop();
    dimmer_->hide();
    QDialog::hideEvent(event);
}

bool FfmpegSetupDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && isVisible()
        && (event->type() == QEvent::Resize || event->type() == QEvent::Move)) {
        dimmer_->setGeometry(parentWidget()->rect());
        centerOnParent();
    }
    return QDialog::eventFilter(watched, event);
}
