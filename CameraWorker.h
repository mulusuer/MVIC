#ifndef CAMERAWORKER_H
#define CAMERAWORKER_H

#include <QObject>
#include <QImage>
#include <QMutex>
#include <QTimer>
#include <QThread>
#include <atomic>
#include <opencv2/opencv.hpp>

class CameraWorker : public QObject {
    Q_OBJECT
public:
    explicit CameraWorker(QObject *parent = nullptr);
    ~CameraWorker() override;

public slots:
    void openCamera(int index);
    void closeCamera();
    void stopAndCleanup();
    void startAcquisition();
    void stopAcquisition();
    void setExposureValue(double val);
    void setGainValue(double val);
    void setFrameValue(double val);

    // 处理开关、忙/闲回调
    void setProcessingEnabled(bool on);
    void setProcBusy(bool busy);

    signals:
    void frameReady(const QImage &img);
    void errorOccurred(const QString &msg);
    void cameraOpened(bool success);
    void pushDisplay();
    void paramsReady(double fps, double exposure, double gain);

    // 给 ImageProcessor 的帧
    void frameForProcess(const QImage &img);

private slots:
    // 定时器到点取一帧：刷新预览，识别开着就顺便送一帧去识别
    void grabFrame();

private:
    void readHardwareParams();

    void applyParam(cv::VideoCaptureProperties prop, double val, const char *name);

    double getFrameMode();
    double getExposureMode();
    double getGainMode();

    cv::VideoCapture cap_;
    QMutex mutex_;
    double fps_ = 0, exposure_ = 0, gain_ = 0;

    QTimer *timer_ = nullptr;
    std::atomic<bool> processEnabled_{false};
    std::atomic<bool> procBusy_{false};
};

#endif // CAMERAWORKER_H