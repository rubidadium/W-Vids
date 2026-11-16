#include <QApplication>

#include "player.h"
#include "window.h"

int main(int argc, char *argv[])
{
    Player::configureBackend();
    QApplication app(argc, argv);
    Window window;
    window.show();
    return app.exec();
}
