#include "camera.h"

#include <QFileDialog>
#include <QPushButton>

Camera::Camera(QWidget *parent) : QWidget(parent), ui(new Ui::Camera),
workerThread_(new QThread(this)), worker_(new CameraWorker()) {
    ui->setupUi(this);

    // 1. worker 搬入采集线程
    worker_->moveToThread(workerThread_);

    // 2. ★ 处理线程和 processor 必须在 connect_init 之前创建
    processorThread_ = new QThread(this);
    processor_       = new ImageProcessor();
    processor_->moveToThread(processorThread_);

    // 3. 建立所有连接
    connect_init();

    // 4. 启动线程
    workerThread_->start();
    processorThread_->start();

    // 5. 加一个"打开图片识别"按钮，插在"保存"按钮那一栏里
    auto *openImageBtn = new QPushButton("打开图片识别", this);
    QWidget *holder = ui->save_btn;
    while (holder && !holder->layout())
        holder = holder->parentWidget();
    if (holder && holder->layout())
        holder->layout()->addWidget(openImageBtn);

    connect(openImageBtn, &QPushButton::clicked, this, [this]() {
        const QString file = QFileDialog::getOpenFileName(
            this, "选择一张车牌照片", QString(),
            "图片 (*.jpg *.jpeg *.png *.bmp)");
        if (file.isEmpty()) return;

        QImage img(file);
        if (img.isNull()) {
            QMessageBox::warning(this, "错误", "这张图片打不开");
            return;
        }

        processingActive_ = true;                 // 让处理结果显示到画面上
        ui->label_result->setText("识别中，第一次要加载模型请稍等...");
        lastImageFile_ = file;

        QMetaObject::invokeMethod(processor_, "processFrame",
                                  Qt::QueuedConnection, Q_ARG(QImage, img));
    });

    // 6. UI 初始化
    data_init();
}

Camera::~Camera() {
    if (workerThread_->isRunning()) {
        QMetaObject::invokeMethod(worker_, "stopAndCleanup", Qt::QueuedConnection);
        workerThread_->wait();
    }
    if (processorThread_ && processorThread_->isRunning()) {
        processorThread_->quit();
        processorThread_->wait();
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
        ui->label_display->setText("请开始采集");
    }
}

void Camera::handleError(const QString &msg) {
    QMessageBox::warning(this, "错误", msg);
}

void Camera::updateFrameUI(const QImage &img) {
    currentImage_ = img;

    // 处理中：由 updateProcessedFrame 负责刷新 label_display
    if (processingActive_) return;

    ui->label_display->setPixmap(
        QPixmap::fromImage(img).scaled(
            ui->label_display->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation));
}

void Camera::updateProcessedFrame(const QImage &img) {
    if (!processingActive_) return;   // 已停止时忽略迟到帧

    ui->label_display->setPixmap(
        QPixmap::fromImage(img).scaled(
            ui->label_display->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation));
}

void Camera::onPlateFound(const QString &text) {
    ui->label_result->setText("车牌: " + text);
}

void Camera::findCamera() {
    ui->comboBox_camera_list->clear();
    cameras_ = QCameraInfo::availableCameras();
    setAllDeviceUiEnabled(0);
    if (cameras_.isEmpty()) {
        ui->comboBox_camera_list->addItem("未检测到摄像头");
        return;
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

void Camera::on_open_btn_clicked() {
    int index = ui->comboBox_camera_list->currentIndex();
    QMetaObject::invokeMethod(worker_, "openCamera",
                              Qt::QueuedConnection, Q_ARG(int, index));
}

void Camera::on_close_btn_clicked() {
    QMetaObject::invokeMethod(worker_, "stopAcquisition", Qt::QueuedConnection);
    QMetaObject::invokeMethod(worker_, "closeCamera", Qt::QueuedConnection);

    processingActive_ = false;
    QMetaObject::invokeMethod(worker_, "setProcessingEnabled",
                              Qt::QueuedConnection, Q_ARG(bool, false));

    setInitModuleEnabled(1);
    setParamModuleEnabled(0);
    setAcquireModuleEnabled(0);
    setSaveModuleEnabled(0);
    setSelectModuleEnabled(1);
}

void Camera::on_start_btn_clicked() {
    QMetaObject::invokeMethod(worker_, "startAcquisition", Qt::QueuedConnection);

    // 开始采集 → 同时打开车牌检测
    processingActive_ = true;
    lastImageFile_.clear();      // 来源是摄像头，不是打开的图片
    QMetaObject::invokeMethod(worker_, "setProcessingEnabled",
                              Qt::QueuedConnection, Q_ARG(bool, true));
    ui->label_result->clear();

    ui->start_btn->setEnabled(false);
    ui->stop_btn->setEnabled(true);

    setSaveModuleEnabled(1);
}

void Camera::on_stop_btn_clicked() {
    QMetaObject::invokeMethod(worker_, "stopAcquisition", Qt::QueuedConnection);

    processingActive_ = false;
    QMetaObject::invokeMethod(worker_, "setProcessingEnabled",
                              Qt::QueuedConnection, Q_ARG(bool, false));

    ui->start_btn->setEnabled(true);
    ui->stop_btn->setEnabled(false);
    setSaveModuleEnabled(0);
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

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
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
    query.bindValue(":mode", lastImageFile_.isEmpty() ? "Camera" : "Image");

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
    ui->label_result->clear();
    findCamera();
}

void Camera::connect_init() {
    // ---- 原 worker 信号 ----
    connect(worker_, &CameraWorker::paramsReady, this, &Camera::updateParamUI);
    connect(worker_, &CameraWorker::frameReady, this, &Camera::updateFrameUI);
    connect(worker_, &CameraWorker::errorOccurred, this, &Camera::handleError);
    connect(worker_, &CameraWorker::cameraOpened, this, &Camera::onCameraOpened);
    connect(worker_, &CameraWorker::pushDisplay, this, [this]() {
        ui->label_display->setText("请打开设备");
    });
    connect(workerThread_, &QThread::finished, worker_, &QObject::deleteLater);

    // ---- 新增：worker <-> processor 连接 ----
    connect(worker_, &CameraWorker::frameForProcess,
            processor_, &ImageProcessor::processFrame);          // 跨线程自动 Queued
    connect(processor_, &ImageProcessor::busyChanged,
            worker_, &CameraWorker::setProcBusy);
    connect(processor_, &ImageProcessor::processedFrame,
            this, &Camera::updateProcessedFrame);
    connect(processor_, &ImageProcessor::plateFound,
            this, &Camera::onPlateFound);
    // Python 服务的状态直接显示在结果栏，看不到进度会以为卡死了
    connect(processor_, &ImageProcessor::statusMessage, this,
            [this](const QString &s) {
                qDebug() << "[proc]" << s;
                ui->label_result->setText(s);
            });
    connect(processorThread_, &QThread::finished,
            processor_, &QObject::deleteLater);

    // ---- 参数输入 ----
    connect(ui->lineEdit_exposure, &QLineEdit::editingFinished, this, [this]() {
        double val = ui->lineEdit_exposure->text().toDouble();
        QMetaObject::invokeMethod(worker_, "setExposureValue",
                                  Qt::QueuedConnection, Q_ARG(double, val));
    });
    connect(ui->lineEdit_gain, &QLineEdit::editingFinished, this, [this]() {
        double val = ui->lineEdit_gain->text().toDouble();
        QMetaObject::invokeMethod(worker_, "setGainValue",
                                  Qt::QueuedConnection, Q_ARG(double, val));
    });
    connect(ui->lineEdit_frame, &QLineEdit::editingFinished, this, [this]() {
        double val = ui->lineEdit_frame->text().toDouble();
        QMetaObject::invokeMethod(worker_, "setFrameValue",
                                  Qt::QueuedConnection, Q_ARG(double, val));
    });
}

void Camera::setSelectModuleEnabled(bool enabled) {
    ui->comboBox_camera_list->setEnabled(enabled);
}

void Camera::setInitModuleEnabled(bool enabled) {
    ui->open_btn->setEnabled(enabled);
    ui->close_btn->setEnabled(1 ^ enabled);
}

void Camera::setAcquireModuleEnabled(bool enabled) {
    ui->start_btn->setEnabled(enabled);
    ui->stop_btn->setEnabled(enabled);
}

void Camera::setSaveModuleEnabled(bool enabled) {
    ui->save_btn->setEnabled(enabled);
}

void Camera::setParamModuleEnabled(bool enabled) {
    ui->lineEdit_exposure->setEnabled(enabled);
    ui->lineEdit_gain->setEnabled(enabled);
    ui->lineEdit_frame->setEnabled(enabled);
}

void Camera::setDisplayModuleEnabled(bool enabled) {
    if (!enabled) {
        ui->label_display->setText("未找到摄像头设备\n请连接设备后点击“查找设备”");
    } else {
        ui->label_display->setText("请打开设备");
    }
}

void Camera::setAllDeviceUiEnabled(bool enabled) {
    setSelectModuleEnabled(enabled);
    setInitModuleEnabled(enabled);
    setAcquireModuleEnabled(enabled);
    setSaveModuleEnabled(enabled);
    setParamModuleEnabled(enabled);
    setDisplayModuleEnabled(enabled);
}