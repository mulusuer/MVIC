#include <qcoreapplication.h>

#include "cameraworker.h"
#include <QDebug>

CameraWorker::CameraWorker(QObject *parent) : QObject(parent), isRunning_(false) {}

CameraWorker::~CameraWorker() {
    closeCamera();
}

double CameraWorker::get_exposure_mode() {
    return cap_.get(cv::CAP_PROP_EXPOSURE);;
}

double CameraWorker::get_gain_mode() {
    return cap_.get(cv::CAP_PROP_GAIN);
}

double CameraWorker::get_frame_mode() {
    return cap_.get(cv::CAP_PROP_FPS);
}

void CameraWorker::openCamera(int index) {
    QMutexLocker locker(&mutex_);
    if (cap_.isOpened()) {
        cap_.release();
    }

    readHardwareParams();
    paramsReady(fps_, exposure_, gain_);

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
    if (cap_.isOpened()) {
        cap_.release();
    }

    pushDisplay();
}

void CameraWorker::setContinuousMode(bool isContinuous) {
    isContinuousMode_ = isContinuous;
}

void CameraWorker::stopAndCleanup() {
    stopAcquisition();
    closeCamera();
    QThread::currentThread()->quit();
}

void CameraWorker::softTriggerOnce() {
    QMutexLocker cacheLocker(&latestFrameMutex_);
    if (latestFrame_.empty()) {
        qDebug() << "软触发失败：暂无最新图像帧";
        return;
    }

    // 把刚才偷偷缓存的最新帧转码，发给界面
    cv::Mat rgbFrame;
    cv::cvtColor(latestFrame_, rgbFrame, cv::COLOR_BGR2RGB);
    QImage qimg(rgbFrame.data, rgbFrame.cols, rgbFrame.rows, rgbFrame.step, QImage::Format_RGB888);
    emit frameReady(qimg.copy());
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

        if (isContinuousMode_) {
            // 连续模式：转码并直接发送给界面显示
            cv::Mat rgbFrame;
            cv::cvtColor(frame, rgbFrame, cv::COLOR_BGR2RGB);
            QImage qimg(rgbFrame.data, rgbFrame.cols, rgbFrame.rows, rgbFrame.step, QImage::Format_RGB888);
            emit frameReady(qimg.copy());
        } else {
            // 触发模式：把最新帧存起来
            QMutexLocker cacheLocker(&latestFrameMutex_);
            latestFrame_ = frame.clone();
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
    if (cap_.isOpened()) {
        cap_.set(cv::CAP_PROP_EXPOSURE, val);
    }
}

void CameraWorker::setExposureValue(double val) {
    QMutexLocker locker(&mutex_);
    if (cap_.isOpened()) {
        bool success = cap_.set(cv::CAP_PROP_EXPOSURE, val);
        qDebug() << "设置曝光为:" << val << " 结果:" << success;
    }
}

void CameraWorker::setGainValue(double val) {
    QMutexLocker locker(&mutex_);
    if (cap_.isOpened()) {
        bool success = cap_.set(cv::CAP_PROP_GAIN, val);
        qDebug() << "设置增益为:" << val << " 结果:" << success;
    }
}

void CameraWorker::setFrameValue(double val) {
    QMutexLocker locker(&mutex_);
    if (cap_.isOpened()) {
        bool success = cap_.set(cv::CAP_PROP_FPS, val);
        qDebug() << "设置帧率为:" << val << " 结果:" << success;
    }
}

void CameraWorker::readHardwareParams() {
    fps_ = get_frame_mode();

    exposure_ = get_exposure_mode();

    gain_ = get_gain_mode();

    emit paramsReady(fps_, exposure_, gain_);
}
