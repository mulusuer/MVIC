#ifndef CAMERAWORKER_H
#define CAMERAWORKER_H

#include <QObject>
#include <QImage>
#include <QMutex>
#include <atomic>
#include <opencv2/opencv.hpp>
#include <QThread>

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
    void setContinuousMode(bool isContinuous);
    void stopAndCleanup();
    void softTriggerOnce();
    void startAcquisition();
    void stopAcquisition();
    void setExposure(double val);
    void setExposureValue(double val);
    void setGainValue(double val);
    void setFrameValue(double val);

    signals:
    void frameReady(const QImage &img);
    void errorOccurred(const QString &msg);
    void cameraOpened(bool success);
    void pushDisplay();
    void paramsReady(double fps, double exposure, double gain);


private:
    void readHardwareParams();
    cv::VideoCapture cap_;
    std::atomic<bool> isRunning_;
    QMutex mutex_;
    double fps_, exposure_, gain_;
    bool isContinuousMode_ = true;
    cv::Mat latestFrame_;
    QMutex latestFrameMutex_;

};

#endif // CAMERAWORKER_H