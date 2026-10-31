#ifndef CAMERAWORKER_H
#define CAMERAWORKER_H

#include <QObject>
#include <QImage>
#include <QMutex>
#include <QThread>
#include <QElapsedTimer>
#include <atomic>
#include <opencv2/opencv.hpp>

class CameraWorker : public QObject {
    Q_OBJECT
public:
    explicit CameraWorker(QObject *parent = nullptr);
    ~CameraWorker() override;

    double get_exposure_mode();
    double get_gain_mode();
    double get_frame_mode();

public slots:
    void openCamera(int index);
    void closeCamera();
    void stopAndCleanup();
    void startAcquisition();
    void stopAcquisition();
    void setExposure(double val);
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

private:
    void readHardwareParams();

    cv::VideoCapture cap_;
    std::atomic<bool> isRunning_;
    QMutex mutex_;
    double fps_ = 0, exposure_ = 0, gain_ = 0;

    // 处理分发控制
    std::atomic<bool> processEnabled_{false};
    std::atomic<int>  procBusy_{0};
    QElapsedTimer     procTimer_;
};

#endif // CAMERAWORKER_H