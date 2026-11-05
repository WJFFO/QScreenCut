#pragma once

#include <QListWidget>
#include <QPixmap>

// 历史列表。
//
// 比普通 QListWidget 多一件事：窗口拉伸时先把当前画面拍成一张图，
// 期间只贴这张图。
//
// 原因是列表每行要画两段文字，Windows 上这些文字的开销比想象中大得多
// （实测 9 行约 13 毫秒），拖窗口边缘时每变一次尺寸就整块重画一遍，
// 帧率直接掉到 40 上下，看起来就是一卡一卡的。
// 贴图只有一次内存拷贝，窗口就能跟手；尺寸稳定后再真正重排重绘一次。
class HistoryListWidget : public QListWidget
{
public:
    explicit HistoryListWidget(QWidget *parent = nullptr);

    // 冻住当前画面。已经在冻结状态时什么都不做
    void freezeFrame();

    // 解除冻结，并重绘一次真实内容
    void releaseFrame();

    bool isFrozen() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QPixmap frozenFrame;   // 非空表示正处于冻结状态，里面是冻结那一刻的画面
};
