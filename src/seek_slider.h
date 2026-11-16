#pragma once

#include <QSlider>
#include <QPointF>

class QMouseEvent;

class SeekSlider : public QSlider
{
public:
    explicit SeekSlider(QWidget *parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    int positionAt(qreal x) const;
    QPointF previousMouse_;
    double dragPosition_ = 0;
};
