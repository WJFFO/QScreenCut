#include "ThumbnailUtils.h"

#include <QDir>
#include <QFileInfo>
#include <QtDebug>

#include <QtCore/qt_windows.h>
#include <shlobj.h>

namespace {

// 能出预览画面的后缀。图片走内容解码，视频走系统缩略图
const char *const IMAGE_EXTENSIONS[] = {
    "png", "jpg", "jpeg", "bmp", "gif", "webp", "tif", "tiff", "heic", "svg"};

const char *const VIDEO_EXTENSIONS[] = {
    "mp4", "avi", "mkv", "mov", "wmv", "flv", "webm", "m4v", "mpg", "mpeg", "ts", "3gp"};

bool extensionInList(const QString &extension, const char *const *list, int count)
{
    for (int index = 0; index < count; ++index) {
        if (extension == QLatin1String(list[index])) {
            return true;
        }
    }
    return false;
}

}

bool isPreviewableFile(const QString &path)
{
    const QString extension = QFileInfo(path).suffix().toLower();

    return extensionInList(extension, IMAGE_EXTENSIONS,
                           sizeof(IMAGE_EXTENSIONS) / sizeof(IMAGE_EXTENSIONS[0]))
        || extensionInList(extension, VIDEO_EXTENSIONS,
                           sizeof(VIDEO_EXTENSIONS) / sizeof(VIDEO_EXTENSIONS[0]));
}

QImage loadFilePreview(const QString &path, int maxSize, bool allowIconFallback)
{
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        return {};
    }

    // Shell 的解析名只认反斜杠路径，数据库里存的是正斜杠，先转成系统格式
    const QString nativePath = QDir::toNativeSeparators(path);

    // Shell 的缩略图接口按路径创建工厂对象，之后就能向它要缩略图
    IShellItemImageFactory *factory = nullptr;
    const HRESULT createResult = SHCreateItemFromParsingName(
        reinterpret_cast<const wchar_t *>(nativePath.utf16()), nullptr, IID_PPV_ARGS(&factory));
    if (FAILED(createResult)) {
        qWarning() << "创建 Shell 项失败:" << nativePath << Qt::hex << static_cast<long>(createResult);
        return {};
    }

    HBITMAP bitmap = nullptr;

    // 默认模式下 Shell 会优先用缩略图缓存，没有缓存时现场提取一份
    // （视频第一次看可能要等一小会）。彻底失败再退回文件图标，聊胜于无；
    // 明确不要图标的调用方（列表缩略图）就到此为止，交给调用方画自己的徽章
    HRESULT imageResult = factory->GetImage(
        SIZE{maxSize, maxSize}, SIIGBF_BIGGERSIZEOK, &bitmap);
    if (FAILED(imageResult) && allowIconFallback) {
        imageResult = factory->GetImage(
            SIZE{maxSize, maxSize}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &bitmap);
    }

    factory->Release();

    if (FAILED(imageResult) || bitmap == nullptr) {
        qWarning() << "取文件预览失败:" << path << Qt::hex << static_cast<long>(imageResult);
        return {};
    }

    const QImage image = QImage::fromHBITMAP(bitmap);
    DeleteObject(bitmap);
    return image;
}

QString formatFileSize(qint64 bytes)
{
    if (bytes < 0) {
        return QString();
    }

    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }

    const double kilo = 1024.0;
    if (bytes < 1024 * 1024) {
        return QStringLiteral("%1 KB").arg(bytes / kilo, 0, 'f', 1);
    }

    if (bytes < 1024LL * 1024 * 1024) {
        return QStringLiteral("%1 MB").arg(bytes / (kilo * kilo), 0, 'f', 1);
    }

    return QStringLiteral("%1 GB").arg(bytes / (kilo * kilo * kilo), 0, 'f', 2);
}
