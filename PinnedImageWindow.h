#pragma once

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QWidget>

// 置顶图片窗口：把截图钉在桌面上对照查看。
// 无边框、始终置顶。按住中间拖动可以移动，按住四边或四角可以拉伸，
// 滚轮也能整体缩放，右键弹出菜单。
class PinnedImageWindow : public QWidget
{
    Q_OBJECT

public:
    // screenPosition 是截图时框选区域的屏幕左上角。传进来就把图片贴回原位，
    // 不传（默认）时由系统决定位置
    explicit PinnedImageWindow(const QImage &sourceImage,
                               const QPoint &screenPosition = QPoint());

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    // 鼠标落在哪条边或哪个角。用位组合表示，角落会同时置两条边
    enum ResizeEdge
    {
        NoEdge = 0x0,
        LeftEdge = 0x1,
        RightEdge = 0x2,
        TopEdge = 0x4,
        BottomEdge = 0x8,
    };

    int edgeAt(const QPoint &position) const;
    void updateCursorShape(int edges);

    QImage image;
    QPoint dragOffset;
    bool dragging = false;

    // 拉伸状态：按住了哪几条边、按下时的鼠标全局坐标、按下时的窗口矩形
    int resizeEdges = NoEdge;
    QPoint resizeStartPosition;
    QRect resizeStartGeometry;
};
