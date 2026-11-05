#include "ScreenshotOverlay.h"

#include "AppSettings.h"
#include "AppStyle.h"
#include "IconUtils.h"
#include "PinnedImageWindow.h"
#include "TextResultWindow.h"

#include <QClipboard>
#include <QCloseEvent>
#include <QColorDialog>
#include <QCursor>
#include <QDateTime>
#include <QFileDialog>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLineF>
#include <QMouseEvent>

#include <cmath>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPolygon>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QSlider>
#include <QStandardPaths>
#include <QStyle>
#include <QtCore/qt_windows.h>
#include <QtDebug>
#include <QtMath>
#include <QtSvg/QSvgRenderer>
#include <QVBoxLayout>

namespace {

// 蒙版颜色：纯黑加透明度，压暗桌面但还看得清底下的内容
const QColor MASK_COLOR(0, 0, 0, 120);

// 选区边框颜色，用蓝色和黑色蒙版形成对比
const QColor SELECTION_COLOR(0, 160, 255);

// 边缘抓取范围：往选区外扩一点、往内缩一点都能抓到，
// 只算边框外侧会让人觉得"明明贴着边框却抓不住"。
// 外扩给到 6 像素：窄选区从外侧按住拉伸也更顺手
constexpr int EDGE_GRAB_OUTSIDE = 6;
constexpr int EDGE_GRAB_INSIDE = 8;

// 选区的四条边，用位标志表示，可以组合：LeftEdge | TopEdge 就是左上角
constexpr int LeftEdge = 1;
constexpr int TopEdge = 2;
constexpr int RightEdge = 4;
constexpr int BottomEdge = 8;

// 拉伸后选区的最小边长，再小就没有截图意义了
constexpr int MIN_SELECTION_SIZE = 16;

// 拉伸手柄小方块的边长
constexpr int HANDLE_SIZE = 7;

// 尺寸文字画在选区上方 22 像素处、高 20 像素。
// 重绘选区时必须把这一带一起算进去，否则选区一移动，旧数字会留在原地变成拖影
constexpr int SIZE_TEXT_AREA = 26;

// 尺寸文字的宽度上限，和 paintEvent 里画文字用的矩形宽度保持一致
constexpr int SIZE_TEXT_WIDTH = 160;

// 拉伸手柄往外露出半个边长，重绘时也要带上
constexpr int HANDLE_MARGIN = 4;

// 画笔和图形的粗细范围。下限 1 是能看清的最细线条，
// 上限 20 已经足够在整屏截图上刷出醒目的色块
constexpr int MIN_MARK_WIDTH = 1;
constexpr int MAX_MARK_WIDTH = 20;

// 橡皮擦的擦除范围由粗细面板调节。默认半径 10 和旧版固定值一致
constexpr int MIN_ERASER_RADIUS = 4;
constexpr int MAX_ERASER_RADIUS = 48;

// 文字标注的字号范围（像素）。上下限覆盖了"小注释"到"大标题"，
// 默认 24 号在整屏截图上足够清楚又不挡住内容
constexpr int MIN_TEXT_SIZE = 12;
constexpr int MAX_TEXT_SIZE = 72;
constexpr int DEFAULT_TEXT_SIZE = 24;

// 文字面板里的预设颜色：红橙黄绿蓝黑，外加白色（给深色底图用）。
// 这几个基本覆盖了标注场景，特殊需求点"更多"自己调
const QColor TEXT_COLOR_PRESETS[] = {
    QColor(0xe6, 0x3c, 0x3c),   // 红
    QColor(0xf0, 0x8c, 0x00),   // 橙
    QColor(0xf5, 0xc4, 0x00),   // 黄
    QColor(0x2f, 0x9e, 0x44),   // 绿
    QColor(0x19, 0x71, 0xc2),   // 蓝
    QColor(0x1f, 0x1f, 0x1f),   // 黑
    QColor(0xff, 0xff, 0xff),   // 白
};

// 文本输入框四周的留白：既是视觉内边距，也是按住拖动的位置。
// 和拖动截图选区是同一个手感——框内任何一处按住都能把它挪走
constexpr int TEXT_EDITOR_PADDING = 6;

// 在文字区按下后移动超过这个距离才算拖动，否则当作一次点击（定位光标）
constexpr int TEXT_EDITOR_DRAG_THRESHOLD = 4;

// 文本输入框的样式：字体和颜色与最终写上去的一致，所见即所得。
// 外框和输入区写在同一个样式表里，作用范围限定在 #textEditorBox 内
QString textEditorStyleSheet(const QColor &color)
{
    return QStringLiteral(
        "#textEditorBox { background-color: rgba(255, 255, 255, 245);"
        "                 border: 1px solid %2; border-radius: 4px; }"
        "#textEditorBox QLineEdit { background-color: transparent; border: none;"
        "                          color: %1; padding: 0 2px;"
        "                          selection-background-color: %2; }")
        .arg(color.name(), AppStyle::accent.name());
}

// 选区和它周围那些装饰（拉伸手柄、上方的尺寸文字）一起占用的范围。
//
// 只按选区本身算重绘范围是不够的：尺寸文字比选区还宽的时候会伸到选区右边外面，
// 快速拉伸时旧位置的数字就会残留在那里，看起来就是拖影。
QRect areaWithDecorations(const QRect &selection)
{
    QRect area = selection.adjusted(-HANDLE_MARGIN, -SIZE_TEXT_AREA, HANDLE_MARGIN, HANDLE_MARGIN);

    // 文字从选区左上角往右排，选区比文字窄时右边界要跟着延长
    area.setRight(qMax(area.right(), selection.left() + SIZE_TEXT_WIDTH));

    return area;
}

// 按抓取的方向换成对应的拉伸光标
Qt::CursorShape cursorForEdges(int edges)
{
    switch (edges) {
    case LeftEdge | TopEdge:
    case RightEdge | BottomEdge:
        return Qt::SizeFDiagCursor;
    case RightEdge | TopEdge:
    case LeftEdge | BottomEdge:
        return Qt::SizeBDiagCursor;
    case LeftEdge:
    case RightEdge:
        return Qt::SizeHorCursor;
    case TopEdge:
    case BottomEdge:
        return Qt::SizeVerCursor;
    default:
        return Qt::ArrowCursor;
    }
}

// 在选区的四角和四边中点画小方块，提示这些位置可以按住拉伸
void paintResizeHandles(QPainter &painter, const QRect &selection)
{
    const int left = selection.left();
    const int right = selection.right();
    const int top = selection.top();
    const int bottom = selection.bottom();
    const int centerX = selection.center().x();
    const int centerY = selection.center().y();

    const QPoint centers[] = {
        QPoint(left, top),          // 左上角
        QPoint(centerX, top),       // 上边中点
        QPoint(right, top),         // 右上角
        QPoint(right, centerY),     // 右边中点
        QPoint(right, bottom),      // 右下角
        QPoint(centerX, bottom),    // 下边中点
        QPoint(left, bottom),       // 左下角
        QPoint(left, centerY),      // 左边中点
    };

    painter.setPen(QPen(SELECTION_COLOR, 1));
    painter.setBrush(Qt::white);

    for (const QPoint &center : centers) {
        painter.drawRect(QRect(center.x() - HANDLE_SIZE / 2, center.y() - HANDLE_SIZE / 2,
                               HANDLE_SIZE, HANDLE_SIZE));
    }
}

// 形状边框参数化的辅助函数，定义在文件后面（原橡皮光标的位置）。
// paintShape 和 eraseAt 都要用，这里先声明
qreal shapePathLength(const MarkShape &shape);
QPointF shapePointAt(const MarkShape &shape, qreal t);
QVector<QPair<qreal, qreal>> keptRanges(const QVector<QPair<qreal, qreal>> &erased);

// 把一笔标注画到画布上，坐标使用覆盖层的局部坐标
void paintShape(QPainter &painter, const MarkShape &shape)
{
    if (shape.tool == MarkTool::None) {
        return;
    }

    painter.setPen(QPen(shape.color, shape.lineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    if (shape.tool == MarkTool::Pen) {
        if (shape.points.size() < 2) {
            return;
        }
        QPainterPath path(shape.points.first());
        for (int index = 1; index < shape.points.size(); ++index) {
            path.lineTo(shape.points.at(index));
        }
        painter.drawPath(path);
        return;
    }

    // 文本：左上角锚点 + 自己记着的字号（和画笔粗细无关）
    if (shape.tool == MarkTool::Text) {
        QFont textFont = painter.font();
        textFont.setPixelSize(shape.fontSize > 0 ? shape.fontSize : DEFAULT_TEXT_SIZE);
        painter.setFont(textFont);
        painter.setPen(QPen(shape.color, 1));
        const QFontMetrics metrics(textFont);
        painter.drawText(QPoint(shape.start.x(), shape.start.y() + metrics.ascent()),
                         shape.text);
        return;
    }

    // 直线：没擦过直接画；擦过按保留区间分段画
    if (shape.tool == MarkTool::Line) {
        if (shape.erasedRanges.isEmpty()) {
            painter.drawLine(shape.start, shape.end);
        } else {
            for (const auto &range : keptRanges(shape.erasedRanges)) {
                painter.drawLine(shapePointAt(shape, range.first).toPoint(),
                                 shapePointAt(shape, range.second).toPoint());
            }
        }
        return;
    }

    // 矩形 / 椭圆 / 三角形
    if (shape.tool == MarkTool::Rectangle || shape.tool == MarkTool::Ellipse
        || shape.tool == MarkTool::Triangle) {
        const QRectF box = QRectF(shape.start, shape.end).normalized();

        // 没擦过走原生绘制：椭圆是 QPainter 的真圆弧，绝不变形
        if (shape.erasedRanges.isEmpty()) {
            if (shape.tool == MarkTool::Rectangle) {
                painter.drawRect(box);
            } else if (shape.tool == MarkTool::Ellipse) {
                painter.drawEllipse(box);
            } else {
                QPolygonF triangle;
                triangle << QPointF(box.center().x(), box.top())
                         << QPointF(box.right(), box.bottom())
                         << QPointF(box.left(), box.bottom());
                painter.drawPolygon(triangle);
            }
            return;
        }

        const qreal total = shapePathLength(shape);
        if (total <= 0) {
            return;
        }

        const QVector<QPair<qreal, qreal>> kept = keptRanges(shape.erasedRanges);

        if (shape.tool == MarkTool::Ellipse) {
            // drawArc 用逆时针角度（单位 1/16 度，0° 在 3 点钟方向）。
            // 我们的参数角在屏幕坐标系（y 向下）里顺时针增大，与 Qt 角度方向相反，
            // 所以起点取区间的末端取负、跨度为正
            for (const auto &range : kept) {
                painter.drawArc(box,
                                qRound(-range.second * 360.0 * 16.0),
                                qRound((range.second - range.first) * 360.0 * 16.0));
            }
            return;
        }

        // 矩形 / 三角形：边框是折线，保留区间可能跨过顶点，
        // 按 8 像素一段采样后逐段连线，拐角才不会走直线抄近路
        for (const auto &range : kept) {
            const int steps = qMax(1, qCeil((range.second - range.first) * total / 8.0));
            QPointF previous = shapePointAt(shape, range.first);
            for (int index = 1; index <= steps; ++index) {
                const QPointF current = shapePointAt(
                    shape, range.first + (range.second - range.first) * index / steps);
                painter.drawLine(previous, current);
                previous = current;
            }
        }
        return;
    }
}

// 点到线段的最短距离。橡皮擦判断有没有碰到某条边时用得上
qreal distanceToSegment(const QPoint &point, const QPointF &from, const QPointF &to)
{
    const qreal dx = to.x() - from.x();
    const qreal dy = to.y() - from.y();
    const qreal lengthSquared = dx * dx + dy * dy;

    if (lengthSquared <= 0) {
        return QLineF(point, from).length();
    }

    // 把点投影到线段所在直线上，t 夹在 0~1 之间就落在线段范围内
    qreal t = ((point.x() - from.x()) * dx + (point.y() - from.y()) * dy) / lengthSquared;
    t = qBound(0.0, t, 1.0);

    const QPointF closest(from.x() + dx * t, from.y() + dy * t);
    return QLineF(point, closest).length();
}

// 判断橡皮擦所在位置有没有碰到这条标注的边框。
// 只看边、不看内部：擦一个空心矩形时，从中间划过不该把它整条删掉。
// 判断橡皮擦所在位置有没有碰到文本标注。
// 文本按整块删除：文字没法只擦一半，碰到就算命中。
// 范围按文本的实际外接框算，比只判锚点准。
// 矩形/椭圆/三角的擦除走参数化区间（见 shapePathLength 一带），不用这里
bool shapeTouches(const MarkShape &shape, const QPoint &position, qreal radius)
{
    if (shape.tool != MarkTool::Text) {
        return false;
    }

    QFont textFont;
    textFont.setPixelSize(shape.fontSize > 0 ? shape.fontSize : DEFAULT_TEXT_SIZE);
    const QFontMetrics metrics(textFont);
    const QRect textRect(shape.start,
                         QSize(metrics.horizontalAdvance(shape.text), metrics.height()));
    return textRect.adjusted(-radius, -radius, radius, radius).contains(position);
}

// ---------- 形状边框的参数化：橡皮擦擦形状、绘制擦剩部分都基于它 ----------
//
// 参数 t 沿边框归一化到 0~1：矩形/三角形沿闭合边框绕一圈，直线从 start 到 end，
// 椭圆沿角度绕一圈。擦除只是在形状上记下"哪一段被擦掉了"（erasedRanges），
// 形状本体不变，剩余部分渲染时仍走 drawArc / drawLine，
// 所以擦剩的圆弧依旧是真圆弧，不会退化成折线

// 边框总弧长（像素）。椭圆按 Ramanujan 近似公式算周长
qreal shapePathLength(const MarkShape &shape)
{
    const QRectF box = QRectF(shape.start, shape.end).normalized();

    if (shape.tool == MarkTool::Line) {
        return QLineF(shape.start, shape.end).length();
    }

    if (shape.tool == MarkTool::Ellipse) {
        const qreal a = box.width() / 2.0;
        const qreal b = box.height() / 2.0;
        if (a <= 0 || b <= 0) {
            return 0;
        }
        const qreal h = (a - b) * (a - b) / ((a + b) * (a + b));
        return M_PI * (a + b) * (1 + 3 * h / (10 + qSqrt(4 - 3 * h)));
    }

    if (shape.tool == MarkTool::Rectangle) {
        return 2 * (box.width() + box.height());
    }

    // 三角形
    const QPointF top(box.center().x(), box.top());
    const QPointF right(box.right(), box.bottom());
    const QPointF left(box.left(), box.bottom());
    return QLineF(top, right).length() + QLineF(right, left).length()
           + QLineF(left, top).length();
}

// 参数 t（0~1）对应边框上的点
QPointF shapePointAt(const MarkShape &shape, qreal t)
{
    const QRectF box = QRectF(shape.start, shape.end).normalized();

    if (shape.tool == MarkTool::Line) {
        return QPointF(shape.start) + (QPointF(shape.end) - QPointF(shape.start)) * t;
    }

    if (shape.tool == MarkTool::Ellipse) {
        const qreal angle = t * 2 * M_PI;
        return QPointF(box.center().x() + box.width() / 2.0 * std::cos(angle),
                       box.center().y() + box.height() / 2.0 * std::sin(angle));
    }

    if (shape.tool == MarkTool::Rectangle) {
        const qreal lengths[] = {box.width(), box.height(), box.width(), box.height()};
        const QPointF corners[] = {box.topLeft(), box.topRight(),
                                   box.bottomRight(), box.bottomLeft(), box.topLeft()};
        qreal distance = t * shapePathLength(shape);
        for (int side = 0; side < 4; ++side) {
            if (distance <= lengths[side] || side == 3) {
                const qreal ratio =
                    lengths[side] > 0 ? qBound(0.0, distance / lengths[side], 1.0) : 0.0;
                return corners[side] + (corners[side + 1] - corners[side]) * ratio;
            }
            distance -= lengths[side];
        }
        return box.topLeft();
    }

    // 三角形：顶点顺序 top → right → left → top
    const QPointF top(box.center().x(), box.top());
    const QPointF right(box.right(), box.bottom());
    const QPointF left(box.left(), box.bottom());
    const qreal lengths[] = {QLineF(top, right).length(),
                             QLineF(right, left).length(),
                             QLineF(left, top).length()};
    const QPointF corners[] = {top, right, left, top};
    qreal distance = t * shapePathLength(shape);
    for (int side = 0; side < 3; ++side) {
        if (distance <= lengths[side] || side == 2) {
            const qreal ratio =
                lengths[side] > 0 ? qBound(0.0, distance / lengths[side], 1.0) : 0.0;
            return corners[side] + (corners[side + 1] - corners[side]) * ratio;
        }
        distance -= lengths[side];
    }
    return top;
}

// 把一段新擦除的区间合并进列表：排序、重叠融合。传入的 from > to 表示跨过
// 闭合路径 0/1 边界的区间，拆成两段处理。返回合并后的总覆盖长度，
// 用于判断整个形状是否已经被擦光
qreal mergeErasedRanges(QVector<QPair<qreal, qreal>> &ranges, qreal from, qreal to)
{
    from = qBound(0.0, from, 1.0);
    to = qBound(0.0, to, 1.0);
    if (from <= to) {
        ranges.append({from, to});
    } else {
        ranges.append({0.0, to});
        ranges.append({from, 1.0});
    }

    std::sort(ranges.begin(), ranges.end(),
              [](const QPair<qreal, qreal> &a, const QPair<qreal, qreal> &b) {
                  return a.first < b.first;
              });

    QVector<QPair<qreal, qreal>> merged;
    for (const auto &range : ranges) {
        if (!merged.isEmpty() && range.first <= merged.last().second) {
            merged.last().second = qMax(merged.last().second, range.second);
        } else {
            merged.append(range);
        }
    }
    ranges = merged;

    qreal covered = 0;
    for (const auto &range : ranges) {
        covered += range.second - range.first;
    }
    return covered;
}

// 计算 0~1 中未被 erasedRanges 覆盖的保留区间，渲染时只画这些段
QVector<QPair<qreal, qreal>> keptRanges(const QVector<QPair<qreal, qreal>> &erased)
{
    QVector<QPair<qreal, qreal>> kept;
    qreal cursor = 0.0;
    for (const auto &range : erased) {
        if (range.first > cursor) {
            kept.append({cursor, range.first});
        }
        cursor = qMax(cursor, range.second);
    }
    if (cursor < 1.0) {
        kept.append({cursor, 1.0});
    }
    return kept;
}

// 工具条上粗细按钮的图标：用圆点的大小表示当前线宽。
// 比在按钮上写一个数字更容易一眼看出现在有多粗，风格也和旁边的单色图标一致
QIcon buildWidthIcon(int lineWidth)
{
    constexpr int ICON_SIZE = 32;
    constexpr int MAX_DIAMETER = 22;

    QImage image(ICON_SIZE, ICON_SIZE, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    // 线宽按比例换算成直径：最细也要留 3 像素，否则细线宽下圆点会完全消失
    const int scaledDiameter = lineWidth * ICON_SIZE / MAX_MARK_WIDTH;
    const int diameter = qBound(3, scaledDiameter, MAX_DIAMETER);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    // 粗细圆点用深灰：工具条现在是浅色底，白点会看不清
    painter.setBrush(AppStyle::iconTint);

    const qreal offset = (ICON_SIZE - diameter) / 2.0;
    painter.drawEllipse(QRectF(offset, offset, diameter, diameter));
    painter.end();

    return QIcon(QPixmap::fromImage(image));
}

}

ScreenshotOverlay::ScreenshotOverlay(const QImage &screenImage, const QRect &coveredGeometry,
                                     qreal imagePixelRatio)
    : screenshot(screenImage)
    , screenGeometry(coveredGeometry)
    , pixelRatio(imagePixelRatio)
{
    // 无边框 + 置顶 + Tool：盖住整个桌面，并且不出现在任务栏里
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setCursor(Qt::CrossCursor);

    // 橡皮擦不再使用专用光标形状，靠画面上的擦除范围提示圈指示
    setFocusPolicy(Qt::StrongFocus);
    // 不按键也要能收到鼠标移动，用于在选区内切换成"可拖动"光标
    setMouseTracking(true);
    // 整个窗口都由 paintEvent 自己画满，声明不透明可以省掉 Qt 每次重绘前
    // 先用调色板颜色填一遍背景，快速拉伸时不会闪出底色
    setAttribute(Qt::WA_OpaquePaintEvent);
    setGeometry(coveredGeometry);
    setAttribute(Qt::WA_DeleteOnClose);

    // 先把"原图 + 黑色蒙版"合成好。拖拽选区时每次鼠标移动都要重绘，
    // 如果每帧都现叠蒙版，2K 分辨率下会明显卡顿、选区跟不上鼠标。
    maskedImage = screenshot.copy();

    // 蒙版按物理像素铺满整张图。QPainter 会按图的设备像素比换算坐标，
    // 所以这里先把比例临时改成 1，用物理像素坐标填充，填完再还原。
    const qreal savedRatio = maskedImage.devicePixelRatio();
    maskedImage.setDevicePixelRatio(1.0);
    QPainter maskPainter(&maskedImage);
    maskPainter.fillRect(QRect(QPoint(0, 0), maskedImage.size()), MASK_COLOR);
    maskPainter.end();
    maskedImage.setDevicePixelRatio(savedRatio);
}

void ScreenshotOverlay::closeEvent(QCloseEvent *event)
{
    // 只发一次：复制、保存、取消、其它屏幕的遮罩关闭都会走到这里
    if (!closeNotified) {
        closeNotified = true;
        emit captureClosed();
    }

    QWidget::closeEvent(event);
}

void ScreenshotOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 第一层：预先压暗好的底图。
    // 图本身带着设备像素比，按原点和原尺寸贴上去就是一个物理像素对一个屏幕像素。
    // 之前是把整张图缩放到控件逻辑尺寸，每次重绘都做一遍 1.25 倍缩放，
    // 既慢、画面也会发虚。
    painter.drawImage(QPointF(0, 0), maskedImage);

    if (selection.isEmpty()) {
        // 还没开始选，给一句提示，避免用户不知道该做什么
        painter.setPen(QColor(255, 255, 255, 220));
        QFont hintFont = font();
        hintFont.setPointSize(14);
        painter.setFont(hintFont);
        painter.drawText(rect(), Qt::AlignCenter, tr("按住鼠标左键拖拽选择截图区域，按 Esc 取消"));
        return;
    }

    // 第三层：选区重新贴一次原图，视觉上就是"选区是透明的"。
    // 源矩形用浮点数算：之前用整数取整，1.25 倍缩放下那 0.25 的小数会随拖动位置来回跳，
    // 选区里的画面就跟着抖一个像素，看起来正是拖影。
    const QRectF sourceRect(selection.x() * pixelRatio, selection.y() * pixelRatio,
                            selection.width() * pixelRatio, selection.height() * pixelRatio);
    painter.drawImage(QRectF(selection), screenshot, sourceRect);

    // 第四层：标注，只画在选区内
    painter.save();
    painter.setClipRect(selection);
    for (const MarkShape &shape : shapes) {
        paintShape(painter, shape);
    }
    if (drawing) {
        paintShape(painter, drawingShape);
    }
    painter.restore();

    // 橡皮擦范围提示圈：白+黑双圈，浅色深色底上都看得清。
    // 只在选区内显示，圈的大小就是实际擦除半径
    if (currentTool == MarkTool::Eraser && eraserPreviewVisible
        && selection.contains(eraserPreviewPos)) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(Qt::white, 1));
        painter.drawEllipse(eraserPreviewPos, eraserRadius, eraserRadius);
        painter.setPen(QPen(Qt::black, 1));
        painter.drawEllipse(eraserPreviewPos, eraserRadius - 1, eraserRadius - 1);
    }

    // 第五层：选区边框和尺寸提示
    painter.setPen(QPen(SELECTION_COLOR, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(selection.adjusted(0, 0, -1, -1));

    // 四角和四边中点的小方块，提示这些位置可以按住拉伸
    paintResizeHandles(painter, selection);

    const QString sizeText = QStringLiteral("%1 × %2").arg(selection.width()).arg(selection.height());
    painter.setFont(font());
    painter.setPen(QColor(255, 255, 255));
    painter.drawText(QRect(selection.left(), selection.top() - 22, 160, 20),
                     Qt::AlignLeft | Qt::AlignVCenter, sizeText);
}

int ScreenshotOverlay::edgeAt(const QPoint &position) const
{
    if (selection.isEmpty()) {
        return 0;
    }

    // 先把范围放宽一点做粗筛，离得太远就不用继续判断
    const QRect hitArea = selection.adjusted(-EDGE_GRAB_OUTSIDE, -EDGE_GRAB_OUTSIDE,
                                             EDGE_GRAB_OUTSIDE, EDGE_GRAB_OUTSIDE);
    if (!hitArea.contains(position)) {
        return 0;
    }

    int edges = 0;

    // 水平方向：选区够宽时左右各留一条判定区，落在哪条上就抓哪条；
    // 太窄时两条判定区会重叠，改按中线二分——靠左半边算抓左边缘、
    // 靠右半边算抓右边缘。否则窄选区（比如一条竖条截图）的左右边
    // 永远抓不到，四个角更是无从下手
    if (selection.width() <= EDGE_GRAB_INSIDE * 2) {
        edges |= (position.x() < selection.center().x()) ? LeftEdge : RightEdge;
    } else if (position.x() <= selection.left() + EDGE_GRAB_INSIDE) {
        edges |= LeftEdge;
    } else if (position.x() >= selection.right() - EDGE_GRAB_INSIDE) {
        edges |= RightEdge;
    }

    // 垂直方向同理：矮选区按水平中线二分出上边缘和下边缘
    if (selection.height() <= EDGE_GRAB_INSIDE * 2) {
        edges |= (position.y() < selection.center().y()) ? TopEdge : BottomEdge;
    } else if (position.y() <= selection.top() + EDGE_GRAB_INSIDE) {
        edges |= TopEdge;
    } else if (position.y() >= selection.bottom() - EDGE_GRAB_INSIDE) {
        edges |= BottomEdge;
    }

    return edges;
}

void ScreenshotOverlay::resizeSelectionTo(const QPoint &position)
{
    // 以按下瞬间的矩形为基准，只改动被抓住的那几条边
    int left = resizeOriginal.left();
    int top = resizeOriginal.top();
    int right = resizeOriginal.right();
    int bottom = resizeOriginal.bottom();

    // 每条边都限制在窗口范围内，并且不许把选区压到最小尺寸以下
    if (resizeEdges & LeftEdge) {
        left = qBound(0, position.x(), right - MIN_SELECTION_SIZE);
    }
    if (resizeEdges & RightEdge) {
        right = qBound(left + MIN_SELECTION_SIZE, position.x(), width() - 1);
    }
    if (resizeEdges & TopEdge) {
        top = qBound(0, position.y(), bottom - MIN_SELECTION_SIZE);
    }
    if (resizeEdges & BottomEdge) {
        bottom = qBound(top + MIN_SELECTION_SIZE, position.y(), height() - 1);
    }

    selection = QRect(QPoint(left, top), QPoint(right, bottom));
}

void ScreenshotOverlay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }

    // 正在输入文字时，画面上任何一处点击都表示"这条写完了"：
    // 先把内容落定成一条标注，再继续处理这次点击。
    // 本次不再自动弹出新的输入框，否则点一下又开一个，永远收不了尾
    const bool wasEditingText = textEditor != nullptr;
    commitTextEntry();

    // 在画面上按下就收起粗细面板、形状菜单和文字面板。面板内部的点击不会走到这里，
    // 所以拖滑杆、选形状、选颜色的时候不影响
    if (widthPanel != nullptr) {
        widthPanel->hide();
    }
    if (shapePanel != nullptr) {
        shapePanel->hide();
    }
    if (textPanel != nullptr) {
        textPanel->hide();
    }

    // 刚写完字就点到画面上：这一下只用来结束这条标注，
    // 不再开新的输入框、不重新框选，也不开始画，
    // 否则刚写好的字会被随后的 shapes.clear() 清掉
    if (wasEditingText) {
        return;
    }

    // 橡皮擦排在最前面：正在擦的时候按到选区边缘上，
    // 不应该突然变成调整选区大小
    if (!selection.isEmpty() && currentTool == MarkTool::Eraser && selection.contains(event->pos())) {
        erasing = true;
        eraseAt(event->pos());
        return;
    }

    // 文本工具：在选区内点一下，就地弹出输入框
    if (!selection.isEmpty() && currentTool == MarkTool::Text && selection.contains(event->pos())) {
        startTextEntry(event->pos());
        return;
    }

    // 已经选好区域时，先判断是不是按在边缘上。
    // 边缘优先于标注和拖动：否则贴着边框画标注时，永远调不了选区大小
    if (!selection.isEmpty()) {
        const int edges = edgeAt(event->pos());
        if (edges != 0) {
            resizingSelection = true;
            resizeEdges = edges;
            resizeOriginal = selection;
            setCursor(cursorForEdges(edges));
            return;
        }
    }

    // 已经选好区域并且选了画笔或图形：在选区内按下就开始画
    if (!selection.isEmpty() && currentTool != MarkTool::None && selection.contains(event->pos())) {
        drawing = true;
        drawingShape = MarkShape();
        drawingShape.tool = currentTool;
        drawingShape.start = event->pos();
        drawingShape.end = event->pos();
        drawingShape.points.append(event->pos());
        drawingShape.color = markColor;
        drawingShape.lineWidth = markWidth;
        update();
        return;
    }

    // 已经选好区域但没选标注工具：在选区内部按下是拖动整个选区，
    // 而不是推倒重来。记录按下点相对选区左上角的偏移，移动时保持这个偏移。
    if (!selection.isEmpty() && currentTool == MarkTool::None && selection.contains(event->pos())) {
        movingSelection = true;
        moveOffset = event->pos() - selection.topLeft();
        setCursor(Qt::SizeAllCursor);
        return;
    }

    // 其余情况都当作重新框选，之前的标注随之作废
    // 先通知一声：其它屏幕的遮罩收到后会把舞台让给这一块
    emit selectionStarted();

    selecting = true;
    dragStart = event->pos();
    selection = QRect(dragStart, dragStart);
    shapes.clear();
    applyTool(MarkTool::None);
    if (toolBar != nullptr) {
        toolBar->hide();
    }
    update();
}

void ScreenshotOverlay::mouseMoveEvent(QMouseEvent *event)
{
    // 橡皮擦模式下维护擦除范围提示圈：记录位置并重绘新旧两圈覆盖的区域。
    // 提示圈让"现在能擦多大"一目了然，也顺便提示当前处于擦除状态
    if (currentTool == MarkTool::Eraser) {
        const QRect oldRing(eraserPreviewPos.x() - eraserRadius - 2,
                            eraserPreviewPos.y() - eraserRadius - 2,
                            (eraserRadius + 2) * 2, (eraserRadius + 2) * 2);
        eraserPreviewPos = event->pos();
        eraserPreviewVisible = true;
        const QRect newRing(eraserPreviewPos.x() - eraserRadius - 2,
                            eraserPreviewPos.y() - eraserRadius - 2,
                            (eraserRadius + 2) * 2, (eraserRadius + 2) * 2);
        update(oldRing.united(newRing));
    }

    if (erasing) {
        eraseAt(event->pos());
        return;
    }

    if (drawing) {
        const QRect oldBounds = QRect(drawingShape.start, drawingShape.end).normalized();
        drawingShape.end = event->pos();
        if (currentTool == MarkTool::Pen) {
            drawingShape.points.append(event->pos());
        }

        // 只重绘标注占据的区域。整屏重绘在 2K 分辨率下会明显卡顿，拖拽就会跟不上鼠标。
        // 外扩的量要跟着线宽走：线越粗，笔迹越容易超出外接框，留窄了会拖出残影
        const QRect newBounds = QRect(drawingShape.start, drawingShape.end).normalized();
        const int margin = drawingShape.lineWidth / 2 + 4;
        update(oldBounds.united(newBounds).adjusted(-margin, -margin, margin, margin));
        return;
    }

    if (resizingSelection) {
        const QRect oldSelection = selection;
        resizeSelectionTo(event->pos());
        placeToolBar();
        // 新旧选区连同各自的尺寸文字都要重绘，
        // 快速拉伸时只重绘几个像素的边距，旧数字根本来不及擦掉
        update(areaWithDecorations(oldSelection).united(areaWithDecorations(selection)));
        return;
    }

    if (movingSelection) {
        const QRect oldSelection = selection;

        // 选区整体平移，贴住屏幕边缘就停住，不会跑出可视范围
        QPoint newTopLeft = event->pos() - moveOffset;
        newTopLeft.setX(qBound(0, newTopLeft.x(), width() - selection.width()));
        newTopLeft.setY(qBound(0, newTopLeft.y(), height() - selection.height()));

        const QPoint delta = newTopLeft - selection.topLeft();
        selection.moveTopLeft(newTopLeft);

        // 标注跟着选区一起平移，否则会和截图内容错位
        for (MarkShape &shape : shapes) {
            shape.start += delta;
            shape.end += delta;
            for (QPoint &point : shape.points) {
                point += delta;
            }
        }

        placeToolBar();
        update(areaWithDecorations(oldSelection).united(areaWithDecorations(selection)));
        return;
    }

    if (!selecting) {
        // 没按住鼠标时只更新光标，提示当前位置能做什么
        if (!selection.isEmpty()) {
            const int edges = edgeAt(event->pos());
            if (edges != 0) {
                setCursor(cursorForEdges(edges));
                return;
            }
        }

        if (!selection.isEmpty() && currentTool == MarkTool::None
            && selection.contains(event->pos())) {
            setCursor(Qt::SizeAllCursor);
        } else {
            // 画笔和图形用十字，橡皮擦换成橡皮形状，其余情况也是十字
            setToolCursor();
        }
        return;
    }

    const QRect oldSelection = selection;
    selection = QRect(dragStart, event->pos()).normalized().intersected(rect());
    update(areaWithDecorations(oldSelection).united(areaWithDecorations(selection)));
}

void ScreenshotOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }

    if (erasing) {
        erasing = false;
        return;
    }

    if (resizingSelection) {
        resizingSelection = false;
        resizeEdges = 0;
        setToolCursor();
        // 快速拉伸时鼠标事件会被合并，中间几帧可能来不及重绘。
        // 松手后整屏重绘一次收尾，保证屏幕上不留旧的边框和数字。
        update();
        return;
    }

    if (movingSelection) {
        movingSelection = false;
        setToolCursor();
        update();
        return;
    }

    if (drawing) {
        drawing = false;
        // 只是点了一下没拖动，不留空标注；直线也一样，点出零长度直线没有意义
        if (drawingShape.tool == MarkTool::Pen && drawingShape.points.size() < 2) {
            drawingShape = MarkShape();
            update();
            return;
        }
        if (drawingShape.tool == MarkTool::Line && drawingShape.start == drawingShape.end) {
            drawingShape = MarkShape();
            update();
            return;
        }
        shapes.append(drawingShape);
        drawingShape = MarkShape();
        update();
        return;
    }

    if (!selecting) {
        return;
    }

    selecting = false;
    selection = QRect(dragStart, event->pos()).normalized().intersected(rect());

    // 太小的区域当作误触，直接复位
    if (selection.width() < 4 || selection.height() < 4) {
        selection = QRect();
        if (toolBar != nullptr) {
            toolBar->hide();
        }
        update();
        return;
    }

    buildToolBar();
    placeToolBar();
    toolBar->show();
    update();
}

void ScreenshotOverlay::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        cancelCapture();
        return;
    }

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        copyResult();
        return;
    }

    if (event->key() == Qt::Key_Z && (event->modifiers() & Qt::ControlModifier)) {
        undoLastMark();
        return;
    }

    QWidget::keyPressEvent(event);
}

void ScreenshotOverlay::buildToolBar()
{
    if (toolBar != nullptr) {
        return;
    }

    toolBar = new QWidget(this);
    toolBar->setObjectName("captureToolBar");
    // Fluent 浅色工具条：和主窗口同一套配色，悬停浅灰、选中强调色。
    // 关闭按钮悬停红色，区分危险操作
    toolBar->setStyleSheet(QStringLiteral(
        "#captureToolBar { background-color: %1; border: 1px solid %2; border-radius: 6px; }"
        "#captureToolBar QPushButton { border: none; border-radius: 4px; color: %3;"
        "                            background-color: transparent; font-size: 13px; }"
        "#captureToolBar QPushButton:hover { background-color: #e9e9e9; }"
        "#captureToolBar QPushButton[active=\"true\"] { background-color: %4; }")
        .arg(AppStyle::titleBarFill.name(), AppStyle::cardStroke.name(),
             AppStyle::textPrimary.name(), AppStyle::accent.name()));

    auto layout = new QHBoxLayout(toolBar);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);

    // 浅色工具条上用深灰图标，和标题栏的图标是同一个颜色
    const QString iconRoot = QStringLiteral(":/QScreenCut/images/");

    // 工具条上按钮外观一致，用一个小工厂减少重复代码。
    // iconFile 传空字符串表示这个按钮暂时没有图标，只用文字
    auto addButton = [this, layout, iconRoot](const QString &text, const QString &tip,
                                              const QString &iconFile) {
        auto button = new QPushButton(text, toolBar);
        button->setToolTip(tip);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedSize(30, 30);

        if (!iconFile.isEmpty()) {
            button->setIconSize(QSize(14, 14));

            // SVG 是矢量描边图，按 32 像素渲染再交给按钮缩放，高分屏也清晰
            if (iconFile.endsWith(QStringLiteral(".svg"))) {
                button->setIcon(loadTintedSvgIcon(iconRoot + iconFile, AppStyle::iconTint,
                                                  QSize(32, 32)));
            } else {
                button->setIcon(loadTintedIcon(iconRoot + iconFile, AppStyle::iconTint));
            }
        }

        layout->addWidget(button);
        return button;
    };

    QPushButton *pinButton = addButton(tr(""), tr("把截图钉在桌面上"), QStringLiteral("图钉.png"));
    QPushButton *saveButton = addButton(tr(""), tr("保存为图片文件"), QStringLiteral("fileicons/device-floppy.svg"));
    QPushButton *copyButton = addButton(tr(""), tr("复制到剪贴板"), QStringLiteral("复制.png"));

    // 文字识别和翻译都走网络接口，按钮放在输出操作这一组，
    // 因为它们和复制、保存一样，都是"把选区内容拿出去"的动作
    QPushButton *ocrButton = addButton(tr(""), tr("提取选区里的文字"),
                                       QStringLiteral("fileicons/file-text.svg"));
    QPushButton *translateButton = addButton(tr(""), tr("识别并翻译选区里的文字"),
                                             QStringLiteral("fileicons/language.svg"));

    QPushButton *undoButton = addButton(tr(""), tr("撤销最后一笔标注"), QStringLiteral("fileicons/arrow-back-up.svg"));
    QPushButton *cancelButton = addButton(tr(""), tr("取消"), QStringLiteral("关闭1.png"));

    // 分隔线，把输出操作和标注工具分成两组
    auto separator = new QFrame(toolBar);
    separator->setFrameShape(QFrame::VLine);
    separator->setStyleSheet(QStringLiteral("color: %1;")
                                 .arg(AppStyle::controlStroke.name()));
    layout->addWidget(separator);

    penButton = addButton(tr(""), tr("自由手绘（再点一次取消）"), QStringLiteral("fileicons/pencil.svg"));

    // 粗细按钮紧跟在画笔后面，因为它管的就是画笔和图形画出来有多粗。
    // 图标上的圆点大小直接表示当前粗细，具体数值放在提示里
    widthButton = addButton(tr(""), tr("画笔粗细"), QString());
    widthButton->setIconSize(QSize(18, 18));
    updateWidthButtonIcon();

    // 形状按钮：矩形/椭圆/三角/直线合并在这里，点它弹出二级横向菜单选具体形状
    shapeButton = addButton(tr(""), tr("画形状：矩形、椭圆、三角形、直线"),
                            QStringLiteral("方形未选中.png"));
    eraserButton = addButton(tr(""), tr("橡皮擦：拖过要擦掉的标注"), QStringLiteral("fileicons/eraser.svg"));

    // 文本工具：在选区内点一下就地输入文字
    textButton = addButton(tr(""), tr("添加文字标注"), QStringLiteral("fileicons/typography.svg"));

    // 颜色按钮放在标注工具后面，色块本身就是当前颜色
    colorButton = new QPushButton(toolBar);
    colorButton->setToolTip(tr("选择标注颜色"));
    colorButton->setCursor(Qt::PointingHandCursor);
    colorButton->setFixedSize(30, 30);
    layout->addWidget(colorButton);
    updateColorButtonStyle();

    // 粗细面板：平时藏着，点粗细按钮才弹出来。
    // 它是覆盖层的子控件而不是工具条的，这样不受工具条布局约束，可以自己定位
    widthPanel = new QFrame(this);
    widthPanel->setObjectName("widthPanel");
    widthPanel->setStyleSheet(QStringLiteral(
        "#widthPanel { background-color: %1; border: 1px solid %2; border-radius: 6px; }"
        "#widthPanel QLabel { color: %3; font-size: 12px; }"
        "#widthPanel QSlider::groove:horizontal { height: 4px; background: %4;"
        "                                         border-radius: 2px; }"
        "#widthPanel QSlider::sub-page:horizontal { background: %5; border-radius: 2px; }"
        "#widthPanel QSlider::handle:horizontal { width: 12px; height: 12px; margin: -4px 0;"
        "                                         border-radius: 6px; background: %1;"
        "                                         border: 1px solid %2; }")
        .arg(AppStyle::titleBarFill.name(), AppStyle::cardStroke.name(),
             AppStyle::textPrimary.name(), AppStyle::controlStroke.name(),
             AppStyle::accent.name()));

    auto panelLayout = new QHBoxLayout(widthPanel);
    panelLayout->setContentsMargins(10, 8, 10, 8);
    panelLayout->setSpacing(8);

    auto panelTitle = new QLabel(tr("粗细"), widthPanel);
    widthTitleLabel = panelTitle;
    panelLayout->addWidget(panelTitle);

    widthSlider = new QSlider(Qt::Horizontal, widthPanel);
    widthSlider->setRange(MIN_MARK_WIDTH, MAX_MARK_WIDTH);
    widthSlider->setValue(markWidth);
    widthSlider->setFixedWidth(120);
    widthSlider->setCursor(Qt::PointingHandCursor);
    // 不让滑杆抢焦点：否则按 Esc 取消截图、回车复制时，
    // 按键会先被滑杆吃掉，截图就退不掉了
    widthSlider->setFocusPolicy(Qt::NoFocus);
    panelLayout->addWidget(widthSlider);

    widthValueLabel = new QLabel(QString::number(markWidth), widthPanel);
    widthValueLabel->setFixedWidth(18);
    widthValueLabel->setAlignment(Qt::AlignCenter);
    panelLayout->addWidget(widthValueLabel);

    widthPanel->adjustSize();
    widthPanel->hide();

    // 形状二级菜单：平时藏着，点形状按钮弹出来，横排四个具体形状。
    // 同样是覆盖层的子控件，定位逻辑和粗细面板一致
    shapePanel = new QFrame(this);
    shapePanel->setObjectName("shapePanel");
    shapePanel->setStyleSheet(QStringLiteral(
        "#shapePanel { background-color: %1; border: 1px solid %2; border-radius: 6px; }"
        "#shapePanel QPushButton { border: none; border-radius: 4px;"
        "                          background-color: transparent; }"
        "#shapePanel QPushButton:hover { background-color: #e9e9e9; }"
        "#shapePanel QPushButton[active=\"true\"] { background-color: %3; }")
        .arg(AppStyle::titleBarFill.name(), AppStyle::cardStroke.name(),
             AppStyle::accent.name()));

    auto shapeLayout = new QHBoxLayout(shapePanel);
    shapeLayout->setContentsMargins(6, 4, 6, 4);
    shapeLayout->setSpacing(2);

    const QVector<QPair<MarkTool, QString>> shapeKinds = {
        {MarkTool::Rectangle, QStringLiteral("方形未选中.png")},
        {MarkTool::Ellipse,   QStringLiteral("圆形未选中.png")},
        {MarkTool::Triangle,  QStringLiteral("形状-三角形.png")},
        {MarkTool::Line,      QStringLiteral("fileicons/line.svg")},
    };

    for (const auto &kind : shapeKinds) {
        auto *button = new QPushButton(shapePanel);
        button->setFixedSize(30, 30);
        button->setCursor(Qt::PointingHandCursor);
        button->setIconSize(QSize(14, 14));
        button->setFocusPolicy(Qt::NoFocus);

        const QString iconFile = kind.second;
        if (iconFile.endsWith(QStringLiteral(".svg"))) {
            button->setIcon(loadTintedSvgIcon(iconRoot + iconFile, AppStyle::iconTint,
                                              QSize(32, 32)));
        } else {
            button->setIcon(loadTintedIcon(iconRoot + iconFile, AppStyle::iconTint));
        }

        shapeLayout->addWidget(button);
        shapeToolButtons.append(button);
        shapeToolKinds.append(kind.first);
    }

    shapePanel->adjustSize();
    shapePanel->hide();

    // 文字面板：字号和颜色都归文字工具自己管，点文字按钮才弹出来
    textPanel = buildTextPanel();

    connect(pinButton, &QPushButton::clicked, this, &ScreenshotOverlay::pinResult);
    connect(saveButton, &QPushButton::clicked, this, &ScreenshotOverlay::saveResult);
    connect(copyButton, &QPushButton::clicked, this, &ScreenshotOverlay::copyResult);
    connect(ocrButton, &QPushButton::clicked, this, &ScreenshotOverlay::recognizeText);
    connect(translateButton, &QPushButton::clicked, this, &ScreenshotOverlay::translateText);
    connect(undoButton, &QPushButton::clicked, this, &ScreenshotOverlay::undoLastMark);
    connect(cancelButton, &QPushButton::clicked, this, &ScreenshotOverlay::cancelCapture);
    connect(colorButton, &QPushButton::clicked, this, &ScreenshotOverlay::chooseMarkColor);
    connect(widthSlider, &QSlider::valueChanged, this, [this](int value) {
        // 滑杆的含义跟着当前工具走：橡皮擦下调大小，其它工具下调粗细
        if (currentTool == MarkTool::Eraser) {
            setEraserRadius(value);
        } else {
            setMarkWidth(value);
        }
    });

    // 再点一次粗细按钮就收起面板，和标注工具"点两下取消"的习惯保持一致
    connect(widthButton, &QPushButton::clicked, this, [this]() {
        if (widthPanel->isVisible()) {
            widthPanel->hide();
            return;
        }
        showWidthPanel();
    });

    // 形状按钮：弹出/收起二级菜单；具体形状在二级菜单里选
    connect(shapeButton, &QPushButton::clicked, this, [this]() {
        if (shapePanel->isVisible()) {
            shapePanel->hide();
            return;
        }
        showShapePanel();
    });

    for (int index = 0; index < shapeToolButtons.size(); ++index) {
        connect(shapeToolButtons.at(index), &QPushButton::clicked, this, [this, index]() {
            shapePanel->hide();
            applyTool(shapeToolKinds.at(index));
        });
    }

    connect(penButton, &QPushButton::clicked, this, [this]() { applyTool(MarkTool::Pen); });
    connect(eraserButton, &QPushButton::clicked, this, [this]() { applyTool(MarkTool::Eraser); });

    // 文字按钮：选中文字工具的同时把字号/颜色面板打开。
    // 工具已经选中但面板收起来了（比如刚才在画面上点了一下）时，再点按钮
    // 只负责把面板叫回来，不能当成"取消选中"，否则想改字号就得重新选工具
    connect(textButton, &QPushButton::clicked, this, [this]() {
        if (currentTool == MarkTool::Text && textPanel != nullptr && textPanel->isVisible()) {
            applyTool(MarkTool::Text);
            return;
        }
        if (currentTool != MarkTool::Text) {
            applyTool(MarkTool::Text);
        }
        showTextPanel();
    });

    // 按钮和间距都是固定的，尺寸在这里算一次就够了。
    // 放到 placeToolBar 里每帧算，快速拉伸时白白多出一堆布局计算。
    toolBar->adjustSize();
}

void ScreenshotOverlay::showShapePanel()
{
    if (shapePanel == nullptr || shapeButton == nullptr) {
        return;
    }

    shapePanel->adjustSize();
    const QSize panelSize = shapePanel->size();

    // 面板在水平方向对着形状按钮居中，垂直方向贴在工具条外侧，
    // 放不下就翻到工具条上方，逻辑与粗细面板一致
    const QPoint buttonTopLeft = shapeButton->mapTo(this, QPoint(0, 0));
    int x = buttonTopLeft.x() + (shapeButton->width() - panelSize.width()) / 2;
    int y = toolBar->geometry().bottom() + 8;
    if (y + panelSize.height() > height()) {
        y = toolBar->geometry().top() - panelSize.height() - 8;
    }

    x = qBound(0, x, width() - panelSize.width());
    y = qBound(0, y, height() - panelSize.height());

    shapePanel->move(x, y);
    shapePanel->show();
    shapePanel->raise();
}

QWidget *ScreenshotOverlay::buildTextPanel()
{
    auto panel = new QFrame(this);
    panel->setObjectName("textPanel");
    panel->setStyleSheet(QStringLiteral(
        "#textPanel { background-color: %1; border: 1px solid %2; border-radius: 6px; }"
        "#textPanel QLabel { color: %3; font-size: 12px; }"
        "#textPanel QSlider::groove:horizontal { height: 4px; background: %4;"
        "                                         border-radius: 2px; }"
        "#textPanel QSlider::sub-page:horizontal { background: %5; border-radius: 2px; }"
        "#textPanel QSlider::handle:horizontal { width: 12px; height: 12px; margin: -4px 0;"
        "                                         border-radius: 6px; background: %1;"
        "                                         border: 1px solid %2; }")
        .arg(AppStyle::titleBarFill.name(), AppStyle::cardStroke.name(),
             AppStyle::textPrimary.name(), AppStyle::controlStroke.name(),
             AppStyle::accent.name()));

    auto layout = new QVBoxLayout(panel);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(8);

    // 第一行：字号
    auto sizeRow = new QHBoxLayout;
    sizeRow->setSpacing(8);

    sizeRow->addWidget(new QLabel(tr("字号"), panel));

    textSizeSlider = new QSlider(Qt::Horizontal, panel);
    textSizeSlider->setRange(MIN_TEXT_SIZE, MAX_TEXT_SIZE);
    textSizeSlider->setValue(textSize);
    textSizeSlider->setFixedWidth(110);
    textSizeSlider->setCursor(Qt::PointingHandCursor);
    // 同粗细滑杆：不让它抢焦点，否则 Esc 取消截图会被滑杆吃掉
    textSizeSlider->setFocusPolicy(Qt::NoFocus);
    sizeRow->addWidget(textSizeSlider);

    textSizeValueLabel = new QLabel(QString::number(textSize), panel);
    textSizeValueLabel->setFixedWidth(22);
    textSizeValueLabel->setAlignment(Qt::AlignCenter);
    sizeRow->addWidget(textSizeValueLabel);

    layout->addLayout(sizeRow);

    // 第二行：预设颜色。色块本身就是按钮，当前颜色用强调色描边标出来
    auto colorRow = new QHBoxLayout;
    colorRow->setSpacing(6);

    colorRow->addWidget(new QLabel(tr("颜色"), panel));

    for (const QColor &preset : TEXT_COLOR_PRESETS) {
        auto *button = new QPushButton(panel);
        button->setFixedSize(20, 20);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        button->setToolTip(preset.name());

        colorRow->addWidget(button);
        textColorButtons.append(button);
        textColorPresets.append(preset);

        connect(button, &QPushButton::clicked, this, [this, preset]() { setTextColor(preset); });
    }

    auto customButton = new QPushButton(tr("更多"), panel);
    customButton->setFixedHeight(20);
    customButton->setCursor(Qt::PointingHandCursor);
    customButton->setFocusPolicy(Qt::NoFocus);
    customButton->setStyleSheet(AppStyle::standardButtonQss());
    connect(customButton, &QPushButton::clicked, this, [this]() {
        const QColor chosen = QColorDialog::getColor(textColor, this, tr("选择文字颜色"));
        if (chosen.isValid()) {
            setTextColor(chosen);
        }
    });
    colorRow->addWidget(customButton);

    layout->addLayout(colorRow);

    connect(textSizeSlider, &QSlider::valueChanged, this, [this](int value) {
        setTextSize(value);
    });

    panel->adjustSize();
    panel->hide();
    updateTextPanelState();

    return panel;
}

void ScreenshotOverlay::showTextPanel()
{
    if (textPanel == nullptr || textButton == nullptr) {
        return;
    }

    textPanel->adjustSize();
    const QSize panelSize = textPanel->size();

    // 和形状菜单一样：水平对着文字按钮居中，垂直贴在工具条外侧，
    // 下面放不下就翻到工具条上方
    const QPoint buttonTopLeft = textButton->mapTo(this, QPoint(0, 0));
    int x = buttonTopLeft.x() + (textButton->width() - panelSize.width()) / 2;
    int y = toolBar->geometry().bottom() + 8;
    if (y + panelSize.height() > height()) {
        y = toolBar->geometry().top() - panelSize.height() - 8;
    }

    x = qBound(0, x, width() - panelSize.width());
    y = qBound(0, y, height() - panelSize.height());

    textPanel->move(x, y);
    textPanel->show();
    textPanel->raise();
}

void ScreenshotOverlay::setTextSize(int size)
{
    textSize = qBound(MIN_TEXT_SIZE, size, MAX_TEXT_SIZE);

    // 只影响正在写的这条文字和接下来要写的：已经提交的标注各自记着自己的字号，
    // 改当前设置不能反过来改动它们
    if (textEditor != nullptr) {
        // 输入框的字体、宽高一起跟着变，放大字号就能直接看到框变大
        updateTextEditorGeometry();
    }

    updateTextPanelState();
}

void ScreenshotOverlay::setTextColor(const QColor &color)
{
    if (!color.isValid()) {
        return;
    }

    textColor = color;

    // 同样只作用于正在写的和之后的文字，已完成的标注保持各自的原色
    if (textEditorBox != nullptr) {
        textEditorBox->setStyleSheet(textEditorStyleSheet(textColor));
    }

    updateTextPanelState();

    // 文字工具下工具条上的色块显示的就是文字颜色，要一起刷新
    updateColorButtonStyle();
}

void ScreenshotOverlay::updateTextPanelState()
{
    if (textSizeSlider != nullptr) {
        // 拖滑杆时值是调用方给的，挡掉信号避免回调里再绕一圈
        const QSignalBlocker blocker(textSizeSlider);
        textSizeSlider->setValue(textSize);
    }
    if (textSizeValueLabel != nullptr) {
        textSizeValueLabel->setText(QString::number(textSize));
    }

    // 色块：当前颜色加一圈强调色描边，其余只留灰色细边
    for (int index = 0; index < textColorButtons.size(); ++index) {
        const QColor preset = textColorPresets.at(index);
        const bool selected = preset == textColor;

        textColorButtons.at(index)->setStyleSheet(
            QStringLiteral("QPushButton { background-color: %1; border: 2px solid %2;"
                           "              border-radius: 5px; }"
                           "QPushButton:hover { border-color: %3; }")
                .arg(preset.name(),
                     selected ? AppStyle::accent.name() : AppStyle::controlStroke.name(),
                     AppStyle::accent.name()));
    }
}

void ScreenshotOverlay::updateColorButtonStyle()
{
    if (colorButton == nullptr) {
        return;
    }

    // 文字工具下这个色块代表文字颜色，其它工具下代表画笔颜色，
    // 否则用户点了文字看到色块还是画笔的红色，会以为没生效
    const QColor shownColor = currentTool == MarkTool::Text ? textColor : markColor;

    // 直接设在按钮上的样式表优先于工具条那条通用规则，否则会被刷成透明底。
    // 浅色工具条上白边看不见，色块描边改用灰色，悬停时亮出强调色
    colorButton->setStyleSheet(
        QStringLiteral("QPushButton { background-color: %1; border: 2px solid %2;"
                       "              border-radius: 6px; }"
                       "QPushButton:hover { border-color: %3; }")
            .arg(shownColor.name(), AppStyle::controlStroke.name(),
                 AppStyle::accent.name()));
}

void ScreenshotOverlay::updateWidthButtonIcon()
{
    if (widthButton == nullptr) {
        return;
    }

    // 圆点大小反映当前工具的数值：橡皮擦的半径范围更大，等比折算到图标上
    const int dotValue = currentTool == MarkTool::Eraser
        ? eraserRadius * MAX_MARK_WIDTH / MAX_ERASER_RADIUS
        : markWidth;

    widthButton->setIcon(buildWidthIcon(dotValue));
    widthButton->setToolTip(currentTool == MarkTool::Eraser
        ? tr("橡皮擦大小：%1").arg(eraserRadius)
        : tr("画笔粗细：%1").arg(markWidth));
}

void ScreenshotOverlay::setMarkWidth(int lineWidth)
{
    markWidth = qBound(MIN_MARK_WIDTH, lineWidth, MAX_MARK_WIDTH);

    // 只影响"接下来画的"：每条标注都记着自己画下时的粗细，
    // 调滑杆不会回头去改已经画好的东西（和文字标注的规则一致）
    syncWidthPanel();
    updateWidthButtonIcon();
}

void ScreenshotOverlay::setEraserRadius(int radius)
{
    eraserRadius = qBound(MIN_ERASER_RADIUS, radius, MAX_ERASER_RADIUS);

    syncWidthPanel();
    updateWidthButtonIcon();
}

void ScreenshotOverlay::syncWidthPanel()
{
    // 面板显示当前工具的值：橡皮擦工具下是擦除半径，其它工具下是画笔粗细。
    // 拖滑杆时值是调用方给的，挡掉信号是为了不在回调里再绕一圈
    const int shownValue = currentTool == MarkTool::Eraser ? eraserRadius : markWidth;
    if (widthSlider != nullptr) {
        const QSignalBlocker blocker(widthSlider);
        widthSlider->setValue(shownValue);
    }
    if (widthValueLabel != nullptr) {
        widthValueLabel->setText(QString::number(shownValue));
    }
}

void ScreenshotOverlay::updateWidthPanelForTool()
{
    if (widthSlider == nullptr) {
        return;
    }

    // 橡皮擦工具下，面板从"粗细"切换成"大小"，范围也换成擦除半径的
    if (currentTool == MarkTool::Eraser) {
        if (widthTitleLabel != nullptr) {
            widthTitleLabel->setText(tr("大小"));
        }
        widthSlider->setRange(MIN_ERASER_RADIUS, MAX_ERASER_RADIUS);
    } else {
        if (widthTitleLabel != nullptr) {
            widthTitleLabel->setText(tr("粗细"));
        }
        widthSlider->setRange(MIN_MARK_WIDTH, MAX_MARK_WIDTH);
    }

    syncWidthPanel();
    updateWidthButtonIcon();
}

void ScreenshotOverlay::showWidthPanel()
{
    if (widthPanel == nullptr || widthButton == nullptr) {
        return;
    }

    widthPanel->adjustSize();
    const QSize panelSize = widthPanel->size();

    // 面板在水平方向对着粗细按钮居中，垂直方向贴在工具条外侧。
    // 工具条在选区下方时面板往下放，贴近屏幕底部时改放到工具条上面
    const QPoint buttonTopLeft = widthButton->mapTo(this, QPoint(0, 0));
    int x = buttonTopLeft.x() + (widthButton->width() - panelSize.width()) / 2;
    int y = toolBar->geometry().bottom() + 8;
    if (y + panelSize.height() > height()) {
        y = toolBar->geometry().top() - panelSize.height() - 8;
    }

    // 最后再夹一次范围，屏幕太窄或选区贴着边缘时也不会有半截露在外面
    x = qBound(0, x, width() - panelSize.width());
    y = qBound(0, y, height() - panelSize.height());

    widthPanel->move(x, y);
    widthPanel->show();
    widthPanel->raise();
}

void ScreenshotOverlay::setToolCursor()
{
    // 橡皮擦不再使用专用光标形状，画面上跟着鼠标的擦除范围提示圈
    // 已经能说明当前处于擦除状态以及能擦多大

    // 文本工具用工字光标，提示当前位置可以输入文字
    if (currentTool == MarkTool::Text) {
        setCursor(Qt::IBeamCursor);
        return;
    }

    setCursor(Qt::CrossCursor);
}

void ScreenshotOverlay::chooseMarkColor()
{
    // 文字工具下改的是文字颜色，其它工具改的是画笔和形状的颜色。
    // 两套颜色分开存，改画笔不会连带改掉已经写好的字
    const bool editingText = currentTool == MarkTool::Text;
    const QColor chosen = QColorDialog::getColor(editingText ? textColor : markColor, this,
                                                 editingText ? tr("选择文字颜色")
                                                             : tr("选择标注颜色"));
    if (!chosen.isValid()) {
        // 用户点了取消，保持原颜色
        return;
    }

    if (editingText) {
        setTextColor(chosen);
        return;
    }

    markColor = chosen;

    // 只影响"接下来画的"：每条标注都记着自己画下时的颜色，
    // 改当前颜色不会把已经画好的笔迹和形状一起改掉
    updateColorButtonStyle();
}

void ScreenshotOverlay::placeToolBar()
{
    const QSize barSize = toolBar->size();

    int x = selection.center().x() - barSize.width() / 2;
    int y = selection.bottom() + 8;

    // 下方放不下就翻到选区上方
    if (y + barSize.height() > height()) {
        y = selection.top() - barSize.height() - 8;
    }
    if (y < 0) {
        y = selection.bottom() + 8;
    }

    x = qBound(0, x, width() - barSize.width());
    toolBar->move(x, y);

    // 工具条一挪位置，贴在它边上的粗细面板、形状菜单和文字面板就对不上了，先收起来
    if (widthPanel != nullptr && widthPanel->isVisible()) {
        widthPanel->hide();
    }
    if (shapePanel != nullptr && shapePanel->isVisible()) {
        shapePanel->hide();
    }
    if (textPanel != nullptr && textPanel->isVisible()) {
        textPanel->hide();
    }
}

void ScreenshotOverlay::applyTool(MarkTool tool)
{
    // 还开着文本输入框就先提交，再切换工具
    commitTextEntry();

    // 再点一次已经选中的工具就取消选中，回到"只能框选和拖动"的状态。
    // 这样不需要额外做一个"取消绘制"按钮，同一个按钮点两下就够了，
    // 而且已经画好的标注不会被清掉。
    if (currentTool == tool && tool != MarkTool::None) {
        tool = MarkTool::None;
    }

    currentTool = tool;

    if (penButton == nullptr) {
        update();
        return;
    }

    // 工具按钮互斥高亮，用动态属性配合样式表实现。
    // 形状按钮在任一形状工具激活时都点亮，具体哪个形状由二级菜单里的按钮指示
    penButton->setProperty("active", tool == MarkTool::Pen);
    shapeButton->setProperty("active", tool == MarkTool::Rectangle
                                           || tool == MarkTool::Ellipse
                                           || tool == MarkTool::Triangle
                                           || tool == MarkTool::Line);
    eraserButton->setProperty("active", tool == MarkTool::Eraser);
    textButton->setProperty("active", tool == MarkTool::Text);

    QPushButton *toolButtons[] = {penButton, shapeButton, eraserButton, textButton};
    for (QPushButton *button : toolButtons) {
        button->style()->unpolish(button);
        button->style()->polish(button);
    }

    // 二级菜单里的形状按钮单独高亮当前选中的形状
    for (int index = 0; index < shapeToolButtons.size(); ++index) {
        QPushButton *button = shapeToolButtons.at(index);
        button->setProperty("active", tool == shapeToolKinds.at(index));
        button->style()->unpolish(button);
        button->style()->polish(button);
    }

    // 换工具时收起粗细面板、形状菜单和文字面板，并把粗细面板的标题、滑杆范围
    // 切换成新工具对应的
    if (widthPanel != nullptr) {
        widthPanel->hide();
    }
    if (shapePanel != nullptr) {
        shapePanel->hide();
    }
    if (textPanel != nullptr) {
        textPanel->hide();
    }
    updateWidthPanelForTool();

    // 色块的含义跟着工具走：文字工具下显示文字颜色
    updateColorButtonStyle();

    setToolCursor();
    update();
}

void ScreenshotOverlay::undoLastMark()
{
    if (shapes.isEmpty()) {
        return;
    }

    shapes.removeLast();
    update();
}

void ScreenshotOverlay::startTextEntry(const QPoint &position)
{
    // 上一个输入框还开着就先把内容提交，再开新的
    commitTextEntry();

    // 容器 + 输入区：整个容器都能按住拖动，和拖动截图选区是一个手感；
    // 点文字区仍然是定位光标，文字没落定之前位置还能调
    auto *box = new QFrame(this);
    box->setObjectName("textEditorBox");
    box->setCursor(Qt::SizeAllCursor);
    box->installEventFilter(this);
    textEditorBox = box;

    textEditor = new QLineEdit(box);
    textEditor->setCursor(Qt::IBeamCursor);
    textEditor->installEventFilter(this);
    textEditor->setPlaceholderText(tr("输入文字"));

    auto boxLayout = new QVBoxLayout(box);
    boxLayout->setContentsMargins(TEXT_EDITOR_PADDING, TEXT_EDITOR_PADDING,
                                  TEXT_EDITOR_PADDING, TEXT_EDITOR_PADDING);
    boxLayout->setSpacing(0);
    boxLayout->addWidget(textEditor);

    // 点下去的位置留给"文字"本身：容器左上角先让出四周的留白，
    // 这样光标出现的地方就是刚才点的地方
    textEditAnchor = position - QPoint(TEXT_EDITOR_PADDING, TEXT_EDITOR_PADDING);
    updateTextEditorGeometry();

    box->show();
    box->raise();
    textEditor->setFocus();

    // 点别处（包括点工具条）时输入框失焦，直接把文字提交掉
    connect(textEditor, &QLineEdit::editingFinished, this, [this]() { commitTextEntry(); });
}

void ScreenshotOverlay::updateTextEditorGeometry()
{
    if (textEditor == nullptr || textEditorBox == nullptr) {
        return;
    }

    // 字体跟着字号走，输入区的大小也跟着字号走：
    // 字号调大以后框还那么矮，打出来的字就看不全了
    QFont editFont = textEditor->font();
    editFont.setPixelSize(textSize);
    textEditor->setFont(editFont);
    textEditorBox->setStyleSheet(textEditorStyleSheet(textColor));

    // 宽度随字号放大，但不超过选区宽度，免得框伸出选区外面
    const int preferredWidth = qMax(180, textSize * 9);
    const int editorWidth = qMin(preferredWidth, qMax(80, selection.width()));
    const int editorHeight = qMax(textEditor->sizeHint().height(), textSize + 14);
    textEditor->setFixedSize(editorWidth, editorHeight);

    // 容器尺寸 = 输入区 + 四周留白（布局自己算），尺寸变了以后重新夹回选区内
    textEditorBox->adjustSize();
    textEditorBox->move(textEditAnchor);
    clampTextEditorPosition();
}

void ScreenshotOverlay::clampTextEditorPosition()
{
    if (textEditor == nullptr || textEditorBox == nullptr) {
        return;
    }

    // 和拖动选区一样：整个框始终留在选区内，拖到边上就贴住，不会跑出去
    const QSize boxSize = textEditorBox->size();
    const int x = qBound(selection.left(), textEditorBox->x(),
                         qMax(selection.left(), selection.right() - boxSize.width()));
    const int y = qBound(selection.top(), textEditorBox->y(),
                         qMax(selection.top(), selection.bottom() - boxSize.height()));

    textEditorBox->move(x, y);
    textEditAnchor = textEditorBox->pos();

    // 文字落点取输入区左上角：提交后文字正好出现在刚才打字的位置，不会跳
    textEditPos = textEditor->mapTo(this, QPoint(0, 0));
}

void ScreenshotOverlay::discardTextEntry()
{
    if (textEditorBox == nullptr) {
        return;
    }

    // 先断开成员指针，再交给事件循环销毁：失焦信号可能还会来一次
    QWidget *box = textEditorBox;
    textEditor = nullptr;
    textEditorBox = nullptr;
    draggingTextEditor = false;

    box->hide();
    box->deleteLater();
}

void ScreenshotOverlay::commitTextEntry()
{
    if (textEditor == nullptr) {
        return;
    }

    const QString text = textEditor->text().trimmed();
    const QPoint anchor = textEditPos;

    // 先把输入框收掉，再决定要不要生成标注
    discardTextEntry();

    if (text.isEmpty()) {
        return;
    }

    MarkShape shape;
    shape.tool = MarkTool::Text;
    shape.start = anchor;
    shape.text = text;
    shape.color = textColor;
    shape.fontSize = textSize;
    shapes.append(shape);
    update();
}

bool ScreenshotOverlay::eventFilter(QObject *watched, QEvent *event)
{
    // 输入框里的 Esc 只关掉输入框，不能按常规流程取消整个截图；
    // 回车等于"写完了"，直接落定成标注
    if (watched == textEditor && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);

        if (keyEvent->key() == Qt::Key_Escape) {
            discardTextEntry();
            return true;
        }

        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            commitTextEntry();
            return true;
        }
    }

    // 输入框容器：按住框内任何一处就能把这条还没落定的文字挪走。
    // 和拖动截图选区一样——按下的点相对左上角的偏移保持不变，跟着鼠标走
    if (watched == textEditorBox) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);

        if (event->type() == QEvent::MouseButtonPress && mouseEvent->button() == Qt::LeftButton) {
            draggingTextEditor = true;
            textEditorDragOffset =
                mouseEvent->globalPosition().toPoint() - textEditorBox->pos();
            return true;
        }

        if (event->type() == QEvent::MouseMove && draggingTextEditor) {
            textEditorBox->move(mouseEvent->globalPosition().toPoint() - textEditorDragOffset);
            clampTextEditorPosition();
            return true;
        }

        if (event->type() == QEvent::MouseButtonRelease && draggingTextEditor) {
            draggingTextEditor = false;
            return true;
        }
    }

    // 文字区：单击仍然是定位光标（不拦截这次按下，交给输入框自己处理），
    // 只有按住后明显拖动，才改成移动整个输入框
    if (watched == textEditor && event->type() == QEvent::MouseButtonPress) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            textEditorPressPosition = mouseEvent->globalPosition().toPoint();
            draggingTextEditor = false;
        }
        return false;
    }

    if (watched == textEditor && event->type() == QEvent::MouseMove) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if ((mouseEvent->buttons() & Qt::LeftButton) == 0) {
            return false;
        }

        const QPoint current = mouseEvent->globalPosition().toPoint();
        if (!draggingTextEditor
            && (current - textEditorPressPosition).manhattanLength() > TEXT_EDITOR_DRAG_THRESHOLD) {
            // 超过阈值才算拖动，顺便清掉按下时可能已经拉出来的文字选区
            draggingTextEditor = true;
            textEditor->deselect();
            textEditorDragOffset = textEditorPressPosition - textEditorBox->pos();
        }

        if (draggingTextEditor) {
            textEditorBox->move(current - textEditorDragOffset);
            clampTextEditorPosition();
            return true;
        }

        return false;
    }

    if (watched == textEditor && event->type() == QEvent::MouseButtonRelease && draggingTextEditor) {
        draggingTextEditor = false;
        return true;
    }

    return QWidget::eventFilter(watched, event);
}

void ScreenshotOverlay::eraseAt(const QPoint &position)
{
    bool changed = false;
    QVector<MarkShape> kept;

    for (const MarkShape &shape : shapes) {
        // 文本没法只擦一半：碰到就整条删掉
        if (shape.tool == MarkTool::Text) {
            if (shapeTouches(shape, position, eraserRadius)) {
                changed = true;
                continue;
            }

            kept.append(shape);
            continue;
        }

        // 画笔按轨迹点拆段：擦除范围内的点被去掉，
        // 剩下的点按连续段拆成几条新的画笔轨迹保留下来
        if (shape.tool == MarkTool::Pen) {
            QVector<QPoint> segment;
            bool shapeChanged = false;
            QVector<MarkShape> pieces;

            for (const QPoint &point : shape.points) {
                if (QLineF(position, point).length() <= eraserRadius) {
                    shapeChanged = true;
                    if (segment.size() >= 2) {
                        MarkShape piece = shape;
                        piece.points = segment;
                        pieces.append(piece);
                    }
                    segment.clear();
                    continue;
                }

                segment.append(point);
            }

            if (segment.size() >= 2) {
                MarkShape piece = shape;
                piece.points = segment;
                pieces.append(piece);
            }

            if (!shapeChanged) {
                // 一点都没碰到就原样保留
                kept.append(shape);
                continue;
            }

            changed = true;
            kept += pieces;
            continue;
        }

        // 矩形/椭圆/三角/直线：在参数化边框上找被擦除中心命中的位置，
        // 往形状的擦除区间列表里记一笔。形状本体不变，
        // 渲染时跳过擦除区间，圆弧依旧是真圆弧
        if (shape.tool == MarkTool::Rectangle || shape.tool == MarkTool::Ellipse
            || shape.tool == MarkTool::Triangle || shape.tool == MarkTool::Line) {
            const qreal total = shapePathLength(shape);
            if (total <= 0) {
                kept.append(shape);
                continue;
            }

            // 沿边框每约 4 像素采样一次，找出落在擦除圆内的参数位置
            const int sampleCount = qMax(8, qCeil(total / 4.0));
            const qreal eraseSpan = eraserRadius / total;
            QVector<QPair<qreal, qreal>> hits;
            for (int index = 0; index <= sampleCount; ++index) {
                const qreal t = qreal(index) / sampleCount;
                if (QLineF(position, shapePointAt(shape, t)).length() <= eraserRadius) {
                    hits.append({t - eraseSpan, t + eraseSpan});
                }
            }

            if (hits.isEmpty()) {
                kept.append(shape);
                continue;
            }

            qreal covered = 0;
            for (const auto &range : shape.erasedRanges) {
                covered += range.second - range.first;
            }

            MarkShape piece = shape;
            for (const auto &range : hits) {
                covered = mergeErasedRanges(piece.erasedRanges, range.first, range.second);
            }

            // 命中的都是已擦过的区域，形状没有实际变化，不用重绘
            qreal before = 0;
            for (const auto &range : shape.erasedRanges) {
                before += range.second - range.first;
            }
            if (covered <= before + 1e-9) {
                kept.append(shape);
                continue;
            }

            changed = true;

            // 整条边框都被擦光，形状移除
            if (covered >= 0.999) {
                continue;
            }

            kept.append(piece);
            continue;
        }

        kept.append(shape);
    }

    // 什么都没擦到时不必重绘，橡皮划过空白区域不该带来额外开销
    if (!changed) {
        return;
    }

    shapes = kept;
    update();
}

QImage ScreenshotOverlay::cropSelection() const
{
    if (selection.isEmpty()) {
        return {};
    }

    // 按物理像素裁剪，保证输出的是屏幕原始清晰度
    const QRect sourceRect(selection.topLeft() * pixelRatio, selection.size() * pixelRatio);
    QImage result = screenshot.copy(sourceRect);
    if (result.isNull()) {
        return {};
    }

    // 裁出来的图就是原始像素，标记成 1.0 才不会在后续绘制里被再缩放一次
    result.setDevicePixelRatio(1.0);
    return result;
}

QImage ScreenshotOverlay::renderResult() const
{
    QImage result = cropSelection();
    if (result.isNull()) {
        return {};
    }

    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // 标注记录的是覆盖层逻辑坐标，这里换算到选区局部坐标，再按像素比例放大
    painter.scale(pixelRatio, pixelRatio);
    painter.translate(-selection.topLeft());

    for (const MarkShape &shape : shapes) {
        paintShape(painter, shape);
    }
    if (drawing) {
        paintShape(painter, drawingShape);
    }

    return result;
}

void ScreenshotOverlay::recognizeText()
{
    // 文字识别直接交给在线大模型看图完成，与翻译走同一条链路
    sendToAi(AiClient::Task::RecognizeText);
}

void ScreenshotOverlay::translateText()
{
    // 翻译走设置里选定的引擎：百度图片翻译或在线大模型
    sendToAi(AiClient::Task::Translate);
}

void ScreenshotOverlay::sendToAi(AiClient::Task task)
{
    if (selection.isEmpty()) {
        return;
    }

    // 正在输入的文字先落定，再裁图
    commitTextEntry();

    const AppSettings &settings = AppSettings::instance();

    // 两条通道至少要有一条能用：识别优先大模型（没配就退到百度图片翻译），
    // 翻译按设置里的引擎走（选中那条没配好会自动换另一条）。
    // 所以这里只拦"两条都没配"的情况，别把覆盖层关掉只留个报错窗口
    const bool aiReady = !settings.aiApiKey().trimmed().isEmpty();
    const bool baiduReady = !settings.baiduAppId().trimmed().isEmpty()
                            && !settings.baiduSecretKey().trimmed().isEmpty();

    if (!aiReady && !baiduReady) {
        QMessageBox::information(this, tr("还没有配置接口"),
                                 tr("文字识别和翻译需要先配置一种服务：\n"
                                    "在线大模型的 API 地址、Key 和模型，或者百度图片翻译的 APP ID 和密钥。\n"
                                    "可以现在打开设置界面填写，也可以先保存、以后再来填。"));
        return;
    }

    // 用不带标注的原图：红色笔迹会被模型当成图里的内容，
    // 提取出来的文字容易混进莫名其妙的符号
    const QImage image = cropSelection();
    if (image.isNull()) {
        return;
    }

    // 覆盖层马上就要关掉，请求和结果都必须由这个新窗口自己管
    auto *resultWindow = new TextResultWindow(image, task);
    resultWindow->show();
    resultWindow->activateWindow();

    close();
}

void ScreenshotOverlay::copyResult()
{
    // 输入框还开着的话先把文字落定，免得刚打的字没进图片
    commitTextEntry();

    const QImage image = renderResult();
    if (image.isNull()) {
        return;
    }

    QGuiApplication::clipboard()->setImage(image);
    close();
}

void ScreenshotOverlay::saveResult()
{
    // 同复制：先把没提交的文字落定
    commitTextEntry();

    const QImage image = renderResult();
    if (image.isNull()) {
        return;
    }

    // 默认文件名带时间戳，连续截图不会互相覆盖
    const QString picturesDirectory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString defaultName = picturesDirectory + "/截图_"
                                + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".png";

    const QString filePath = QFileDialog::getSaveFileName(
        this, tr("保存截图"), defaultName, tr("PNG 图片 (*.png);;JPEG 图片 (*.jpg)"));
    if (filePath.isEmpty()) {
        return;
    }

    if (!image.save(filePath)) {
        qWarning() << "保存截图失败:" << filePath;
        return;
    }

    close();
}

void ScreenshotOverlay::pinResult()
{
    // 同复制：先把没提交的文字落定
    commitTextEntry();

    const QImage image = renderResult();
    if (image.isNull()) {
        return;
    }

    // 置顶窗口贴回框选的位置：选区坐标是覆盖层内部的，加上覆盖层对应的
    // 屏幕原点才是全局坐标。这样钉出来的图片正好盖住原来那块地方
    const QPoint screenPosition = screenGeometry.topLeft() + selection.topLeft();

    auto *pinnedWindow = new PinnedImageWindow(image, screenPosition);
    pinnedWindow->show();
    close();
}

void ScreenshotOverlay::cancelCapture()
{
    // 先把工具条收掉再关窗口，避免关闭过程中还残留一次旧的绘制
    if (toolBar != nullptr) {
        toolBar->hide();
    }

    close();
}
