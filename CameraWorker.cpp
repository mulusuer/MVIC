#include "CameraWorker.h"
#include <QDebug>

CameraWorker::CameraWorker(QObject *parent) : QObject(parent) {}

CameraWorker::~CameraWorker() {
    if (timer_) timer_->stop();
    QMutexLocker locker(&mutex_);
    if (cap_.isOpened()) cap_.release();
}

double CameraWorker::getExposureMode() {
    return cap_.get(cv::CAP_PROP_EXPOSURE);
}

double CameraWorker::getGainMode() {
    return cap_.get(cv::CAP_PROP_GAIN);
}

double CameraWorker::getFrameMode() {
    return cap_.get(cv::CAP_PROP_FPS);
}

void CameraWorker::openCamera(int index) {
    bool opened = false;
    {
        QMutexLocker locker(&mutex_);
        if (cap_.isOpened()) cap_.release();
        cap_.open(index, cv::CAP_DSHOW);
        opened = cap_.isOpened();
    }

    if (!opened) {
        emit errorOccurred("无法打开摄像头，请检查是否被占用！");
        emit cameraOpened(false);
        return;
    }

    {
        QMutexLocker locker(&mutex_);
        // 只调摄像头硬件参数，采集流程一个字不改。
        // DSHOW 默认用 YUY2 未压缩，高分辨率下往往只有 5~10fps；
        // 换成 MJPG 压缩格式，同一分辨率能跑到 30fps。
        cap_.set(cv::CAP_PROP_FOURCC,
                 cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
        if (cap_.get(cv::CAP_PROP_FPS) < 30)
            cap_.set(cv::CAP_PROP_FPS, 30);
        cap_.set(cv::CAP_PROP_BUFFERSIZE, 1);   // 只保留最新一帧，少一点延迟

        const double cur = cap_.get(cv::CAP_PROP_EXPOSURE);
        cap_.set(cv::CAP_PROP_AUTO_EXPOSURE, 0);      // 0 = 手动
        readHardwareParams();
    }

    emit cameraOpened(true);

    QMetaObject::invokeMethod(this, &CameraWorker::startAcquisition,
                              Qt::QueuedConnection);
}

void CameraWorker::closeCamera() {
    stopAcquisition();

    QMutexLocker locker(&mutex_);
    if (cap_.isOpened()) cap_.release();

    emit pushDisplay();
}

void CameraWorker::stopAndCleanup() {
    closeCamera();
    QThread::currentThread()->quit();
}

void CameraWorker::startAcquisition() {
    if (!timer_) {
        timer_ = new QTimer(this);
        timer_->setTimerType(Qt::PreciseTimer);   // 10ms 定时用粗糙时钟会抖
        connect(timer_, &QTimer::timeout, this, &CameraWorker::grabFrame);
    }
    timer_->start(10);
}

void CameraWorker::stopAcquisition() {
    if (timer_) timer_->stop();
}

void CameraWorker::grabFrame() {
    cv::Mat frame;
    {
        QMutexLocker locker(&mutex_);
        if (!cap_.isOpened()) return;
        cap_ >> frame;
    }

    if (frame.empty()) return;

    cv::Mat rgb;
    cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
    QImage qimg(rgb.data, rgb.cols, rgb.rows,
                rgb.step, QImage::Format_RGB888);

    emit frameReady(qimg.copy());

    if (processEnabled_.load() && procBusy_.load() == 0) {
        procBusy_.store(1);
        emit frameForProcess(qimg.copy());
    }
}

void CameraWorker::setExposureValue(double val) {
    applyParam(cv::CAP_PROP_EXPOSURE, val, "曝光");
}

void CameraWorker::setGainValue(double val) {
    applyParam(cv::CAP_PROP_GAIN, val, "增益");
}

void CameraWorker::setFrameValue(double val) {
    applyParam(cv::CAP_PROP_FPS, val, "帧率");
}

void CameraWorker::applyParam(cv::VideoCaptureProperties prop,
                              double val, const char *name) {
    QMutexLocker locker(&mutex_);
    if (!cap_.isOpened()) {
        qDebug() << "摄像头未打开，设置" << name << "失败";
        return;
    }

    const bool ok = cap_.set(prop, val);
    qDebug() << "设置" << name << "为:" << val << "结果:" << ok;

    readHardwareParams();
}

void CameraWorker::readHardwareParams() {
    fps_ = getFrameMode();
    exposure_ = getExposureMode();
    gain_ = getGainMode();
    emit paramsReady(fps_, exposure_, gain_);
}

void CameraWorker::setProcessingEnabled(bool on) {
    processEnabled_.store(on);
}

void CameraWorker::setProcBusy(bool busy) {
    procBusy_.store(busy);
}