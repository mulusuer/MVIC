//
// Created by yyh on 2026/11/3.
//

#include "Sql.h"

Sql::Sql() {
    data_init();
}

Sql::~Sql() {
    if (db_) {
        db_->close();
        db_.reset();
    }
    QSqlDatabase::removeDatabase("carData");
}

bool Sql::query(const QString &str) {
    QSqlQuery query(*db_);
    QList<QVariantMap> rows;
    if (!query.exec(str)) {
        QMessageBox::warning(nullptr, "查询失败", query.lastError().text());
        return 0;
    }
    while (query.next()) {
        QVariantMap row;
        row["plate"] = query.value("plate");
        row["name"] = query.value("name");
        rows.append(row);
    }
    emit sendData(rows);
    return 1;
}

void Sql::check(const QString &str, const int& index) {
    QSqlQuery query(*db_);
    QList<QVariantMap> rows;
    if (!query.exec(str)) {
        if (index == 0) {
            emit send_is_data("不存在该车牌");
        } else {
            emit send_is_data_image("不存在该车牌");
        }
    }
    while (query.next()) {
        if (index == 0) {
            emit send_is_data(query.value("name").toString());
        } else {
            emit send_is_data_image(query.value("name").toString());
        }
        return ;
    }
}

bool Sql::data_init() {
    db_ = std::make_unique<QSqlDatabase>(
            QSqlDatabase::addDatabase("QSQLITE", "carData"));
    db_->setDatabaseName("carData.db");
    if (!db_->open()) {
        qDebug() << "carData 打开失败: " << db_->lastError().text();
        return 0;
    }
    qDebug() << "carData 打开成功";
    QSqlQuery query(*db_);
    bool ok = query.exec(
        "CREATE TABLE IF NOT EXISTS test ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "plate TEXT NOT NULL UNIQUE, "
        "name TEXT NOT NULL)"
    );
    if (!ok) {
        qDebug() << "建表失败:" << query.lastError().text();
        return 0;
    }
    return 1;
}
