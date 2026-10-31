"""
车牌识别服务（PaddleOCR）

用法一，常驻服务（C++ 用这个，模型只加载一次，快）：
    python plate_ocr.py
    启动后先输出一行 READY，之后每行读一个图片路径，
    输出一行结果，格式：  车牌号|x1,y1,x2,y2
    多块车牌用分号隔开，没识别到就输出空行。

用法二，单次识别（调试用）：
    python plate_ocr.py 图片路径

做法很简单：
    1. 整张图直接交给 PaddleOCR 找文字
    2. 从识别出的所有文字里，用正则挑出长得像车牌的那个
    3. 整张找不到，就把图切成 2x2 四块分别再找一次（小车牌靠这招救回来）

注意：stdout 只用于和 C++ 通信，PaddleOCR 的刷屏日志全部被赶到 stderr。
"""

import os
import re
import sys

os.environ["GLOG_minloglevel"] = "3"
os.environ["FLAGS_call_stack_level"] = "3"

import warnings

warnings.filterwarnings("ignore")

_STDOUT_FD = 1
_STDERR_FD = 2


class _StdoutGuard:
    """顶替 sys.stdout，把第三方库的 print 全部写到 stderr。"""

    def write(self, s):
        if s:
            try:
                os.write(_STDERR_FD, s.encode("utf-8", "replace"))
            except OSError:
                pass
        return len(s)

    def flush(self):
        pass

    def isatty(self):
        return False

    def fileno(self):
        return _STDOUT_FD


sys.stdout = _StdoutGuard()


def _emit(line):
    """给 C++ 回一行结果，直接写 fd 1，绕开上面那个守卫。"""
    os.write(_STDOUT_FD, (line + "\n").encode("utf-8"))


# ---------------------------------------------------------------- 正则部分

# 全国省份简称
PROVINCES = "京津沪渝冀豫云辽黑湘皖鲁新苏浙赣鄂桂甘晋蒙陕吉闽贵粤青藏川宁琼使领"
# 车牌里不会出现字母 I 和 O
PLATE_CHARS = "ABCDEFGHJKLMNPQRSTUVWXYZ0123456789"

# 省份简称 + 一位发牌字母 + 4~6 位编号
PLATE_RE = re.compile("([" + PROVINCES + "])[" + PLATE_CHARS + "]([" + PLATE_CHARS + "]{4,6})")


def normalize(text):
    """去掉 OCR 爱加的分隔符，并把容易认错的 O/I 纠回 0/1。"""
    if not text:
        return ""
    s = re.sub(r"[\s·.、,，\-—_:：|]", "", str(text))
    return s.replace("O", "0").replace("I", "1").upper()


def pick_plate(texts):
    """给一堆 OCR 文字，挑出最像车牌的那个；挑不出就返回空串。

    检测器有时会把车牌切成好几块（"粤" "B" "88888"），
    所以先试整体拼接，再逐个试。
    """
    raw = [str(t) for t in (texts or [])]
    candidates = ["".join(normalize(t) for t in raw)]
    candidates += [normalize(t) for t in raw]

    best = ""
    for s in candidates:
        if len(s) < 4:
            continue
        for m in PLATE_RE.finditer(s):
            cand = m.group(0)
            if len(cand) == 7:                       # 普通车牌 7 位
                if len(cand) > len(best):
                    best = cand
            elif len(cand) == 8 and (cand[2] in "DF" or cand[-1] in "DF"):
                best = cand                          # 新能源车牌 8 位
    return best


# ---------------------------------------------------------------- 识别部分

def _poly_box(poly):
    """文字块是个四边形，取它的外接矩形 (x1, y1, x2, y2)。"""
    xs = [float(p[0]) for p in poly]
    ys = [float(p[1]) for p in poly]
    return int(min(xs)), int(min(ys)), int(max(xs)), int(max(ys))


class PlateEngine:
    def __init__(self):
        from paddleocr import PaddleOCR

        # 这三个必须关掉：文档方向矫正会把图片旋转后再检测，
        # 返回的坐标是旋转后的坐标系，画到原图上框就是歪的。
        # 关掉还能少加载 3 个模型，启动快一半。
        self.ocr = PaddleOCR(
            lang="ch",
            use_doc_orientation_classify=False,
            use_doc_unwarping=False,
            use_textline_orientation=False,
        )

    def find(self, img):
        """在一张图上找车牌，返回 [(车牌号, 矩形框), ...]。"""
        try:
            result = self.ocr.predict(img)
        except Exception as exc:
            self._log("predict failed: %r" % (exc,))
            return []

        if not result or not result[0]:
            return []

        page = result[0]
        texts = list(page.get("rec_texts") or [])
        if not texts:
            return []

        # 用 dt_polys（检测阶段的框，坐标基于原图），
        # 不要用 rec_polys（识别阶段的框，坐标系可能被内部预处理改过）
        polys = page.get("dt_polys")
        if polys is None:
            polys = page.get("rec_polys")
        has_box = polys is not None and len(polys) == len(texts)

        # 情况一：某一块文字本身就是完整车牌（最常见）
        found = []
        for i, t in enumerate(texts):
            plate = pick_plate([t])
            if plate:
                box = _poly_box(polys[i]) if has_box else None
                found.append((plate, box))

        if found:
            return found

        # 情况二：车牌被切成好几块，按从左到右拼起来再认
        if has_box:
            order = sorted(range(len(texts)), key=lambda i: _poly_box(polys[i])[0])
        else:
            order = list(range(len(texts)))

        plate = pick_plate([texts[i] for i in order])
        if plate and has_box:
            boxes = [_poly_box(polys[i]) for i in order]
            merged = (min(b[0] for b in boxes), min(b[1] for b in boxes),
                      max(b[2] for b in boxes), max(b[3] for b in boxes))
            return [(plate, merged)]
        if plate:
            return [(plate, None)]

        return []

    def find_with_tiles(self, img):
        """整张找一遍；找不到就切成 2x2 四块各找一遍。"""
        found = self.find(img)
        if found:
            return self._dedup(found)

        h, w = img.shape[:2]
        tw, th = w // 2, h // 2
        ovx, ovy = int(tw * 0.15), int(th * 0.15)   # 块之间留点重叠，免得车牌被切开

        for r in range(2):
            for c in range(2):
                x0 = max(0, c * tw - ovx)
                x1 = min(w, (c + 1) * tw + ovx)
                y0 = max(0, r * th - ovy)
                y1 = min(h, (r + 1) * th + ovy)
                tile = img[y0:y1, x0:x1]
                if tile.size == 0:
                    continue

                for plate, box in self.find(tile):
                    if box is None:
                        found.append((plate, None))
                    else:
                        # 块内坐标要偏移回整张图的坐标
                        found.append((plate, (box[0] + x0, box[1] + y0,
                                              box[2] + x0, box[3] + y0)))
                if found:
                    return self._dedup(found)

        return found

    @staticmethod
    def _dedup(found):
        """四块有重叠，同一块车牌可能被认到两次，按车牌号去重。"""
        out, seen = [], set()
        for plate, box in found:
            if plate in seen:
                continue
            seen.add(plate)
            out.append((plate, box))
        return out

    @staticmethod
    def _log(msg):
        try:
            os.write(_STDERR_FD, ("plate_ocr: %s\n" % msg).encode("utf-8", "replace"))
        except OSError:
            pass


# ---------------------------------------------------------------- 入口

def _format(found):
    """把结果编成一行： 京A12345|10,20,200,60;粤B88888|..."""
    parts = []
    for plate, box in found:
        if box is None:
            parts.append(plate)
        else:
            parts.append("%s|%d,%d,%d,%d" % (plate, box[0], box[1], box[2], box[3]))
    return ";".join(parts)


def main():
    import cv2

    engine = PlateEngine()

    if len(sys.argv) >= 2:
        img = cv2.imread(sys.argv[1])
        if img is None:
            os.write(_STDERR_FD, ("读不到图片: %s\n" % sys.argv[1]).encode("utf-8", "replace"))
            return 1
        _emit(_format(engine.find_with_tiles(img)))
        return 0

    _emit("READY")

    try:
        sys.stdin.reconfigure(encoding="utf-8", errors="replace")
    except AttributeError:
        pass

    while True:
        line = sys.stdin.readline()
        if not line:
            break
        path = line.strip()
        if not path:
            _emit("")
            continue
        try:
            img = cv2.imread(path)
            if img is None:
                _emit("")
                continue
            _emit(_format(engine.find_with_tiles(img)))
        except Exception as exc:
            engine._log("error: %r" % (exc,))
            _emit("")

    return 0


if __name__ == "__main__":
    sys.exit(main())
