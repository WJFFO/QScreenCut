#pragma once

#include <QDialog>
#include <QString>
#include <QVBoxLayout>

#include "ClipboardStore.h"

class QLabel;
class QPlainTextEdit;

// 历史条目的详情弹窗。
//
// 列表上只显示一个名字，点开这里才看到完整内容：
// 文本给全文，图片给缩略图和尺寸，文件给大小、路径，
// 视频和图片文件再补一张资源管理器同款的预览图。
class EntryDetailDialog : public QDialog
{
    Q_OBJECT

public:
    explicit EntryDetailDialog(qint64 entryId, QWidget *parent = nullptr);

signals:
    // 用户点了“复制到剪贴板”。写剪贴板和防回环的标志都在主窗口里，
    // 弹窗只负责把请求发出去
    void restoreRequested(qint64 entryId);

private:
    void buildHeader();
    void buildTextPage(const QByteArray &content);
    void buildImagePage(const QByteArray &content);
    void buildFilesPage(const QByteArray &content);
    void buildButtons();

    // 把一组预览图排成可滚动的网格。列表上只看得到第一张，
    // 完整的一组图片 / 视频画面放在这里
    void buildPreviewGrid(const QStringList &paths);

    // 所有能出预览图的文件路径，按原顺序保留。一个都没有时返回空列表
    QStringList findPreviewFiles(const QStringList &paths) const;

    ClipboardEntry entry;
    QVBoxLayout *mainLayout = nullptr;
    QLabel *statusLabel = nullptr;
};
