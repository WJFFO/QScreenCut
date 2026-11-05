#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QVector>

// 一条剪贴板记录。
// 数据库里只保存体积可控的内容：文本原文（超长会被截断）、图片缩略图（不是原图）、
// 文件路径列表。这样即使频繁复制大图片，数据库也不会迅速膨胀。
struct ClipboardEntry
{
    qint64 id = 0;
    QString kind;        // text / image / files
    QByteArray content;  // 按 kind 解释：UTF-8 文本、PNG 缩略图、JSON 路径数组
    QString preview;     // 列表里显示的摘要文字
    QDateTime createdAt;

    // 只有 files 类型才有：第一个文件的扩展名（小写、不含点）和完整路径。
    // 列表用它来决定显示哪个文件类型图标，判断不出来时为空
    QString fileExtension;
    QString firstFilePath;

    // 同样是 files 类型才有：整组文件里第一个图片 / 视频的路径。
    // 列表上直接拿它出缩略图，一个都没有时为空（那就还是显示类型徽章）
    QString previewFilePath;
};

// 打开数据库并建表；失败返回 false，调用方可以据此提示用户
bool openClipboardDatabase();
void closeClipboardDatabase();

// 写入一条记录。若与最新一条内容完全相同，则不新增，只刷新时间
qint64 insertClipboardEntry(const ClipboardEntry &entry);

// 分页读取历史记录：按时间倒序，从第 offset 条开始取 limit 条。
// 返回的条数少于 limit 说明已经取完了。
// 列表窗口只在滚动到底部时才取下一页，几千条记录也不会一次性塞进界面
QVector<ClipboardEntry> loadClipboardEntries(int limit, int offset);

// 取出单条完整记录（含 content），详情弹窗用。不存在时返回 kind 为空的记录
ClipboardEntry loadClipboardEntry(qint64 id);

// 单独取出某条记录的 content，点击历史项恢复时才读
QByteArray loadClipboardEntryContent(qint64 id);

void deleteClipboardEntry(qint64 id);
void clearClipboardEntries();
