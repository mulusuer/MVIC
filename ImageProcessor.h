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

// ImageProcessor：在独立线程里识别车牌
//
// 流程只有三步，没有自己写的定位算法：
//   1. 把画面缩到最长边 1280（太大识别慢，太小看不清）
//   2. 存成临时 jpg，把路径交给常驻的 Python 服务（plate_ocr.py）
//   3. Python 回一行 "车牌号|x1,y1,x2,y2"，照着画框、写字
//
// Python 那边模型只加载一次（约 3 秒，只在第一帧），之后每帧约 0.3 秒。
class ImageProcessor : public QObject {
    Q_OBJECT
public:
    explicit ImageProcessor(QObject *parent = nullptr);
    ~ImageProcessor() override;

    // Python 解释器和脚本路径，换机器时改这里
    void setPythonExe(const QString &exe)     { pythonExe_    = exe; }
    void setPythonScript(const QString &path) { pythonScript_ = path; }

public slots:
    // 处理一帧（跨线程会自动排队到处理线程执行）
    void processFrame(const QImage &img);

signals:
    // 画好框的图，主线程拿去刷新 label_display
    void processedFrame(const QImage &img);

    // 识别到车牌号（已经过正则校验，乱码不会出现在这里）
    void plateFound(const QString &text);

    // 忙/闲通知给 worker，避免帧堆积
    void busyChanged(bool busy);

    // 状态提示
    void statusMessage(const QString &msg);

private:
    // 一块车牌：号码 + 在画面里的位置
    struct Plate {
        QString  text;
        cv::Rect box;
    };

    static QImage matToQImage(const cv::Mat &bgr);

    // 把 Python 回的一行 "京A12345|10,20,200,60;粤B88888|..." 拆开
    static QList<Plate> parseResult(const QString &line);

    // 存临时图 -> 问 Python -> 拿回一行结果
    QString recognize(const cv::Mat &bgr);

    // 在几个常见位置里找 plate_ocr.py，找不到就退回默认路径
    static QString findScript();

    // 常驻 Python 进程的管理
    bool    ensureOcrReady();
    QString ocrQuery(const QString &imagePath, int timeoutMs = 5000);

    QProcess  *ocrProc_    = nullptr;
    QByteArray ocrBuf_;
    QString    pythonExe_;
    QString    pythonScript_;
    bool       ocrReady_   = false;
    bool       ocrFailed_  = false;

    QString lastPlate_;   // 上一次结果，一样的就不重复上报
};

#endif // IMAGEPROCESSOR_H
