//
// Created by yyh on 2026/10/19.
//

#ifndef MVIC_CAMERA_H
#define MVIC_CAMERA_H

#include <QWidget>
#include <QString>
#include "ui_Camera.h"
#include <QDebug>
#include <QCameraInfo>
#include "CameraWorker.h"
#include <QThread>
#include <QMessageBox>
#include <QFileDialog>
#include <QDateTime>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QDateTime>
#include <QMessageBox>

QT_BEGIN_NAMESPACE

namespace Ui {
    class Camera;
}

QT_END_NAMESPACE

class Camera : public QWidget {
    Q_OBJECT

public:
    explicit Camera(QWidget *parent = nullptr);
    ~Camera() override;

    void onCameraOpened(bool success);
    void handleError(const QString &msg);
    void updateFrameUI(const QImage &img);
    void findCamera();

public slots:
    void on_find_btn_clicked();
    void on_comboBox_camera_list_currentIndexChanged(const QString& arg);
    void on_checkBox_soft_trigger_stateChanged(int arg);
    void on_radioButton_connect_clicked();
    void on_radioButton_trigger_clicked();
    void on_open_btn_clicked();
    void on_close_btn_clicked();
    void on_start_btn_clicked();
    void on_stop_btn_clicked();
    void on_soft_trigger_btn_clicked();
    void on_save_btn_clicked();

    void updateParamUI(double fps, double exposure, double gain);

private:

    void data_init();
    void connect_init();

    void setSelectModuleEnabled(bool enabled); // 相机选取
    void setInitModuleEnabled(bool enabled);   // 初始化
    void setAcquireModuleEnabled(bool enabled);// 图像采集
    void setSaveModuleEnabled(bool enabled);   // 图片保存
    void setParamModuleEnabled(bool enabled);  // 参数
    void setDisplayModuleEnabled(bool enabled);// 相机显示

    // 总控函数（一键控制所有依赖硬件的模块）
    void setAllDeviceUiEnabled(bool enabled);

    Ui::Camera *ui;
    QString camera_name_;
    bool soft_trigger_;
    int trigger_mode_;
    QList<QCameraInfo> cameras_;
    QThread *workerThread_;
    CameraWorker *worker_;
    QImage currentImage_;

    enum TriggerMode {
        Connect_Mode,
        Trigger_Mode,
    };
};


#endif //MVIC_CAMERA_H