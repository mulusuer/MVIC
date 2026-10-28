#include <QApplication>
#include "camera.h"


int main(int argc, char *argv[]) {
    QApplication a(argc, argv);

    Camera w;
    w.show();

    return QApplication::exec();
}