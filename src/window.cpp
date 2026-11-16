#include "window.h"

#include "player.h"

Window::Window(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Media Player - test.mp4"));
    resize(960, 600);
    setCentralWidget(new Player(this));
}
