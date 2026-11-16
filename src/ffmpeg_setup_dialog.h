#pragma once

#include <QDialog>
#include <QPointer>

class FfmpegInstaller;
class QLabel;
class QPushButton;
class QProgressBar;
class QPlainTextEdit;
class QPropertyAnimation;
class QHideEvent;
class QShowEvent;

class FfmpegSetupDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FfmpegSetupDialog(FfmpegInstaller *installer, QWidget *parent);
    ~FfmpegSetupDialog() override;

signals:
    void exitRequested();

protected:
    void reject() override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class Mode { Prompt, Downloading, Retry };
    void showPrompt();
    void beginDownload();
    void showFailure(const QString &message);
    void toggleDetails();
    void centerOnParent();
    void appendOutput(const QString &text);

    FfmpegInstaller *installer_;
    QLabel *title_;
    QLabel *message_;
    QProgressBar *progress_;
    QPushButton *primary_;
    QPushButton *detailsButton_;
    QPlainTextEdit *terminal_;
    QPointer<QWidget> dimmer_;
    QPropertyAnimation *fade_;
    Mode mode_ = Mode::Prompt;
};
