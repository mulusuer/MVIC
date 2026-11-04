#include <QApplication>
#include "camera.h"
#include <QVariant>

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    qRegisterMetaType<QList<QVariantMap>>("QList<QVariantMap>");

    Camera w;
    w.show();

    return QApplication::exec();
}