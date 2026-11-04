#include "camera.h"

#include "Sql.h"

Camera::Camera(QWidget *parent) : QWidget(parent), ui(new Ui::Camera),
                                  workerThread_(new QThread(this)), worker_(new CameraWorker()) {
    ui->setupUi(this);

    worker_->moveToThread(workerThread_);

    processorThread_ = new QThread(this);
    processor_ = new ImageProcessor();
    data_ = new Sql();
    dataThread_ = new QThread(this);
    data_->moveToThread(dataThread_);
    processor_->moveToThread(processorThread_);

    connect_init();

    workerThread_->start();
    processorThread_->start();
    dataThread_->start();

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
    if (dataThread_ && dataThread_->isRunning()) {
        dataThread_->quit();
        dataThread_->wait();
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
    const QSize target = ui->label_display->size();
    // 原来先把整张原图转成 QPixmap、再做全尺寸平滑缩放，两步都在 UI 线程，
    // 一帧几十毫秒，30fps 根本跟不上，事件队列越堆越多就越卡。
    // 改成先把 QImage 快速缩到显示区大小，再转 QPixmap：
    // 像素量只剩原来的几分之一，UI 线程开销大幅下降。
    // 放大时才用平滑缩放（小图放大代价低），保持观感。
    const Qt::TransformationMode mode =
        (img.width() > target.width() || img.height() > target.height())
        ? Qt::FastTransformation : Qt::SmoothTransformation;

    const QImage small = img.scaled(target, Qt::KeepAspectRatio, mode);
    ui->label_display->setPixmap(QPixmap::fromImage(small));
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
    QString sql = "SELECT plate, name FROM test WHERE plate = " + text;
    emit check(sql, 0);
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

void Camera::on_PageChanged(int index) {
    if (index == 0) {
        QMetaObject::invokeMethod(worker_, "startAcquisition", Qt::QueuedConnection);
        processingActive_ = 0;
    }
    else if (index == 1) {
        QMetaObject::invokeMethod(worker_, "stopAcquisition", Qt::QueuedConnection);
        QMetaObject::invokeMethod(worker_, "setProcessingEnabled", Qt::QueuedConnection,Q_ARG(bool, 0));
        processingActive_ = 0;
        QMetaObject::invokeMethod(worker_, "setProcBusy", Qt::QueuedConnection, Q_ARG(bool, 0));
        if (!ui->open_btn->isEnabled()) {
            ui->start_btn->setEnabled(1);
        }
        ui->stop_btn->setEnabled(0);
    } else {
        QMetaObject::invokeMethod(worker_, "stopAcquisition", Qt::QueuedConnection);
        QMetaObject::invokeMethod(worker_, "setProcessingEnabled", Qt::QueuedConnection,Q_ARG(bool, 0));
        processingActive_ = 0;
        QMetaObject::invokeMethod(worker_, "setProcBusy", Qt::QueuedConnection, Q_ARG(bool, 0));
        if (!ui->open_btn->isEnabled()) {
            ui->start_btn->setEnabled(1);
        }
        ui->stop_btn->setEnabled(0);
    }
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

void Camera::updateParamUI_image(double fps, double exposure, double gain) {
    ui->lineEdit_frame_image->setText(QString::number(fps, 'f', 2));
    ui->lineEdit_exposure_image->setText(QString::number(exposure, 'f', 2));
    ui->lineEdit_gain_image->setText(QString::number(gain, 'f', 2));
}

void Camera::on_open_btn_image_clicked() {
    // 打开本地文件对话框
    imgPath = QFileDialog::getOpenFileName(this, tr("选择图片"), QDir::homePath() + "/Pictures", tr("图片文件 (*.png *.jpg *.jpeg *.bmp);;所有文件 (*)"));

    // 用户没点取消，拿到路径
    if (imgPath.isEmpty()) {
        return ;
    }
    QPixmap pix(imgPath);
    ui->label_display_image->setPixmap(pix.scaled(ui->label_display_image->size(),
                                                 Qt::KeepAspectRatio,
                                                 Qt::SmoothTransformation));

    ui->lineEdit_exposure_image->setEnabled(1);
    ui->lineEdit_gain_image->setEnabled(1);
    ui->lineEdit_frame_image->setEnabled(1);
    ui->start_btn_image->setEnabled(1);
}


void Camera::on_start_btn_image_clicked() {
    emit send_image(imgPath);
}

void Camera::on_btn_find_car_clicked() {
    QString plate = ui->lineEdit_find_car->text();
    QString name = ui->lineEdit_find_name->text();
    if (!plate.isEmpty() && !name.isEmpty()) {
        QString sql = "SELECT plate, name FROM test WHERE plate = " + plate + " AND name = " + name;
        send_sql(sql);
    } else if (!plate.isEmpty()) {
        QString sql = "SELECT plate, name FROM test WHERE plate = " + plate;
        send_sql(sql);
    } else if (!name.isEmpty()) {
        QString sql = "SELECT plate, name FROM test WHERE name = " + name;
        send_sql(sql);
    } else {
        QString sql = "SELECT plate, name FROM test";
        send_sql(sql);
    }
}

void Camera::on_btn_refresh_plate_clicked() {
    QString sql = "SELECT plate, name FROM test";
    send_sql(sql);
}

void Camera::on_btn_add_plate_clicked() {
    QString plate = ui->lineEdit_change_car->text();
    QString name = ui->lineEdit_change_name->text();
    if (plate.isEmpty() || name.isEmpty()) {
        QMessageBox::information(this, "提示", "车牌号或者姓名未填写");
        return ;
    }
    QString sql = "INSERT INTO test (plate, name) VALUES (" + plate + ", " + name + ")";
    emit send_sql(sql);
    on_btn_refresh_plate_clicked();
}

void Camera::on_btn_delete_plate_clicked() {
    QString plate = ui->lineEdit_change_car->text();
    QString name = ui->lineEdit_change_name->text();
    if (!plate.isEmpty()) {
        QString sql = "DELETE FROM test WHERE plate = " + plate;
        send_sql(sql);
    } else if (!name.isEmpty()) {
        QString sql = "DELETE FROM test WHERE name = " + name;
        send_sql(sql);
    } else {
        QMessageBox::information(this, "提示", "车牌号或者姓名未填写");
    }
    on_btn_refresh_plate_clicked();
}

void Camera::data_show(const QList<QVariantMap> &data) {
    ui->tableWidget->setColumnCount(2);
    ui->tableWidget->setHorizontalHeaderLabels(
        QStringList()<< "车牌号" << "姓名");
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidget->setRowCount(data.size());
    for (int i = 0; i < data.size(); ++i) {
        ui->tableWidget->setItem(i, 0, new QTableWidgetItem(data[i]["plate"].toString()));
        ui->tableWidget->setItem(i, 1, new QTableWidgetItem(data[i]["name"].toString()));
    }
}

void Camera::data_init() {
    // 实时
    findCamera();

    // 图片
    ui->start_btn_image->setEnabled(0);
    ui->lineEdit_exposure_image->setEnabled(0);
    ui->lineEdit_gain_image->setEnabled(0);
    ui->lineEdit_frame_image->setEnabled(0);

    // 管理
    emit send_sql("SELECT plate, name FROM test");
}

void Camera::connect_init() {
    connect(ui->stackedWidget, &QStackedWidget::currentChanged,
            this, &Camera::on_PageChanged);

    connect(worker_, &CameraWorker::paramsReady, this, &Camera::updateParamUI);
    connect(worker_, &CameraWorker::frameReady, this, &Camera::updateFrameUI);
    connect(worker_, &CameraWorker::errorOccurred, this, &Camera::handleError);
    connect(worker_, &CameraWorker::cameraOpened, this, &Camera::onCameraOpened);
    connect(worker_, &CameraWorker::pushDisplay, this, [this]() {
        ui->label_display->setText("请打开设备");
    });

    connect(this, &Camera::check, data_, &Sql::check);
    connect(data_, &Sql::send_is_data, this,[this](const QString& str) {
        ui->label_result_name->setText(str);
    });


    // 管理
    connect(data_, &Sql::sendData, this, &Camera::data_show);
    connect(this, &Camera::send_sql, data_, &Sql::query);
    // 图片
    connect(this, &Camera::send_image, processor_, &ImageProcessor::processImage);
    connect(processor_, &ImageProcessor::processedImage, this, [this](const QImage& img) {
        ui->label_display_image->setPixmap(
        QPixmap::fromImage(img).scaled(
            ui->label_display_image->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation));

    });
    connect(processor_, &ImageProcessor::plateFound_image, this,
        [this](const QString &s) {
            ui->label_result_image->setText(s);
            QString sql = "SELECT plate, name FROM test WHERE plate = " + s;
            emit check(sql, 1);
    });
    connect(data_, &Sql::send_is_data_image, this, [this](const QString& str) {
        ui->label_result_image_name->setText(str);
    });

    connect(workerThread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(processorThread_, &QThread::finished, processor_, &QObject::deleteLater);
    connect(dataThread_, &QThread::finished, data_, &QObject::deleteLater);

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
                ui->label_result_image_py->setText(s);
                ui->label_frame_py->setText(s);
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