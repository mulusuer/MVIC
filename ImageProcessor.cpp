#include "ImageProcessor.h"

#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QPainter>
#include <QTimer>
#include <QUuid>

// ImageProcessor：把画面交给 Python 服务识别，拿到结果画框写字
// ----------------------------------------------------------------

ImageProcessor::ImageProcessor(QObject *parent) : QObject(parent) {
    // Python 解释器：先用写死的路径，找不着就用环境变量里的 python
    pythonExe_ = "D:/Python/python.exe";
    if (!QFile::exists(pythonExe_)) pythonExe_ = "python";

    pythonScript_ = findScript();
}

// 在几个常见位置里找 plate_ocr.py
QString ImageProcessor::findScript() {
    const QString appDir = QCoreApplication::applicationDirPath();

    QStringList candidates;
    candidates << appDir + "/plate_ocr.py";                       // 和 exe 放一起
    candidates << appDir + "/../plate_ocr.py";                    // exe 在 build 子目录里
    candidates << QDir::currentPath() + "/plate_ocr.py";          // 当前工作目录
    candidates << "C:/Users/yyh/WorkBuddy/Worktrees/MVIC/main-28670981/plate_ocr.py";

    for (const QString &p : candidates) {
        if (QFile::exists(p)) return QDir::cleanPath(p);
    }
    return QDir::cleanPath(candidates.last());   // 都找不到，退回默认，启动时报错再说
}

ImageProcessor::~ImageProcessor() {
    if (!ocrProc_) return;

    ocrProc_->disconnect();
    if (ocrProc_->state() == QProcess::Running) {
        // 关掉输入，Python 那边 readline 读到结尾会自己退出
        ocrProc_->closeWriteChannel();
        if (!ocrProc_->waitForFinished(3000)) {
            ocrProc_->kill();
            ocrProc_->waitForFinished(1000);
        }
    }
    delete ocrProc_;
    ocrProc_ = nullptr;
}

// cv::Mat(BGR) -> QImage(RGB)
// .copy() 必须加：rgb 是局部变量，函数返回后内存就没了
QImage ImageProcessor::matToQImage(const cv::Mat &bgr) {
    if (bgr.empty()) return {};
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    return QImage(rgb.data, rgb.cols, rgb.rows,
                  static_cast<int>(rgb.step),
                  QImage::Format_RGB888).copy();
}

// 把 Python 回的一行结果拆成 "一块块车牌"
// 格式： 京A12345|10,20,200,60;粤B88888|300,40,480,90
QList<ImageProcessor::Plate> ImageProcessor::parseResult(const QString &line) {
    QList<Plate> plates;

    if (line.trimmed().isEmpty()) return plates;

    const QStringList parts = line.split(';', Qt::SkipEmptyParts);
    for (const QString &one : parts) {
        const int bar = one.indexOf('|');
        if (bar < 0) continue;                       // 没带坐标，跳过

        Plate p;
        p.text = one.left(bar).trimmed();
        if (p.text.isEmpty()) continue;

        const QStringList nums = one.mid(bar + 1).split(',');
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

void ImageProcessor::processFrame(const QImage &img) {
    if (img.isNull()) return;

    emit busyChanged(true);

    // 1) QImage 转成 OpenCV 的 BGR 图
    QImage rgb = img.convertToFormat(QImage::Format_RGB888);
    cv::Mat view(rgb.height(), rgb.width(), CV_8UC3,
                 rgb.bits(), static_cast<size_t>(rgb.bytesPerLine()));
    cv::Mat bgr;
    cv::cvtColor(view, bgr, cv::COLOR_RGB2BGR);

    // 2) 缩到最长边 1280：太大识别慢，太小看不清
    const int maxSide = 1280;
    const int big = std::max(bgr.cols, bgr.rows);
    if (big > maxSide) {
        const double s = double(maxSide) / double(big);
        cv::resize(bgr, bgr, cv::Size(), s, s, cv::INTER_AREA);
    }

    // 3) 交给 Python 识别，拿回一行结果
    const QString line = recognize(bgr);
    const QList<Plate> plates = parseResult(line);

    // 4) 画绿框
    cv::Mat marked = bgr.clone();
    for (const Plate &p : plates)
        cv::rectangle(marked, p.box, cv::Scalar(0, 255, 0), 2);

    QImage out = matToQImage(marked);

    // 5) 写车牌号。用 QPainter 而不是 cv::putText：
    //    OpenCV 画不了中文，省份字会变成问号
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

    // 6) 结果上报（和上次一样就不重复报，免得界面一直闪）
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

// 存成临时 jpg -> 问 Python -> 拿回一行
QString ImageProcessor::recognize(const cv::Mat &bgr) {
    if (bgr.empty()) return {};

    // 文件名用随机串，避免上一帧还没处理完就被覆盖
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

// 启动常驻 Python 服务（只在第一帧付一次加载模型的代价）
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

        // 第一次要加载模型，给 60 秒
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        connect(ocrProc_, &QIODevice::readyRead, &loop, &QEventLoop::quit);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(60000);
        loop.exec();
        timer.stop();

        const QString hello = QString::fromUtf8(ocrBuf_ + ocrProc_->readAllStandardOutput());
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

// 发一行图片路径给 Python，读回一行结果
QString ImageProcessor::ocrQuery(const QString &imagePath, int timeoutMs) {
    if (!ensureOcrReady()) return {};

    ocrBuf_.clear();

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    // 收到数据就追加到缓冲区，凑够一整行就结束等待
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

    // 顺手把 Python 的日志读走，不然管道会堵
    const QByteArray err = ocrProc_->readAllStandardError();
    if (!err.isEmpty()) qDebug() << "[python]" << err;

    const int idx = ocrBuf_.indexOf('\n');
    if (idx < 0) {              // 超时了，清空缓冲区免得污染下一次
        ocrBuf_.clear();
        qDebug() << "识别超时";
        return {};
    }

    const QString line = QString::fromUtf8(ocrBuf_.left(idx)).trimmed();
    ocrBuf_.remove(0, idx + 1);
    return line;
}
