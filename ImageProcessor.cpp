#include "ImageProcessor.h"
#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QPainter>
#include <QTimer>
#include <QUuid>

ImageProcessor::ImageProcessor(QObject *parent) : QObject(parent) {
    pythonExe_    = "D:/Python/python.exe";
    pythonScript_ = "C:/Users/yyh/WorkBuddy/Worktrees/MVIC/main-28670981/plate_ocr.py";
}

ImageProcessor::~ImageProcessor() {
    if (!ocrProc_) return;

    ocrProc_->disconnect();
    if (ocrProc_->state() == QProcess::Running) {
        ocrProc_->closeWriteChannel();
        if (!ocrProc_->waitForFinished(3000)) {
            ocrProc_->kill();
            ocrProc_->waitForFinished(1000);
        }
    }
    delete ocrProc_;
    ocrProc_ = nullptr;
}

QImage ImageProcessor::matToQImage(const cv::Mat &bgr) {
    if (bgr.empty()) return {};
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    return QImage(rgb.data, rgb.cols, rgb.rows,
                  static_cast<int>(rgb.step),
                  QImage::Format_RGB888).copy();
}

// Python 返回结果
// 格式： 京A12345|10,20,200,60
QList<ImageProcessor::Plate> ImageProcessor::parseResult(const QString &line) {
    QList<Plate> plates;

    if (line.trimmed().isEmpty()) return plates;

    const QStringList parts = line.split(';', Qt::SkipEmptyParts);
    for (const QString &i : parts) {
        const int bar = i.indexOf('|');
        if (bar < 0) continue;

        Plate p;
        p.text = i.left(bar).trimmed();
        if (p.text.isEmpty()) continue;

        const QStringList nums = i.mid(bar + 1).split(',');
        if (nums.size() != 4) continue;

        bool ok1 = false, ok2 = false, ok3 = false, ok4 = false;
        const int x1 = nums[0].toInt(&ok1);
        const int y1 = nums[1].toInt(&ok2);
        const int x2 = nums[2].toInt(&ok3);
        const int y2 = nums[3].toInt(&ok4);
        if (!(ok1 && ok2 && ok3 && ok4)) continue;

        p.box = cv::Rect(x1, y1, x2 - x1, y2 - y1);
        plates.append(p);
    }

    return plates;
}

void ImageProcessor::warmup() {
    ensureOcrReady();
}

void ImageProcessor::processFrame(const QImage &img) {
    if (img.isNull()) return;

    emit busyChanged(true);

    QImage rgb = img.convertToFormat(QImage::Format_RGB888);
    cv::Mat view(rgb.height(), rgb.width(), CV_8UC3,
                 rgb.bits(), static_cast<size_t>(rgb.bytesPerLine()));
    cv::Mat bgr;
    cv::cvtColor(view, bgr, cv::COLOR_RGB2BGR);

    const int maxSide = 1280;
    const int big = std::max(bgr.cols, bgr.rows);
    if (big > maxSide) {
        const double s = double(maxSide) / double(big);
        cv::resize(bgr, bgr, cv::Size(), s, s, cv::INTER_AREA);
    }

    const QString line = recognize(bgr);
    const QList<Plate> plates = parseResult(line);


    cv::Mat marked = bgr.clone();
    for (const Plate &p : plates)
        cv::rectangle(marked, p.box, cv::Scalar(0, 255, 0), 2);

    QImage out = matToQImage(marked);

    // 车牌号
    if (!plates.isEmpty()) {
        QPainter painter(&out);
        QFont font;
        font.setPointSize(14);
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(QPen(Qt::yellow, 2));

        for (const Plate &p : plates) {
            int textY = p.box.y - 6;
            if (textY < 18) textY = p.box.y + p.box.height + 22;  // 太靠上就画到框下面
            painter.drawText(p.box.x, textY, p.text);
        }
        painter.end();
    }

    if (!plates.isEmpty()) {
        QStringList names;
        for (const Plate &p : plates) names << p.text;
        const QString joined = names.join(", ");
        if (joined != lastPlate_) {
            lastPlate_ = joined;
            emit plateFound(joined);
        }
    }

    emit processedFrame(out);
    emit busyChanged(false);
}

QString ImageProcessor::recognize(const cv::Mat &bgr) {
    if (bgr.empty()) return {};

    const QString path = QDir::temp().filePath(
        QString("plate_%1.jpg").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));

    if (!cv::imwrite(path.toStdString(), bgr)) {
        qDebug() << "临时图片保存失败:" << path;
        return {};
    }

    const QString result = ocrQuery(path, 5000);
    QFile::remove(path);

    if (!result.isEmpty())
        qDebug() << "【识别结果】" << result;

    return result;
}

// 启动常驻 Python 服务
bool ImageProcessor::ensureOcrReady() {
    if (ocrReady_)  return true;
    if (ocrFailed_) return false;

    if (!ocrProc_) {
        ocrProc_ = new QProcess(this);
        ocrProc_->setProcessChannelMode(QProcess::SeparateChannels);
    }

    if (ocrProc_->state() == QProcess::NotRunning) {
        ocrProc_->start(pythonExe_, QStringList() << pythonScript_);
        if (!ocrProc_->waitForStarted(10000)) {
            ocrFailed_ = true;
            emit statusMessage(QString("启动不了 Python（%1）：%2")
                                   .arg(pythonExe_, ocrProc_->errorString()));
            return false;
        }

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        connect(ocrProc_, &QIODevice::readyRead, &loop, &QEventLoop::quit);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(60000);
        loop.exec();
        timer.stop();

        const QString hello = QString::fromUtf8(ocrProc_->readAllStandardOutput());
        ocrBuf_.clear();

        if (!hello.contains("READY")) {
            ocrFailed_ = true;
            emit statusMessage(QString("Python 服务启动超时：%1")
                                   .arg(QString::fromUtf8(ocrProc_->readAllStandardError())));
            return false;
        }

        ocrReady_ = true;
        emit statusMessage(QString("识别服务已就绪（%1）").arg(pythonScript_));
    }

    return ocrReady_;
}

// 发给 Python 读回结果
QString ImageProcessor::ocrQuery(const QString &imagePath, int timeoutMs) {
    if (!ensureOcrReady()) return {};

    ocrBuf_.clear();

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    auto conn = connect(ocrProc_, &QIODevice::readyRead, &loop, [&]() {
        ocrBuf_ += ocrProc_->readAllStandardOutput();
        if (ocrBuf_.contains('\n')) loop.quit();
    });
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    ocrProc_->write((imagePath + "\n").toUtf8());
    timer.start(timeoutMs);
    loop.exec();
    timer.stop();
    disconnect(conn);

    const QString errText = QString::fromUtf8(ocrProc_->readAllStandardError());
    for (const QString &raw : errText.split('\n')) {
        const QString s = raw.trimmed();
        if (s.isEmpty())                         continue;
        if (s.contains("INFO:"))                 continue;
        if (s.contains("Creating model:"))       continue;
        if (s.contains("Model files already exist")) continue;
        qDebug() << "[python]" << s;
    }

    const int idx = ocrBuf_.indexOf('\n');
    if (idx < 0) {
        ocrBuf_.clear();
        qDebug() << "识别超时";
        return {};
    }

    const QString line = QString::fromUtf8(ocrBuf_.left(idx)).trimmed();
    ocrBuf_.remove(0, idx + 1);
    return line;
}
