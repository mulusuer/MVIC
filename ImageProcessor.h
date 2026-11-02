#ifndef IMAGEPROCESSOR_H
#define IMAGEPROCESSOR_H

#include <QObject>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QList>
#include <QProcess>
#include <QDir>
#include <QFile>
#include <opencv2/opencv.hpp>

class ImageProcessor : public QObject {
    Q_OBJECT
public:
    explicit ImageProcessor(QObject *parent = nullptr);
    ~ImageProcessor() override;

public slots:
    void warmup();

    void processFrame(const QImage &img);

signals:
    // 结果刷新 label_display
    void processedFrame(const QImage &img);

    // 识别到车牌号
    void plateFound(const QString &text);

    // 忙/闲通知给 worker
    void busyChanged(bool busy);

    // 状态提示
    void statusMessage(const QString &msg);

private:
    struct Plate {
        QString  text;
        cv::Rect box;
    };

    static QImage matToQImage(const cv::Mat &bgr);

    // 拆解py返回的数据
    static QList<Plate> parseResult(const QString &line);

    // py返回结果
    QString recognize(const cv::Mat &bgr);

    // 常驻 Python 进程的管理
    bool    ensureOcrReady();
    QString ocrQuery(const QString &imagePath, int timeoutMs = 5000);

    QProcess *ocrProc_    = nullptr;
    QByteArray ocrBuf_;
    QString pythonExe_;
    QString pythonScript_;
    bool ocrReady_   = false;
    bool ocrFailed_  = false;

    QString lastPlate_;
};

#endif // IMAGEPROCESSOR_H
