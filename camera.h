#ifndef MVIC_CAMERA_H
#define MVIC_CAMERA_H

#include <QWidget>
#include <QString>
#include <QDebug>
#include <QCameraInfo>
#include <QThread>
#include <QMessageBox>
#include <QDateTime>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QImage>

#include "ui_Camera.h"
#include "CameraWorker.h"
#include "ImageProcessor.h"

QT_BEGIN_NAMESPACE
namespace Ui { class Camera; }
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
    void on_open_btn_clicked();
    void on_close_btn_clicked();
    void on_start_btn_clicked();
    void on_stop_btn_clicked();
    void on_save_btn_clicked();

    void updateParamUI(double fps, double exposure, double gain);

    // 车牌处理相关
    void updateProcessedFrame(const QImage &img);
    void onPlateFound(const QString &text);

private:
    void data_init();
    void connect_init();

    void setSelectModuleEnabled(bool enabled);
    void setInitModuleEnabled(bool enabled);
    void setAcquireModuleEnabled(bool enabled);
    void setSaveModuleEnabled(bool enabled);
    void setParamModuleEnabled(bool enabled);
    void setDisplayModuleEnabled(bool enabled);
    void setAllDeviceUiEnabled(bool enabled);

    Ui::Camera *ui;
    QString camera_name_;
    QList<QCameraInfo> cameras_;
    QThread *workerThread_;
    CameraWorker *worker_;
    QImage currentImage_;

    QThread        *processorThread_ = nullptr;
    ImageProcessor *processor_       = nullptr;
    bool            processingActive_ = false;
    QString         lastImageFile_;      // 最近打开的图片，保存时用得上
};

#endif // MVIC_CAMERA_H