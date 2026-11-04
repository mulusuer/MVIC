//
// Created by yyh on 2026/11/3.
//

#ifndef MVIC_SQL_H
#define MVIC_SQL_H

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QMessageBox>

class Sql : public QObject {
    Q_OBJECT
public:
    Sql();
    ~Sql();
    signals:
    void sendData(const QList<QVariantMap>& rows);

    void send_is_data(const QString& str);

    void send_is_data_image(const QString& str);

public slots:
    bool query(const QString &str);

    void check(const QString &str, const int& index);

private:
    bool data_init();
    std::unique_ptr<QSqlDatabase> db_;
};


#endif //MVIC_SQL_H