#pragma once

#include <QImage>
#include <QPoint>
#include <QWidget>

#include "AiClient.h"

class BaiduTranslateClient;
class QLabel;
class QPlainTextEdit;

// 识别 / 翻译结果的展示窗口。
//
// 无边框、置顶、可以按住顶部拖动。内容是只读的，可以选中后复制，
// 也可以一键把全文复制走。请求由窗口自己发、自己收：
// 截图覆盖层在发起识别时就已经关掉了，结果回来时得有个还活着的对象接着。
//
// 文字提取固定走在线大模型；翻译按设置里的引擎选择，可以是百度图片翻译
// 或在线大模型。
class TextResultWindow : public QWidget
{
    Q_OBJECT

public:
    TextResultWindow(const QImage &image, AiClient::Task task, QWidget *parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    // 创建大模型客户端并接好信号
    void ensureClient();

    // 创建百度图片翻译客户端并接好信号
    void ensureBaiduClient();

    // 识别 / 翻译结果就位后的统一收尾：填文本框 + 更新状态栏
    void setResultText(const QString &text);

    // 出错时的统一处理：状态栏提示 + 把原因写进文本区方便复制
    void showFailure(const QString &message);

    void showStatus(const QString &text, bool isError);

    AiClient::Task task;                          // 当前任务（识别 / 翻译）
    AiClient *client = nullptr;                   // 在线大模型，按需创建
    BaiduTranslateClient *baiduClient = nullptr;  // 百度图片翻译，按需创建
    QLabel *statusLabel = nullptr;
    QPlainTextEdit *textEdit = nullptr;

    // 按住顶部空白区域拖动窗口
    bool dragging = false;
    QPoint dragOffset;
};
