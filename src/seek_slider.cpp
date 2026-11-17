#include "seek_slider.h"

#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionSlider>
#include <algorithm>
#include <cmath>

SeekSlider::SeekSlider(QWidget *parent) : QSlider(Qt::Horizontal, parent)
{
    setSingleStep(1);
    setMinimumHeight(40);
    setToolTip(QStringLiteral("Drag to seek. Hold Shift for fine adjustments. Arrow keys adjust by 1 ms."));
    setAccessibleName(QStringLiteral("Video timeline"));
}

void SeekSlider::setSelection(int start, int end)
{
    selectionStart_ = std::clamp(start, minimum(), maximum());
    selectionEnd_ = std::clamp(end, selectionStart_, maximum());
    update();
}

QRect SeekSlider::timelineRect() const
{
    return QRect(2, (height() - 16) / 2, std::max(1, width() - 4), 16);
}

void SeekSlider::paintEvent(QPaintEvent *)
{
    QStyleOptionSlider option;
    initStyleOption(&option);
    QPainter painter(this);
    const QRect bar = timelineRect();
    painter.setOpacity(isEnabled() ? 1.0 : 0.45);
    const auto xAt = [&](int position) {
        return bar.left() + QStyle::sliderPositionFromValue(
            minimum(), maximum(), position, std::max(1, bar.width() - 1), option.upsideDown);
    };
    const int start = xAt(selectionStart_);
    const int end = xAt(selectionEnd_);
    painter.fillRect(bar, QColor("#45494c"));
    painter.fillRect(QRect(QPoint(std::min(start, end), bar.top()),
                           QPoint(std::max(start, end), bar.bottom())), QColor("#9b7059"));
    painter.setPen(QPen(QColor("#e1b598"), 2));
    for (const int x : {start, end})
        painter.drawLine(x, bar.top() - 3, x, bar.bottom() + 3);
    // Draw the playhead last so it remains visible over the selection and trim markers.
    painter.setPen(QPen(QColor("#f5f5f5"), 2, Qt::SolidLine, Qt::FlatCap));
    const int playhead = xAt(value());
    painter.drawLine(playhead, bar.top() - 6, playhead, bar.bottom() + 7);
}

int SeekSlider::positionAt(qreal x) const
{
    QStyleOptionSlider option;
    initStyleOption(&option);
    const QRect bar = timelineRect();
    return QStyle::sliderValueFromPosition(minimum(), maximum(),
        qRound(x - bar.left()), std::max(1, bar.width() - 1), option.upsideDown);
}

void SeekSlider::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QSlider::mousePressEvent(event);
        return;
    }
    setFocus(Qt::MouseFocusReason);
    previousMouse_ = event->position();
    setSliderDown(true);
    // Shift begins a relative fine drag without jumping to the pointer.
    if (!(event->modifiers() & Qt::ShiftModifier))
        setValue(positionAt(event->position().x()));
    dragPosition_ = value();
    event->accept();
}

void SeekSlider::mouseMoveEvent(QMouseEvent *event)
{
    if (!isSliderDown()) {
        QSlider::mouseMoveEvent(event);
        return;
    }
    const double delta = positionAt(event->position().x()) - positionAt(previousMouse_.x());
    dragPosition_ = std::clamp(dragPosition_ + delta
        * (event->modifiers() & Qt::ShiftModifier ? 0.1 : 1.0),
        double(minimum()), double(maximum()));
    previousMouse_ = event->position();
    setValue(qRound(dragPosition_));
    event->accept();
}

void SeekSlider::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && isSliderDown()) {
        mouseMoveEvent(event);
        setSliderDown(false);
        event->accept();
        return;
    }
    QSlider::mouseReleaseEvent(event);
}
