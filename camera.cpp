#include "camera.h"

Camera::Camera(QWidget *parent) : QWidget(parent), ui(new Ui::Camera),
workerThread_(new QThread(this)), worker_(new CameraWorker()) {
    ui->setupUi(this);

    worker_->moveToThread(workerThread_);

    processorThread_ = new QThread(this);
    processor_ = new ImageProcessor();
    processor_->moveToThread(processorThread_);

    connect_init();

    workerThread_->start();
    processorThread_->start();

    // 后台预热 OCR 服务（加载模型要 4 秒左右），不阻塞界面
    QMetaObject::invokeMethod(processor_, &ImageProcessor::warmup,
                              Qt::QueuedConnection);

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
    if (!success) return;

    setInitModuleEnabled(0);
    setParamModuleEnabled(1);
    setSelectModuleEnabled(0);

    ui->start_btn->setEnabled(true);
    ui->stop_btn->setEnabled(false);
}

void Camera::handleError(const QString &msg) {
    QMessageBox::warning(this, "错误", msg);
}

void Camera::showImage(const QImage &img) {
    ui->label_display->setPixmap(
        QPixmap::fromImage(img).scaled(
            ui->label_display->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation));
}

// 摄像头原始画面
void Camera::updateFrameUI(const QImage &img) {
    if (processingActive_) return;
    showImage(img);
}

// 识别的结果画面
void Camera::updateProcessedFrame(const QImage &img) {
    if (!processingActive_) return;
    showImage(img);
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

    setSelectModuleEnabled(1);
    setDisplayModuleEnabled(1);
}

void Camera::on_find_btn_clicked() {
    findCamera();
}

void Camera::on_comboBox_camera_list_currentIndexChanged(const QString &arg) {
    if (cameras_.isEmpty()) return;
    setInitModuleEnabled(1);
}

void Camera::on_open_btn_clicked() {
    int index = ui->comboBox_camera_list->currentIndex();
    QMetaObject::invokeMethod(worker_, "openCamera",
                              Qt::QueuedConnection, Q_ARG(int, index));
}

void Camera::on_close_btn_clicked() {
    QMetaObject::invokeMethod(worker_, "closeCamera", Qt::QueuedConnection);

    processingActive_ = false;
    QMetaObject::invokeMethod(worker_, "setProcessingEnabled",
                              Qt::QueuedConnection, Q_ARG(bool, false));

    setInitModuleEnabled(1);
    setParamModuleEnabled(0);
    setAcquireModuleEnabled(0);
    setSelectModuleEnabled(1);
}

void Camera::on_start_btn_clicked() {
    processingActive_ = true;
    QMetaObject::invokeMethod(worker_, "setProcessingEnabled",
                              Qt::QueuedConnection, Q_ARG(bool, true));
    ui->label_result->clear();

    ui->start_btn->setEnabled(false);
    ui->stop_btn->setEnabled(true);
}

void Camera::on_stop_btn_clicked() {
    processingActive_ = false;
    QMetaObject::invokeMethod(worker_, "setProcessingEnabled",
                              Qt::QueuedConnection, Q_ARG(bool, false));

    ui->start_btn->setEnabled(true);
    ui->stop_btn->setEnabled(false);
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
    connect(worker_, &CameraWorker::paramsReady, this, &Camera::updateParamUI);
    connect(worker_, &CameraWorker::frameReady, this, &Camera::updateFrameUI);
    connect(worker_, &CameraWorker::errorOccurred, this, &Camera::handleError);
    connect(worker_, &CameraWorker::cameraOpened, this, &Camera::onCameraOpened);
    connect(worker_, &CameraWorker::pushDisplay, this, [this]() {
        ui->label_display->setText("请打开设备");
    });

    connect(workerThread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(processorThread_, &QThread::finished, processor_, &QObject::deleteLater);

    connect(worker_, &CameraWorker::frameForProcess,
            processor_, &ImageProcessor::processFrame);

    connect(processor_, &ImageProcessor::busyChanged,
            worker_, &CameraWorker::setProcBusy);
    connect(processor_, &ImageProcessor::processedFrame,
            this, &Camera::updateProcessedFrame);
    connect(processor_, &ImageProcessor::plateFound,
            this, &Camera::onPlateFound);
    connect(processor_, &ImageProcessor::statusMessage, this,
            [this](const QString &s) {
                qDebug() << "[proc]" << s;
                ui->label_result->setText(s);
            });

    connectParamEdit(ui->lineEdit_exposure, "setExposureValue", "曝光");
    connectParamEdit(ui->lineEdit_gain, "setGainValue", "增益");
    connectParamEdit(ui->lineEdit_frame, "setFrameValue", "帧率");
}

void Camera::connectParamEdit(QLineEdit *edit, const char *slot, const QString &label) {
    connect(edit, &QLineEdit::editingFinished, this, [this, edit, slot, label]() {
        bool ok = false;
        const double val = edit->text().toDouble(&ok);
        if (!ok) {
            ui->label_result->setText(label + "：请输入数字");
            return;
        }
        QMetaObject::invokeMethod(worker_, slot,
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
    setParamModuleEnabled(enabled);
    setDisplayModuleEnabled(enabled);
}