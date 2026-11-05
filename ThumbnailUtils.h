#pragma once

#include <QImage>
#include <QString>

// 用 Windows 资源管理器同款的缩略图接口取文件的预览图。
// 视频拿的是系统生成的缩略图缓存，图片则由 Shell 直接读内容，
// 不用自己接入各种视频解码器。文件不存在或提取失败时返回空图。
// allowIconFallback 为 false 时只要真实画面：列表上宁可用后缀徽章，
// 也不想把系统给的文件图标当成缩略图贴上去
QImage loadFilePreview(const QString &path, int maxSize, bool allowIconFallback = true);

// 图片或视频：只有这两类文件能出缩略图，
// 其它文件在列表上仍然显示"后缀文字 + 颜色背景"的徽章
bool isPreviewableFile(const QString &path);

// 把字节数转成"1.2 MB"这种好读的格式
QString formatFileSize(qint64 bytes);
