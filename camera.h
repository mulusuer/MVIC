#ifndef MVIC_CAMERA_H
#define MVIC_CAMERA_H

#include <QWidget>
#include <QString>
#include <QDebug>
#include <QCameraInfo>
#include <QThread>
#include <QMessageBox>
#include <QLineEdit>
#include <QFileDialog>
#include <QPixmap>
#include <QDebug>
#include <QImage>
#include "ui_Camera.h"
#include "CameraWorker.h"
#include "ImageProcessor.h"
#include "Sql.h"


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

    signals:
    void send_image(const QString& path);
    void send_sql(const QString& str);
    void check(const QString& str, const int& index);

public slots:

    void on_PageChanged(int index);
    // 实时
    void on_find_btn_clicked();
    void on_comboBox_camera_list_currentIndexChanged(const QString& arg);
    void on_open_btn_clicked();
    void on_close_btn_clicked();
    void on_start_btn_clicked();
    void on_stop_btn_clicked();

    void updateParamUI(double fps, double exposure, double gain);

    // 图片
    void updateParamUI_image(double fps, double exposure, double gain);
    void on_open_btn_image_clicked();
    void on_start_btn_image_clicked();

    // 管理
    void on_btn_find_car_clicked();
    void on_btn_refresh_plate_clicked();
    void on_btn_add_plate_clicked();
    void on_btn_delete_plate_clicked();

    // 车牌处理相关
    void updateProcessedFrame(const QImage &img);
    void onPlateFound(const QString &text);

private:
    void data_init();
    void connect_init();

    void data_show(const QList<QVariantMap>& data);

    void showImage(const QImage &img);

    void connectParamEdit(QLineEdit *edit, const char *slot, const QString &label);

    void setSelectModuleEnabled(bool enabled);
    void setInitModuleEnabled(bool enabled);
    void setAcquireModuleEnabled(bool enabled);
    void setParamModuleEnabled(bool enabled);
    void setDisplayModuleEnabled(bool enabled);
    void setAllDeviceUiEnabled(bool enabled);

    Ui::Camera *ui;
    QList<QCameraInfo> cameras_;
    QThread *workerThread_;
    CameraWorker *worker_;
    Sql *data_;
    QThread *dataThread_;

    QThread *processorThread_ = nullptr;
    ImageProcessor *processor_ = nullptr;
    bool processingActive_ = false;

    QString imgPath;
};

#endif // MVIC_CAMERA_H