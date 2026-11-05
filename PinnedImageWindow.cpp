#include "PinnedImageWindow.h"

#include <QClipboard>
#include <QContextMenuEvent>
#include <QFileDialog>
#include <QGuiApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QWheelEvent>

PinnedImageWindow::PinnedImageWindow(const QImage &sourceImage, const QPoint &screenPosition)
    : image(sourceImage)
{
    // 无边框 + 置顶 + Tool：浮在其它窗口之上，也不占任务栏
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose);
    setCursor(Qt::OpenHandCursor);
    // 不按键也要收到鼠标移动，才能在靠近边缘时切换成拉伸光标
    setMouseTracking(true);

    // 用目标位置所在的那块屏。多屏时截图可能来自副屏，
    // 按主屏的尺寸和边界来算会导致钉出来的图跑到另一块屏幕上或者被缩得莫名其妙
    const QScreen *screen = screenPosition.isNull()
        ? QGuiApplication::primaryScreen()
        : QGuiApplication::screenAt(screenPosition);
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }

    // 太大的截图先缩到这块屏幕的八成，避免置顶窗口占满整个桌面
    if (screen != nullptr) {
        const QSize limit = screen->availableGeometry().size() * 0.8;
        if (image.width() > limit.width() || image.height() > limit.height()) {
            image = image.scaled(limit, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
    }

    // 四周留 1 像素边框，方便看清窗口边界
    resize(image.size() + QSize(2, 2));

    // 按框选的位置摆放：截图是从屏幕哪块地方截的，就贴回哪块地方，
    // 一眼能对上原内容。缩小过也以左上角对齐，再夹一次防止跑出这块屏幕
    if (!screenPosition.isNull() && screen != nullptr) {
        QRect target(screenPosition, size());
        const QRect available = screen->availableGeometry();

        if (target.right() > available.right()) {
            target.moveRight(available.right());
        }
        if (target.bottom() > available.bottom()) {
            target.moveBottom(available.bottom());
        }
        if (target.left() < available.left()) {
            target.moveLeft(available.left());
        }
        if (target.top() < available.top()) {
            target.moveTop(available.top());
        }

        move(target.topLeft());
    }
}

void PinnedImageWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.fillRect(rect(), QColor(60, 60, 60));
    // 用整个绘图区铺满，拉伸窗口时图片跟着变形，符合"像窗口一样拉大拉小"的预期
    painter.drawImage(rect().adjusted(1, 1, -1, -1), image);
}

int PinnedImageWindow::edgeAt(const QPoint &position) const
{
    // 边缘 8 像素内算作拉伸区域，角落会同时命中两条边
    constexpr int EDGE_MARGIN = 8;

    int edges = NoEdge;

    if (position.x() <= EDGE_MARGIN) {
        edges |= LeftEdge;
    } else if (position.x() >= width() - EDGE_MARGIN) {
        edges |= RightEdge;
    }

    if (position.y() <= EDGE_MARGIN) {
        edges |= TopEdge;
    } else if (position.y() >= height() - EDGE_MARGIN) {
        edges |= BottomEdge;
    }

    return edges;
}

void PinnedImageWindow::updateCursorShape(int edges)
{
    if (edges == NoEdge) {
        setCursor(dragging ? Qt::ClosedHandCursor : Qt::OpenHandCursor);
        return;
    }

    const bool horizontal = (edges & (LeftEdge | RightEdge)) != 0;
    const bool vertical = (edges & (TopEdge | BottomEdge)) != 0;

    if (horizontal && vertical) {
        // 左上、右下方向一致，右上、左下方向相反，两者用不同的斜向光标
        const bool sameDirection = ((edges & LeftEdge) && (edges & TopEdge))
                                   || ((edges & RightEdge) && (edges & BottomEdge));
        setCursor(sameDirection ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
        return;
    }

    setCursor(horizontal ? Qt::SizeHorCursor : Qt::SizeVerCursor);
}

void PinnedImageWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }

    // 先看是不是按在边缘上：是就进入拉伸，否则整体拖动
    resizeEdges = edgeAt(event->pos());
    if (resizeEdges != NoEdge) {
        resizeStartPosition = event->globalPosition().toPoint();
        resizeStartGeometry = geometry();
        return;
    }

    dragging = true;
    dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
    setCursor(Qt::ClosedHandCursor);
}

void PinnedImageWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (resizeEdges != NoEdge) {
        // 拉伸始终基于按下那一刻的窗口矩形计算，避免逐帧累加产生抖动
        const QPoint delta = event->globalPosition().toPoint() - resizeStartPosition;
        QRect newGeometry = resizeStartGeometry;

        constexpr int MIN_SIZE = 48;

        if (resizeEdges & LeftEdge) {
            const int newLeft = qMin(resizeStartGeometry.left() + delta.x(),
                                     resizeStartGeometry.right() - MIN_SIZE);
            newGeometry.setLeft(newLeft);
        }
        if (resizeEdges & RightEdge) {
            const int newRight = qMax(resizeStartGeometry.right() + delta.x(),
                                      resizeStartGeometry.left() + MIN_SIZE);
            newGeometry.setRight(newRight);
        }
        if (resizeEdges & TopEdge) {
            const int newTop = qMin(resizeStartGeometry.top() + delta.y(),
                                    resizeStartGeometry.bottom() - MIN_SIZE);
            newGeometry.setTop(newTop);
        }
        if (resizeEdges & BottomEdge) {
            const int newBottom = qMax(resizeStartGeometry.bottom() + delta.y(),
                                       resizeStartGeometry.top() + MIN_SIZE);
            newGeometry.setBottom(newBottom);
        }

        setGeometry(newGeometry);
        return;
    }

    if (dragging) {
        move(event->globalPosition().toPoint() - dragOffset);
        return;
    }

    // 没按键时只切换光标，提示这里可以拖动或拉伸
    updateCursorShape(edgeAt(event->pos()));
}

void PinnedImageWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }

    resizeEdges = NoEdge;
    dragging = false;
    updateCursorShape(edgeAt(event->pos()));
}

void PinnedImageWindow::wheelEvent(QWheelEvent *event)
{
    // 滚轮上下调整窗口大小，图片跟着拉伸显示
    const int step = event->angleDelta().y() > 0 ? 24 : -24;
    QSize newSize = size() + QSize(step, step);
    newSize.setWidth(qMax(48, newSize.width()));
    newSize.setHeight(qMax(48, newSize.height()));
    resize(newSize);
}

void PinnedImageWindow::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    QAction *copyAction = menu.addAction(tr("复制图片"));
    QAction *saveAction = menu.addAction(tr("保存图片"));
    menu.addSeparator();
    QAction *closeAction = menu.addAction(tr("关闭"));

    QAction *chosenAction = menu.exec(event->globalPos());

    if (chosenAction == copyAction) {
        QGuiApplication::clipboard()->setImage(image);
        return;
    }

    if (chosenAction == saveAction) {
        const QString filePath = QFileDialog::getSaveFileName(
            this, tr("保存图片"), QString(), tr("PNG 图片 (*.png);;JPEG 图片 (*.jpg)"));
        if (!filePath.isEmpty()) {
            image.save(filePath);
        }
        return;
    }

    if (chosenAction == closeAction) {
        close();
    }
}
