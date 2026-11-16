#include "window.h"

#include "player.h"
#include "video_downloader.h"

#include <QFileInfo>
#include <QCoreApplication>
#include <QFont>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QFrame>
#include <QHBoxLayout>
#include <QProgressBar>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QVBoxLayout>
#include <QUrl>
#include <nfd.h>
#include <algorithm>

namespace {
constexpr char videoExtensions[] = "mp4,mkv,mov,avi,webm,m4v,mpg,mpeg,wmv,flv,ts,mts,m2ts,ogv";
}

Window::Window(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Media Player"));
    resize(960, 600);
    auto *welcome = new QWidget(this);
    welcome->setObjectName(QStringLiteral("welcome"));
    welcome->setStyleSheet(QStringLiteral("QWidget#welcome { background: #121212; }"));

    auto *select = new QPushButton(welcome);
    selectButton_ = select;
    select->setObjectName(QStringLiteral("selectVideo"));
    select->setAccessibleName(QStringLiteral("Select a Video File"));
    select->setCursor(Qt::PointingHandCursor);
    select->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    select->setStyleSheet(QStringLiteral(
        "QPushButton#selectVideo { background: #171717; border: 4px dashed #454545;"
        " border-radius: 10px; }"
        "QPushButton#selectVideo:hover { background: #1c1c1c; border-color: #777777; }"
        "QPushButton#selectVideo:focus { border-color: #aaaaaa; }"
        "QPushButton#selectVideo:pressed { background: #222222; }"));

    auto *title = new QLabel(QStringLiteral("Select a Video File"), select);
    welcomeTitle_ = title;
    title->setAlignment(Qt::AlignCenter);
    title->setWordWrap(true);
    title->setStyleSheet(QStringLiteral("color: #ededed;"));
    const auto extensions = QString::fromLatin1(videoExtensions).toUpper().split(QLatin1Char(','));
    auto *formats = new QLabel(
        QStringLiteral("Supported Formats: ") + extensions.join(QStringLiteral(" · ")), select);
    welcomeFormats_ = formats;
    formats->setAlignment(Qt::AlignCenter);
    formats->setWordWrap(true);
    formats->setStyleSheet(QStringLiteral("color: #969696;"));
    title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    formats->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    // Let clicks on either label reach the button, including keyboard activation.
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    formats->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *contents = new QVBoxLayout(select);
    contents->setContentsMargins(32, 32, 32, 32);
    contents->setSpacing(14);
    contents->addStretch();
    contents->addWidget(title);
    contents->addWidget(formats);
    contents->addStretch();

    auto *layout = new QVBoxLayout(welcome);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->addWidget(select, 1);
    auto *divider = new QFrame(welcome);
    divider->setFrameShape(QFrame::HLine);
    divider->setStyleSheet(QStringLiteral("color: #454545;"));
    layout->addSpacing(10);
    layout->addWidget(divider);
    layout->addSpacing(10);
    link_ = new QLineEdit(welcome);
    link_->setObjectName(QStringLiteral("videoLink"));
    link_->setPlaceholderText(QStringLiteral("Or load from a link"));
    link_->setAccessibleName(QStringLiteral("Or load from a link"));
    link_->setClearButtonEnabled(true);
    link_->setMinimumHeight(42);
    link_->setStyleSheet(QStringLiteral(
        "QLineEdit { color: #eeeeee; background: #1b1b1b; border: 1px solid #454545;"
        " border-radius: 6px; padding: 0 12px; font-size: 15px; }"
        "QLineEdit:focus { border-color: #08a4bf; }"));
    loadLinkButton_ = new QPushButton(QStringLiteral("Download"), welcome);
    loadLinkButton_->setObjectName(QStringLiteral("loadLink"));
    loadLinkButton_->setMinimumHeight(42);
    loadLinkButton_->setStyleSheet(QStringLiteral(
        "QPushButton { color: white; background: #079bb3; border: none; border-radius: 6px; padding: 0 20px; }"
        "QPushButton:hover { background: #0aafc8; }"
        "QPushButton:disabled { background: #333333; color: #888888; }"));
    cancelDownloadButton_ = new QPushButton(QStringLiteral("Cancel"), welcome);
    cancelDownloadButton_->setObjectName(QStringLiteral("cancelDownload"));
    cancelDownloadButton_->setMinimumHeight(42);
    cancelDownloadButton_->hide();
    auto *linkRow = new QHBoxLayout;
    linkRow->addWidget(link_, 1);
    linkRow->addWidget(loadLinkButton_);
    linkRow->addWidget(cancelDownloadButton_);
    layout->addLayout(linkRow);
    downloadStatus_ = new QLabel(welcome);
    downloadStatus_->setObjectName(QStringLiteral("downloadStatus"));
    downloadStatus_->setWordWrap(true);
    downloadStatus_->setTextFormat(Qt::PlainText);
    downloadStatus_->setStyleSheet(QStringLiteral("color: #b8b8b8;"));
    downloadStatus_->hide();
    downloadProgress_ = new QProgressBar(welcome);
    downloadProgress_->setObjectName(QStringLiteral("downloadProgress"));
    downloadProgress_->hide();
    layout->addWidget(downloadStatus_);
    layout->addWidget(downloadProgress_);
    const QDir application(QCoreApplication::applicationDirPath());
    downloader_ = new VideoDownloader(application.filePath("download"), application.filePath("tools"), this);
    connect(link_, &QLineEdit::returnPressed, this, &Window::loadLink);
    connect(loadLinkButton_, &QPushButton::clicked, this, &Window::loadLink);
    connect(cancelDownloadButton_, &QPushButton::clicked, this, [this] {
        downloader_->cancel();
        downloadStatus_->setText(QStringLiteral("Download canceled."));
        downloadProgress_->hide();
        updateDownloadControls();
    });
    connect(downloader_, &VideoDownloader::statusChanged, this, [this](const QString &text) {
        if (player_) return;
        downloadStatus_->setText(text);
        downloadStatus_->show();
    });
    connect(downloader_, &VideoDownloader::progressChanged, this, [this](int percent) {
        if (player_) return;
        downloadProgress_->setRange(0, percent < 0 ? 0 : 100);
        if (percent >= 0) downloadProgress_->setValue(percent);
    });
    connect(downloader_, &VideoDownloader::failed, this, [this](const QString &message) {
        downloadStatus_->setText(message);
        downloadStatus_->show();
        downloadProgress_->hide();
        updateDownloadControls();
    });
    connect(downloader_, &VideoDownloader::completed, this, &Window::openVideo);
    updateWelcomeText(size());
    select->installEventFilter(this);
    setCentralWidget(welcome);
    connect(select, &QPushButton::clicked, this, &Window::selectVideo);
}

bool Window::eventFilter(QObject *watched, QEvent *event)
{
    if (!player_ && watched == selectButton_ && event->type() == QEvent::Resize)
        updateWelcomeText(static_cast<QResizeEvent *>(event)->size());
    return QMainWindow::eventFilter(watched, event);
}

void Window::updateWelcomeText(const QSize &size)
{
    const double scale = std::min(size.width() / 912.0, size.height() / 552.0);
    auto titleFont = welcomeTitle_->font();
    titleFont.setPixelSize(std::clamp(qRound(30 * scale), 18, 64));
    titleFont.setWeight(QFont::Medium);
    welcomeTitle_->setFont(titleFont);
    auto formatsFont = welcomeFormats_->font();
    formatsFont.setPixelSize(std::clamp(qRound(14 * scale), 11, 28));
    welcomeFormats_->setFont(formatsFont);
    const int padding = std::clamp(qRound(40 * scale), 16, 80);
    selectButton_->layout()->setContentsMargins(padding, padding, padding, padding);
    selectButton_->layout()->setSpacing(std::clamp(qRound(18 * scale), 10, 32));
}

void Window::selectVideo()
{
    if (selectingVideo_ || downloader_->isBusy())
        return;
    QScopedValueRollback<bool> selecting(selectingVideo_, true);

    if (NFD_Init() != NFD_OKAY) {
        QMessageBox::warning(this, QStringLiteral("Unable to open file dialog"),
                             QString::fromUtf8(NFD_GetError()));
        return;
    }

    const nfdu8filteritem_t filters[] = {
        {"Video files", videoExtensions}
    };
    nfdopendialogu8args_t args{};
    args.filterList = filters;
    args.filterCount = 1;
    args.title = "Select a video";
#ifdef Q_OS_WIN
    args.parentWindow = {NFD_WINDOW_HANDLE_TYPE_WINDOWS, reinterpret_cast<void *>(winId())};
#endif

    nfdu8char_t *path = nullptr;
    const nfdresult_t result = NFD_OpenDialogU8_With(&path, &args);
    const QString videoPath = result == NFD_OKAY ? QString::fromUtf8(path) : QString();
    const QString error = result == NFD_ERROR ? QString::fromUtf8(NFD_GetError()) : QString();
    if (path)
        NFD_FreePathU8(path);
    NFD_Quit();

    if (result == NFD_ERROR) {
        QMessageBox::warning(this, QStringLiteral("Unable to open file dialog"), error);
    } else if (result == NFD_OKAY) {
        openVideo(videoPath);
    }
}

void Window::openVideo(const QString &path)
{
    player_ = new Player(this);
    player_->setExportOptions(ffmpegPath_,
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("clips")));
    setCentralWidget(player_);
    setWindowTitle(QStringLiteral("Media Player - ") + QFileInfo(path).fileName());
    player_->openFile(path);
}

void Window::loadLink()
{
    if (player_ || downloader_->isBusy() || !centralWidget()->isEnabled()) return;
    downloadStatus_->show();
    downloadProgress_->show();
    downloader_->start(QUrl(link_->text().trimmed()), ffmpegPath_);
    updateDownloadControls();
}

void Window::updateDownloadControls()
{
    const bool busy = downloader_->isBusy();
    selectButton_->setEnabled(!busy);
    link_->setEnabled(!busy);
    loadLinkButton_->setEnabled(!busy);
    cancelDownloadButton_->setVisible(busy);
}

void Window::setFfmpegPath(const QString &path)
{
    ffmpegPath_ = path;
}
