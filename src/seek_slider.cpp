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

void SeekSlider::paintEvent(QPaintEvent *)
{
    QStyleOptionSlider option;
    initStyleOption(&option);
    QPainter painter(this);
    const QRect groove = style()->subControlRect(QStyle::CC_Slider, &option,
                                                  QStyle::SC_SliderGroove, this);
    const QRect handle = style()->subControlRect(QStyle::CC_Slider, &option,
                                                  QStyle::SC_SliderHandle, this);
    const auto xAt = [&](int position) {
        return groove.left() + handle.width() / 2 + QStyle::sliderPositionFromValue(
            minimum(), maximum(), position, std::max(1, groove.width() - handle.width()), option.upsideDown);
    };
    const int start = xAt(selectionStart_);
    const int end = xAt(selectionEnd_);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#45494c"));
    painter.drawRoundedRect(QRect(groove.left(), groove.center().y() - 5, groove.width(), 10), 3, 3);
    painter.setBrush(QColor("#9b7059"));
    painter.drawRect(QRect(QPoint(std::min(start, end), groove.center().y() - 5),
                           QPoint(std::max(start, end), groove.center().y() + 5)));
    painter.setPen(QPen(QColor("#e1b598"), 2));
    for (const int x : {start, end})
        painter.drawLine(x, groove.center().y() - 11, x, groove.center().y() + 11);
    option.subControls = QStyle::SC_SliderHandle;
    style()->drawComplexControl(QStyle::CC_Slider, &option, &painter, this);
}

int SeekSlider::positionAt(qreal x) const
{
    QStyleOptionSlider option;
    initStyleOption(&option);
    const QRect groove = style()->subControlRect(QStyle::CC_Slider, &option,
                                                  QStyle::SC_SliderGroove, this);
    const QRect handle = style()->subControlRect(QStyle::CC_Slider, &option,
                                                  QStyle::SC_SliderHandle, this);
    return QStyle::sliderValueFromPosition(minimum(), maximum(),
        qRound(x - groove.left() - handle.width() / 2.0),
        std::max(1, groove.width() - handle.width()), option.upsideDown);
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
