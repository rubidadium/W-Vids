#pragma once

#include <QSlider>
#include <QPointF>

class QMouseEvent;

class SeekSlider : public QSlider
{
public:
    explicit SeekSlider(QWidget *parent = nullptr);
    void setSelection(int start, int end);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QRect timelineRect() const;
    int positionAt(qreal x) const;
    QPointF previousMouse_;
    double dragPosition_ = 0;
    int selectionStart_ = 0;
    int selectionEnd_ = 0;
};
