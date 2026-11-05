#include "HistoryListWidget.h"

#include "AppStyle.h"

#include <QPaintEvent>
#include <QPainter>

HistoryListWidget::HistoryListWidget(QWidget *parent)
    : QListWidget(parent)
{
}

void HistoryListWidget::freezeFrame()
{
    // 还没显示过就没有可冻的画面，抓出来只会是空白图，
    // 反而会让窗口刚打开时闪一下白
    if (!isVisible() || viewport()->width() <= 0 || viewport()->height() <= 0) {
        return;
    }

    // 只有第一次需要拍照；后续的 resize 继续用同一张，避免反复重绘
    if (!frozenFrame.isNull()) {
        return;
    }

    frozenFrame = viewport()->grab();
}

void HistoryListWidget::releaseFrame()
{
    if (frozenFrame.isNull()) {
        return;
    }

    frozenFrame = QPixmap();
    viewport()->update();
}

bool HistoryListWidget::isFrozen() const
{
    return !frozenFrame.isNull();
}

void HistoryListWidget::paintEvent(QPaintEvent *event)
{
    // 平时走默认绘制，和普通列表完全一样
    if (frozenFrame.isNull()) {
        QListWidget::paintEvent(event);
        return;
    }

    // 冻结期间只贴图。窗口变大时右侧和下方是新露出来的区域，
    // 先铺成卡片底色再贴图，免得露出灰底或上一帧的残影
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), AppStyle::cardBackground);
    painter.drawPixmap(0, 0, frozenFrame);
}
