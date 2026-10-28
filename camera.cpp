//
// Created by yyh on 2026/10/19.
//

// You may need to build the project (run Qt uic code generator) to get "ui_Camera.h" resolved

#include "camera.h"



Camera::Camera(QWidget *parent) : QWidget(parent), ui(new Ui::Camera),
trigger_mode_(Connect_Mode), soft_trigger_(false),
workerThread_(new QThread(this)), worker_(new CameraWorker()) {
    ui->setupUi(this);

    worker_->moveToThread(workerThread_);

    connect_init();

    workerThread_->start();

    data_init();
}

Camera::~Camera() {
    if (workerThread_->isRunning()) {
        QMetaObject::invokeMethod(worker_, "stopAndCleanup",
                                  Qt::QueuedConnection);
        workerThread_->wait();
    }
    delete ui;
}

void Camera::onCameraOpened(bool success) {
    if (success) {
        setInitModuleEnabled(0);
        setParamModuleEnabled(1);
        setAcquireModuleEnabled(1);
        ui->stop_btn->setEnabled(0);
        setSelectModuleEnabled(0);
        ui->checkBox_soft_trigger->setEnabled(0);
        ui->soft_trigger_btn->setEnabled(0);
        ui->label_display->setText("请开始采集");
    }
}

void Camera::handleError(const QString &msg) {
    QMessageBox::warning(this, "错误", msg);
}

void Camera::updateFrameUI(const QImage &img) {
    currentImage_ = img;
    ui->label_display->setPixmap(QPixmap::fromImage(img).scaled(
        ui->label_display->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void Camera::findCamera() {
    ui->comboBox_camera_list->clear();
    cameras_ = QCameraInfo::availableCameras();
    setAllDeviceUiEnabled(0);
    if (cameras_.isEmpty()) {
        ui->comboBox_camera_list->addItem("未检测到摄像头");
        return ;
    }

    for (auto &cameraInfo : cameras_) {
        ui->comboBox_camera_list->addItem(cameraInfo.description());
    }
    ui->comboBox_camera_list->setCurrentIndex(0);
    camera_name_ = ui->comboBox_camera_list->currentText();

    setSelectModuleEnabled(1);
    setDisplayModuleEnabled(1);
}

void Camera::on_find_btn_clicked() {
    findCamera();
}

void Camera::on_comboBox_camera_list_currentIndexChanged(const QString &arg) {
    camera_name_ = arg;
    setInitModuleEnabled(1);
}

void Camera::on_checkBox_soft_trigger_stateChanged(int arg) {
    if (arg == Qt::Checked) {
        soft_trigger_ = 1;
    } else {
        soft_trigger_ = 0;
    }
    if (trigger_mode_ == Trigger_Mode) {
        ui->soft_trigger_btn->setEnabled(soft_trigger_);
    }
}

void Camera::on_radioButton_connect_clicked() {
    trigger_mode_ = Connect_Mode;
    ui->checkBox_soft_trigger->setEnabled(false);
    ui->soft_trigger_btn->setEnabled(false);
    // 通知子线程：切到连续模式
    QMetaObject::invokeMethod(worker_, "setContinuousMode", Qt::QueuedConnection, Q_ARG(bool, true));
}

void Camera::on_radioButton_trigger_clicked() {
    trigger_mode_ = Trigger_Mode;
    ui->checkBox_soft_trigger->setEnabled(true);
    ui->soft_trigger_btn->setEnabled(soft_trigger_);
    // 通知子线程：切到触发模式
    QMetaObject::invokeMethod(worker_, "setContinuousMode", Qt::QueuedConnection, Q_ARG(bool, false));
}

void Camera::on_open_btn_clicked() {
    int index = ui->comboBox_camera_list->currentIndex();
    // 子线程打开设备
    QMetaObject::invokeMethod(worker_, "openCamera", Qt::QueuedConnection, Q_ARG(int, index));
    // 子线程开始死循环读图
}

void Camera::on_close_btn_clicked() {
    // 关闭设备前，先停止采集
    QMetaObject::invokeMethod(worker_, "stopAcquisition", Qt::QueuedConnection);
    // 再关闭设备
    QMetaObject::invokeMethod(worker_, "closeCamera", Qt::QueuedConnection);

    setInitModuleEnabled(1);
    setParamModuleEnabled(0);
    setAcquireModuleEnabled(0);
    setSaveModuleEnabled(0);
    setSelectModuleEnabled(1);
}

void Camera::on_start_btn_clicked() {
    QMetaObject::invokeMethod(worker_, "startAcquisition", Qt::QueuedConnection);

    ui->start_btn->setEnabled(false);
    ui->stop_btn->setEnabled(true);

    if (trigger_mode_ == Trigger_Mode && soft_trigger_) {
        ui->soft_trigger_btn->setEnabled(true);
    }
    setSaveModuleEnabled(1);
}

void Camera::on_stop_btn_clicked() {
    QMetaObject::invokeMethod(worker_, "stopAcquisition", Qt::QueuedConnection);
    ui->start_btn->setEnabled(true);
    ui->stop_btn->setEnabled(false);
    ui->soft_trigger_btn->setEnabled(false);
    setSaveModuleEnabled(0);
}

void Camera::on_soft_trigger_btn_clicked() {
    // 调用子线程进行单次抓图
    QMetaObject::invokeMethod(worker_, "softTriggerOnce", Qt::QueuedConnection);
}

void Camera::on_save_btn_clicked() {
    if (currentImage_.isNull()) {
        QMessageBox::warning(this, "提示", "当前没有可保存的图像！");
        return;
    }

    QString saveDir = QCoreApplication::applicationDirPath() + "/images";
    QDir dir(saveDir);
    if (!dir.exists()) dir.mkpath(".");

    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz");
    QString fileName = saveDir + "/" + timestamp + ".jpg";

    if (!currentImage_.save(fileName, "JPG")) {
        QMessageBox::warning(this, "错误", "图片文件保存失败！");
        return;
    }

    // 将元数据存入 SQLite 数据库
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
    // 数据库文件放在程序运行目录下的 data.db
    db.setDatabaseName(QCoreApplication::applicationDirPath() + "/data.db");

    if (!db.open()) {
        QMessageBox::warning(this, "数据库错误", db.lastError().text());
        return;
    }

    QSqlQuery query;
    query.exec("CREATE TABLE IF NOT EXISTS captures ("
               "id INTEGER PRIMARY KEY AUTOINCREMENT, "
               "file_path TEXT NOT NULL, "
               "capture_time TEXT, "
               "trigger_mode TEXT)");

    query.prepare("INSERT INTO captures (file_path, capture_time, trigger_mode) "
                  "VALUES (:path, :time, :mode)");
    query.bindValue(":path", fileName);
    query.bindValue(":time", timestamp);
    query.bindValue(":mode", trigger_mode_ == Connect_Mode ? "Continuous" : "Trigger");

    if (!query.exec()) {
        QMessageBox::warning(this, "数据库错误", query.lastError().text());
    } else {
        qDebug() << "图片及记录已成功存入数据库！";
        QMessageBox::information(this, "成功", "图片已保存并录入数据库！");
    }

    db.close();
}

void Camera::updateParamUI(double fps, double exposure, double gain) {
    ui->lineEdit_frame->setText(QString::number(fps, 'f', 2));
    ui->lineEdit_exposure->setText(QString::number(exposure, 'f', 2));
    ui->lineEdit_gain->setText(QString::number(gain, 'f', 2));
}

void Camera::data_init() {
    ui->lineEdit_exposure->setText(QString::number(0));
    ui->lineEdit_frame->setText(QString::number(0));
    ui->lineEdit_gain->setText(QString::number(0));

    findCamera();
}

void Camera::connect_init() {
    connect(worker_, &CameraWorker::paramsReady, this, &Camera::updateParamUI);

    connect(worker_, &CameraWorker::frameReady, this, &Camera::updateFrameUI);
    connect(worker_, &CameraWorker::errorOccurred, this, &Camera::handleError);
    connect(worker_, &CameraWorker::cameraOpened, this, &Camera::onCameraOpened);

    connect(worker_,&CameraWorker::pushDisplay, this, [&]() {
        ui->label_display->setText("请打开设备");
    });

    connect(workerThread_, &QThread::finished, worker_, &QObject::deleteLater);

    // 输入完曝光值
    connect(ui->lineEdit_exposure, &QLineEdit::editingFinished, this, [=]() {
        double val = ui->lineEdit_exposure->text().toDouble();
        // 跨线程将指令发送给子线程去执行！
        QMetaObject::invokeMethod(worker_, "setExposureValue", Qt::QueuedConnection, Q_ARG(double, val));
    });

    // 输入完增益值
    connect(ui->lineEdit_gain, &QLineEdit::editingFinished, this, [=]() {
        double val = ui->lineEdit_gain->text().toDouble();
        QMetaObject::invokeMethod(worker_, "setGainValue", Qt::QueuedConnection, Q_ARG(double, val));
    });

    // 输入完帧率值
    connect(ui->lineEdit_frame, &QLineEdit::editingFinished, this, [=]() {
        double val = ui->lineEdit_frame->text().toDouble();
        QMetaObject::invokeMethod(worker_, "setFrameValue", Qt::QueuedConnection, Q_ARG(double, val));
    });
}

// 相机选取模块
void Camera::setSelectModuleEnabled(bool enabled) {
    ui->comboBox_camera_list->setEnabled(enabled);
}

// 初始化模块
void Camera::setInitModuleEnabled(bool enabled) {
    ui->open_btn->setEnabled(enabled);
    ui->close_btn->setEnabled(1 ^ enabled);
}

// 图像采集模块
void Camera::setAcquireModuleEnabled(bool enabled) {
    ui->radioButton_connect->setEnabled(enabled);
    ui->radioButton_trigger->setEnabled(enabled);
    ui->start_btn->setEnabled(enabled);
    ui->stop_btn->setEnabled(enabled);
    ui->soft_trigger_btn->setEnabled(enabled);
    ui->checkBox_soft_trigger->setEnabled(enabled);
}

// 图片保存模块
void Camera::setSaveModuleEnabled(bool enabled) {
    ui->save_btn->setEnabled(enabled);
}

// 参数模块
void Camera::setParamModuleEnabled(bool enabled) {
    ui->lineEdit_exposure->setEnabled(enabled);
    ui->lineEdit_gain->setEnabled(enabled);
    ui->lineEdit_frame->setEnabled(enabled);
}

// 相机显示模块
void Camera::setDisplayModuleEnabled(bool enabled) {
    if (!enabled) {
        ui->label_display->setText("未找到摄像头设备\n请连接设备后点击“查找设备”");
    } else {
        ui->label_display->setText("请打开设备");
    }
}

// 总控函数
void Camera::setAllDeviceUiEnabled(bool enabled) {
    setSelectModuleEnabled(enabled);
    setInitModuleEnabled(enabled);
    setAcquireModuleEnabled(enabled);
    setSaveModuleEnabled(enabled);
    setParamModuleEnabled(enabled);
    setDisplayModuleEnabled(enabled);
}