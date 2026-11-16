#include "window.h"

#include "player.h"

#include <QFileInfo>
#include <QCoreApplication>
#include <QFont>
#include <QDir>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QVBoxLayout>
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
        "QPushButton#selectVideo { background: #171717; border: 2px dashed #454545;"
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
    if (selectingVideo_)
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
        player_ = new Player(this);
        player_->setExportOptions(ffmpegPath_,
            QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("clips")));
        setCentralWidget(player_);
        setWindowTitle(QStringLiteral("Media Player - ") + QFileInfo(videoPath).fileName());
        player_->openFile(videoPath);
    }
}

void Window::setFfmpegPath(const QString &path)
{
    ffmpegPath_ = path;
}
