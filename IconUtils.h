#pragma once

#include <QtCore/QSize>
#include <QtCore/QString>
#include <QtGui/QColor>
#include <QtGui/QIcon>

// 把一张图标重新着色后加载。
//
// images 目录里的图标都是深色线条 + 透明背景：放在浅色标题栏上没问题，
// 但直接贴到深色工具条上几乎看不见，所以工具条那边要传白色进来。
// 着色时保留原图的透明度，笔画边缘的抗锯齿才不会变成硬边。
QIcon loadTintedIcon(const QString &path, const QColor &color);

// 同上，但输入是 SVG。SVG 是描边图形，先按指定尺寸渲染成位图再着色，
// 这样同一个图标文件可以用在任何大小的按钮或列表项上，不会糊。
QIcon loadTintedSvgIcon(const QString &path, const QColor &color, const QSize &size);
