#include "CameraWorker.h"
#include <QDebug>
#include <QCoreApplication>

CameraWorker::CameraWorker(QObject *parent) : QObject(parent), isRunning_(false) {
    procTimer_.start();
}

CameraWorker::~CameraWorker() {
    closeCamera();
}

double CameraWorker::get_exposure_mode() {
    return cap_.get(cv::CAP_PROP_EXPOSURE);
}

double CameraWorker::get_gain_mode() {
    return cap_.get(cv::CAP_PROP_GAIN);
}

double CameraWorker::get_frame_mode() {
    return cap_.get(cv::CAP_PROP_FPS);
}

void CameraWorker::openCamera(int index) {
    QMutexLocker locker(&mutex_);
    if (cap_.isOpened()) cap_.release();

    readHardwareParams();
    emit paramsReady(fps_, exposure_, gain_);

    cap_.open(index, cv::CAP_DSHOW);
    if (!cap_.isOpened()) {
        emit errorOccurred("无法打开摄像头，请检查是否被占用！");
        emit cameraOpened(false);
        return;
    }
    emit cameraOpened(true);
}

void CameraWorker::closeCamera() {
    stopAcquisition();

    QMutexLocker locker(&mutex_);
    if (cap_.isOpened()) cap_.release();

    emit pushDisplay();
}

void CameraWorker::stopAndCleanup() {
    stopAcquisition();
    closeCamera();
    QThread::currentThread()->quit();
}

void CameraWorker::startAcquisition() {
    if (isRunning_.load()) return;
    isRunning_.store(true);

    while (isRunning_.load()) {
        cv::Mat frame;
        {
            QMutexLocker locker(&mutex_);
            if (!cap_.isOpened()) break;
            cap_ >> frame;
        }

        if (frame.empty()) {
            QThread::msleep(10);
            continue;
        }

        // BGR -> RGB，界面显示和识别都用这一份
        cv::Mat rgb;
        cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
        QImage qimg(rgb.data, rgb.cols, rgb.rows,
                    rgb.step, QImage::Format_RGB888);

        emit frameReady(qimg.copy());       // -> UI 显示

        // 送一帧去识别（限流 200ms，上一帧还没处理完就跳过）
        if (processEnabled_.load()
            && procBusy_.load() == 0
            && procTimer_.elapsed() >= 200) {
            procTimer_.restart();
            procBusy_.store(1);
            emit frameForProcess(qimg.copy());
        }

        QThread::msleep(10);
        QCoreApplication::processEvents();
    }
}

void CameraWorker::stopAcquisition() {
    isRunning_.store(false);
}

void CameraWorker::setExposure(double val) {
    QMutexLocker locker(&mutex_);
    if (cap_.isOpened()) cap_.set(cv::CAP_PROP_EXPOSURE, val);
}

void CameraWorker::setExposureValue(double val) {
    QMutexLocker locker(&mutex_);
    if (cap_.isOpened()) {
        bool ok = cap_.set(cv::CAP_PROP_EXPOSURE, val);
        qDebug() << "设置曝光为:" << val << " 结果:" << ok;
    }
}

void CameraWorker::setGainValue(double val) {
    QMutexLocker locker(&mutex_);
    if (cap_.isOpened()) {
        bool ok = cap_.set(cv::CAP_PROP_GAIN, val);
        qDebug() << "设置增益为:" << val << " 结果:" << ok;
    }
}

void CameraWorker::setFrameValue(double val) {
    QMutexLocker locker(&mutex_);
    if (cap_.isOpened()) {
        bool ok = cap_.set(cv::CAP_PROP_FPS, val);
        qDebug() << "设置帧率为:" << val << " 结果:" << ok;
    }
}

void CameraWorker::readHardwareParams() {
    fps_ = get_frame_mode();
    exposure_ = get_exposure_mode();
    gain_ = get_gain_mode();
    emit paramsReady(fps_, exposure_, gain_);
}

// 新增
void CameraWorker::setProcessingEnabled(bool on) {
    processEnabled_.store(on);
}

void CameraWorker::setProcBusy(bool busy) {
    procBusy_.store(busy);
}