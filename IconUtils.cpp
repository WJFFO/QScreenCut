#include "IconUtils.h"

#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtGui/QPixmap>
#include <QtSvg/QSvgRenderer>

QIcon loadTintedIcon(const QString &path, const QColor &color)
{
    const QImage source(path);
    if (source.isNull()) {
        // 图标缺失时返回空图标，按钮会退化成只显示文字，不影响功能
        return QIcon();
    }

    QImage tinted(source.size(), QImage::Format_ARGB32_Premultiplied);
    tinted.fill(Qt::transparent);

    QPainter painter(&tinted);
    painter.drawImage(0, 0, source);

    // SourceIn 表示只在原图不透明的地方落色：
    // 笔画内的不透明像素被换成目标颜色，边缘的半透明像素保留原来的透明度
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(tinted.rect(), color);
    painter.end();

    return QIcon(QPixmap::fromImage(tinted));
}

QIcon loadTintedSvgIcon(const QString &path, const QColor &color, const QSize &size)
{
    QSvgRenderer renderer(path);
    if (!renderer.isValid()) {
        // 图标缺失时返回空图标，调用方退化成纯文字或默认外观
        return QIcon();
    }

    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    renderer.render(&painter);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(image.rect(), color);
    painter.end();

    return QIcon(QPixmap::fromImage(image));
}
