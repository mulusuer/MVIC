# MVIC · 车牌识别桌面程序

一个 Windows 桌面小工具：用摄像头或本地图片识别车牌，把结果画到画面上，再去 SQLite 里查车主。

技术栈：**Qt 5.15.2 + OpenCV 4.11（C++ 界面与图像处理）+ PaddleOCR 3.7.0（Python 识别）+ SQLite**。

---

## 一、能做什么

界面左侧三个页面：

| 页面 | 用途 |
| --- | --- |
| 实时识别 | 查找设备 → 打开设备 → 开始识别。摄像头画面实时显示，识别到车牌时画绿框 + 黄色车牌号，并查询车主 |
| 图片识别 | 选择一张本地图片 → 开始识别，显示标注后的图片和车牌号 |
| 车牌管理 | 按车牌号 / 姓名查询、添加、删除登记记录，表格展示 |

实时页，图片页能手动设置曝光、增益、帧率（实时页需先打开摄像头）。

---

## 二、识别流程

```
摄像头 / 图片
   ↓  QImage → cv::Mat，最长边缩到 1280
存一张临时 jpg，把路径写进常驻 Python 进程的 stdin
   ↓
plate_ocr.py：PaddleOCR 找文字 → 正则挑出车牌号 → 整图找不到就切 2×2 重叠块再找
   ↓  回一行 "车牌号|x1,y1,x2,y2"
C++ 解析坐标，画绿框 + 黄色车牌号（QPainter，能画中文）
   ↓
拿车牌号去 carData.db 查车主姓名
```

Python 进程是**常驻**的：程序启动时后台预热（PaddleOCR 加载模型约 4 秒），之后每帧只发一个路径、收一行结果，握手开销只付一次。

三个模块各跑在独立 `QThread`：采集（`CameraWorker`）、识别（`ImageProcessor`）、数据库（`Sql`）。

---

## 三、环境要求

开发机实际用的版本（换机时按自己情况调整）：

| 依赖 | 版本 / 路径 |
| --- | --- |
| 系统 | Windows |
| Qt | 5.15.2 mingw81_64， |
| 编译器 | MinGW 13.1.0|
| CMake | ≥ 4.0，生成器 |
| OpenCV | 4.11 MinGW 版|
| Python | 3.11 |
| Python 包 | `paddleocr 3.7.0`、`paddlepaddle 3.1.0、`opencv-python 4.10` |
| 硬件 | 一个支持 DirectShow 的摄像头 |

Python 侧依赖：

```bash
pip install paddleocr==3.7.0 paddlepaddle opencv-python
```

首次运行 PaddleOCR 会下载模型文件，**需要联网**，第一次启动会慢一些。

---

## 四、编译和运行

下面的命令是 **Git Bash** 写法。用 CMD / PowerShell 的话，把 `/d/...` 换成 `D:\...` 风格并自行设置 PATH。

```bash
cd <项目根目录>

# 首次配置（已有 cmake-build-debug 目录可跳过）
PATH="/d/Qt/Tools/mingw1310_64/bin:/d/Qt/5.15.2/mingw81_64/bin:$PATH" \
  cmake -S . -B cmake-build-debug -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug

# 编译
PATH="/d/Qt/Tools/mingw1310_64/bin:/d/Qt/5.15.2/mingw81_64/bin:$PATH" \
  cmake --build cmake-build-debug -j4
```

编译产物 `cmake-build-debug/MVIC.exe`，直接运行即可。Qt 的 DLL 会在编译后自动拷到 exe 同目录。

也可以单独调试 Python 识别服务：

```bash
python plate_ocr.py 某张图片.jpg
# 输出：粤B12345|120,80,260,130
# 不带参数运行则进入常驻服务模式，先打印 READY，之后每行读一个路径、回一行结果
```

---

## 五、换一台电脑必须改的地方

项目里有几处**绝对路径**，需要修改：

| 文件                   | 行   | 内容                                                  |
| -------------------- | --- | --------------------------------------------------- |
| `ImageProcessor.cpp` | 10  | `pythonExe_` → 本机 python.exe 路径                     |
| `ImageProcessor.cpp` | 11  | `pythonScript_` → 本机 `plate_ocr.py` 绝对路径            |
| `CMakeLists.txt`     | 9   | `CMAKE_PREFIX_PATH` → 本机 Qt 路径                      |
| `CMakeLists.txt`     | 19  | `find_package(OpenCV ... PATHS ...)` → 本机 OpenCV 路径 |

---

## 六、数据库

用的是 Qt 自带的 **QSQLITE**，不需要装 MySQL。文件 `carData.db` 会在**程序当前工作目录**自动创建，启动时自动建表：

```sql
CREATE TABLE IF NOT EXISTS test (
    id    INTEGER PRIMARY KEY AUTOINCREMENT,
    plate TEXT NOT NULL UNIQUE,
    name  TEXT NOT NULL
);
```

`plate` 有唯一约束，添加重复车牌会失败并弹窗。想清空数据，直接删掉 `carData.db` 再运行程序。

---

## 七、Python 通信协议

`plate_ocr.py` 的 stdout **只用于和 C++ 通信**，PaddleOCR 的日志全部被重定向到 stderr

- 启动后先输出一行 `READY`
- 之后每行读入一个图片路径
- 回一行：`车牌号|x1,y1,x2,y2`
- 多块车牌用 `;` 分隔，例如 `京A12345|10,20,200,60;粤B88888|300,20,440,60`
- 没识别到就回一个空行

C++ 侧 5 秒收不到结果就按超时处理，跳过这一帧。

---

## 八、文件说明

| 文件                                      | 作用                              |
| --------------------------------------- | ------------------------------- |
| `main.cpp`                              | 程序入口                            |
| `camera.ui` / `camera.h` / `camera.cpp` | 主窗口：界面、三个页面、所有信号槽连接             |
| `CameraWorker.h` / `.cpp`               | 摄像头采集线程                         |
| `ImageProcessor.h` / `.cpp`             | 图像缩放、画框、管理常驻 Python 进程、解析识别结果   |
| `Sql.h` / `.cpp`                        | SQLite 建表、查询、校验车牌是否在库           |
| `plate_ocr.py`                          | PaddleOCR 识别服务，stdin/stdout 行协议 |
| `CMakeLists.txt`                        | 构建配置                            |
| `.gitignore`                            | 忽略构建产物、Qt 自动生成文件、运行时产物          |
|                                         |                                 |

